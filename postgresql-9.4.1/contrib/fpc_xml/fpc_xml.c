/* main file of the the library "fpc_xml.so" */

#include "postgres.h"
#include "commands/prepare.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/clauses.h"
#include "optimizer/cost.h"
#include "optimizer/geqo.h"
#include "optimizer/joininfo.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"
#include "optimizer/plancat.h"
#include "optimizer/planner.h"
#include "optimizer/prep.h"
#include "optimizer/restrictinfo.h"
#include "parser/scansup.h"
#include "tcop/utility.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "libxml/parser.h"
#include "libxml/tree.h"
#include "catalog/pg_class.h"
#include "nodes/pg_list.h"
#include "fpc_xml.h"

/* identification for postgres magic block */
#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC
;
#endif

/* Module callbacks */
void _PG_init(void);
void _PG_fini(void);

/* error log msg */
#define fpc_error(str, detail) ereport(INFO,(errmsg("FPC: syntax error in \"%s\"", (str)),errdetail detail))

typedef struct Fpc_Hint Fpc_Hint;
typedef struct FpcHintState FpcHintState;

/* generic function pointers to handle all types of hints */
typedef Fpc_Hint *(*HintCreateFunction)(const char *fpc_hint_str,
		const char *keyword);
typedef void (*HintFreeFunction)(Fpc_Hint *hint);
typedef int (*HintCmpFunction)(const Fpc_Hint *a, const Fpc_Hint *b);
typedef const char *(*HintParseFunction)(Fpc_Hint *hint, FpcHintState *hstate,
		Query *parse, const char *str);

static int set_config_option_wrapper(const char *name, const char *value,
		GucContext context, GucSource source, GucAction action, bool changeVal,
		int elevel);

/* data structure generic for all hints. */
struct Fpc_Hint {
	const char *hint_str;
	const char *keyword;
	HintType type;
	HintStatus state;

	/* generic handler methods */
	HintFreeFunction free_func;
	HintCmpFunction cmp_func;
	HintParseFunction parser_func;
};

typedef struct OuterInnerRels {
	char *relation;
	List *outer_inner_pair;
} OuterInnerRels;

/* scan method hints */
typedef struct Fpc_Scan_Hint {
	Fpc_Hint base;
	char *rel_name;
	List *index_names;
	unsigned char scan_enforce_mask;
} Fpc_Scan_Hint;

/* join method hints */
typedef struct Fpc_Join_Hint {
	Fpc_Hint base;
	int nrels;
	int inner_nrels;
	char **rel_names;
	unsigned char join_enforce_mask;
	Relids join_relids;
	Relids inner_joinrelids;
} Fpc_Join_Hint;

/* join order hints */
typedef struct Fpc_Order_Hint {
	Fpc_Hint base;
	List *rel_names; /* relation names specified in Order hint */
	OuterInnerRels *outer_inner;
} Fpc_Order_Hint;

/* Holds all the information about a hint context */
struct FpcHintState {
	/* original hint string */
	char *fpc_hint_str;

	/* info related to all hints */
	/* # of valid all hints */
	int nall_hints;

	/* # of max hints allowed(incremental)*/
	int max_all_hints;

	/* parsed all hints */
	Fpc_Hint **all_hints;

	/* How many hints are there of each type */
	int num_hints[NUM_HINT_TYPE];

	/* hints for type of scan method */
	/* parsed scan hints */
	Fpc_Scan_Hint **scan_hints;

	/* initial value scan parameter */
	int init_scan_mask;

	/* inherit parent table relid */
	Index parent_relid;

	/* inherit parent table scan hint */
	Fpc_Scan_Hint *parent_hint;

	/* hints for type of join to be used */
	/* parsed join hints */
	Fpc_Join_Hint **join_hints;

	/* initial value join parameter */
	int init_join_mask;
	List **join_hint_level;

	/* Hints for Order of processing relations */
	/* parsed last specified order hint */
	Fpc_Order_Hint **order_hint;

	/* GUC context */
	GucContext context;
};

/* associates each hint keyword and its create function */
typedef struct Fpc_Hint_Parser {
	char *keyword;
	HintCreateFunction create_func;
} Fpc_Hint_Parser;

static const char *HintTypeName[] = { "scan method", "join method", "order" };
static void push_hint(FpcHintState *hstate);
static void pop_hint(void);

/******************** XML parsing related fields and methods ******************/
/* lists used while parsing xml plan tree */
static List *xml_rel_order_list;
static List *xml_seq_scan_rels_list;
static List *xml_idx_scan_rels_list;
static List *xml_idx_only_scan_rels_list;
static List *xml_idx_bmp_scan_rels_list;
static List *xml_joins_list;
List *join_xml_rel_order_list;

/* Global variables used in XML Parsing */
unsigned char join_type_found;
unsigned char tot_noof_joins_present, cur_join_no, join_no_in_search;
bool add_rel_enabled;
char join_types_str[3][10] = { HINT_NESTLOOP, HINT_HASHJOIN, HINT_MERGEJOIN };
StringInfoData join_hint_buf, order_hint_buf;

/* Parsing methods */
static void parse_xml(char *file, StringInfoData a);
static void xml_parse_query_node(xmlDocPtr doc, xmlNodePtr cur);
static void xml_parse_plan_node(xmlDocPtr doc, xmlNodePtr cur);
void parse_join_types(xmlDocPtr doc);

/* Join parsing methods */
static void join_xml_chkfor_query_node(xmlDocPtr doc);
static void join_xml_chkfor_plan_node(xmlDocPtr doc, xmlNodePtr cur);
static void join_xml_parse_plan_node(xmlDocPtr doc, xmlNodePtr cur);

/* Scan hint methods */
static Fpc_Hint *Fpc_Scan_HintCreate(const char *hint_str, const char *keyword);
static void Fpc_Scan_Hint_Free(Fpc_Scan_Hint *hint);
static int ScanMethodHintCmp(const Fpc_Scan_Hint *a, const Fpc_Scan_Hint *b);
static const char *Parse_Scan_Method_Hint(Fpc_Scan_Hint *hint,
		FpcHintState *hstate, Query *parse, const char *str);
/* Join hint methods */
static Fpc_Hint *Fpc_Join_Hint_Create(const char *hint_str, const char *keyword);
static void Fpc_Join_Hint_Free(Fpc_Join_Hint *hint);
static int JoinMethodHintCmp(const Fpc_Join_Hint *a, const Fpc_Join_Hint *b);
static const char *Parse_Join_Method_Hint(Fpc_Join_Hint *hint,
		FpcHintState *hstate, Query *parse, const char *str);

/* Order hint methods */
static Fpc_Hint *Fpc_Order_Hint_Create(const char *hint_str,
		const char *keyword);
static void Fpc_Order_Hint_Free(Fpc_Order_Hint *hint);
static int OrderHintCmp(const Fpc_Order_Hint *a, const Fpc_Order_Hint *b);
static const char *Parse_Order_Hint(Fpc_Order_Hint *hint, FpcHintState *hstate,
		Query *parse, const char *str);


///* hook methods */
//static void my_set_baserel_size_estimates(PlannerInfo *root, RelOptInfo *rels);
static PlannedStmt *fpc_planner(Query *parse, int cursorOptions,
		ParamListInfo boundParams);
static void fpc_get_relation_info(PlannerInfo *root, Oid relationObjectId,
		bool inhparent, RelOptInfo *rel);
static RelOptInfo *fpc_join_search(PlannerInfo *root, int levels_needed,
		List *initial_rels);

/* methods copied from PostgreSQL standard distribution */
RelOptInfo *fpc_standard_join_search(PlannerInfo *root, int levels_needed,
		List *initial_rels);
void fpc_add_paths_to_joinrel(PlannerInfo *root, RelOptInfo *joinrel,
		RelOptInfo *outerrel, RelOptInfo *innerrel, JoinType jointype,
		SpecialJoinInfo *sjinfo, List *restrictlist);
void fpc_join_search_one_level(PlannerInfo *root, int level);

static void make_rels_by_clause_joins(PlannerInfo *root, RelOptInfo *old_rel,
		ListCell *other_rels);
static void make_rels_by_clauseless_joins(PlannerInfo *root,
		RelOptInfo *old_rel, ListCell *other_rels);
static bool has_join_restriction(PlannerInfo *root, RelOptInfo *rel);
static void set_append_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
		Index rti, RangeTblEntry *rte);

static void generate_mergeappend_paths(PlannerInfo *root, RelOptInfo *rel,
		List *live_childrels, List *all_child_pathkeys);
static List *accumulate_append_subpath(List *subpaths, Path *path);

RelOptInfo *fpc_make_join_rel(PlannerInfo *root, RelOptInfo *rel1,
		RelOptInfo *rel2, bool byepass_search);

static int find_relid_aliasname(PlannerInfo *root, char *aliasname,
		List *initial_rels, const char *str);

static const char * skip_parenthesis(const char *str, char parenthesis);
static Fpc_Join_Hint *find_join_hint(Relids joinrelids);
/* GUC variables */
static bool fpc_enable = true;

/* Saved hook values to restore state in case of unload*/
static planner_hook_type prev_planner = NULL;
static get_relation_info_hook_type prev_get_relation_info = NULL;
static join_search_hook_type prev_join_search = NULL;
//static set_baserel_size_estimates_hook_type prev_set_baserel_size_estimates = NULL;

/* Pointer to currently active hint */
static FpcHintState *current_hint = NULL;

/* List of hint contexts(stack). current_hint always points the first
 * element of this list */
static List *FpcHintStateStack = NIL;

static const Fpc_Hint_Parser parsers[] = {
		{ HINT_SEQSCAN, Fpc_Scan_HintCreate }, { HINT_INDEXSCAN,
				Fpc_Scan_HintCreate }, { HINT_BITMAPHEAPSCAN, Fpc_Scan_HintCreate },
		{ HINT_TIDSCAN, Fpc_Scan_HintCreate }, { HINT_INDEXONLYSCAN,
				Fpc_Scan_HintCreate }, { HINT_NESTLOOP, Fpc_Join_Hint_Create },
		{ HINT_MERGEJOIN, Fpc_Join_Hint_Create }, { HINT_HASHJOIN,
				Fpc_Join_Hint_Create }, { HINT_ORDER, Fpc_Order_Hint_Create }, {
				NULL, NULL } };

