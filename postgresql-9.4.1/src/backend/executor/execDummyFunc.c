/*--------------------------------------------------------------------------
 * execDummyFunc.c
 * These function work as intermediate function from ExecProcNode to
 * other actual plan node execution function.
 *
 *
 *
 * e.g.
 *
 * ExecProcNode-------------------calls------------------>ExecNestLoop
 *
 * flow of execution converted to
 *
 * ExecProcNode----calls--->ExecDummyNestLoop----calls--->ExecNestLoop
 *
 * cost function is called in Dummy EExecution node.
 *
 * ------------------------------------------------------------------------
 */

#include "postgres.h"
#include "nodes/execnodes.h"
#include "executor/executionCost.h"
#include "executor/execDummyFunc.h"
#include "executor/costCheck.h"
#include "executor/instrument.h"
#include "utils/tuplesort.h"


#include "executor/executor.h"
#include "executor/nodeAgg.h"
#include "executor/nodeAppend.h"
#include "executor/nodeBitmapAnd.h"
#include "executor/nodeBitmapHeapscan.h"
#include "executor/nodeBitmapIndexscan.h"
#include "executor/nodeBitmapOr.h"
#include "executor/nodeFunctionscan.h"
#include "executor/nodeGroup.h"
#include "executor/nodeHash.h"
#include "executor/nodeHashjoin.h"
#include "executor/nodeIndexscan.h"
#include "executor/nodeLimit.h"
#include "executor/nodeMaterial.h"
#include "executor/nodeMergejoin.h"
#include "executor/nodeNestloop.h"
#include "executor/nodeResult.h"
#include "executor/nodeSeqscan.h"
#include "executor/nodeSetOp.h"
#include "executor/nodeSort.h"
#include "executor/nodeSubplan.h"
#include "executor/nodeSubqueryscan.h"
#include "executor/nodeTidscan.h"
#include "executor/nodeUnique.h"
#include "executor/nodeValuesscan.h"


TupleTableSlot *ExecDummyResult(ResultState *node)
{
	TupleTableSlot *slot;
	slot=ExecResult(node);
#ifdef PARTIAL_EXEC
	exec_cost_result(node);
#endif
	return(slot);
}
TupleTableSlot *ExecDummyAppend(AppendState *node)
{
	TupleTableSlot *slot;
	slot=ExecAppend(node);
#ifdef PARTIAL_EXEC
	exec_cost_append(node);
#endif
	return(slot);
}
Node *MultiExecDummyBitmapAnd(BitmapAndState *node)
{
	Node *n;
	int tbm_intersect_count=0;
	double subPath_cost=0.0;
	n=MultiExecBitmapAnd(node,&tbm_intersect_count,&subPath_cost);
#ifdef PARTIAL_EXEC
	exec_cost_bitmap_and_node(node,tbm_intersect_count,subPath_cost);
#endif
	return(n);
}

Node *MultiExecDummyBitmapOr(BitmapOrState *node)
{
	Node *n;
	int tbm_union_count=0;
	double subPath_cost=0.0;
	n=MultiExecBitmapOr(node,&tbm_union_count,&subPath_cost);
#ifdef PARTIAL_EXEC
	exec_cost_bitmap_or_node(node,tbm_union_count,subPath_cost);
#endif
	return(n);
}

//Node *MultiExecDummyBitmapIndexScan(BitmapIndexScanState *node)
//{
//	Node *n;
//
//	Assert(index_pages_read==0);
//	Assert(pages_read==0);
//
//	double indexTuples;
//	n=MultiExecBitmapIndexScan(node,&indexTuples);
//	exec_cost_bitmap_index_scan(node,indexTuples);
//	return(n);
//}

