/*-------------------------------------------------------------------------
 *
 * nodeGroup.h
 *	  prototypes for nodeGroup.c
 *
 *
 * Portions Copyright (c) 1996-2014, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/nodeGroup.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEGROUP_H
#define NODEGROUP_H

#include "nodes/execnodes.h"

extern GroupState *ExecInitGroup(Group *node, EState *estate, int eflags);
/* Modified by Rajmohan on 28/06/2014 for QUEST */
extern TupleTableSlot *ExecGroup(GroupState *node, double *tuples);
extern void ExecEndGroup(GroupState *node);
extern void ExecReScanGroup(GroupState *node);

#endif   /* NODEGROUP_H */