/* FPC Module load callback */
void _PG_init(void) {
	/* Define custom GUC variable to enable FPC */
	DefineCustomBoolVariable("fpc_enable",
			"Force planner to use plans specified in xml file ",
			NULL, &fpc_enable, true, PGC_USERSET, 0, NULL, NULL, NULL);

	/* Install hooks. */
	/* copy previous function pointers so that they can be restored */
	prev_planner = planner_hook;
	planner_hook = fpc_planner;
	prev_get_relation_info = get_relation_info_hook;
	get_relation_info_hook = fpc_get_relation_info;
	prev_join_search = join_search_hook;
	join_search_hook = fpc_join_search;

//	prev_set_baserel_size_estimates = set_baserel_size_estimates_hook;
//	set_baserel_size_estimates_hook = my_set_baserel_size_estimates;
}

/* FPC Module unload callback */
void _PG_fini(void) {
	/* Uninstall hooks. Restore previous function pointers if any */
	planner_hook = prev_planner;
	get_relation_info_hook = prev_get_relation_info;
	join_search_hook = prev_join_search;
//	set_baserel_size_estimates_hook = prev_set_baserel_size_estimates;
}

/******************************************** BEGINNING OF XML HANDLER METHODS *************************************/

/* Parses XML file and builds data structures by analyzing the xml plan */
static void parse_xml(char *file, StringInfoData buf) {
	/* pointer to parse xml Document */
	xmlDocPtr doc;

	/* node pointer */
	xmlNodePtr cur;
	int i;

	tot_noof_joins_present = 0;
	cur_join_no = 0;
	join_no_in_search = 0;
	add_rel_enabled = false;

	/* initialize list structures to be used in parsing */
	xml_rel_order_list = NIL;
	xml_seq_scan_rels_list = NIL;
	xml_idx_scan_rels_list = NIL;
	xml_idx_only_scan_rels_list = NIL;
	xml_idx_bmp_scan_rels_list = NIL;
	xml_joins_list = NIL;

	doc = xmlParseFile(file);

	/* Check to see that the document was successfully parsed. */
	if (doc == NULL) {
		fprintf(stderr, "FPC: Error. XML Document is not valid \n");
		return;
	}

	/* Retrieve the document's root element - system-properties */
	cur = xmlDocGetRootElement(doc); /* <explain> tag */

	/* Check to make sure the document actually contains something */
	if (cur == NULL) {
		fprintf(stderr, "FPC: Error. XML Document is Empty\n");
		xmlFreeDoc(doc);
		return;
	}

	initStringInfo(&order_hint_buf);

	cur = cur->xmlChildrenNode;

	while (cur != NULL)
	{
		if ((!xmlStrcmp(cur->name, (const xmlChar *) "Query")))
		{
			/* order of join information */
			appendStringInfoString(&order_hint_buf, "Order");
			appendStringInfoString(&order_hint_buf, "(");

			xml_parse_query_node(doc, cur);

			appendStringInfoString(&order_hint_buf, ") ");
		}
		cur = cur->next;
	}

	/* Sequential scan */
	for (i = length(xml_seq_scan_rels_list) - 1; i >= 0; i--)
	{
		appendStringInfoString(&buf, "SeqScan(");
		appendStringInfoString(&buf,
				(char*) list_nth(xml_seq_scan_rels_list, i));
		appendStringInfoString(&buf, ") ");
	}

	/* Index scan */
	for (i = length(xml_idx_scan_rels_list) - 1; i >= 0; i--)
	{
		appendStringInfoString(&buf, "IndexScan(");
		appendStringInfoString(&buf,
				(char*) list_nth(xml_idx_scan_rels_list, i));
		appendStringInfoString(&buf, ") ");
	}

	/* Index only scan */
	for (i = length(xml_idx_only_scan_rels_list) - 1; i >= 0; i--)
	{
		appendStringInfoString(&buf, "IndexOnlyScan(");
		appendStringInfoString(&buf,
				(char*) list_nth(xml_idx_only_scan_rels_list, i));
		appendStringInfoString(&buf, ") ");
	}

	/* Bitmap Heap Scan */
	for (i = length(xml_idx_bmp_scan_rels_list) - 1; i >= 0; i--)
	{
		appendStringInfoString(&buf, "BitmapHeapScan(");
		appendStringInfoString(&buf,
				(char*) list_nth(xml_idx_bmp_scan_rels_list, i));
		appendStringInfoString(&buf, ") ");
	}

	/* Joins */
	parse_join_types(doc);

	if (tot_noof_joins_present > 0)
	{
		appendStringInfoString(&buf, join_hint_buf.data);
		appendStringInfoString(&buf, order_hint_buf.data);
	}

//	printf("\nPlan: %s\n", buf.data);

	/* free the document */
	xmlFreeDoc(doc);

	/* Free the global variables that may have been allocated by the parser. */
	xmlCleanupParser();
}

/* Handler method to process join types in xml */
void parse_join_types(xmlDocPtr doc) {

	int i;

	/* node pointer */
	initStringInfo(&join_hint_buf);
	for (join_no_in_search = 0;	join_no_in_search < tot_noof_joins_present;
			join_no_in_search++)
	{
		/* for each join hint, identify relations involved */
		join_xml_rel_order_list = NIL;
		cur_join_no = 0;

		join_xml_chkfor_query_node(doc);
		appendStringInfoString(&join_hint_buf,
				(char*) join_types_str[join_type_found]);
		appendStringInfoString(&join_hint_buf, "(");
		for (i = 0; i < length(join_xml_rel_order_list); i++)
		{
			appendStringInfoString(&join_hint_buf,
					(char*) list_nth(join_xml_rel_order_list, i));
			appendStringInfoString(&join_hint_buf, " ");
		}
		appendStringInfoString(&join_hint_buf, ") ");
	}
}

/* Parses and identifies a query node then calls the handler for query node */
static void join_xml_chkfor_query_node(xmlDocPtr doc) {
	xmlNodePtr cur;

	cur = xmlDocGetRootElement(doc);
	cur = cur->xmlChildrenNode;

	while (cur != NULL) {
		if ((!xmlStrcmp(cur->name, (const xmlChar *) "Query"))) {
			join_xml_chkfor_plan_node(doc, cur);
		}
		cur = cur->next;
	}
}

/* Parses and identifies a plan node then calls the handler for plan node */
static void join_xml_chkfor_plan_node(xmlDocPtr doc, xmlNodePtr cur) {
	cur = cur->xmlChildrenNode;

	while (cur != NULL) {
		if ((!xmlStrcmp(cur->name, (const xmlChar *) "Plan"))) {
			join_xml_parse_plan_node(doc, cur);
		}
		cur = cur->next;
	}
	return;
}

/* Parses a Plan node completely */
static void join_xml_parse_plan_node(xmlDocPtr doc, xmlNodePtr cur)
{

	xmlChar *key, *data;
	cur = cur->xmlChildrenNode;

	while (cur != NULL)
	{
		if ((!xmlStrcmp(cur->name, (const xmlChar *) "Node-Type")))
		{
			key = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);

			if ((strcasecmp((char*) "Seq Scan", (char*) key) == 0)
					|| (strcasecmp((char*) "Index Scan", (char*) key) == 0)
					|| (strcasecmp((char*) "Index Only Scan", (char*) key) == 0)
					|| (strcasecmp((char*) "Bitmap Heap Scan", (char*) key)== 0))
			{
				if (add_rel_enabled)
				{
					while (cur && xmlStrcmp(cur->name, (const xmlChar *) "Alias"))
						cur = cur->next;

					if (cur && !(xmlStrcmp(cur->name, (const xmlChar *) "Alias")))
					{
						data = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
						join_xml_rel_order_list =
								lappend(join_xml_rel_order_list, (char*) data);
					}
				}
			}
			else if (strcasecmp((char*) "Nested Loop", (char*) key)	== 0)
			{
				if (cur_join_no == join_no_in_search)
				{
					cur_join_no++;
					join_type_found = 0;

					while (cur && (xmlStrcmp(cur->name, (const xmlChar *) "Plans")))
						cur = cur->next;

					add_rel_enabled = true;
					if (cur && (!xmlStrcmp(cur->name, (const xmlChar *) "Plans")))
					{
						join_xml_chkfor_plan_node(doc, cur);
					}
					add_rel_enabled = false;

					return;
				}
				else
					cur_join_no++;
			}
			else if (strcasecmp((char*) "Hash Join", (char*) key) == 0)
			{
				if (cur_join_no == join_no_in_search)
				{
					cur_join_no++;
					join_type_found = 1;

					while (cur && (xmlStrcmp(cur->name, (const xmlChar *) "Plans")))
						cur = cur->next;

					add_rel_enabled = true;
					if (cur && (!xmlStrcmp(cur->name, (const xmlChar *) "Plans")))
					{
						join_xml_chkfor_plan_node(doc, cur);
					}
					add_rel_enabled = false;

					return;
				}
				else
					cur_join_no++;
			}
			else if (strcasecmp((char*) "Merge Join", (char*) key) == 0)
			{
				if (cur_join_no == join_no_in_search)
				{
					cur_join_no++;
					join_type_found = 2;

					while (cur && (xmlStrcmp(cur->name, (const xmlChar *) "Plans")))
						cur = cur->next;

					add_rel_enabled = true;
					if (cur && (!xmlStrcmp(cur->name, (const xmlChar *) "Plans")))
					{
						join_xml_chkfor_plan_node(doc, cur);
					}
					add_rel_enabled = false;
					return;
				}
				else
					cur_join_no++;
			}
		}

		/* if it is a plans node, recurse thorough */
		if ((!xmlStrcmp(cur->name, (const xmlChar *) "Plans")))
		{
			join_xml_chkfor_plan_node(doc, cur);
		}

		cur = cur->next;
	}
}

/* Parses and builds data structures for a xml query node */
static void
xml_parse_query_node(xmlDocPtr doc, xmlNodePtr cur)
{
	cur = cur->xmlChildrenNode;

	while (cur != NULL)
	{
		if ((!xmlStrcmp(cur->name, (const xmlChar *) "Plan")))
		{
			xml_parse_plan_node(doc, cur);
		}
		cur = cur->next;
	}
}

