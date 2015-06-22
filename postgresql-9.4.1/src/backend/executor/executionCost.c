/* -------------------------------------------------------------
 * executionCost.c
 * functions to compute actual execution cost.
 *
 * Same costing function are used as costsize.c.
 *
 * Estimated costs are stored in two variable:
 * startup_cost: cost that is expended before first tuple is fetched
 * total_cost: total cost to fetch all tuples
 *
 * -------------------------------------------------------------
 */

#include "postgres.h"
#include "executor/executor.h"
#include "executor/executionCost.h"
#include "nodes/execnodes.h"
#include "optimizer/cost.h"
#include "access/genam.h"
#include "utils/tuplesort.h"
#include "executor/instrument.h"
#include "pgstat.h"
#include "utils/rel.h"

#include "utils/elog.h"
/*
 * Result node Cost function.
 */
void exec_cost_result(ResultState *node)
{
	if(node->ps.lefttree)
	{
		node->ps.startup_cost=node->ps.lefttree->startup_cost;
		node->ps.total_cost=node->ps.lefttree->total_cost;
	}
	else
	{
		node->ps.startup_cost += node->ps.plan->startup_tuple + node->ps.plan->per_tuple;
		node->ps.total_cost += node->ps.startup_cost;
		node->ps.total_cost += cpu_tuple_cost;
		/*
		 * Adding cost of work done at this node to query_cost.
		 */
		query_cost += cpu_tuple_cost + node->ps.startup_cost;
	}
}

/*
 * Append Cost function.
 */
void exec_cost_append(AppendState *node)
{
	PlanState *subnode;
	subnode = node->appendplans[node->as_whichplan];

	/*Ensuring that this will be executed only first time*/
	if(node->ps.startup_cost==0)
	{
		/*
		 * Checking for first subplan
		 */
		if(node->as_whichplan==0)
		{
			node->ps.startup_cost=subnode->startup_cost;
		}
	}
	/* subplan cost*/
	/* subplan node cost increased in this cycle.*/
	node->ps.total_cost += subnode->total_cost - subnode->prev_total_cost;
	subnode->prev_total_cost = subnode->total_cost;
}

/*
 * Merge Append Cost Function
 * tuples_comp: tuples compared in plan node.
 * tuples_comp_heap_create: tuples compared while heap creation.
 * isNull: Tuple returned by node is Null, i.e. end of execution of that node.
 */
//void exec_cost_merge_append(MergeAppendState *node, double tuples_comp, double tuples_comp_heap_create, bool isNull)
//{
//	/*variable for measuring cost occurred at this node*/
//	Cost temp_query_cost=0;
//	Cost comparision_cost=2*cpu_operator_cost;
//	Cost per_tuple_cost=cpu_operator_cost;
//	int i;
//
//	/*Ensuring that this will be executed only first time*/
//	if(node->ps.startup_cost==0)
//	{
//		/*
//		 * tuples_comp_heap_create var holds total comparison count for
//		 * initial heap creation;
//		 */
//		node->ps.startup_cost+=tuples_comp_heap_create*comparision_cost;
//		node->ps.total_cost+=node->ps.startup_cost;
//		temp_query_cost+=node->ps.startup_cost;
//
//		/*
//		 * subplans cost
//		 */
//		for(i=0;i<node->ms_nplans;i++)
//		{
//			node->ps.startup_cost+=(node->mergeplans[i])->startup_cost;
//		}
//	}
//	/*
//	 * subplans cost
//	 */
//	for(i=0;i<node->ms_nplans;i++)
//	{
//		/*Sub plan node cost increased in this cycle*/
//		node->ps.total_cost+=(node->mergeplans[i])->total_cost - (node->mergeplans[i])->prev_total_cost;
//		(node->mergeplans[i])->prev_total_cost=(node->mergeplans[i])->total_cost;
//	}
//
//	/* CPU cost: per tuple processing cost
//	 * single tuple cost is added.
//	 */
//	/*Checking whether tuple returned by node is not null.*/
//	if(!isNull)
//	{
//		node->ps.total_cost+=per_tuple_cost;
//		temp_query_cost+=per_tuple_cost;
//	}
//	/*
//	 * cost for getting one tuple from heap and
//	 * adding one tuple to heap.
//	 */
//	node->ps.total_cost+=tuples_comp*comparision_cost;
//
//	temp_query_cost+=tuples_comp*comparision_cost;
//
//	/*
//	 * Adding cost of work done at this node to query_cost.
//	 */
//	query_cost+=temp_query_cost;
//}

/*
 * Recursive union Cost function
 * subplan_tuples: number of tuples in children node.
 */
//void exec_cost_recursive_union(RecursiveUnionState *node,double subplan_tuples)
//{
//	/*Ensuring that this will be executed only first time*/
//	if(node->ps.startup_cost==0)
//	{
//		node->ps.startup_cost+=node->ps.lefttree->startup_cost;
//	}
//	/*left subplan cost*/
//	/*left subplan node cost increased in this cycle*/
//	node->ps.total_cost+=node->ps.lefttree->total_cost-node->ps.lefttree->prev_total_cost;
//	node->ps.lefttree->prev_total_cost=node->ps.lefttree->total_cost;
//
//	/*right subplan cost*/
//	/*right subplan node cost increased in this cycle*/
//	node->ps.total_cost+=node->ps.righttree->total_cost-node->ps.righttree->prev_total_cost;
//	node->ps.righttree->prev_total_cost=node->ps.righttree->total_cost;
//
//	/*CPU Cost: per tuple processing cost*/
//	node->ps.total_cost+=(subplan_tuples)*node->ps.plan->per_tuple;
//
//	/*
//	 * Adding cost of work done at this node to query_cost.
//	 */
//	query_cost+=subplan_tuples*node->ps.plan->per_tuple;
//}

/*
 * Bitmap And Node Cost Function.
 * tbm_intersect_count: number of intersection operation performed on bitmaps.
 * subPath_cost: cost of child node.
 */
void exec_cost_bitmap_and_node(BitmapAndState *node, int tbm_intersect_count, double subPath_cost)
{
	Cost cpu_operator_cost_and_node = DEFAULT_CPU_OPERATOR_COST*100.0;

	/*
	 * Cost of subplan node and bitmap intersect cost
	 */
	Assert(subPath_cost >= 0);
	Assert(tbm_intersect_count >= 0);

	node->ps.startup_cost = subPath_cost + tbm_intersect_count * cpu_operator_cost_and_node;
	node->ps.total_cost = node->ps.startup_cost;
	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += tbm_intersect_count * cpu_operator_cost_and_node;
}

/*
 * Bitmap Or Node Cost Function.
 * tbm_union_count: number of intersection operation performed on bitmaps.
 * subPath_cost: cost of child node.
 */
