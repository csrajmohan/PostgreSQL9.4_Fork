
#include "nodes/execnodes.h"

void exec_cost_result(ResultState *node);
void exec_cost_seqscan(ScanState *node,double tuples);
void exec_cost_index1(IndexScanState *node,double tuples);
void exec_cost_bitmap_heap_scan(BitmapHeapScanState *node, double tuples);
void exec_cost_bitmap_and_node(BitmapAndState *node,int tbm_intersect_count,double subPath_cost);
void exec_cost_bitmap_or_node(BitmapOrState *node, int tbm_union_count, double subPath_cost);
void exec_cost_tidscan(ScanState *node, double tuples);
void exec_cost_subqueryscan(SubqueryScanState *node,double tuples);
void exec_cost_functionscan(ScanState *node,double tuples);
void exec_cost_valuesscan(ScanState *node,double tuples);
void exec_cost_ctescan(ScanState *node,double tuples);
//void exec_cost_recursive_union(RecursiveUnionState *node,double subplan_tuples);
void exec_cost_sort(SortState *node, int output_tuples);
//void exec_cost_merge_append(MergeAppendState *node, double tuples_comp, double tuples_comp_heap_create, bool isNull);
void exec_cost_material(MaterialState *node, int tuples);
void exec_cost_agg(AggState *aggState,double tuples,int output_tuple);
//void exec_cost_windowagg(WindowAggState *node,double tuples,bool isNull);
void exec_cost_group(GroupState *node,double tuples);
void exec_cost_nestloop1(NestLoopState *node,double outer_tuples, double tuple_comp_count, Cost rescan_startup_cost);
void exec_cost_mergejoin(MergeJoinState *node, double tuples_comp, int tuple);
void exec_cost_hashjoin(HashJoinState *node,double outer_tuples,double qual_tuples_comp,double hash_qual_tuples_comp);
void exec_cost_subplan();
Cost exec_cost_rescan(PlanState *node);
void exec_cost_multiExecHash(HashState *node,double tuples);
void exec_cost_unique(UniqueState *node, double tuples);
//void exec_cost_lock_rows(LockRowsState *node, double subplanTuples);
void exec_cost_append(AppendState *node);
void exec_cost_setOp(SetOpState *node,double subplan_tuples);
//void exec_cost_foreign_scan(ForeignScanState *node,double tuples, bool isNull);
void exec_cost_limit(LimitState *node,double outside_window_cost);
void exec_cost_bitmap_index_scan(BitmapIndexScanState *node, double indexTuples);