/* Parses and builds data structures from a xml plan node and its descendants */
static void
xml_parse_plan_node(xmlDocPtr doc, xmlNodePtr cur)
{
	xmlChar *key, *data;
	static bool last_seen_node_is_join = false;

	cur = cur->xmlChildrenNode;

	while (cur != NULL)
	{
		if ((!xmlStrcmp(cur->name, (const xmlChar *) "Node-Type")))
		{
			last_seen_node_is_join = false;

			key = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);

			/* If its a sequential scan node, recurse and get relation's alias name */
			if (strcasecmp((char*) "Seq Scan", (char*) key) == 0)
			{
				while (cur && xmlStrcmp(cur->name, (const xmlChar *) "Alias"))
					cur = cur->next;
				if (cur && !(xmlStrcmp(cur->name, (const xmlChar *) "Alias")))
				{
					data = xmlNodeListGetString(doc,
							cur->xmlChildrenNode, 1);
					xml_rel_order_list = lappend(xml_rel_order_list,
							(char*) data);
					xml_seq_scan_rels_list = lappend(
							xml_seq_scan_rels_list, (char*) data);

					appendStringInfoString(&order_hint_buf,(char *)data);
					appendStringInfoString(&order_hint_buf," ");

				}

			}
			/* If its a index scan node, recurse and get relation's alias name */
			else if (strcasecmp((char*) "Index Scan", (char*) key) == 0)
			{
				while (cur && xmlStrcmp(cur->name, (const xmlChar *) "Alias"))
					cur = cur->next;

				if (cur && !(xmlStrcmp(cur->name, (const xmlChar *) "Alias")))
				{
					data = xmlNodeListGetString(doc,
							cur->xmlChildrenNode, 1);
					xml_rel_order_list = lappend(xml_rel_order_list,
							(char*) data);
					xml_idx_scan_rels_list = lappend(
							xml_idx_scan_rels_list, (char*) data);

					appendStringInfoString(&order_hint_buf,(char *)data);
					appendStringInfoString(&order_hint_buf," ");
				}
			}
			/* If its a index only scan node, recurse and get relation's alias name */
			else if (strcasecmp((char*) "Index Only Scan", (char*) key) == 0)
			{
				while (cur && xmlStrcmp(cur->name, (const xmlChar *) "Alias"))
					cur = cur->next;

				if (cur && !(xmlStrcmp(cur->name, (const xmlChar *) "Alias")))
				{
					data = xmlNodeListGetString(doc,
							cur->xmlChildrenNode, 1);
					xml_rel_order_list = lappend(xml_rel_order_list,
							(char*) data);
					xml_idx_only_scan_rels_list = lappend(
							xml_idx_only_scan_rels_list, (char*) data);

					appendStringInfoString(&order_hint_buf,(char *)data);
					appendStringInfoString(&order_hint_buf," ");
				}

			}
			/* If its a Bitmap Heap Scan scan node, recurse and get relation's alias name */
			else if (strcasecmp((char*) "Bitmap Heap Scan", (char*) key) == 0)
			{
				while (cur && xmlStrcmp(cur->name, (const xmlChar *) "Alias"))
					cur = cur->next;

				if (cur && !(xmlStrcmp(cur->name, (const xmlChar *) "Alias")))
				{
					data = xmlNodeListGetString(doc,
							cur->xmlChildrenNode, 1);
					xml_rel_order_list = lappend(xml_rel_order_list,
							(char*) data);
					xml_idx_bmp_scan_rels_list = lappend(
							xml_idx_bmp_scan_rels_list, (char*) data);

					appendStringInfoString(&order_hint_buf,(char *)data);
					appendStringInfoString(&order_hint_buf," ");
				}

			}
			/* update information about joins */
			else if (strcasecmp((char*) "Hash Join", (char*) key) == 0)
			{
				tot_noof_joins_present++;
				xml_joins_list = lappend(xml_joins_list, (char*) "HashJoin");
				last_seen_node_is_join = true;
			}
			else if (strcasecmp((char*) "Nested Loop", (char*) key) == 0)
			{
				tot_noof_joins_present++;
				xml_joins_list = lappend(xml_joins_list, (char*) "NestLoop");
				last_seen_node_is_join = true;
			}
			else if (strcasecmp((char*) "Merge Join", (char*) key) == 0)
			{
				tot_noof_joins_present++;
				xml_joins_list = lappend(xml_joins_list, (char*) "MergeJoin");
				last_seen_node_is_join = true;
			}
			xmlFree(key);
		}
		/* if it is a plans node, recurse through */
		else if ((!xmlStrcmp(cur->name, (const xmlChar *) "Plans")))
		{
			if(last_seen_node_is_join)
			{
				appendStringInfoString(&order_hint_buf,"(");
				xml_parse_query_node (doc, cur);
				appendStringInfoString(&order_hint_buf,")");
			}
			else
				xml_parse_query_node (doc, cur);
		}
//		else if ((!xmlStrcmp(cur->name, (const xmlChar *) "Parent-Relationship")))
//		{
//			key = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
//
//			/* If its a sub-plan node close previous plan tree.. */
//			if (strcasecmp((char*) "SubPlan", (char*) key) == 0)
//			{
//				appendStringInfoString(&order_hint_buf,")");
//			}
//		}

		cur = cur->next;
	}
}

/*****************************************  END OF XML HANDLER METHODS ************************************************/

/************************  BEGINNING OF PRE-PROCESSING(POPULATION OF STRUCTURES ***************************************/
/* creates a scan method hint.
 * Updates hint string and function pointers
 * Called when processing hints one by one */