void exec_cost_bitmap_or_node(BitmapOrState *node, int tbm_union_count, double subPath_cost)
{
	Cost cpu_operator_cost_or_node = DEFAULT_CPU_OPERATOR_COST * 100.0;

	/*
	 * cost of sub plan and bitmap union cost.
	 */
	Assert(subPath_cost >=0);
	Assert(tbm_union_count >=0);

	node->ps.startup_cost = subPath_cost + tbm_union_count * cpu_operator_cost_or_node;
	node->ps.total_cost = node->ps.startup_cost;
	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += tbm_union_count * cpu_operator_cost_or_node;
}

/*
 * Sequential Scan Cost Function
 * tuples: Number of tuples processed by plan node
 */
void exec_cost_seqscan(ScanState *node, double tuples)
{
	/*
	 * temp_query_cost holds cost of current node before calculating cost in this
	 * cost function call.
	 */
	Cost 	temp_query_cost;
	Cost 	per_page_cost;

	Assert(tuples >= 0);
	Assert(pages_read >= 0);

	temp_query_cost = node->ps.total_cost;
	per_page_cost = node->ps.plan->cost2;

	/*
	 * Following instructions will be executed when this cost function will be
	 * called first time.
	 */
	if(node->ps.startup_cost == 0)
	{
		node->ps.startup_cost += node->ps.plan->startup_tuple;
		node->ps.total_cost += node->ps.plan->startup_tuple;
	}
	/*
	 * disk costs
	 */

	node->ps.total_cost += pages_read * per_page_cost;
	pages_read=0;

	/* CPU costs */
	node->ps.total_cost += tuples * node->ps.plan->per_tuple;

	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += node->ps.total_cost - temp_query_cost;
}

/*
 * Index Scan Cost Function
 * tuples: Number of tuples processed by plan node
 */
void exec_cost_index1(IndexScanState *node,double tuples)
{
	/*
	 * temp_query_cost holds cost of current node before calculating cost in this
	 * cost function call.
	 */
	Cost 			temp_query_cost;
	unsigned int 	heap_pages_read;
	double 			max_io_cost,min_io_cost;

	/*
	 * if this index scan node is right node of some join node then loop count holds number of tuples
	 * in left of join node (estimated at compile time). This is useful in deciding costing formula
	 * only. This number is not used in calculation.
	 */
	double 			loop_count=node->loop_count;
	/*cost1: random page cost*/
	double 			spc_random_page_cost=node->ss.ps.plan->cost1;;
	/*cost2: sequential access page cost */
	double 			spc_seq_page_cost=node->ss.ps.plan->cost2;

	Assert(tuples >= 0);
	Assert(pages_read >= 0);
	Assert(index_pages_read >= 0);
	Assert(index_tuples >= 0);

	/*
	 * index_pages_read variable contains count of number of index pages read.
	 * pages_read contains count of total pages read. So for getting heap pages read
	 * index_pages_read is subtracted from pages_read;
	 */
	heap_pages_read = pages_read - index_pages_read;

	temp_query_cost = node->ss.ps.total_cost;
	/*
	 * This if block contains total_cost computation also. So if condition was applied to
	 * node->ss.ps.startup_cost then in case of startup_cost was zero total_cost instruction
	 * was executed more than one times.
	 */
	if(node->ss.ps.total_cost==0)
	{
		node->ss.ps.startup_cost = node->ss.ps.plan->startup_tuple;
		node->ss.ps.startup_cost += node->qual_arg_cost;
		/*
		 * adding a very small "fudge factor" that depends on the index size.
		 */
		node->ss.ps.startup_cost += node->iss_RelationDesc->rd_rel->relpages * node->spc_random_page_cost / 100000.0;
		node->ss.ps.startup_cost += node->num_sa_scans * 100.0 * cpu_operator_cost;
		//		node->ss.ps.startup_cost += node->index_pages*node->spc_random_page_cost/100000.0;
		node->ss.ps.total_cost = node->ss.ps.startup_cost;

		if(loop_count>1)
		{
			node->ss.ps.total_cost += heap_pages_read * spc_random_page_cost;
		}
		else
		{
			max_io_cost = heap_pages_read * spc_random_page_cost;

			if(heap_pages_read>=1)
			{
				min_io_cost = spc_random_page_cost;
				min_io_cost += (heap_pages_read - 1) * spc_seq_page_cost;
			}
			node->ss.ps.total_cost += max_io_cost+node->index_correlation * (min_io_cost - max_io_cost);
		}
	}
	else
	{
		if(loop_count>1)
		{
			/*
			 * spc_random_page_cost
			 */
			node->ss.ps.total_cost += heap_pages_read * spc_random_page_cost;
		}
		else
		{
			max_io_cost = heap_pages_read * spc_random_page_cost;
			min_io_cost = heap_pages_read * spc_seq_page_cost;
			node->ss.ps.total_cost += max_io_cost + node->index_correlation * (min_io_cost - max_io_cost);
			//			node->ss.ps.total_cost+=spc_random_page_cost;
			//			node->ss.ps.total_cost+=node->ss.ps.plan->per_tuple;
		}
	}
	/*
	 * Per tuple cost
	 */

	/*
	 * CPU cost: any complex expressions in the indexquals will need to be
	 * evaluated. We model the per-tuple CPU costs as cpu_index_tuple_cost
	 * plus one cpu_operator_cost per indexqual operator and add costs for
	 * any index ORDER BY expressions.
	 * These all costs are included in plan->per_tuple cost;
	 */

	/*
	 * Adding cost of proceesing heap tuples.
	 */
	node->ss.ps.total_cost+= tuples * node->ss.ps.plan->per_tuple;

	/*
	 * Adding cost of processing index tuples.
	 */
	node->ss.ps.total_cost+= index_tuples * node->per_tuple_cost;

	/*
	 * For a index scan, we just charge spc_random_page_cost per
	 * page touched.
	 */
	node->ss.ps.total_cost+= index_pages_read * node->spc_random_page_cost;

	/*
	 * clearing all counter values.
	 */
	index_tuples= 0;
	index_pages_read= 0;
	pages_read= 0;

	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost+= node->ss.ps.total_cost - temp_query_cost;
}

/*
 * Bitmap Index Node Cost function
 * indexTuples: number of index tuples processed.
 */
