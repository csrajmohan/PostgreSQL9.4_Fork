/*-------------------------------------------------------------------------
 *
 * nodeBitmapOr.h
 *
 *
 *
 * Portions Copyright (c) 1996-2014, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/nodeBitmapOr.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEBITMAPOR_H
#define NODEBITMAPOR_H

#include "nodes/execnodes.h"

extern BitmapOrState *ExecInitBitmapOr(BitmapOr *node, EState *estate, int eflags);
extern Node *MultiExecBitmapOr(BitmapOrState *node,  int *tbm_union_count, Cost *subPath_cost); // modified by rajmohan for quest
extern void ExecEndBitmapOr(BitmapOrState *node);
extern void ExecReScanBitmapOr(BitmapOrState *node);

#endif   /* NODEBITMAPOR_H */
