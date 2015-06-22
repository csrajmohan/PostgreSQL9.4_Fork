#include "postgres.h"
#include "nodes/execnodes.h"
#include "executor/executionCost.h"

TupleTableSlot *ExecDummyResult(ResultState *node);
TupleTableSlot *ExecDummyAppend(AppendState *node);
Node *MultiExecDummyBitmapAnd(BitmapAndState *node);
Node *MultiExecDummyBitmapOr(BitmapOrState *node);
Node *MultiExecDummyBitmapIndexScan(BitmapIndexScanState *node);
TupleTableSlot* ExecDummyNestLoop(NestLoopState *node);
TupleTableSlot* ExecDummyMergeJoin(MergeJoinState *node);
TupleTableSlot* ExecDummyHashJoin(HashJoinState *node);
TupleTableSlot* ExecDummyMaterial(MaterialState *node);
TupleTableSlot* ExecDummySort(SortState *node);
TupleTableSlot* ExecDummyGroup(GroupState *node);
TupleTableSlot* ExecDummyAgg(AggState *aggState);
TupleTableSlot* ExecDummyUnique(UniqueState *node);
TupleTableSlot *ExecDummySetOp(SetOpState *node);
TupleTableSlot *ExecDummyLimit(LimitState *node);