void exec_cost_bitmap_index_scan(BitmapIndexScanState *node, double indexTuples)
{
	/*
	 * temp_query_cost holds cost of current node before calculating cost in this
	 * cost function call.
	 */
	Cost temp_query_cost;

	Assert(indexTuples >= 0);
	Assert(index_pages_read >= 0);

	temp_query_cost=node->ss.ps.total_cost;

	/*
	 * Following instructions will be executed when this cost function will be
	 * called first time.
	 */
	if(node->ss.ps.total_cost==0)
	{
		node->ss.ps.total_cost += node->biss_RelationDesc->rd_rel->relpages * node->spc_random_page_cost / 10000.0;
//		node->ss.ps.total_cost += node->index_pages * node->spc_random_page_cost/10000.0;
		node->ss.ps.total_cost += node->qual_arg_cost;

		node->ss.ps.total_cost += node->num_sa_scans * 100.0 * cpu_operator_cost;
		node->ss.ps.total_cost += 0.1 * cpu_operator_cost * node->biss_RelationDesc->rd_rel->reltuples;
	}

	node->ss.ps.total_cost += index_pages_read * node->spc_random_page_cost;
	node->ss.ps.total_cost += indexTuples * node->per_tuple_cost;

	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += node->ss.ps.total_cost - temp_query_cost;
	index_pages_read = 0;
	pages_read = 0;
}

/*
 * Bitmap Heap Scan Cost Function
 * tuples: Number of tuples processed by plan node
 */

void exec_cost_bitmap_heap_scan(BitmapHeapScanState *node, double tuples)
{

	double pages_access;
	double prev_io_cost;
	double diff_io_cost=0;
	/*variable for measuring cost occurred at this node*/
	Cost temp_query_cost=0;

	Cost per_page_cost;

	Assert(tuples >= 0);
	Assert(pages_read >= 0);

	/*Ensuring that this will be executed only first time*/
	if(node->ss.ps.startup_cost==0)
	{
		node->ss.ps.startup_cost += node->ss.ps.plan->startup_tuple;
		node->ss.ps.startup_cost += node->ss.ps.lefttree->total_cost;
		node->ss.ps.total_cost += node->ss.ps.plan->startup_tuple;

		temp_query_cost += node->ss.ps.plan->startup_tuple;
	}

	node->ss.ps.total_cost += node->ss.ps.lefttree->total_cost - node->ss.ps.lefttree->prev_total_cost;
	node->ss.ps.lefttree->prev_total_cost = node->ss.ps.lefttree->total_cost;


	prev_io_cost = node->io_cost;
	/*disk costs*/
	if(pages_read!=0)
	{
		if(node->ss.ss_currentRelation->pgstat_info==NULL||pgstat_track_counts != true)
			elog(ERROR ,"Default statistics collecting counter are not active");


		pages_access = node->ss.ss_currentRelation->pgstat_info->t_counts.t_blocks_fetched - node->ss.ss_currentRelation->pgstat_info->t_counts.t_blocks_hit;

		if(pages_access>=2.0)
		{
			double correlation;

			if(pages_access < node->ss.ss_currentRelation->rd_rel->relpages)
				correlation = sqrt(pages_access / node->ss.ss_currentRelation->rd_rel->relpages);
			else
				correlation = 1.0;

			per_page_cost = random_page_cost - (random_page_cost - seq_page_cost) * correlation;
		}
		else
			per_page_cost = random_page_cost;

		node->io_cost = pages_access * per_page_cost;

		diff_io_cost = node->io_cost - prev_io_cost;
	}

	//	node->ss.ps.total_cost+=pages_read*per_page_cost;
	//	temp_query_cost+=pages_read*per_page_cost;

	/*CPU Cost: tuple processing cost*/

	/*
	 * if isNull is true then last tuple will be null.
	 * So (tuples-1) is multiplied by per_tuple cost.
	 */


	node->cpu_cost += tuples * node->ss.ps.plan->per_tuple;
	temp_query_cost += tuples * node->ss.ps.plan->per_tuple;

	//	if(isNull)
	//	{
	//		node->ss.ps.total_cost+=(tuples-1)*node->ss.ps.plan->per_tuple;
	//		temp_query_cost+=(tuples-1)*node->ss.ps.plan->per_tuple;
	//	}
	//	else
	//	{
	//		node->ss.ps.total_cost+=tuples*node->ss.ps.plan->per_tuple;
	//		temp_query_cost+=tuples*node->ss.ps.plan->per_tuple;
	//	}

//	node->ss.ps.total_cost=node->cpu_cost+node->io_cost;

	temp_query_cost += diff_io_cost;
	node->ss.ps.total_cost += temp_query_cost;

	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += temp_query_cost;
	pages_read = 0;
}

/*
 * TID Scan Cost Function
 * tuples: Number of tuples processed by plan node
 * isNull: Tuple returned by node is Null, i.e. end of execution of that node.
 */
void exec_cost_tidscan(ScanState *node, double tuples)
{
	/*variable for measuring cost occurred at this node*/
	Cost temp_query_cost;

	/*cost2: per page cost*/
	Cost per_page_cost=node->ps.plan->cost2;

	Assert(tuples >= 0);
	Assert(pages_read >= 0);

	temp_query_cost=node->ps.total_cost;

	/*Ensuring that this will be executed only first time*/
	if(node->ps.startup_cost==0)
	{
		node->ps.startup_cost += node->ps.plan->per_tuple;
		node->ps.total_cost += node->ps.startup_cost;
	}
	/*
	 * CPU costs : per tuple processing cost.
	 */
	/*
	 * if isNull is true then last tuple will be null.
	 * So (tuples-1) is multiplied by per_tuple cost.
	 */
	node->ps.total_cost += tuples * node->ps.plan->per_tuple;

	/* random_page_cost is used for per page access. Assuming each tuple
	 * on a different page.
	 */
	//	node->ps.total_cost+=pages_read*node->ps.plan->per_page;
	node->ps.total_cost +=pages_read * per_page_cost;
	pages_read = 0;

	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += node->ps.total_cost - temp_query_cost;
}

/*
 * Sub query Scan Cost Function
 * tuples: Number of tuples processed by plan node
 * isNull: Tuple returned by node is Null, i.e. end of execution of that node.
 */
void exec_cost_subqueryscan(SubqueryScanState *node,double tuples)
{
	/*variable for measuring cost occurred at this node*/
	Cost temp_query_cost=0;

	Assert(tuples >= 0);
	Assert(pages_read >= 0);

	/*Ensuring that this will be executed only first time*/
	if(node->ss.ps.startup_cost==0)
	{
		node->ss.ps.startup_cost += node->ss.ps.plan->startup_tuple;
		node->ss.ps.total_cost += node->ss.ps.plan->startup_tuple;
		/*subplan cost*/
		node->ss.ps.startup_cost += node->subplan->startup_cost;

		temp_query_cost += node->ss.ps.plan->startup_tuple;
	}

	/*subplan cost*/
	/*Sub plan node cost increased in this cycle*/
	node->ss.ps.total_cost += node->subplan->total_cost - node->subplan->prev_total_cost;
	node->subplan->prev_total_cost = node->subplan->total_cost;

	/*CPU cost: per tuple processing cost*/
	/*
	 * if isNull is true then last tuple will be null.
	 * So (tuples-1) is multiplied by per_tuple cost.
	 */

	node->ss.ps.total_cost += tuples * node->ss.ps.plan->per_tuple;
	temp_query_cost += tuples * node->ss.ps.plan->per_tuple;
	pages_read = 0;
	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += temp_query_cost;
}