TupleTableSlot* ExecDummyNestLoop(NestLoopState *node)
{
	TupleTableSlot *slot;
	double outer_tuples=0;
	double tuple_comp_count=0;
	Cost rescan_startup_cost;
	slot = ExecNestLoop(node,&outer_tuples,&tuple_comp_count,&rescan_startup_cost);
#ifdef PARTIAL_EXEC
	if(!TupIsNull(slot))
		IncreaseTupleCount(node, 1);
	exec_cost_nestloop1(node,outer_tuples,tuple_comp_count,rescan_startup_cost);
#endif
	return(slot);
}
TupleTableSlot* ExecDummyMergeJoin(MergeJoinState *node)
{
	TupleTableSlot *slot;
	double tuples_comp=0;
	int tuple;
	slot = ExecMergeJoin(node,&tuples_comp);
#ifdef PARTIAL_EXEC
	tuple=TupIsNull(slot) ? 0 : 1;
	IncreaseTupleCount(node, tuple);
	exec_cost_mergejoin(node,tuples_comp,tuple);
#endif
	return(slot);
}
TupleTableSlot* ExecDummyHashJoin(HashJoinState *node)
{
	double hash_qual_tuples_comp=0;
	double qual_tuples_comp=0;
	double outer_tuples=0;
	TupleTableSlot *slot;

	Assert(pages_read==0);
	Assert(pages_written==0);

	slot = ExecHashJoin(node,&outer_tuples,&qual_tuples_comp,&hash_qual_tuples_comp);
#ifdef PARTIAL_EXEC
	if(!TupIsNull(slot))
		IncreaseTupleCount(node, 1);
#ifndef REQUESTMODEL
	exec_cost_hashjoin(node,outer_tuples,qual_tuples_comp,hash_qual_tuples_comp);
#else
	exec_cost_hashjoin_rq(node);
#endif
#endif
	return(slot);
}
TupleTableSlot* ExecDummyMaterial(MaterialState *node)
{
	TupleTableSlot *slot;

	Assert(pages_read==0);
	Assert(pages_written==0);

	int tuples;
	slot=ExecMaterial(node);
#ifdef PARTIAL_EXEC
	tuples = TupIsNull(slot) ? 0 : 1;
	IncreaseTupleCount(node, tuples);
#ifndef REQUESTMODEL
	exec_cost_material(node, tuples);
#else
	exec_cost_material_rq(node);
#endif
#endif
	return(slot);
}
TupleTableSlot* ExecDummySort(SortState *node)
{
	TupleTableSlot *slot;
	int output_tuples;

	Assert(pages_read==0);
	Assert(pages_written==0);

	slot=ExecSort(node);
#ifdef PARTIAL_EXEC
	output_tuples=TupIsNull(slot) ? 0 : 1;
	IncreaseTupleCount(node, output_tuples);
#ifndef REQUESTMODEL
	exec_cost_sort(node, output_tuples);
#else
	exec_cost_sort_rq(node);
#endif
#endif
	return(slot);
}
TupleTableSlot* ExecDummyGroup(GroupState *node)
{
	double tuples=0;
	TupleTableSlot *slot;
	slot=ExecGroup(node, &tuples);
#ifdef PARTIAL_EXEC
	tuples = TupIsNull(slot) ? tuples-1 : tuples;
	IncreaseTupleCount(node, tuples);
	exec_cost_group(node,tuples);
#endif
	return(slot);
}
TupleTableSlot* ExecDummyAgg(AggState *aggState)
{
	double tuples=0;
	int output_tuple;
	TupleTableSlot *slot;
	slot=ExecAgg(aggState,&tuples);
#ifdef PARTIAL_EXEC
	output_tuple = TupIsNull(slot) ? 0 : 1;
	IncreaseTupleCount(aggState, output_tuple);
	exec_cost_agg(aggState,tuples,output_tuple);
//	exec_cost_agg(aggState,tuples,TupIsNull(slot));
#endif
	return(slot);
}
TupleTableSlot* ExecDummyUnique(UniqueState *node)
{
	TupleTableSlot *slot;
	double tuples=0;
	slot=ExecUnique(node, &tuples);
#ifdef PARTIAL_EXEC
	IncreaseTupleCount(node, tuples);
	exec_cost_unique(node, tuples);
#endif
	return(slot);
}
TupleTableSlot *ExecDummySetOp(SetOpState *node)
{
	TupleTableSlot *slot;
	double subplan_tuples=0;
	slot=ExecSetOp(node,&subplan_tuples);
#ifdef PARTIAL_EXEC
	exec_cost_setOp(node,subplan_tuples);
#endif
	return(slot);
}
TupleTableSlot *ExecDummyLimit(LimitState *node)
{
	TupleTableSlot *slot;
	double outside_window_cost=0;
	slot=ExecLimit(node,&outside_window_cost);
#ifdef PARTIAL_EXEC
	exec_cost_limit(node,outside_window_cost);
#endif
	return(slot);
}