static Fpc_Hint *
Fpc_Scan_HintCreate(const char *hint_str, const char *keyword)
{
	Fpc_Scan_Hint *hint;

	hint = palloc(sizeof(Fpc_Scan_Hint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.type = HINT_TYPE_SCAN;
	hint->base.state = HINT_STATUS_UNUSED;
	hint->base.free_func = (HintFreeFunction) Fpc_Scan_Hint_Free;
	hint->base.cmp_func = (HintCmpFunction) ScanMethodHintCmp;
	hint->base.parser_func = (HintParseFunction) Parse_Scan_Method_Hint;
	hint->rel_name = NULL;
	hint->index_names = NIL;
	hint->scan_enforce_mask = 0;

	return (Fpc_Hint *) hint;
}

/* Free memory used to store a scan method hint.
 * called after processing all hints */
static void Fpc_Scan_Hint_Free(Fpc_Scan_Hint *hint)
{
	if (!hint)
		return;
	if (hint->rel_name)
		pfree(hint->rel_name);
	list_free_deep(hint->index_names);
	pfree(hint);
}

/* creates a join method hint.
 * Updates hint string and function pointers
 * Called when processing hints one by one */
static Fpc_Hint *
Fpc_Join_Hint_Create(const char *hint_str, const char *keyword)
{
	Fpc_Join_Hint *hint;

	hint = palloc(sizeof(Fpc_Join_Hint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.type = HINT_TYPE_JOIN;
	hint->base.state = HINT_STATUS_UNUSED;
	hint->base.free_func = (HintFreeFunction) Fpc_Join_Hint_Free;
	hint->base.cmp_func = (HintCmpFunction) JoinMethodHintCmp;
	hint->base.parser_func = (HintParseFunction) Parse_Join_Method_Hint;
	hint->nrels = 0;
	hint->inner_nrels = 0;
	hint->rel_names = NULL;
	hint->join_enforce_mask = 0;
	hint->join_relids = NULL;
	hint->inner_joinrelids = NULL;

	return (Fpc_Hint *) hint;
}

/* Free memory used to store a join method hint.
 * called after processing all hints */
static void Fpc_Join_Hint_Free(Fpc_Join_Hint *hint)
{
	if (!hint)
		return;

	if (hint->rel_names) {
		int i;

		for (i = 0; i < hint->nrels; i++)
			pfree(hint->rel_names[i]);
		pfree(hint->rel_names);
	}
	bms_free(hint->join_relids);
	bms_free(hint->inner_joinrelids);
	pfree(hint);
}

/* creates a relation order hint.
 * Update hint string and function pointers
 * Called when processing hints one by one */
static Fpc_Hint *
Fpc_Order_Hint_Create(const char *hint_str, const char *keyword)
{
	Fpc_Order_Hint *hint;

	hint = palloc(sizeof(Fpc_Order_Hint));
	hint->base.hint_str = hint_str;
	hint->base.keyword = keyword;
	hint->base.type = HINT_TYPE_ORDER;
	hint->base.state = HINT_STATUS_UNUSED;
	hint->base.free_func = (HintFreeFunction) Fpc_Order_Hint_Free;
	hint->base.cmp_func = (HintCmpFunction) OrderHintCmp;
	hint->base.parser_func = (HintParseFunction) Parse_Order_Hint;
	hint->rel_names = NIL;
	hint->outer_inner = NULL;

	return (Fpc_Hint *) hint;
}

/* Free memory used to store a rel order hint.
 * called after processing all hints */
static void Fpc_Order_Hint_Free(Fpc_Order_Hint *hint)
{
	if (!hint)
		return;

	list_free_deep(hint->rel_names);
	if (hint->outer_inner)
		pfree(hint->outer_inner);
	pfree(hint);
}

/* Initializes a new hint context
 * Called when we start a new session of plan forcing */
static FpcHintState *
HintStateCreate(void)
{
	FpcHintState *hstate;

	/* initialize all fields to default values */
	hstate = palloc(sizeof(FpcHintState));
	hstate->fpc_hint_str = NULL;
	hstate->nall_hints = 0; /* total valid hints */
	hstate->max_all_hints = 0; /* max allowed hints */
	hstate->all_hints = NULL; /* pointer to each hint */
	memset(hstate->num_hints, 0, sizeof(hstate->num_hints)); /* how many hints are there of each type */
	hstate->scan_hints = NULL;
	hstate->init_scan_mask = 0;
	hstate->parent_relid = 0;
	hstate->parent_hint = NULL;
	hstate->join_hints = NULL;
	hstate->init_join_mask = 0;
	hstate->join_hint_level = NULL;
	hstate->order_hint = NULL;
	hstate->context = superuser() ? PGC_SUSET : PGC_USERSET;

	return hstate;
}

/* Free hint related data in memory */
static void FreeHintState(FpcHintState *hstate)
{
	int i;

	if (!hstate)
		return;

	if (hstate->fpc_hint_str)
		pfree(hstate->fpc_hint_str);

	/* free hints one by one */
	for (i = 0; i < hstate->num_hints[HINT_TYPE_SCAN]; i++)
		hstate->all_hints[i]->free_func(hstate->all_hints[i]);
	if (hstate->all_hints)
		pfree(hstate->all_hints);
}

/* compare functions to order hints */
static int RelnameCmp(const void *a, const void *b)
{
	const char *relnamea = *((const char **) a);
	const char *relnameb = *((const char **) b);

	return strcmp(relnamea, relnameb);
}

/* Compares relation names of scan type hints */
static int ScanMethodHintCmp(const Fpc_Scan_Hint *a,
		const Fpc_Scan_Hint *b)
{
	return RelnameCmp(&a->rel_name, &b->rel_name);
}

/* Compares whether join type hints refer to same relations */
static int JoinMethodHintCmp(const Fpc_Join_Hint *a,
		const Fpc_Join_Hint *b)
{
	int i;

	if (a->nrels != b->nrels)
		return a->nrels - b->nrels;

	for (i = 0; i < a->nrels; i++) {
		int result;
		if ((result = RelnameCmp(&a->rel_names[i], &b->rel_names[i]))
				!= 0)
			return result;
	}

	return 0;
}

/* for uniformity */
static int
OrderHintCmp(const Fpc_Order_Hint *a,
		const Fpc_Order_Hint *b)
{
	/* nothing to compare */
	return 0;
}

static int
HintCmp(const void *a, const void *b)
{
	const Fpc_Hint *hinta = *((const Fpc_Hint **) a);
	const Fpc_Hint *hintb = *((const Fpc_Hint **) b);

	if (hinta->type != hintb->type)
		return hinta->type - hintb->type;
	if (hinta->state == HINT_STATUS_ERROR)
		return -1;
	if (hintb->state == HINT_STATUS_ERROR)
		return 1;

	return hinta->cmp_func(hinta, hintb);
}

/* Returns byte offset of hint b from hint a.  If hint a was specified before
 * b, positive value is returned */
static int
HintCmpWithPos(const void *a, const void *b)
{
	const Fpc_Hint *hinta = *((const Fpc_Hint **) a);
	const Fpc_Hint *hintb = *((const Fpc_Hint **) b);
	int result;

	result = HintCmp(a, b);
	if (result == 0)
		result = hinta->hint_str - hintb->hint_str;

	return result;
}

/* parse functions */
/* parse and return the hint keyword */
static const char *
parse_keyword(const char *str, StringInfo buf) {
	ignore_space(str);

	while (!isspace(*str) && *str != '(' && *str != '\0')
		appendStringInfoCharMacro(buf, *str++);

	return str;
}

/* checks whether opening parenthesis is present and skips it */
static const char *
skip_open_braces(const char *str) {
	ignore_space(str);

	if (*str != '(') {
		fpc_error(str, ("FPC: Error. Opening parenthesis missing"));
		return NULL;
	}

	str++;

	return str;
}
static const char *
skip_parenthesis(const char *str, char parenthesis) {
	ignore_space(str);

	if (*str != parenthesis) {
		if (parenthesis == '(')
			fpc_error(str, ("Opening parenthesis is necessary."));
		else if (parenthesis == ')')
			fpc_error(str, ("Closing parenthesis is necessary."));

		return NULL;
	}

	str++;

	return str;
}
/* checks whether closinging parenthesis is present and skips it */
static const char *
skip_close_braces(const char *str) {
	ignore_space(str);

	if (*str != ')') {
		fpc_error(str, ("FPC: Error. Closing parenthesis missing!"));
		return NULL;
	}

	str++;

	return str;
}

/*
 * Parse a token from str, and store malloc'd copy into word.  A token can be
 * quoted with '"'.  Return value is pointer to unparsed portion of original
 * string, or NULL if an error occurred.
 *
 * Parsed token is truncated within NAMEDATALEN-1 bytes, when truncate is true.
 */
static const char *
parse_value(const char *str, char **word, char *value_type,
		bool truncate) {
	StringInfoData buf;
	bool in_quote;

	/* Skip leading spaces. */
	ignore_space(str);

	initStringInfo(&buf);
	if (*str == '"') {
		str++;
		in_quote = true;
	} else
		in_quote = false;

	while (true) {
		if (in_quote) {
			/* Double quotation must be closed. */
			if (*str == '\0') {
				pfree(buf.data);
				fpc_error(str, ("Unterminated quoted %s.", value_type));
				return NULL;
			}

			/* Skip escaped double quotation */
			if (*str == '"') {
				str++;
				if (*str != '"')
					break;
			}
		} else if (isspace(*str) || *str == ')' || *str == '"' || *str == '('
				|| *str == '\0')
			break;

		appendStringInfoCharMacro(&buf, *str++);
	}

	if (buf.len == 0) {
		char *type;

		type = pstrdup(value_type);
		type[0] = toupper(type[0]);
		fpc_error(str, ("%s is required", type));

		pfree(buf.data);
		pfree(type);

		return NULL;
	}

	/* Truncate name if it's too long. truncate_identifier is avbl in postgres */
	if (truncate)
		truncate_identifier(buf.data, strlen(buf.data), true);

	*word = buf.data;

	return str;
}

static OuterInnerRels *
OuterInnerRelsCreate(char *name, List *outer_inner_list)
{
	OuterInnerRels *outer_inner;

	outer_inner = palloc(sizeof(OuterInnerRels));
	outer_inner->relation = name;
	outer_inner->outer_inner_pair = outer_inner_list;

	return outer_inner;
}

static bool
OuterInnerPairCheck(OuterInnerRels *outer_inner)
{
	ListCell *l;
	if (outer_inner->outer_inner_pair == NIL)
	{
		if (outer_inner->relation)
			return true;
		else
			return false;
	}

	if (list_length(outer_inner->outer_inner_pair) == 2)
	{
		foreach(l, outer_inner->outer_inner_pair)
		{
			if (!OuterInnerPairCheck(lfirst(l)))
				return false;
		}
	}
	else
		return false;

	return true;
}

static List *
OuterInnerList(OuterInnerRels *outer_inner)
{
	List *outer_inner_list = NIL;
	ListCell *l;
	OuterInnerRels *outer_inner_rels;

	foreach(l, outer_inner->outer_inner_pair)
	{
		outer_inner_rels = (OuterInnerRels *) (lfirst(l));

		if (outer_inner_rels->relation != NULL)
			outer_inner_list = lappend(outer_inner_list,
					outer_inner_rels->relation);
		else
			outer_inner_list = list_concat(outer_inner_list,
					OuterInnerList(outer_inner_rels));
	}
	return outer_inner_list;
}

static const char *
parse_parentheses_Leading_in(const char *str,
		OuterInnerRels **outer_inner)
{
	List *outer_inner_pair = NIL;

	if ((str = skip_parenthesis(str, '(')) == NULL)
		return NULL;

	ignore_space(str);

	/* Store words in parentheses into outer_inner_list. */
	while (*str != ')' && *str != '\0')
	{
		OuterInnerRels *outer_inner_rels;

		if (*str == '(')
		{
			str = parse_parentheses_Leading_in(str, &outer_inner_rels);
			if (str == NULL)
				break;
		}
		else
		{
			char *name;

			if ((str = parse_value(str, &name,"join order", true)) == NULL)
				break;
			else
				outer_inner_rels = OuterInnerRelsCreate(name, NIL);
		}

		outer_inner_pair = lappend(outer_inner_pair, outer_inner_rels);
		ignore_space(str);
	}

	if (str == NULL || (str = skip_parenthesis(str, ')')) == NULL)
	{
		list_free(outer_inner_pair);
		return NULL;
	}

	*outer_inner = OuterInnerRelsCreate(NULL, outer_inner_pair);

	return str;
}

static const char *
parse_parentheses_JoinOrder(const char *str, List **name_list,
		OuterInnerRels **outer_inner)
{
	char *name;
	bool truncate = true;

	if ((str = skip_parenthesis(str, '(')) == NULL)
		return NULL;

	ignore_space(str);
	if (*str == '(')
	{
		if ((str = parse_parentheses_Leading_in(str, outer_inner))
				== NULL)
			return NULL;
	}
	else
	{
		/* Store words in parentheses into name_list. */
		while (*str != ')' && *str != '\0')
		{
			if ((str = parse_value(str, &name,"join order", truncate)) == NULL)
			{
				list_free(*name_list);
				return NULL;
			}

			*name_list = lappend(*name_list, name);
			ignore_space(str);
		}
	}

	if ((str = skip_parenthesis(str, ')')) == NULL)
		return NULL;
	return str;
}

/* Parse the hint string and populate structures relevantly. Note that we have
 * parsed the XML Plan file and extracted required information about scans and
 * joins from it. We then temporarily stored this information in a string
 * before calling this method. */
static void parse_plan_hints(FpcHintState *hstate, Query *parse,
		const char *str)
{
	StringInfoData buf;
	char *head;

	initStringInfo(&buf);
	while (*str != '\0')
	{
		const Fpc_Hint_Parser *parser;

		/* in error message, we output the comment including the keyword. */
		head = (char *) str;

		/* Get a keyword from fpc string e.g. SeqScan */
		resetStringInfo(&buf);
		str = parse_keyword(str, &buf);

		/* try to match it with each known keyword */
		for (parser = parsers; parser->keyword != NULL; parser++) {
			char *keyword = parser->keyword;
			Fpc_Hint *hint;

			if (strcasecmp(buf.data, keyword) != 0)
				continue;

			/* match is found */

			hint = parser->create_func(head, keyword);

			if (strcasecmp(keyword, HINT_ORDER) == 0) {
				if ((str = hint->parser_func(hint, hstate, parse, str))
						== NULL) {
					hint->free_func(hint);
					pfree(buf.data);
					return;
				}
			} else {
				/* check whether open paranthesis and close paranthesis are intact. If not
				 * delete this hint */
				if ((str = skip_open_braces(str)) == NULL
						|| (str = hint->parser_func(hint, hstate, parse,
								str)) == NULL || (str =
								skip_close_braces(str)) == NULL) {
					hint->free_func(hint);
					pfree(buf.data);
					return;
				}
			}

			/* Add hint information into all_hints array. If we don't have
			 * enough space, double the array */
			if (hstate->nall_hints == 0) {
				hstate->max_all_hints = DEFAULT_MAX_NOOF_HINTS;
				hstate->all_hints = (Fpc_Hint **) palloc(
						sizeof(Fpc_Hint *) * hstate->max_all_hints);
			} else if (hstate->nall_hints == hstate->max_all_hints) {
				hstate->max_all_hints *= 2;
				hstate->all_hints = (Fpc_Hint **) repalloc(
						hstate->all_hints,
						sizeof(Fpc_Hint *) * hstate->max_all_hints);
			}

			hstate->all_hints[hstate->nall_hints] = hint;
			hstate->nall_hints++;

			ignore_space(str);

			break;
		}

		if (parser->keyword == NULL) {
			fpc_error(head,
					("FPC: Error. Unrecognized hint \"%s\".", buf.data));
			pfree(buf.data);
			return;
		}
	}

	pfree(buf.data);
}

/*
 * Get client-supplied query string.
 */
static const char *
get_query_string(void)
{
	const char *p;

	p = debug_query_string;

	return p;
}

/*
 * Get hints from the head block comment in client-supplied query string.
 */
static const char *
extract_plan_data(const char *p)
{
	const char *hint_head;
	char *head;
	char *tail;
	int len;
	StringInfoData buf;

	if (p == NULL)
		return NULL;

	/* extract query head comment. */
	hint_head = strstr(p, FPC_HINT_START);
	if (hint_head == NULL)
		return NULL;

	len = strlen(FPC_HINT_START);
	head = (char *) p;
	p += len;
	ignore_space(p);

	/* find hint end keyword. */
	if ((tail = strstr(p, FPC_HINT_END)) == NULL) {
		fpc_error(head, ("Unterminated block comment."));
		return NULL;
	}

	/* Make a copy of hint. */
	len = tail - p;
	head = palloc(len + 1);
	memcpy(head, p, len);
	head[len] = '\0';

	initStringInfo(&buf);
	parse_xml(head,buf);
//	p = head; /* flipflop for debugging */
	p = buf.data;
	return p;
}

/*
 * Parse hints that got, create hint struct from parse tree and parse hints.
 */
static FpcHintState *
create_hintstate(Query *parse, const char *hints) {
	const char *p;
	int i;
	FpcHintState *hstate;

	if (hints == NULL)
		return NULL;

	p = hints;
	hstate = HintStateCreate();
	hstate->fpc_hint_str = (char *) hints;

	/* parse each hint. */
	parse_plan_hints(hstate, parse, p);

	/* When nothing specified a hint, we free HintState and returns NULL. */
	if (hstate->nall_hints == 0) {

		FreeHintState(hstate);
		return NULL;
	}

	/* Sort hints in order of original position. */
	qsort(hstate->all_hints, hstate->nall_hints, sizeof(Fpc_Hint *),
			HintCmpWithPos);

	/* Count number of hints per hint-type. */
	for (i = 0; i < hstate->nall_hints; i++) {
		Fpc_Hint *cur_hint = hstate->all_hints[i];
		hstate->num_hints[cur_hint->type]++;
	}

	/*
	 * If an object (or a set of objects) has multiple hints of same hint-type,
	 * only the last hint is valid and others are ignored in planning.
	 * Hints except the last are marked as 'duplicated' to remember the order.
	 */
	for (i = 0; i < hstate->nall_hints - 1; i++) {
		Fpc_Hint *cur_hint = hstate->all_hints[i];
		Fpc_Hint *next_hint = hstate->all_hints[i + 1];

		/*
		 * Leading hint is marked as 'duplicated' in transform_join_hints.
		 */
		if (cur_hint->type == HINT_TYPE_ORDER
				&& next_hint->type == HINT_TYPE_ORDER)
			continue;

		/*
		 * Note that we need to pass addresses of hint pointers, because
		 * HintCmp is designed to sort array of Hint* by qsort.
		 */
		if (HintCmp(&cur_hint, &next_hint) == 0) {
			fpc_error(cur_hint->hint_str,
					("Conflict %s hint.", HintTypeName[cur_hint->type]));
			cur_hint->state = HINT_STATUS_DUP;
		}
	}

	/*
	 * Make sure that per-type array pointers point proper position in the
	 * array which consists of all hints.
	 */
	hstate->scan_hints = (Fpc_Scan_Hint **) hstate->all_hints;
	hstate->join_hints = (Fpc_Join_Hint **) (hstate->scan_hints
			+ hstate->num_hints[HINT_TYPE_SCAN]);
	hstate->order_hint = (Fpc_Order_Hint **) (hstate->join_hints
			+ hstate->num_hints[HINT_TYPE_JOIN]);
	return hstate;
}

/* Parse scan-method hints */
/*    ScanType(RelName IndexName*) eg. SeqScan(nation) IndexScan(nation n_nationkey)   */
static const char *
Parse_Scan_Method_Hint(Fpc_Scan_Hint *hint, FpcHintState *hstate,
		Query *parse, const char *str) {
	const char *keyword = hint->base.keyword;

	/* Get the relation name from string and store it in hint structure */
	if ((str = parse_value(str, &hint->rel_name, "relation name", true))
			== NULL)
		return NULL;

	ignore_space(str);

	/* If its an index scan request then look for indices
	 * Note that FPC allows specifying index names along with relation names
	 * in case of index scans(optional) */
	if (strcmp(keyword, HINT_INDEXSCAN) == 0
			|| strcmp(keyword, HINT_INDEXONLYSCAN) == 0
			|| strcmp(keyword, HINT_BITMAPHEAPSCAN) == 0) {
		while (*str != ')' && *str != '\0') {
			char *indexname;

			str = parse_value(str, &indexname, "index name", true);
			if (str == NULL)
				return NULL;

			hint->index_names = lappend(hint->index_names, indexname);
			ignore_space(str);
		}
	}

	/* Set mask bit of scan type in scan method hint */
	if (strcasecmp(keyword, HINT_SEQSCAN) == 0)
		hint->scan_enforce_mask = ENABLE_SEQSCAN;
	else if (strcasecmp(keyword, HINT_INDEXSCAN) == 0)
		hint->scan_enforce_mask = ENABLE_INDEXSCAN;
	else if (strcasecmp(keyword, HINT_BITMAPHEAPSCAN) == 0)
		hint->scan_enforce_mask = ENABLE_BITMAPHEAPSCAN;
	else if (strcasecmp(keyword, HINT_TIDSCAN) == 0)
		hint->scan_enforce_mask = ENABLE_TIDSCAN;
	else if (strcasecmp(keyword, HINT_INDEXONLYSCAN) == 0)
		hint->scan_enforce_mask = ENABLE_INDEXSCAN
				| ENABLE_INDEXONLYSCAN;
	else {
		fpc_error(str, ("Unrecognized FPC keyword \"%s\".", keyword));
		return NULL;
	}

	return str;
}

/* Parses join method hint */
/* Each Join hint contains all the relations involved in join(from start) as part
 * of it. You need to refer to Order hint to know the order of joining them
 */
static const char *
Parse_Join_Method_Hint(Fpc_Join_Hint *hint, FpcHintState *hstate,
		Query *parse, const char *str) {
	char *relname;
	const char *keyword = hint->base.keyword;

	ignore_space(str);

	hint->rel_names = palloc(sizeof(char *));

	/* get relation names involved in join and put them in a linked list */
	while ((str = parse_value(str, &relname, "relation name", true))
			!= NULL) {
		hint->nrels++;

		/* grow memory accordingly */
		hint->rel_names = repalloc(hint->rel_names,
				sizeof(char *) * hint->nrels);

		/* put the relations invloved in join operation into a list */
		hint->rel_names[hint->nrels - 1] = relname;

		ignore_space(str);
		if (*str == ')')
			break;
	}

	if (str == NULL)
		return NULL;

	/* A join hint requires at least two relations to be specified. */
	if (hint->nrels < 2) {
		fpc_error(str,
				("%s hint requires at least two relations.", hint->base.keyword));
		hint->base.state = HINT_STATUS_ERROR;

		return str;
	}

	/* Sort hints in alphabetical order of relation names.
	 * required to handle nested joins */
	qsort(hint->rel_names, hint->nrels, sizeof(char *), RelnameCmp);

	/* Set Enforce join mask bits */
	if (strcasecmp(keyword, HINT_NESTLOOP) == 0)
		hint->join_enforce_mask = ENABLE_NESTLOOP;
	else if (strcasecmp(keyword, HINT_MERGEJOIN) == 0)
		hint->join_enforce_mask = ENABLE_MERGEJOIN;
	else if (strcasecmp(keyword, HINT_HASHJOIN) == 0)
		hint->join_enforce_mask = ENABLE_HASHJOIN;
	else {
		fpc_error(str,
				("Unrecognized FPC hint keyword \"%s\".", keyword));
		return NULL;
	}

	return str;
}

/* Parses relation order hint */
static const char *
Parse_Order_Hint(Fpc_Order_Hint *hint, FpcHintState *hstate,
		Query *parse, const char *str) {
	List *name_list = NIL;
	OuterInnerRels *outer_inner = NULL;

	if ((str = parse_parentheses_JoinOrder(str, &name_list, &outer_inner))
			==
			NULL)
		return NULL;

	if (outer_inner != NULL)
		name_list = OuterInnerList(outer_inner);

	hint->rel_names = name_list;
	hint->outer_inner = outer_inner;

	/* A Leading hint requires at least two relations */
	if (hint->outer_inner == NULL && list_length(hint->rel_names) < 2) {
		fpc_error(hint->base.hint_str,
				("%s hint requires at least two relations.", HINT_ORDER));
		hint->base.state = HINT_STATUS_ERROR;
	} else if (hint->outer_inner != NULL
			&& !OuterInnerPairCheck(hint->outer_inner)) {
		fpc_error(hint->base.hint_str,
				("%s hint requires two sets of relations when parentheses nests.", HINT_ORDER));
		hint->base.state = HINT_STATUS_ERROR;
	}

	return str;
}

/*Push a hint into hint stack which is implemented with List struct.  Head of
 * list is top of stack */
static void push_hint(FpcHintState *hstate) {
	/* Prepend new hint to the list means pushing to stack. */
	FpcHintStateStack = lcons(hstate, FpcHintStateStack);

	/* Pushed hint is the one which should be used hereafter. */
	current_hint = hstate;
}

/* Pop a hint from hint stack.
 * Popped hint is discarded. */
static void pop_hint(void) {
	/* Hint stack must not be empty. */
	if (FpcHintStateStack == NIL)
		elog(ERROR, "hint stack is empty");

	/*
	 * Take a hint at the head from the list, and free it.  Switch current_hint
	 * to point new head (NULL if the list is empty).
	 */
	FpcHintStateStack = list_delete_first(FpcHintStateStack);
	FreeHintState(current_hint);
	if (FpcHintStateStack == NIL)
		current_hint = NULL;
	else
		current_hint =
				(FpcHintState *) lfirst(list_head(FpcHintStateStack));
}

static Relids OuterInnerJoinCreate(OuterInnerRels *outer_inner,
		Fpc_Order_Hint *order_hint, PlannerInfo *root,
		List *initial_rels, FpcHintState *hstate, int nbaserel) {
	OuterInnerRels *outer_rels;
	OuterInnerRels *inner_rels;
	Relids outer_relids;
	Relids inner_relids;
	Relids join_relids;
	Fpc_Join_Hint *hint;

	if (outer_inner->relation != NULL) {
		return bms_make_singleton(
				find_relid_aliasname(root, outer_inner->relation,
						initial_rels, order_hint->base.hint_str));
	}

	outer_rels = lfirst(outer_inner->outer_inner_pair->head);
	inner_rels = lfirst(outer_inner->outer_inner_pair->tail);

	outer_relids = OuterInnerJoinCreate(outer_rels, order_hint, root,
			initial_rels, hstate, nbaserel);
	inner_relids = OuterInnerJoinCreate(inner_rels, order_hint, root,
			initial_rels, hstate, nbaserel);

	join_relids = bms_add_members(outer_relids, inner_relids);

	if (bms_num_members(join_relids) > nbaserel)
		return join_relids;

	/*
	 * If we don't have join method hint, create new one for the
	 * join combination with all join methods are enabled.
	 */
	hint = find_join_hint(join_relids);
	if (hint == NULL) {
		/*
		 * Here relnames is not set, since Relids bitmap is sufficient to
		 * control paths of this query afterward.
		 */
		hint = (Fpc_Join_Hint *) Fpc_Join_Hint_Create(
				order_hint->base.hint_str, HINT_ORDER);
		hint->base.state = HINT_STATUS_USED;
		hint->nrels = bms_num_members(join_relids);
		hint->join_enforce_mask = ENABLE_ALL_JOIN;
		hint->join_relids = bms_copy(join_relids);
		hint->inner_nrels = bms_num_members(inner_relids);
		hint->inner_joinrelids = bms_copy(inner_relids);

		hstate->join_hint_level[hint->nrels] = lappend(
				hstate->join_hint_level[hint->nrels], hint);
	} else {
		hint->inner_nrels = bms_num_members(inner_relids);
		hint->inner_joinrelids = bms_copy(inner_relids);
	}

	return join_relids;
}

/******************************   END OF PRE-PROCESSING & BEGINNING OF PLANNER TWEAKS *****************************************/

// GUC_ACTION_SAVE => temp assignement of guc variable for the duration of a function call
#define SET_CONFIG_OPTION(name, type_bits) \
set_config_option_wrapper((name), (mask & (type_bits)) ? "true" : "false", \
context, PGC_S_SESSION, GUC_ACTION_SAVE, true, ERROR)

/* set GUC parameters temporarily using SET command */
static int set_config_option_wrapper(const char *name,
		const char *value, GucContext context, GucSource source,
		GucAction action, bool changeVal, int elevel) {
	int result = 0;
	MemoryContext ccxt = CurrentMemoryContext;

	PG_TRY()
		;
		{
			/* call the method which sets guc configuration parameters */
			result = set_config_option(name, value, context, source,
					action, changeVal, 0);
		}PG_CATCH();
		{
			/* in case failure occurs */
			ErrorData *errdata;

			/* Save error info */
			MemoryContextSwitchTo(ccxt);
			errdata = CopyErrorData();
			FlushErrorState();

			/* method used from utils/elog.h of postgresql */
			elog(ERROR, "could not set GUC parameter");
			FreeErrorData(errdata);
		}PG_END_TRY();

	return result;
}

/* check enforce mask for scan hints and set it */
static void set_scan_config_options(unsigned char enforce_mask,
		GucContext context) {
	unsigned char mask;

	if (enforce_mask == ENABLE_SEQSCAN
			|| enforce_mask == ENABLE_INDEXSCAN
			|| enforce_mask == ENABLE_BITMAPHEAPSCAN
			|| enforce_mask == ENABLE_TIDSCAN
			|| enforce_mask
					== (ENABLE_INDEXSCAN | ENABLE_INDEXONLYSCAN))
		mask = enforce_mask;
	else
		mask = enforce_mask & current_hint->init_scan_mask; /* this enables all */

	/* above set mask is used in this #define */
	SET_CONFIG_OPTION("enable_seqscan", ENABLE_SEQSCAN);
	SET_CONFIG_OPTION("enable_indexscan", ENABLE_INDEXSCAN);
	SET_CONFIG_OPTION("enable_bitmapscan", ENABLE_BITMAPHEAPSCAN);
	SET_CONFIG_OPTION("enable_tidscan", ENABLE_TIDSCAN);
	SET_CONFIG_OPTION("enable_indexonlyscan", ENABLE_INDEXONLYSCAN);
}

/* check enforce mask for join hints and set it */
static void set_join_config_options(unsigned char enforce_mask,
		GucContext context) {
	unsigned char mask;

	if (enforce_mask == ENABLE_NESTLOOP
			|| enforce_mask == ENABLE_MERGEJOIN
			|| enforce_mask == ENABLE_HASHJOIN)
		mask = enforce_mask;
	else
		mask = enforce_mask & current_hint->init_join_mask; /* this enables all */

	SET_CONFIG_OPTION("enable_nestloop", ENABLE_NESTLOOP);
	SET_CONFIG_OPTION("enable_mergejoin", ENABLE_MERGEJOIN);
	SET_CONFIG_OPTION("enable_hashjoin", ENABLE_HASHJOIN);
}

/* This is the hook method for standard_planner of planner.c
 * This method is called by Optimizer at the beginning of planning phase for a query */
static PlannedStmt *
fpc_planner(Query *parse, int cursorOptions, ParamListInfo boundParams) {
	int save_nestlevel;
	PlannedStmt *result;
	FpcHintState *hstate;
	const char *hints = NULL;
	const char *query;

	/* If FPC is not enabled, call standard planner */
	if (!fpc_enable) {
		current_hint = NULL;

		if (prev_planner)
			return (*prev_planner)(parse, cursorOptions, boundParams);
		else
			return standard_planner(parse, cursorOptions, boundParams);
	}

	/* Next few lines of code extract hint information for the XML file, stores them
	 * in convenient structures for later use then call standard planner */
	/* Create hint structure from xml fpc plan */
	query = get_query_string();

	/* Create hint structure from xml fpc plan */
	hints = extract_plan_data(query);

	hstate = create_hintstate(parse, hints);

	/* If there is no valid fpc hint obtained from input, use standard planner itself */
	if (!hstate) {
		current_hint = NULL;

		if (prev_planner)
			return (*prev_planner)(parse, cursorOptions, boundParams);
		else
			return standard_planner(parse, cursorOptions, boundParams);
	}

	/* Push new hint struct to the hint stack to disable previous hint context */
	push_hint(hstate);

	/* Our GUC settings are going to use this nesting level. Note that this level
	 * expires at the end of planning phase for the query */
	save_nestlevel = NewGUCNestLevel();

	/* By default, all are enabled */
	if (enable_seqscan)
		current_hint->init_scan_mask |= ENABLE_SEQSCAN;
	if (enable_indexscan)
		current_hint->init_scan_mask |= ENABLE_INDEXSCAN;
	if (enable_bitmapscan)
		current_hint->init_scan_mask |= ENABLE_BITMAPHEAPSCAN;
	if (enable_tidscan)
		current_hint->init_scan_mask |= ENABLE_TIDSCAN;
	if (enable_indexonlyscan)
		current_hint->init_scan_mask |= ENABLE_INDEXONLYSCAN;
	if (enable_nestloop)
		current_hint->init_join_mask |= ENABLE_NESTLOOP;
	if (enable_mergejoin)
		current_hint->init_join_mask |= ENABLE_MERGEJOIN;
	if (enable_hashjoin)
		current_hint->init_join_mask |= ENABLE_HASHJOIN;

	/* Go to Standard Planner Method from where other FPC methods will be called
	 * at appropriate times using hooks.
	 * Use PG_TRY mechanism to recover GUC parameters and current_hint to the
	 * state when this planner started when error occurs in planner.
	 */
	PG_TRY()
		;
		{
			if (prev_planner)
				result = (*prev_planner)(parse, cursorOptions,
						boundParams);
			else
				result = standard_planner(parse, cursorOptions,
						boundParams);
		}PG_CATCH();
		{
			/* Something went wrong! Rollback changes of GUC parameters,
			 * and pop current hint context from hint stack to rewind the state */
			/* Undo transient assignment to guc config variables */
			AtEOXact_GUC(true, save_nestlevel);
			pop_hint();
			PG_RE_THROW();
		}PG_END_TRY();

	/* Rollback changes of GUC parameters, and pop current hint context
	 * from hint stack to rewind the state */
	AtEOXact_GUC(true, save_nestlevel);
	pop_hint();

	return result;
}

/* Looks for and Returns scan method hint which
 * matches given aliasname */
static Fpc_Scan_Hint *
find_scan_hint(PlannerInfo *root, RelOptInfo *rel) {
	RangeTblEntry *rte;
	int i;

	/* Note: We can't apply scan method hint if the relation is:
	 *   1. not a base relation
	 *   2. not an ordinary relation (such as join and subquery)
	 */
	if (rel->reloptkind != RELOPT_BASEREL
			|| rel->rtekind != RTE_RELATION)
		return NULL;

	rte = root->simple_rte_array[rel->relid];

	/* We can't force scan method of foreign tables */
	if (rte->relkind == RELKIND_FOREIGN_TABLE)
		return NULL;

	/* Find scan method hint, which matches given names, from the list. */
	for (i = 0; i < current_hint->num_hints[HINT_TYPE_SCAN]; i++) {
		Fpc_Scan_Hint *hint = current_hint->scan_hints[i];

		/* We ignore disabled hints. */
		if (!fpc_hint_state_enabled(hint))
			continue;

		if (RelnameCmp(&rte->eref->aliasname, &hint->rel_name) == 0)
			return hint;
	}

	return NULL;
}

static void delete_indexes(Fpc_Scan_Hint *hint, RelOptInfo *rel) {
	ListCell *cell;
	ListCell *prev;
	ListCell *next;

	/*
	 * We delete all the IndexOptInfo list and prevent you from being usable by
	 * a scan.
	 */
	if (hint->scan_enforce_mask == ENABLE_SEQSCAN
			|| hint->scan_enforce_mask == ENABLE_TIDSCAN) {
		list_free_deep(rel->indexlist);
		rel->indexlist = NIL;
		hint->base.state = HINT_STATUS_USED;

		return;
	}

	/*
	 * When a list of indexes is not specified, we just use all indexes.
	 */
	if (hint->index_names == NIL)
		return;

	/* Leaving only a specified index, delete from IndexOptInfo list all
	 * other than that */
	prev = NULL;
	for (cell = list_head(rel->indexlist); cell; cell = next) {
		IndexOptInfo *info = (IndexOptInfo *) lfirst(cell);
		char *indexname = get_rel_name(info->indexoid);
		ListCell *l;
		bool use_index = false;

		next = lnext(cell);

		foreach(l, hint->index_names)
		{
			if (RelnameCmp(&indexname, &lfirst(l)) == 0) {
				use_index = true;
				break;
			}
		}

		if (!use_index)
			rel->indexlist = list_delete_cell(rel->indexlist, cell,
					prev);
		else
			prev = cell;

		pfree(indexname);
	}
}

/*
 * Disclaimer: Codes written below implement minor modifications to postgresql standard
 * distribution code to tweak Optimizer to take up forced Plan :-)
 */

/* Hook method for get_relation_info(). This method is called from
 * within get_relation_info() of plancat.c as the last line.
 * This method edits the info we obtained from the catalogs.
 */
static void fpc_get_relation_info(PlannerInfo *root,
		Oid relationObjectId, bool inhparent, RelOptInfo *rel) {
	Fpc_Scan_Hint *hint;

	if (prev_get_relation_info)
		(*prev_get_relation_info)(root, relationObjectId, inhparent,
				rel);

	/* Do nothing if we don't have valid hint in this context. */
	if (!current_hint)
		return;

	if (inhparent) {
		current_hint->parent_relid = rel->relid;
	} else if (current_hint->parent_relid != 0) {
		/*
		 * We use the same GUC parameter if this table is the child table of a
		 * table called fpc_get_relation_info just before that.
		 */
		ListCell *l;

		/* append_rel_list contains all append rels; ignore others */
		foreach(l, root->append_rel_list)
		{
			AppendRelInfo *appinfo = (AppendRelInfo *) lfirst(l);

			/* This rel is child table. */
			if (appinfo->parent_relid == current_hint->parent_relid
					&& appinfo->child_relid == rel->relid) {
				if (current_hint->parent_hint)
					delete_indexes(current_hint->parent_hint, rel);

				return;
			}
		}

		/* This rel is not inherit table. */
		current_hint->parent_relid = 0;
		current_hint->parent_hint = NULL;
	}

	/*
	 * If scan method hint was given, reset GUC parameters which control
	 * planner behavior about choosing scan methods.
	 */
	if ((hint = find_scan_hint(root, rel)) == NULL) {
		/* execution comes here if no valid scan hint is availble for given
		 * relation. Hence use default scan mask */
		set_scan_config_options(current_hint->init_scan_mask,
				current_hint->context);
		return;
	}
	set_scan_config_options(hint->scan_enforce_mask,
			current_hint->context);
	hint->base.state = HINT_STATUS_USED;
	if (inhparent)
		current_hint->parent_hint = hint;

	/* If a non-index scan is to be forced, then remove index related
	 * information from structures
	 */
	delete_indexes(hint, rel);
}

//static void my_set_baserel_size_estimates(PlannerInfo *root, RelOptInfo *rel)
//{
////	double		nrows;
////
////	/* Should only be applied to base relations */
////	Assert(rel->relid > 0);
////
////	printf(" GET SELECTIVITY OF %d\n", rel->relid);
////
////	switch(rel->relid)
////	{
////	case 1:
////		nrows = rel->tuples * 0.5;
////		break;
////	case 2:
////		nrows = rel->tuples * 0.5;
////		break;
////	case 3:
////		nrows = rel->tuples * 1;
////		break;
////	case 4:
////		nrows = rel->tuples * 0.5;
////		break;
////	case 5:
////		nrows = rel->tuples * 0.5;
////		break;
////	default:
////		nrows = rel->tuples * clauselist_selectivity(root,rel->baserestrictinfo,  0, JOIN_INNER, NULL);
////
////		break;
////	}
////	rel->rows = clamp_row_est(nrows);
////
////	cost_qual_eval(&rel->baserestrictcost, rel->baserestrictinfo, root);
////
////	set_rel_width(root, rel);
//
//	if (prev_set_baserel_size_estimates)
//		(*prev_set_baserel_size_estimates) (root, rel);
//	else
//		set_baserel_size_estimates(root, rel);
//
//}

/*
 * Return index of relation which matches given aliasname, or 0 if not found.
 * If same aliasname was used multiple times in a query, return -1.
 */
static int find_relid_aliasname(PlannerInfo *root, char *aliasname,
		List *initial_rels, const char *str) {
	int i;
	Index found = 0;

	for (i = 1; i < root->simple_rel_array_size; i++) {
		ListCell *l;

		if (root->simple_rel_array[i] == NULL)
			continue;

		Assert(i == root->simple_rel_array[i]->relid);

		if (RelnameCmp(&aliasname,
				&root->simple_rte_array[i]->eref->aliasname) != 0)
			continue;

		foreach(l, initial_rels)
		{
			RelOptInfo *rel = (RelOptInfo *) lfirst(l);

			if (rel->reloptkind == RELOPT_BASEREL) {
				if (rel->relid != i)
					continue;
			} else {
				Assert(rel->reloptkind == RELOPT_JOINREL);

				if (!bms_is_member(i, rel->relids))
					continue;
			}

			if (found != 0) {
				fpc_error(str,
						("Relation name \"%s\" is not proper.", aliasname));
				return -1;
			}

			found = i;
			break;
		}

	}

	return found;
}

/*
 * Return join hint which matches given joinrelids.
 */
static Fpc_Join_Hint *
find_join_hint(Relids joinrelids) {
	List *join_hint;
	ListCell *l;

	join_hint = current_hint->join_hint_level[bms_num_members(
			joinrelids)];

	foreach(l, join_hint)
	{
		Fpc_Join_Hint *hint = (Fpc_Join_Hint *) lfirst(l);

		if (bms_equal(joinrelids, hint->join_relids))
			return hint;
	}

	return NULL;
}

/*
 * Transform join method hint into handy form.
 *
 *   - create bitmap of relids from alias names, to make it easier to check
 *     whether a join path matches a join method hint.
 *   - add join method hints which are necessary to enforce join order
 *     specified by Order hint
 */
static bool process_join_hints(FpcHintState *hstate, PlannerInfo *root,
		int nbaserel, List *initial_rels,
		Fpc_Join_Hint **join_method_hints) {
	int i;
	int relid;
	Relids joinrelids;
	int njoinrels;
	ListCell *l;
	char *relname;
	Fpc_Order_Hint *lhint = NULL;

	/*
	 * Create bitmap of relids from alias names for each join method hint.
	 * Bitmaps are more handy than strings in join searching.
	 */
	for (i = 0; i < hstate->num_hints[HINT_TYPE_JOIN]; i++) {
		Fpc_Join_Hint *hint = hstate->join_hints[i];
		int j;

		if (!fpc_hint_state_enabled(hint) || hint->nrels > nbaserel)
			continue;

		bms_free(hint->join_relids);
		hint->join_relids = NULL;
		relid = 0;
		for (j = 0; j < hint->nrels; j++) {
			relname = hint->rel_names[j];

			relid = find_relid_aliasname(root, relname, initial_rels,
					hint->base.hint_str);

			if (relid == -1)
				hint->base.state = HINT_STATUS_ERROR;

			if (relid <= 0)
				break;

			if (bms_is_member(relid, hint->join_relids)) {
				fpc_error(hint->base.hint_str,
						("Relation name \"%s\" is duplicated.", relname));
				hint->base.state = HINT_STATUS_ERROR;
				break;
			}

			hint->join_relids = bms_add_member(hint->join_relids,
					relid);
		}

		if (relid <= 0 || hint->base.state == HINT_STATUS_ERROR)
			continue;

		hstate->join_hint_level[hint->nrels] = lappend(
				hstate->join_hint_level[hint->nrels], hint);
	}

	/* Do nothing if no Order hint was supplied. */
	if (hstate->num_hints[HINT_TYPE_ORDER] == 0)
		return false;

	/*
	 * Decide to use order hint。
	 */
	for (i = 0; i < hstate->num_hints[HINT_TYPE_ORDER]; i++) {
		Fpc_Order_Hint *leading_hint =
				(Fpc_Order_Hint *) hstate->order_hint[i];
		Relids relids;

		if (leading_hint->base.state == HINT_STATUS_ERROR)
			continue;

		relid = 0;
		relids = NULL;

		foreach(l, leading_hint->rel_names)
		{
			relname = (char *) lfirst(l);

			relid = find_relid_aliasname(root, relname, initial_rels,
					leading_hint->base.hint_str);
			if (relid == -1)
				leading_hint->base.state = HINT_STATUS_ERROR;

			if (relid <= 0)
				break;

			if (bms_is_member(relid, relids)) {
				fpc_error(leading_hint->base.hint_str,
						("Relation name \"%s\" is duplicated.", relname));
				leading_hint->base.state = HINT_STATUS_ERROR;
				break;
			}

			relids = bms_add_member(relids, relid);
		}

		if (relid <= 0 || leading_hint->base.state == HINT_STATUS_ERROR)
			continue;

		if (lhint != NULL) {

			printf("error duplicate hint\n");
			fpc_error(lhint->base.hint_str,
					("Conflict %s hint.", HintTypeName[lhint->base.type]));
			lhint->base.state = HINT_STATUS_DUP;
		}
		leading_hint->base.state = HINT_STATUS_USED;
		lhint = leading_hint;           //under construction by rajmohan
	}

	/* check to exist Leading hint marked with 'used'. */
	if (lhint == NULL)
		return false;

	joinrelids = NULL;
	njoinrels = 0;
	if (lhint->outer_inner == NULL)
	{
		foreach(l, lhint->rel_names)
		{
			Fpc_Join_Hint *hint;

			relname = (char *) lfirst(l);

			/*
			 * Find relid of the relation which has given name.  If we have the
			 * name given in Leading hint multiple times in the join, nothing to
			 * do.
			 */
			relid = find_relid_aliasname(root, relname, initial_rels,
					hstate->fpc_hint_str);

			/* Create bitmap of relids for current join level. */
			joinrelids = bms_add_member(joinrelids, relid);
			njoinrels++;

			/* We never have join method hint for single relation. */
			if (njoinrels < 2)
				continue;

			/*
			 * If we don't have join method hint, create new one for the
			 * join combination with all join methods are enabled.
			 */
			hint = find_join_hint(joinrelids);
			if (hint == NULL) {
				/*
				 * Here relnames is not set, since Relids bitmap is sufficient
				 * to control paths of this query afterward.
				 */
				hint = (Fpc_Join_Hint *) Fpc_Join_Hint_Create(
						lhint->base.hint_str,
						HINT_ORDER);
				hint->base.state = HINT_STATUS_USED;
				hint->nrels = njoinrels;
				hint->join_enforce_mask = ENABLE_ALL_JOIN;
				hint->join_relids = bms_copy(joinrelids);
			}

			join_method_hints[njoinrels] = hint;

			if (njoinrels >= nbaserel)
				break;
		}
		bms_free(joinrelids);

		if (njoinrels < 2)
			return false;

		/*
		 * Delete all join hints which have different combination from Leading
		 * hint.
		 */
		for (i = 2; i <= njoinrels; i++) {
			list_free(hstate->join_hint_level[i]);

			hstate->join_hint_level[i] = lappend(NIL,
					join_method_hints[i]);
		}
	}
	else
	{
		joinrelids = OuterInnerJoinCreate(lhint->outer_inner, lhint,
				root, initial_rels, hstate, nbaserel);

		njoinrels = bms_num_members(joinrelids);
		Assert(njoinrels >= 2);

		/* Delete all join hints which have different combination from Order hint */
		for (i = 2; i <= njoinrels; i++) {
			if (hstate->join_hint_level[i] != NIL) {
				ListCell *prev = NULL;
				ListCell *next = NULL;
				for (l = list_head(hstate->join_hint_level[i]); l; l =
						next) {

					Fpc_Join_Hint *hint = (Fpc_Join_Hint *) lfirst(l);

					next = lnext(l);

					if (hint->inner_nrels == 0
							&& !(bms_intersect(hint->join_relids,
									joinrelids) == NULL
									|| bms_equal(
											bms_union(hint->join_relids,
													joinrelids),
											hint->join_relids))) {
						hstate->join_hint_level[i] = list_delete_cell(
								hstate->join_hint_level[i], l, prev);
					} else
						prev = l;
				}
			}
		}

		bms_free(joinrelids);
	}

	if (fpc_hint_state_enabled(lhint)) {
		set_join_config_options(DISABLE_ALL_JOIN,
				current_hint->context);
		return true;
	}
	return false;

}

/*
 * set_plain_rel_pathlist
 *	  Build access paths for a plain relation (no subquery, no inheritance)
 *
 * This function is copied and edited from set_plain_rel_pathlist() in
 * src/backend/optimizer/path/allpaths.c
 */
static void set_plain_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
		RangeTblEntry *rte) {
	/* Consider sequential scan */
	add_path(rel, create_seqscan_path(root, rel, NULL));

	/* Consider index scans */
	create_index_paths(root, rel);

	/* Consider TID scans */
	create_tidscan_paths(root, rel);

	/* Now find the cheapest of the paths for this rel */
	set_cheapest(rel);
}

static void rebuild_scan_path(FpcHintState *hstate, PlannerInfo *root,
		int level, List *initial_rels) {
	ListCell *l;

	foreach(l, initial_rels)
	{
		RelOptInfo *rel = (RelOptInfo *) lfirst(l);
		RangeTblEntry *rte;
		Fpc_Scan_Hint *hint;

		/* Skip relations which we can't choose scan method. */
		if (rel->reloptkind != RELOPT_BASEREL
				|| rel->rtekind != RTE_RELATION)
			continue;

		rte = root->simple_rte_array[rel->relid];

		/* We can't force scan method of foreign tables */
		if (rte->relkind == RELKIND_FOREIGN_TABLE)
			continue;

		/*
		 * Create scan paths with GUC parameters which are at the beginning of
		 * planner if scan method hint is not specified, otherwise use
		 * specified hints and mark the hint as used.
		 */
		if ((hint = find_scan_hint(root, rel)) == NULL) /* default : all enabled */
			set_scan_config_options(hstate->init_scan_mask,
					hstate->context);
		else {
			set_scan_config_options(hint->scan_enforce_mask,
					hstate->context);
			hint->base.state = HINT_STATUS_USED;
		}

		list_free_deep(rel->pathlist);
		rel->pathlist = NIL;
		if (rte->inh) {
			/* It's an "append relation", process accordingly */
			set_append_rel_pathlist(root, rel, rel->relid, rte);
		} else {
			set_plain_rel_pathlist(root, rel, rte);
		}
	}

	/*
	 * Restore the GUC variables we set above.
	 */
	set_scan_config_options(hstate->init_scan_mask, hstate->context);
}

/*
 * wrapper of make_join_rel()
 *
 * call make_join_rel() after changing enable_* parameters according to given
 * hints.
 */
static RelOptInfo *
make_join_rel_wrapper(PlannerInfo *root, RelOptInfo *rel1,
		RelOptInfo *rel2) {
	Relids joinrelids;
	Fpc_Join_Hint *hint;
	RelOptInfo *rel;
	int save_nestlevel;
	bool byepass_joinsearch;

	joinrelids = bms_union(rel1->relids, rel2->relids);
	hint = find_join_hint(joinrelids);
	bms_free(joinrelids);

	if (!hint && fpc_enable)
		byepass_joinsearch = true;
	else
		byepass_joinsearch = false;

	if (!hint)
		return fpc_make_join_rel(root, rel1, rel2, byepass_joinsearch);

	if (hint->inner_nrels == 0)
	{
		save_nestlevel = NewGUCNestLevel();

		set_join_config_options(hint->join_enforce_mask,
				current_hint->context);

		rel = fpc_make_join_rel(root, rel1, rel2, byepass_joinsearch);
		hint->base.state = HINT_STATUS_USED;

		/*
		 * Restore the GUC variables we set above.
		 */
		AtEOXact_GUC(true, save_nestlevel);
	}
	else
		rel = fpc_make_join_rel(root, rel1, rel2, byepass_joinsearch);

	return rel;
}

static int get_num_baserels(List *initial_rels) {
	int nbaserel = 0;
	ListCell *l;

	foreach(l, initial_rels)
	{
		RelOptInfo *rel = (RelOptInfo *) lfirst(l);

		if (rel->reloptkind == RELOPT_BASEREL)
			nbaserel++;
		else if (rel->reloptkind == RELOPT_JOINREL)
			nbaserel += bms_num_members(rel->relids);
		else {
			/* other values not expected here */
			elog(ERROR, "Unrecognized reloptkind type: %d",
					rel->reloptkind);
		}
	}

	return nbaserel;
}
/* Hook method for standard_join_search
 * This method is called from make_rel_from_joinlist () of allpaths.c
 * for standard_join_search */
static RelOptInfo *
fpc_join_search(PlannerInfo *root, int levels_needed,
		List *initial_rels) {
	Fpc_Join_Hint **join_method_hints;
	int nbaserel;
	RelOptInfo *rel;
	int i;
	bool order_hint_enable;

	/*
	 * Use standard planner (or geqo planner) if fpc is disabled or no
	 * valid hint is supplied.
	 */
	if (!current_hint) {
		if (prev_join_search)
			return (*prev_join_search)(root, levels_needed,
					initial_rels);
		else if (enable_geqo && levels_needed >= geqo_threshold)
			return geqo(root, levels_needed, initial_rels);
		else
			return standard_join_search(root, levels_needed,
					initial_rels);
	}

	/* We apply scan method hint rebuild scan path. */
	rebuild_scan_path(current_hint, root, levels_needed, initial_rels);

	/*
	 * In the case using GEQO, only scan method hints and Set hints have
	 * effect.  Join method and join order is not controllable by hints.
	 */
	if (enable_geqo && levels_needed >= geqo_threshold)
		return geqo(root, levels_needed, initial_rels);

	nbaserel = get_num_baserels(initial_rels);
	current_hint->join_hint_level = palloc0(
			sizeof(List *) * (nbaserel + 1));
	join_method_hints = palloc0(
			sizeof(Fpc_Join_Hint *) * (nbaserel + 1));

	order_hint_enable = process_join_hints(current_hint, root,
			nbaserel, initial_rels, join_method_hints);

	rel = fpc_standard_join_search(root, levels_needed, initial_rels);

	for (i = 2; i <= nbaserel; i++) {
		list_free(current_hint->join_hint_level[i]);

		/* free Order hint only */
		if (join_method_hints[i] != NULL
				&& join_method_hints[i]->join_enforce_mask
						== ENABLE_ALL_JOIN)
			Fpc_Join_Hint_Free(join_method_hints[i]);
	}
	pfree(current_hint->join_hint_level);
	pfree(join_method_hints);

	if (order_hint_enable)
		set_join_config_options(current_hint->init_join_mask,
				current_hint->context);

	return rel;
}

/*
 * set_rel_pathlist
 *	  Build access paths for a base relation
 *
 * This function is copied and edited from set_rel_pathlist() in
 * src/backend/optimizer/path/allpaths.c
 */
static void set_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
		Index rti, RangeTblEntry *rte)
{
	if (IS_DUMMY_REL(rel))
	{
		/* We already proved the relation empty, so nothing more to do */
	}
	else if (rte->inh)
	{
		/* It's an "append relation", process accordingly */
		set_append_rel_pathlist(root, rel, rti, rte);
	}
	else
	{
		if (rel->rtekind == RTE_RELATION)
		{
			if (rte->relkind == RELKIND_RELATION)
			{
				/* Plain relation */
				set_plain_rel_pathlist(root, rel, rte);
			}
			else
				elog(ERROR, "Unexpected relkind: %c", rte->relkind);
		}
		else
			elog(ERROR, "Unexpected rtekind: %d", (int ) rel->rtekind);
	}
}

/* wrapper for add_paths_to_joinrel() in optimizer/path/joinpath.c */
void fpc_add_paths_to_joinrel(PlannerInfo *root, RelOptInfo *joinrel,
		RelOptInfo *outerrel, RelOptInfo *innerrel, JoinType jointype,
		SpecialJoinInfo *sjinfo, List *restrictlist) {
	Fpc_Scan_Hint *scan_hint = NULL;
	Relids joinrelids;
	Fpc_Join_Hint *join_hint;
	int save_nestlevel;

	if ((scan_hint = find_scan_hint(root, innerrel)) != NULL) {
		set_scan_config_options(scan_hint->scan_enforce_mask,
				current_hint->context);
		scan_hint->base.state = HINT_STATUS_USED;
	}

	joinrelids = bms_union(outerrel->relids, innerrel->relids);
	join_hint = find_join_hint(joinrelids);
	bms_free(joinrelids);

	if (join_hint && join_hint->inner_nrels != 0)
	{
		save_nestlevel = NewGUCNestLevel();

		if (bms_equal(join_hint->inner_joinrelids, innerrel->relids))
		{

			set_join_config_options(join_hint->join_enforce_mask,
					current_hint->context);

			add_paths_to_joinrel(root, joinrel, outerrel, innerrel,
					jointype, sjinfo, restrictlist);
			join_hint->base.state = HINT_STATUS_USED;
		}
		else
		{
			set_join_config_options(DISABLE_ALL_JOIN,
					current_hint->context);
			add_paths_to_joinrel(root, joinrel, outerrel, innerrel,
					jointype, sjinfo, restrictlist);
		}

		/*
		 * Restore the GUC variables we set above.
		 */
		AtEOXact_GUC(true, save_nestlevel);
	}
	else
		add_paths_to_joinrel(root, joinrel, outerrel, innerrel,
				jointype, sjinfo, restrictlist);

	if (scan_hint != NULL)
		set_scan_config_options(current_hint->init_scan_mask,
				current_hint->context);
}

/* local definitions for standard postgres methods */
#define standard_join_search fpc_standard_join_search
#define join_search_one_level fpc_join_search_one_level
#define make_join_rel make_join_rel_wrapper
#include "core_postgres.c"

#undef make_join_rel
#define make_join_rel fpc_make_join_rel
#define add_paths_to_joinrel fpc_add_paths_to_joinrel
#include "make_join_rel.c"