/*
 * Function Scan Cost Function
 * tuples: Number of tuples processed by plan node
 * isNull: Tuple returned by node is Null, i.e. end of execution of that node.
 */
void exec_cost_functionscan(ScanState *node,double tuples)
{
	/*variable for measuring cost occurred at this node*/
	Cost temp_query_cost;
	temp_query_cost = node->ps.total_cost;

	/*Ensuring that this will be executed only first time*/
	if(node->ps.startup_cost==0)
	{
		node->ps.startup_cost += node->ps.plan->startup_tuple;
		node->ps.total_cost += node->ps.plan->startup_tuple;
	}

	/*CPU cost: per tuple processing cost*/
	node->ps.total_cost += tuples * node->ps.plan->per_tuple;

	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += node->ps.total_cost - temp_query_cost;
}

/*
 * Value Scan Cost Function
 * tuples: Number of tuples processed by plan node
 * isNull: Tuple returned by node is Null, i.e. end of execution of that node.
 */
void exec_cost_valuesscan(ScanState *node,double tuples)
{
	/*variable for measuring cost occurred at this node*/
	Cost temp_query_cost;
	temp_query_cost = node->ps.total_cost;

	Assert(tuples >= 0);

	/*Ensuring that this will be executed only first time*/
	if(node->ps.startup_cost==0)
	{
		node->ps.startup_cost += node->ps.plan->startup_tuple;
		node->ps.total_cost += node->ps.plan->startup_tuple;
	}

	/*CPU cost: per tuple processing cost*/
	node->ps.total_cost += tuples * node->ps.plan->per_tuple;

	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += node->ps.total_cost - temp_query_cost;

}

/*
 * CTE Scan Cost Function
 * tuples: Number of tuples processed by plan node
 * isNull: Tuple returned by node is Null, i.e. end of execution of that node.
 */
void exec_cost_ctescan(ScanState *node,double tuples)
{
	/*variable for measuring cost occurred at this node*/
	Cost temp_query_cost;
	temp_query_cost = node->ps.total_cost;

	Assert(tuples >= 0);

	/*Ensuring that this will be executed only first time*/
	if(node->ps.startup_cost==0)
	{
		node->ps.startup_cost += node->ps.plan->startup_tuple;
		node->ps.total_cost += node->ps.startup_cost;
	}
	/*CPU cost: per tuple processing cost*/
	node->ps.total_cost += tuples * node->ps.plan->per_tuple;

	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += tuples * node->ps.total_cost - temp_query_cost;
}

/*
 * Foreign Scan Cost Function
 * tuples: Number of tuples processed by plan node
 * isNull: Tuple returned by node is Null, i.e. end of execution of that node.
 */

//void exec_cost_foreign_scan(ForeignScanState *node,double tuples, bool isNull)
//{
//	/*variable for measuring cost occurred at this node*/
//	Cost temp_query_cost=0;
//
//	/*Ensuring that this will be executed only first time*/
//	if(node->ss.ps.startup_cost==0)
//	{
//		node->ss.ps.startup_cost+=node->ss.ps.plan->startup_tuple;
//		node->ss.ps.total_cost+=node->ss.ps.startup_cost;
//		temp_query_cost+=node->ss.ps.startup_cost;
//	}
//
//	/*
//	 * if isNull is true then last tuple will be null.
//	 * So (tuples-1) is multiplied by per_tuple cost.
//	 */
//	if(isNull)
//	{
//		node->ss.ps.total_cost+=(tuples-1)*node->ss.ps.plan->per_tuple;
//		temp_query_cost+=(tuples-1)*node->ss.ps.plan->per_tuple;
//	}
//	else
//	{
//		node->ss.ps.total_cost+=tuples*node->ss.ps.plan->per_tuple;
//		temp_query_cost+=tuples*node->ss.ps.plan->per_tuple;
//	}
//	node->ss.ps.total_cost+=pages_read*seq_page_cost;
//	pages_read=0;
//	/*
//	 * Adding cost of work done at this node to query_cost.
//	 */
//	query_cost+=temp_query_cost;
//}


/*
 * Nested Loop Cost function.
 * outer_tuples: number of tuples of outer plan node fetched for processing.
 * tuples_comp_count: number of inner and outer node tuples compared.
 * rescan_startup_cost: Rescan Startup cost for inner plan node.
 */
