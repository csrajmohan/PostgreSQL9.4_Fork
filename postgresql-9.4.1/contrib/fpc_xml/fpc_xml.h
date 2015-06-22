
/* hint keywords */
#define HINT_SEQSCAN			"SeqScan"
#define HINT_INDEXSCAN			"IndexScan"
#define HINT_BITMAPHEAPSCAN		"BitmapHeapScan"
#define HINT_TIDSCAN			"TidScan"
#define HINT_INDEXONLYSCAN		"IndexOnlyScan"
#define HINT_NESTLOOP			"NestLoop"
#define HINT_MERGEJOIN			"MergeJoin"
#define HINT_HASHJOIN			"HashJoin"
#define HINT_ORDER				"Order"

#define DEFAULT_MAX_NOOF_HINTS 	20
#define NUM_HINT_TYPE			3

#define USUAL_COMMENT_START			"/*"
#define USUAL_COMMENT_END			"*/"
#define FPC_HINT_COMMENT_KEYWORD	"#"
#define FPC_HINT_START				USUAL_COMMENT_START FPC_HINT_COMMENT_KEYWORD
#define FPC_HINT_END				USUAL_COMMENT_END

/* Scan Types */
enum
{
	ENABLE_SEQSCAN = 0x01,
	ENABLE_INDEXSCAN = 0x02,
	ENABLE_BITMAPHEAPSCAN = 0x04,
	ENABLE_TIDSCAN = 0x08,
	ENABLE_INDEXONLYSCAN = 0x10
} SCAN_TYPE_BITS;

/* Join Types */
enum
{
	ENABLE_NESTLOOP = 0x01,
	ENABLE_MERGEJOIN = 0x02,
	ENABLE_HASHJOIN = 0x04
} JOIN_TYPE_BITS;


#define ENABLE_ALL_JOIN (ENABLE_NESTLOOP | ENABLE_MERGEJOIN | ENABLE_HASHJOIN)
#define DISABLE_ALL_JOIN 0

/* skips spaces */
#define ignore_space(str)	while (isspace(*str)) str++;

/* hint types */
typedef enum HintType
{
	HINT_TYPE_SCAN,
	HINT_TYPE_JOIN,
	HINT_TYPE_ORDER,
} HintType;

/* hint status */
typedef enum HintStatus
{
	HINT_STATUS_UNUSED = 0,		/* specified relation not used in query */
	HINT_STATUS_USED,			/* hint is used */
	HINT_STATUS_ERROR,			/* error */
	HINT_STATUS_DUP
} HintStatus;

/* to check which hints are enabled to apply */
#define fpc_hint_state_enabled(hint) ((hint)->base.state == HINT_STATUS_UNUSED || \
								  (hint)->base.state == HINT_STATUS_USED)