void exec_cost_nestloop1(NestLoopState *node,double outer_tuples, double tuple_comp_count, Cost rescan_startup_cost)
{
	/*variable for measuring cost occurred at this node*/
	Cost temp_query_cost=0;

	Assert(outer_tuples >= 0);
	Assert(tuple_comp_count >= 0);
	Assert(rescan_startup_cost >= 0);

	/*
	 * For same outer tuple, another inner tuple is found, i.e. in same cycle another match has occurred.
	 */
	if(outer_tuples==0)
	{
		/*left subplan cost*/
		/*left subplan node cost increased in this cycle*/
		node->js.ps.total_cost += node->js.ps.lefttree->total_cost - node->js.ps.lefttree->prev_total_cost;
		node->js.ps.lefttree->prev_total_cost = node->js.ps.lefttree->total_cost;

		/*right subplan cost */
		/*right subplan node cost increased in this cycle*/
		node->js.ps.total_cost += node->js.ps.righttree->total_cost-node->js.ps.righttree->prev_total_cost;
		node->js.ps.righttree->prev_total_cost = node->js.ps.righttree->total_cost;

		/*tuple comparison cost*/
		node->js.ps.total_cost += tuple_comp_count * node->js.ps.plan->per_tuple;
		temp_query_cost += tuple_comp_count * node->js.ps.plan->per_tuple;
	}
	/* In next cycle matching tuple is found*/
	else if(outer_tuples==1)
	{
		/*First time NestLoop node has returned a tuple*/
		if(node->js.ps.startup_cost==0)
		{
			node->js.ps.startup_cost += node->js.ps.lefttree->startup_cost + node->js.ps.righttree->startup_cost;
			node->js.ps.startup_cost += node->js.ps.plan->startup_tuple;
			node->js.ps.total_cost += node->js.ps.plan->startup_tuple;
			//			node->js.ps.total_cost+=node->js.ps.startup_cost;			//TBT

			temp_query_cost += node->js.ps.plan->startup_tuple;
		}
		/*
		 * Matching tuple is found in next cycle, i.e. for next outer tuple.
		 */
		else
		{
			node->js.ps.total_cost+=rescan_startup_cost;
			temp_query_cost+=rescan_startup_cost;
		}
		/*subplan cost*/
		/*Sub plan node cost increased in this cycle*/
		node->js.ps.total_cost+=node->js.ps.lefttree->total_cost-node->js.ps.lefttree->prev_total_cost;
		node->js.ps.total_cost+=node->js.ps.righttree->total_cost-node->js.ps.righttree->prev_total_cost;

		node->js.ps.lefttree->prev_total_cost = node->js.ps.lefttree->total_cost;
		node->js.ps.righttree->prev_total_cost = node->js.ps.righttree->total_cost;

		/*tuple comparison cost*/
		node->js.ps.total_cost+=tuple_comp_count*node->js.ps.plan->per_tuple;

		temp_query_cost+=tuple_comp_count*node->js.ps.plan->per_tuple;
	}
	/*
	 * After more than one cycle matching tuple is found.
	 */
	else
	{
		/*First time NestLoop node has returned a tuple*/
		if(node->js.ps.startup_cost==0)
		{
			node->js.ps.startup_cost+=node->js.ps.lefttree->startup_cost+node->js.ps.righttree->startup_cost;
			node->js.ps.startup_cost+=node->js.ps.plan->startup_tuple;
			node->js.ps.total_cost+=node->js.ps.plan->startup_tuple;

			temp_query_cost+=node->js.ps.plan->startup_tuple;
		}
		/*
		 * Matching tuple is found after more than one cycle, i.e. after more than
		 * one outer tuple.
		 */
		else
		{
			node->js.ps.total_cost+=(outer_tuples-1)*rescan_startup_cost;
			temp_query_cost+=(outer_tuples-1)*rescan_startup_cost;
		}
		/*subplan cost*/
		/*Sub plan node cost increased in this cycle*/
		node->js.ps.total_cost+=node->js.ps.lefttree->total_cost-node->js.ps.lefttree->prev_total_cost;
		node->js.ps.total_cost+=node->js.ps.righttree->total_cost-node->js.ps.righttree->prev_total_cost;

		node->js.ps.lefttree->prev_total_cost=node->js.ps.lefttree->total_cost;
		node->js.ps.righttree->prev_total_cost=node->js.ps.righttree->total_cost;

		/*tuple comparison cost*/
		node->js.ps.total_cost+=tuple_comp_count*node->js.ps.plan->per_tuple;
		temp_query_cost+=tuple_comp_count*node->js.ps.plan->per_tuple;
	}
	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost+=temp_query_cost;
}

/*
 * Merge Join Cost function
 * tuples_comp: number of tuples compared.
 * isNull: Tuple returned by node is Null, i.e. end of execution of that node.
 */
void exec_cost_mergejoin(MergeJoinState *node, double tuples_comp, int tuple)
{
	/*variable for measuring cost occurred at this node*/
	Cost temp_query_cost=0;

	Cost merge_qual_startup_cost = node->js.ps.plan->cost1;
	Cost merge_qual_per_tuple_cost = node->js.ps.plan->cost2;

	Assert(tuples_comp >= 0);
	Assert(tuple >= 0);

	/*Ensuring that this will be executed only first time*/
	if(node->js.ps.startup_cost==0)
	{
		/*
		 * At this point first tuple will be returned by merge join node.
		 * So cost of left and right sub tree(up to this point) is added in startup_cost.
		 *
		 */
		node->js.ps.startup_cost += node->js.ps.lefttree->total_cost;
		node->js.ps.startup_cost += node->js.ps.righttree->total_cost;

		node->js.ps.lefttree->prev_total_cost = node->js.ps.lefttree->total_cost;
		node->js.ps.righttree->prev_total_cost = node->js.ps.righttree->total_cost;

		/*merge qual start up cost*/
		node->js.ps.startup_cost += merge_qual_startup_cost;

		/*qp qual start up cost*/
		node->js.ps.startup_cost += node->js.ps.plan->startup_tuple;

		/*
		 * CPU Cost: tuple comparison cost
		 * (tuple_comp-1) will be number of comparisons without comparison of matching tuple
		 * which is returned in this function call.
		 */
		node->js.ps.startup_cost += (tuples_comp - 1) * merge_qual_per_tuple_cost;

		/*
		 * adding startup cost to total cost;
		 */
		node->js.ps.total_cost = node->js.ps.startup_cost;
		/*
		 * CPU Cost: tuple comparison cost
		 * this is comparison cost of matching tuple returned in this function call.
		 * This is added in total_cost
		 */
		node->js.ps.total_cost += merge_qual_per_tuple_cost;

		temp_query_cost += merge_qual_startup_cost;
		temp_query_cost += node->js.ps.plan->startup_tuple;
		temp_query_cost += tuples_comp * merge_qual_per_tuple_cost;
	}
	else
	{
		/*
		 * left and right subplan cost.
		 * Sub plan node cost increased in this cycle.
		 */
		node->js.ps.total_cost += node->js.ps.lefttree->total_cost - node->js.ps.lefttree->prev_total_cost;
		node->js.ps.total_cost += node->js.ps.righttree->total_cost - node->js.ps.righttree->prev_total_cost;

		node->js.ps.lefttree->prev_total_cost = node->js.ps.lefttree->total_cost;
		node->js.ps.righttree->prev_total_cost = node->js.ps.righttree->total_cost;

		/*
		 * CPU Cost: tuple comparison cost
		 */
		node->js.ps.total_cost += tuples_comp * merge_qual_per_tuple_cost;

		temp_query_cost += tuples_comp * merge_qual_per_tuple_cost;
	}

	/*CPU Cost: tuple processing cost*/
	node->js.ps.total_cost += tuple * node->js.ps.plan->per_tuple;
	temp_query_cost += tuple * node->js.ps.plan->per_tuple;

	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += temp_query_cost;
}

/*
 * Hash Join Cost function
 * outer_tuples: number of tuples of outer plan node fetched for processing.
 * qual_tuples_comp: number of qual_comp done at node.
 * hash_qual_tuples_comp: number of tuple comparison is made inside hash bucket.
 */
void exec_cost_hashjoin(HashJoinState *node, double outer_tuples, double qual_tuples_comp, double hash_qual_tuples_comp)
{

	/*variable for measuring cost occurred at this node*/
	Cost 			temp_query_cost=0;

	Cost 			hash_qual_startup_cost=node->js.ps.plan->cost1;
	Cost			hash_qual_per_tuple_cost=node->js.ps.plan->cost2;
	int 			num_hashclauses;
	unsigned int 	pages_access;

	Assert(outer_tuples >= 0);
	Assert(qual_tuples_comp >= 0);
	Assert(hash_qual_tuples_comp >= 0);
	Assert(pages_read >= 0);
	Assert(pages_written >= 0);


	num_hashclauses = list_length(node->hashclauses);
	pages_access = pages_read+pages_written;

	/*Ensuring that this will be executed only first time*/
	if(node->js.ps.startup_cost==0)
	{
		node->js.ps.startup_cost += node->js.ps.lefttree->startup_cost;
		/*
		 * Hash Node cost.
		 */
		node->js.ps.startup_cost += node->js.ps.righttree->total_cost;

		/*hash_qual_cost*/
		node->js.ps.startup_cost += hash_qual_startup_cost;

		/*qp_qual cost*/
		node->js.ps.startup_cost += node->js.ps.plan->startup_tuple;

		/*
		 * adding hash_qual_startup_cost part to total_cost and qp_qual
		 * cost.
		 */
		node->js.ps.total_cost += hash_qual_startup_cost;
		node->js.ps.total_cost += node->js.ps.plan->startup_tuple;

		temp_query_cost += hash_qual_startup_cost;
		temp_query_cost += node->js.ps.plan->startup_tuple;
	}

	/* left subplan node cost*/
	/* left subplan node cost increased in this cycle.*/
	/* remembering total_cost added to this node using prev_total_cost. */
	node->js.ps.total_cost += node->js.ps.lefttree->total_cost - node->js.ps.lefttree->prev_total_cost;
	node->js.ps.lefttree->prev_total_cost = node->js.ps.lefttree->total_cost;

	/* right subplan node cost*/
	/* right subplan node cost increased in this cycle.*/
	/* remembering total_cost added to this node using prev_total_cost. */
	node->js.ps.total_cost += node->js.ps.righttree->total_cost - node->js.ps.righttree->prev_total_cost;
	node->js.ps.righttree->prev_total_cost = node->js.ps.righttree->total_cost;

	/*hash cost for per outer tuple*/
	node->js.ps.total_cost += outer_tuples * num_hashclauses * DEFAULT_CPU_OPERATOR_COST;

	/*disk cost*/
	node->js.ps.total_cost += pages_access * DEFAULT_SEQ_PAGE_COST;

	/*
	 * cost for comparing outer sub node tuple node in one of the
	 * hash bucket of inner plan node.
	 */
	node->js.ps.total_cost += hash_qual_tuples_comp * hash_qual_per_tuple_cost;

	/*
	 * other qual condition cost
	 */
	node->js.ps.total_cost += qual_tuples_comp * node->js.ps.plan->per_tuple;

	temp_query_cost += outer_tuples * num_hashclauses*DEFAULT_CPU_OPERATOR_COST;
	temp_query_cost += pages_access * DEFAULT_SEQ_PAGE_COST;

	temp_query_cost += hash_qual_tuples_comp * hash_qual_per_tuple_cost;
	temp_query_cost += qual_tuples_comp * node->js.ps.plan->per_tuple;

	pages_read = 0;
	pages_written = 0;
	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += temp_query_cost;
}


/*
 * Material Cost Function
 * tuples: Number of tuples processed by plan node
 */
void exec_cost_material(MaterialState *node, int tuples)
{
	/*
	 * temp_query_cost holds cost of current node before calculating cost in this
	 * cost function call.
	 */
	Cost 		temp_query_cost = 0;
	double 		pages_access;
	Cost 		per_page_cost = node->ss.ps.plan->cost2;

	Assert(tuples >= 0);
	Assert(pages_read >= 0);
	Assert(pages_written >= 0);

	pages_access = pages_read + pages_written;

	/*Ensuring that following instructions will be executed only once*/
	if(node->ss.ps.startup_cost==0)
	{
		node->ss.ps.startup_cost += node->ss.ps.lefttree->startup_cost;
	}

	/*subplan cost*/
	/*Sub plan node cost increased in this cycle*/
	node->ss.ps.total_cost += node->ss.ps.lefttree->total_cost-node->ss.ps.lefttree->prev_total_cost;
	node->ss.ps.lefttree->prev_total_cost = node->ss.ps.lefttree->total_cost;

	/*Disk cost*/
	node->ss.ps.total_cost += pages_access * per_page_cost;

	temp_query_cost += pages_access * per_page_cost;

	/* CPU cost: per tuple processing cost*/

	node->ss.ps.total_cost += tuples * node->ss.ps.plan->per_tuple;
	temp_query_cost += tuples * node->ss.ps.plan->per_tuple;

	pages_read = 0;
	pages_written = 0;

	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += temp_query_cost;
}


/*
 * Sort Cost function
 * tuples_comparision: number of tuples compared for sorting.
 * output_tuples: tuples returned by this node.
 */
void exec_cost_sort(SortState *node, int output_tuples)
{
	/*variable for measuring cost occurred at this node*/
	Cost 	temp_query_cost=0;

	/*cost2: per page cost*/
	Cost 	per_page_cost = node->ss.ps.plan->cost2;
	Cost 	comparision_cost = node->ss.ps.plan->startup_tuple;
	int 	pages_access;

	Assert(output_tuples >= 0);
	Assert(pages_read >= 0);
	Assert(pages_written >= 0);
	Assert(tuple_comparisions >= 0);

	pages_access=pages_read+pages_written;

	/*Ensuring that this will be executed only first time*/
	if(node->ss.ps.startup_cost==0)
	{
		node->ss.ps.startup_cost += node->ss.ps.lefttree->total_cost;
		node->ss.ps.startup_cost += tuple_comparisions * comparision_cost;
		node->ss.ps.startup_cost += pages_access * per_page_cost;
		node->ss.ps.total_cost += node->ss.ps.startup_cost;

		/* Calculate cost for work that has been done in this plan node.
		 * That's why exclude leftree's total cost.
		 */
		temp_query_cost += node->ss.ps.startup_cost - node->ss.ps.lefttree->total_cost;
	}
	else
	{
		/* page access cost denotes cost of fetching tuple from tuple store
		 * after doing sorting.
		 */
		node->ss.ps.total_cost += pages_access * per_page_cost;
		temp_query_cost += pages_access * per_page_cost;
	}

	/*
	 * CPU cost: per tuple processing cost
	 */
	node->ss.ps.total_cost += node->ss.ps.plan->per_tuple * output_tuples;
	temp_query_cost += node->ss.ps.plan->per_tuple * output_tuples;

	pages_read=0;
	pages_written=0;
	tuple_comparisions=0;

	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost+=temp_query_cost;
}



/*
 * Group Node Cost Function
 * tuples: Number of tuples processed by plan node
 */
void exec_cost_group(GroupState *node,double tuples)
{

	/*variable for measuring cost occurred at this node*/
	Cost temp_query_cost=0;

	Assert(tuples >= 0);

	/*Ensuring that this will be executed only first time*/
	if(node->ss.ps.startup_cost==0)
	{
		/*subplan cost*/
		node->ss.ps.startup_cost = node->ss.ps.lefttree->startup_cost;
	}
	/*subplan cost*/
	/*Sub plan node cost increased in this cycle*/
	node->ss.ps.total_cost += node->ss.ps.lefttree->total_cost - node->ss.ps.lefttree->prev_total_cost;
	node->ss.ps.lefttree->prev_total_cost = node->ss.ps.lefttree->total_cost;

	/*
	 * per tuple cost.
	 */
	node->ss.ps.total_cost += tuples * node->ss.ps.plan->per_tuple;
	temp_query_cost += tuples * node->ss.ps.plan->per_tuple;
	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += temp_query_cost;
}


/*
 * Aggregate Node Cost function
 * tuples: Number of tuples processed by plan node
 * isNull: Tuple returned by node is Null, i.e. end of execution of that node.
 */
void exec_cost_agg(AggState *aggState,double tuples, int output_tuple)
{
	/*variable for measuring cost occurred at this node*/
	Cost 	temp_query_cost=0;

	Cost per_tuple_cost=aggState->ss.ps.plan->cost2;
	Agg *node=(Agg*)aggState->ss.ps.plan;

	Assert(tuples >= 0);
	Assert(output_tuple >= 0);

	switch(node->aggstrategy)
	{
	case AGG_PLAIN:
		if(aggState->ss.ps.startup_cost==0)
		{
//			aggState->ss.ps.startup_cost+=aggState->ss.ps.plan->startup_tuple;
			aggState->ss.ps.startup_cost += aggState->ss.ps.plan->per_tuple * tuples;

			/*subplan cost*/
			aggState->ss.ps.startup_cost += aggState->ss.ps.lefttree->total_cost;

			aggState->ss.ps.total_cost = aggState->ss.ps.startup_cost + per_tuple_cost;

			temp_query_cost += aggState->ss.ps.plan->startup_tuple;
			temp_query_cost += aggState->ss.ps.plan->per_tuple * tuples;
			temp_query_cost += per_tuple_cost;
		}
		break;
	case AGG_SORTED:

		/*Ensuring that this will be executed only first time*/
		if(aggState->ss.ps.startup_cost==0)
		{
			/*subplan cost*/
			aggState->ss.ps.startup_cost += aggState->ss.ps.lefttree->startup_cost;
		}

		/*subplan cost*/
		/*Sub plan node cost increased in this cycle*/
		aggState->ss.ps.total_cost += aggState->ss.ps.lefttree->total_cost - aggState->ss.ps.lefttree->prev_total_cost;
		aggState->ss.ps.lefttree->prev_total_cost = aggState->ss.ps.lefttree->total_cost;

		aggState->ss.ps.total_cost += tuples * aggState->ss.ps.plan->per_tuple;
		temp_query_cost += tuples * aggState->ss.ps.plan->per_tuple;

		aggState->ss.ps.total_cost += per_tuple_cost * output_tuple;
		temp_query_cost += per_tuple_cost * output_tuple;
		break;
	case AGG_HASHED:

		aggState->ss.ps.startup_cost += aggState->ss.ps.lefttree->total_cost - aggState->ss.ps.lefttree->prev_total_cost;
		aggState->ss.ps.total_cost += aggState->ss.ps.lefttree->total_cost - aggState->ss.ps.lefttree->prev_total_cost;

		aggState->ss.ps.lefttree->prev_total_cost = aggState->ss.ps.lefttree->total_cost;

		/*
		 * CPU cost: tuple processing cost.
		 */
		aggState->ss.ps.startup_cost += tuples * aggState->ss.ps.plan->per_tuple;
		aggState->ss.ps.total_cost += tuples * aggState->ss.ps.plan->per_tuple;
		temp_query_cost += tuples * aggState->ss.ps.plan->per_tuple;

		aggState->ss.ps.total_cost += per_tuple_cost * output_tuple;
		temp_query_cost+=per_tuple_cost * output_tuple;
		break;
	}
	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost+=temp_query_cost;
}

/*
 * Window Aggregate Node Cost Function
 * tuples: Number of tuples processed by plan node
 * isNull: Tuple returned by node is Null, i.e. end of execution of that node.
 */
//void exec_cost_windowagg(WindowAggState *node,double tuples,bool isNull)
//{
//
//	/*variable for measuring cost occurred at this node*/
//	Cost temp_query_cost=0;
//
//	/*Ensuring that this will be executed only first time*/
//	if(node->ss.ps.startup_cost==0)
//	{
//		node->ss.ps.startup_cost+=node->ss.ps.lefttree->startup_cost;
//		node->ss.ps.startup_cost+=node->ss.ps.plan->startup_tuple;
//
//		node->ss.ps.total_cost+=node->ss.ps.plan->startup_tuple;
//
//		temp_query_cost+=node->ss.ps.plan->startup_tuple;
//	}
//
//	/*subplan cost*/
//	/*Sub plan node cost increased in this cycle*/
//	node->ss.ps.total_cost+=node->ss.ps.lefttree->total_cost-node->ss.ps.lefttree->prev_total_cost;
//	node->ss.ps.lefttree->prev_total_cost=node->ss.ps.lefttree->total_cost;
//
//
//	/*
//	 * if isNull is true then last tuple will be null.
//	 * So (tuples-1) is multiplied by per_tuple cost.
//	 */
//	if(isNull)
//	{
//		node->ss.ps.total_cost+=node->ss.ps.plan->per_tuple*(tuples-1);
//		temp_query_cost+=node->ss.ps.plan->per_tuple*(tuples-1);
//	}
//	else
//	{
//		node->ss.ps.total_cost+=node->ss.ps.plan->per_tuple*tuples;
//		temp_query_cost+=node->ss.ps.plan->per_tuple*tuples;
//	}
//
//	/*
//	 * Adding cost of work done at this node to query_cost.
//	 */
//	query_cost+=temp_query_cost;
//
//}


/*
 * Unique Node Cost function
 * tuples: Number of tuples processed by plan node
 * isNull: Tuple returned by node is Null, i.e. end of execution of that node.
 */
void exec_cost_unique(UniqueState *node, double tuples)
{

	Assert(tuples >= 0);

	/*Ensuring that this will be executed only first time*/
	if(node->ps.startup_cost==0)
	{
		node->ps.startup_cost = node->ps.lefttree->startup_cost;
	}
	/* subplan cost*/
	/* subplan node cost increased in this cycle.*/
	node->ps.total_cost += node->ps.lefttree->total_cost - node->ps.lefttree->prev_total_cost;
	node->ps.lefttree->prev_total_cost = node->ps.lefttree->total_cost;

	node->ps.total_cost += tuples * node->ps.plan->per_tuple;

	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += tuples * node->ps.plan->per_tuple;
}


/*
 * Hash Node Cost function.
 * tuples: tuples processed by this plan node.
 */
void exec_cost_multiExecHash(HashState *node,double tuples)
{
	/*variable for measuring cost occurred at this node*/
	Cost 		temp_query_cost=0;
	int 		num_hashclauses;
	unsigned int pages_access=pages_read+pages_written;

	Assert(tuples >= 0);
	Assert(pages_read >= 0);
	Assert(pages_written >= 0);

	num_hashclauses = list_length(node->hashkeys);

	Assert(num_hashclauses > 0);

	/* subplan node cost increased in this cycle.*/
	node->ps.startup_cost += node->ps.lefttree->total_cost - node->ps.lefttree->prev_total_cost;

	node->ps.total_cost += node->ps.lefttree->total_cost - node->ps.lefttree->prev_total_cost;

	node->ps.lefttree->prev_total_cost = node->ps.lefttree->total_cost;

	node->ps.total_cost += tuples * (DEFAULT_CPU_OPERATOR_COST * num_hashclauses + DEFAULT_CPU_TUPLE_COST);
	node->ps.total_cost += pages_access * DEFAULT_SEQ_PAGE_COST;

	temp_query_cost += tuples * (DEFAULT_CPU_OPERATOR_COST * num_hashclauses + DEFAULT_CPU_TUPLE_COST);
	temp_query_cost += pages_access * DEFAULT_SEQ_PAGE_COST;

	pages_read = 0;
	pages_written = 0;
	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost+=temp_query_cost;
}


/*
 * Set Operation Cost function
 * subplan_tuples: number of tuples processed by subplan node.
 */
void exec_cost_setOp(SetOpState *node,double subplan_tuples)
{
	Assert(subplan_tuples >= 0);

	if(node->ps.startup_cost==0)
	{
		node->ps.startup_cost = node->ps.lefttree->startup_cost;
	}
	node->ps.total_cost += node->ps.lefttree->total_cost - node->ps.lefttree->prev_total_cost;
	node->ps.lefttree->prev_total_cost = node->ps.lefttree->total_cost;

	node->ps.total_cost += subplan_tuples * node->ps.plan->per_tuple;
	/*
	 * Adding cost of work done at this node to query_cost.
	 */
	query_cost += subplan_tuples * node->ps.plan->per_tuple;
}


/*
 * Lock Row Cost function
 * subplanTuples: number of tuples processed by sub plan node.
 */
//void exec_cost_lock_rows(LockRowsState *node, double subplanTuples)
//{
//
//	/*Ensuring that this will be executed only first time*/
//	if(node->ps.startup_cost==0)
//	{
//		node->ps.startup_cost=node->ps.lefttree->startup_cost;
//	}
//	/* subplan cost*/
//	/* subplan node cost increased in this cycle.*/
//	node->ps.total_cost+=node->ps.lefttree->total_cost-node->ps.lefttree->prev_total_cost;
//	node->ps.lefttree->prev_total_cost=node->ps.lefttree->total_cost;
//
//	node->ps.total_cost+=cpu_tuple_cost*subplanTuples;
//
//	/*
//	 * Adding cost of work done at this node to query_cost.
//	 */
//	query_cost+=cpu_tuple_cost*subplanTuples;
//}

/*
 * Limit node Cost function
 * outside_window_cost: cost of skipping tuples.
 * isNull: Tuple returned by node is Null, i.e. end of execution of that node.
 */
void exec_cost_limit(LimitState *node,double outside_window_cost)
{
	Assert(outside_window_cost >= 0);

	/*Ensuring that this will be executed only first time*/
	if(node->ps.startup_cost==0)
	{
		//		node->ps.startup_cost+=node->ps.lefttree->startup_cost;

		node->ps.startup_cost += outside_window_cost;
	}

	/* subplan cost*/
	/* subplan node cost increased in this cycle.*/
	node->ps.total_cost += node->ps.lefttree->total_cost - node->ps.lefttree->prev_total_cost;
	node->ps.lefttree->prev_total_cost = node->ps.lefttree->total_cost;
}


void exec_cost_subplan()
{

}

/*
 * Rescan Cost
 * Returns rescan startup cost of plan node.
 */
Cost exec_cost_rescan(PlanState *node)
{
	Cost rescan_startup_cost;
	switch(node->type)
	{
	case T_FunctionScan:
		rescan_startup_cost = 0;
		break;
	case T_HashJoin:
		rescan_startup_cost = 0;
		break;
		//	case T_CteScan:
		//	case T_WorkTableScan:
		//	{
		//		rescan_startup_cost = 0;
		//		node->plan->per_tuple=cpu_tuple_cost;
		//	}
		//	break;
	case T_Material:
	case T_Sort:
	{
		rescan_startup_cost = 0;
		node->plan->per_tuple=cpu_operator_cost;
	}
	break;
	default:
		rescan_startup_cost = node->startup_cost;
		break;
	}
	return(rescan_startup_cost);
}

//void exec_cost_multiExecHash(HashJoinState *node,double tuples)
//{
//	Cost 		temp_query_cost=0;
//	Cost 		temp_total_cost;
//	int 		num_hashclauses;
//	HashState 	*hashNode;
//	unsigned int pages_access=pages_read+pages_written;
//	hashNode=(HashState *) innerPlanState(node);
//	temp_total_cost=hashNode->ps.total_cost;
//
//	num_hashclauses=list_length(node->hashclauses);
//
//	hashNode->ps.startup_cost+=hashNode->ps.lefttree->total_cost;
//
//	hashNode->ps.total_cost+=hashNode->ps.lefttree->total_cost;
//	hashNode->ps.total_cost+=tuples*(DEFAULT_CPU_OPERATOR_COST*num_hashclauses+DEFAULT_CPU_TUPLE_COST);
//	hashNode->ps.total_cost+=pages_access*DEFAULT_SEQ_PAGE_COST;
//
//	temp_query_cost+=tuples*(DEFAULT_CPU_OPERATOR_COST*num_hashclauses+DEFAULT_CPU_TUPLE_COST);
//	temp_query_cost+=pages_access*DEFAULT_SEQ_PAGE_COST;
//	pages_read=0;
//	pages_written=0;
//	query_cost+=temp_query_cost;
//}

