#include "postgres.h"
#include "executor/executor.h"
#include "executor/instrument.h"
#include "executor/hashjoin.h"
#include "nodes/execnodes.h"
#include "optimizer/cost.h"
#include "miscadmin.h"

#define LOG2(x)  (log(x) / 0.693147180559945)

double pages_fetched(double tuples_fetched, double pages,
		double index_pages, double total_table_pages);
double relation_size(double tuples, int width);
double clamp_row_est_rq(double nrows);

void exec_cost_seqscan_rq(ScanState *node)
{
	double tuples_generated;
	Cost prev_cost;
	Cost per_page_cost;
	Selectivity sel;
	int32 rel_pages;
	int32 total_pages_read;


	prev_cost = node->ps.total_cost;
	per_page_cost = node->ps.plan->cost2;

	if(node->ps.instrument == NULL)
		elog(ERROR ,"Instrument counters are not active");

	/*
	 * total tuples processed at this node
	 */
	tuples_generated = node->ps.instrument->totaltuples;

	sel = tuples_generated / (double)node->ss_currentRelation->rd_rel->reltuples;

	/*
	 * number of page accesses are calculated from selectivity.
	 */
	rel_pages = node->ss_currentRelation->rd_rel->relpages;
	total_pages_read = ceil(sel * rel_pages);

	//	if(total_pages_read > rel_pages)
	//		total_pages_read = rel_pages;
	if(total_pages_read < 0)
		total_pages_read = 0;

	if (node->ps.startup_cost == 0)
	{
		node->ps.startup_cost += node->ps.plan->startup_tuple;
	}
	node->ps.total_cost = node->ps.plan->startup_tuple;

	/*
	 * CPU cost : tuple processing cost
	 */
	node->ps.total_cost += tuples_generated * node->ps.plan->per_tuple;
	/*
	 * Disk Cost : page access cost
	 */
	node->ps.total_cost += total_pages_read * per_page_cost;

	/*
	 * prev_cost is subtracted to exclude previous added cost to
	 * query_cost.
	 */
	query_cost += node->ps.total_cost - prev_cost;
}

void exec_cost_index_rq(IndexScanState *node)
{
	double tuples_fetched;
	Selectivity sel;
	Selectivity index_sel;
	int32 rel_pages;
	int32 index_pages;
	int32 rel_pages_read;
	int32 index_pages_read;
	Cost max_IO_cost;
	Cost min_IO_cost;
	Cost prev_cost;
	Cost random_page_cost = node->ss.ps.plan->cost1;
	Cost seq_page_cost = node->ss.ps.plan->cost2;

	prev_cost = node->ss.ps.total_cost;

	/*
	 * total tuples processed at this node.
	 */
	tuples_fetched = node->ss.ps.instrument->totaltuples;

	/*
	 * calculating selectivity
	 */
	if(node->ss.ss_currentRelation->rd_rel->reltuples >= 1)
		sel = tuples_fetched / (double)node->ss.ss_currentRelation->rd_rel->reltuples;
	else
		sel=0.0;

	/*
	 * calculating index selectivity
	 */
	if(node->iss_RelationDesc->rd_rel->reltuples >= 1)
		index_sel = tuples_fetched / (double)node->iss_RelationDesc->rd_rel->reltuples;

	/*
	 * total pages of relation.
	 */
	rel_pages = node->ss.ss_currentRelation->rd_rel->relpages;
	/*
	 * total index pages of relation.
	 */
	index_pages = node->iss_RelationDesc->rd_rel->relpages;

	/*
	 * getting index pages read using index selectivity.
	 */
	if(index_pages > 1)
		index_pages_read = ceil(index_sel * index_pages);
	else
		index_pages_read = 1.0;

	if(node->ss.ps.startup_cost == 0)
	{
		node->ss.ps.startup_cost += node->ss.ps.plan->startup_tuple;
		node->ss.ps.startup_cost += node->qual_arg_cost;
		/*
		 * fudge factor. (See actual cost function)
		 */
		node->ss.ps.startup_cost += index_pages * random_page_cost / 100000.0;
		node->ss.ps.startup_cost += node->num_sa_scans * 100.0 * cpu_operator_cost;
	}
	if(node->loop_count > 0)
	{
		/*
		 * calculating min and max IO cost using Mackert and Lohman formula.
		 */
		rel_pages_read = pages_fetched(tuples_fetched, rel_pages, index_pages, node->total_table_pages);
		max_IO_cost = rel_pages_read * random_page_cost;

		rel_pages_read = ceil(sel * rel_pages);
//		rel_pages_read = pages_fetched(rel_pages_read, rel_pages, index_pages, node->total_table_pages);

		rel_pages_read = pages_fetched(tuples_fetched, rel_pages, index_pages, node->total_table_pages);
		min_IO_cost = rel_pages_read * random_page_cost;

		/*
		 * If loop count is more than 1 then index pages read count in calculated using
		 * Mackert and Lohman formula to include cache effect.
		 */
		index_pages_read = pages_fetched(tuples_fetched, index_pages, index_pages, node->total_table_pages);

	}
	else
	{
		/*
		 * calculating max IO cost using Mackert and Lohman formula.
		 */
		rel_pages_read = pages_fetched(tuples_fetched, rel_pages, index_pages, node->total_table_pages);
		max_IO_cost = rel_pages_read * random_page_cost;

		rel_pages_read = ceil(sel * rel_pages);
		min_IO_cost = random_page_cost;
		if(rel_pages_read > 1)
			min_IO_cost += (rel_pages_read -1) * seq_page_cost;
	}
	node->ss.ps.total_cost = node->ss.ps.startup_cost;

	/*
	 * Disk Cost : page access cost.
	 */
	node->ss.ps.total_cost += max_IO_cost + node->index_correlation * (min_IO_cost - max_IO_cost);
	/*
	 * CPU cost : tuple processing cost.
	 */
	node->ss.ps.total_cost += tuples_fetched * node->ss.ps.plan->per_tuple;

	/*
	 * index page access and index tuple processing cost
	 */
	node->ss.ps.total_cost += index_pages_read * random_page_cost;
	node->ss.ps.total_cost += tuples_fetched * node->per_tuple_cost;

	/*
	 * prev_cost is subtracted to exclude previous added cost to
	 * query_cost.
	 */

	if(node->ss.ps.total_cost > 1129871.0)
	{
		int a=0;
		a++;
	}
	query_cost += node->ss.ps.total_cost - prev_cost;
}

void exec_cost_bitmap_index_scan_rq(BitmapIndexScanState *node)
{
	double tuples_fetched;
	Selectivity sel;
	int32 index_pages;
	int32 index_pages_read;
	double min_IO_cost;
	double max_IO_cost;
	double prev_cost;
	Cost random_page_cost = node->ss.ps.plan->cost1;
	Cost seq_page_cost = node->ss.ps.plan->cost2;

	prev_cost = node->ss.ps.total_cost;
	/*
	 * total index pages.
	 */
	index_pages = ((node->biss_RelationDesc)->rd_rel)->relpages;
	if(node->ss.ps.startup_cost == 0)
	{
		/*
		 * fudge factor.
		 */
		node->ss.ps.startup_cost = (double)index_pages * random_page_cost / 100000.0;
		node->ss.ps.startup_cost += node->qual_arg_cost;
		node->ss.ps.startup_cost += node->num_sa_scans * 100.0 * cpu_operator_cost;
	}

	/*
	 * total tuples processed at this node.
	 */
	tuples_fetched = node->ss.ps.instrument->totaltuples;
	sel = tuples_fetched / (double)node->biss_RelationDesc->rd_rel->reltuples;
	/*
	 * index page read count is calculated using selectivity.
	 */
	index_pages_read = ceil(sel * index_pages);

	node->ss.ps.total_cost = node->ss.ps.startup_cost;
	node->ss.ps.total_cost += 0.1 * cpu_operator_cost * node->biss_RelationDesc->rd_rel->reltuples;

	/*
	 * CPU Cost : tuples processing cost.
	 */
	node->ss.ps.total_cost += tuples_fetched * node->per_tuple_cost;
	/*
	 * Disk Cost : index pages cost.
	 */
	node->ss.ps.total_cost += index_pages_read * random_page_cost;

	/*
	 * prev_cost is subtracted to exclude previous added cost to
	 * query_cost.
	 */
	query_cost += node->ss.ps.total_cost - prev_cost;

}

void exec_cost_bitmap_heap_scan_rq(BitmapHeapScanState *node)
{
	double tuples_fetched;
	Selectivity sel;
	int32 rel_pages;
	int32 rel_pages_read;
	double prev_cost;
	double per_page_cost;
	BitmapIndexScanState *b_node;
	double total_index_pages = node->ss.ps.plan->cost1;
	double total_table_pages = node->ss.ps.plan->cost2;

	prev_cost = node->ss.ps.total_cost;


	if(node->ss.ps.startup_cost == 0)
	{
		node->ss.ps.startup_cost = node->ss.ps.plan->startup_tuple;
		node->ss.ps.startup_cost += node->ss.ps.lefttree->total_cost;
	}

	/*
	 * total tuples processed at this node.
	 */
	tuples_fetched = node->ss.ps.instrument->totaltuples;
	sel = tuples_fetched / (double)node->ss.ss_currentRelation->rd_rel->reltuples;

	/*
	 * total relation pages.
	 */
	rel_pages = node->ss.ss_currentRelation->rd_rel->relpages;

	if(rel_pages < 1)
		rel_pages = 1;

	b_node = (BitmapIndexScanState*)node->ss.ps.lefttree;

	/*
	 * Use Mackert and Lohman formula to calculate page access.
	 */
	if(b_node->loop_count > 1)
		rel_pages_read = pages_fetched (tuples_fetched, rel_pages, total_index_pages, total_table_pages);
	else
	{
		rel_pages_read = (2.0 * rel_pages * tuples_fetched) / (2.0 * rel_pages + tuples_fetched);
		if(rel_pages_read > rel_pages)
			rel_pages_read = rel_pages;
	}

	/*
	 * calculate per page cost depending on number of page access.
	 */

	if(rel_pages_read >= 2.0)
	{
		double correlation;

		correlation = sqrt((double)rel_pages_read / (double)rel_pages);
		if(correlation > 1.0)
			correlation = 1.0;
		per_page_cost = random_page_cost - (random_page_cost - seq_page_cost) * correlation;
	}
	else
		per_page_cost = random_page_cost;

	node->ss.ps.total_cost = node->ss.ps.plan->startup_tuple;
	node->ss.ps.total_cost += node->ss.ps.lefttree->total_cost;

	/*
	 * CPU Cost : tuples processing cost.
	 */
	node->ss.ps.total_cost += tuples_fetched * node->ss.ps.plan->per_tuple;
	/*
	 * Disk cost : page access cost.
	 */
	node->ss.ps.total_cost += rel_pages_read * per_page_cost;

	/*
	 * prev_cost and sub plan costs are subtracted to exclude previous added cost to
	 * query_cost.
	 */
	query_cost += node->ss.ps.total_cost - prev_cost - (node->ss.ps.lefttree->total_cost - node->ss.ps.lefttree->prev_total_cost);

	node->ss.ps.lefttree->prev_total_cost = node->ss.ps.lefttree->total_cost;
}

void exec_cost_material_rq (MaterialState *node)
{
	double tuples;
	double prev_cost;
	int width;
	double rel_size;
	double per_page_cost = node->ss.ps.plan->cost2;
	long work_mem_bytes = work_mem * 1024L;

	/*
	 * total tuples processed at this node.
	 */
	//	tuples = node->ss.ps.instrument->totaltuples;

	tuples = node->ss.ps.instrument->tuplecount;
	width = node->ss.ps.plan->plan_width;
	rel_size = relation_size(tuples, width);

	if(node->ss.ps.startup_cost == 0)
	{
		node->ss.ps.startup_cost = node->ss.ps.lefttree->total_cost;
	}

	node->ss.ps.total_cost = node->ss.ps.startup_cost;
	/*
	 * Disk Cost : page access cost.
	 */
	if(rel_size > work_mem_bytes)
	{
		double pages = (rel_size) / BLCKSZ;
		node->ss.ps.total_cost += pages * per_page_cost;
	}

	/*
	 * CPU Cost : tuple processing cost.
	 */
	node->ss.ps.total_cost += tuples * node->ss.ps.plan->per_tuple;

	/*
	 * prev_cost and sub plan costs are subtracted to exclude previous added cost to
	 * query_cost.
	 */
	query_cost += node->ss.ps.total_cost - prev_cost - (node->ss.ps.lefttree->total_cost - node->ss.ps.lefttree->prev_total_cost);

	node->ss.ps.lefttree->prev_total_cost = node->ss.ps.lefttree->total_cost;
}

void exec_cost_sort_rq(SortState *node)
{
	double input_tuples;
	double output_tuples;
	int width;
	double input_bytes;
	double output_bytes;
	double prev_cost;

	long work_mem_bytes = work_mem * 1024L;
	Cost per_page_cost = node->ss.ps.plan->cost2;
	Cost comparision_cost = node->ss.ps.plan->startup_tuple;

	/*
	 * tuples got from sub plan node.
	 */
	//	input_tuples = node->ss.ps.lefttree->instrument->totaltuples;
	input_tuples = node->ss.ps.lefttree->instrument->tuplecount;
	width = node->ss.ps.lefttree->plan->plan_width;
	/*
	 * size of base relation in bytes.
	 */
	input_bytes =  relation_size(input_tuples, width);

	/*
	 * tuples processed at this node.
	 */
	output_tuples = node->ss.ps.instrument->totaltuples;
	output_bytes = relation_size(output_tuples, width);

	prev_cost = node->ss.ps.total_cost;

	/*
	 * if working memory is enough to contain tuples then disk use is
	 * not needed otherwise page access cost will be added.
	 */
//	if(node->ss.ps.startup_cost == 0)
//	{
	node->ss.ps.startup_cost = node->ss.ps.lefttree->total_cost;

	if(output_tuples < 2.0)
		output_tuples = 2.0;

	if(input_tuples > 0)
	{
		if(output_bytes > work_mem_bytes)
		{
			double		npages = ceil(input_bytes / BLCKSZ);
			double		nruns = (input_bytes / work_mem_bytes) * 0.5;
			double		mergeorder = tuplesort_merge_order(work_mem_bytes);
			double		log_runs;
			double		npageaccesses;

			node->ss.ps.startup_cost += comparision_cost * input_tuples * LOG2(input_tuples);

			if (nruns > mergeorder)
				log_runs = ceil(log(nruns) / log(mergeorder));
			else
				log_runs = 1.0;
			npageaccesses = 2.0 * npages * log_runs;
			node->ss.ps.startup_cost += npageaccesses * per_page_cost;
		}
		else if(input_tuples > 2 * output_tuples || input_bytes > work_mem_bytes)
		{
			node->ss.ps.startup_cost += comparision_cost * input_tuples * LOG2(2.0 * output_tuples);
		}
		else
		{
			node->ss.ps.startup_cost += comparision_cost * input_tuples * LOG2(input_tuples);
		}
//	}
	}
	node->ss.ps.total_cost = node->ss.ps.startup_cost;
	/*
	 * CPU Cost : tuples processing cost.
	 */
	node->ss.ps.total_cost += output_tuples * node->ss.ps.plan->per_tuple;

	/*
	 * prev_cost and sub plan costs are subtracted to exclude previous added cost to
	 * query_cost.
	 */
	query_cost += node->ss.ps.total_cost - prev_cost - (node->ss.ps.lefttree->total_cost - node->ss.ps.lefttree->prev_total_cost);
	node->ss.ps.lefttree->prev_total_cost = node->ss.ps.lefttree->total_cost;
}

void exec_cost_hashjoin_rq(HashJoinState *node)
{
	double inner_tuples;
	double outer_tuples;
	int num_buckets;
	int num_batches;
	int inner_width;
	int outer_width;
	int num_hashclauses;
	double prev_cost;

	double hash_qual_startup_cost = node->js.ps.plan->cost1;
	double hash_qual_per_tuple_cost = node->js.ps.plan->cost2;

	/*
	 * left sub plan tuples.
	 */
	//	outer_tuples =node->js.ps.lefttree->instrument->totaltuples;
	outer_tuples = node->js.ps.lefttree->instrument->tuplecount;

	outer_width = node->js.ps.lefttree->plan->plan_width;
	inner_width = node->js.ps.righttree->plan->plan_width;

	num_hashclauses = list_length(node->hashclauses);

	Assert (num_hashclauses > 0);

	prev_cost = node->js.ps.total_cost;

	if(node->js.ps.startup_cost == 0)
	{
		node->js.ps.startup_cost = node->js.ps.lefttree->startup_cost;
		node->js.ps.startup_cost += node->js.ps.righttree->total_cost;
		node->js.ps.startup_cost += hash_qual_startup_cost;
		node->js.ps.startup_cost += node->js.ps.plan->startup_tuple;
	}

	node->js.ps.total_cost = hash_qual_startup_cost;
	node->js.ps.total_cost += node->js.ps.plan->startup_tuple;
	node->js.ps.total_cost += node->js.ps.lefttree->total_cost;
	node->js.ps.total_cost += node->js.ps.righttree->total_cost;

	if(outer_tuples > 0)
	{
		inner_tuples = node->hj_HashTable->totalTuples;
		num_batches = node->hj_HashTable->nbatch;
		num_buckets = num_batches * node->hj_HashTable->nbuckets;


		/*
		 * Cost of applying hash function on outer sub plan nodes.
		 */
		node->js.ps.total_cost += outer_tuples * num_hashclauses * DEFAULT_CPU_OPERATOR_COST;

		/*
		 * if batches in more then 1 then pages will be read and written.
		 */
		if(num_batches > 1)
		{
			double outer_pages;
			double inner_pages;

			outer_pages = relation_size(outer_tuples, outer_width) / BLCKSZ;
			inner_pages = relation_size(inner_tuples, inner_width) / BLCKSZ;
			node->js.ps.total_cost += seq_page_cost * (inner_pages + 2 * outer_pages);
		}

		/*
		 * Comparison cost.
		 */
		node->js.ps.total_cost += hash_qual_per_tuple_cost *
				outer_tuples * clamp_row_est_rq(inner_tuples / num_buckets) * 0.5;

	}

	/*
	 * CPU Cost : tuples processing cost.
	 */
	node->js.ps.total_cost += node->js.ps.instrument->totaltuples * node->js.ps.plan->per_tuple;

	prev_cost = prev_cost + (node->js.ps.lefttree->total_cost - node->js.ps.lefttree->prev_total_cost)
															+ (node->js.ps.righttree->total_cost - node->js.ps.righttree->prev_total_cost);

	/*
	 * prev_cost and sub plan costs (included in prev_cost) are subtracted to exclude
	 * previous added cost to query_cost.
	 */
	query_cost += node->js.ps.total_cost - prev_cost;

	node->js.ps.lefttree->prev_total_cost = node->js.ps.lefttree->total_cost;
	node->js.ps.righttree->prev_total_cost = node->js.ps.righttree->total_cost;
}

void exec_cost_multiExecHash_rq(HashState *node)
{
	double tuples_fetched;
	double pages_written;
	int num_hashclauses;
	HashJoinTable hashtable;
	int num_batches;
	int width;
	double prev_cost;

	num_hashclauses = list_length(node->hashkeys);
	hashtable = node->hashtable;
	num_batches = hashtable->nbatch;

	//	tuples_fetched = hashtable->totalTuples;
	/*
	 * tuples processed at this node.
	 */
	tuples_fetched = node->ps.instrument->totaltuples;
	width = node->ps.plan->plan_width;

	prev_cost = node->ps.total_cost;

	node->ps.startup_cost = node->ps.lefttree->total_cost;
	node->ps.total_cost = node->ps.startup_cost;

	/*
	 * cost of applying cost function.
	 */
	//	if(tuples_fetched > 0)
	//	{
	node->ps.total_cost += tuples_fetched * (DEFAULT_CPU_OPERATOR_COST * num_hashclauses + DEFAULT_CPU_TUPLE_COST);
	/*
	 * if batches are more than 1 pages will be written and read.
	 */
	if(num_batches > 1)
	{
		pages_written = relation_size(tuples_fetched, width) / BLCKSZ;
		node->ps.total_cost += pages_written * seq_page_cost;
	}
	//	}
	/*
	 * prev_cost and sub plan costs (included in prev_cost) are subtracted to exclude
	 * previous added cost to query_cost.
	 */
	query_cost += node->ps.total_cost - prev_cost - (node->ps.lefttree->total_cost - node->ps.lefttree->prev_total_cost);

	node->ps.lefttree->prev_total_cost = node->ps.lefttree->total_cost;
}

double pages_fetched(double tuples_fetched, double pages, double index_pages, double total_table_pages)
{
	double		pages_fetched;
	double		total_pages;
	double		T,
	b;

	/* T is # pages in table, but don't allow it to be zero */
	T = (pages > 1) ? pages : 1.0;

	/* Compute number of pages assumed to be competing for cache space */
	total_pages = total_table_pages + index_pages;
	total_pages = Max(total_pages, 1.0);
	Assert(T <= total_pages);

	/* b is pro-rated share of effective_cache_size */
	b = (double) effective_cache_size *T / total_pages;

	/* force it positive and integral */
	if (b <= 1.0)
		b = 1.0;
	else
		b = ceil(b);

	/* This part is the Mackert and Lohman formula */
	if (T <= b)
	{
		pages_fetched =
				(2.0 * T * tuples_fetched) / (2.0 * T + tuples_fetched);
		if (pages_fetched >= T)
			pages_fetched = T;
		else
			pages_fetched = ceil(pages_fetched);
	}
	else
	{
		double		lim;

		lim = (2.0 * T * b) / (2.0 * T - b);
		if (tuples_fetched <= lim)
		{
			pages_fetched =
					(2.0 * T * tuples_fetched) / (2.0 * T + tuples_fetched);
		}
		else
		{
			pages_fetched =
					b + (tuples_fetched - lim) * (T - b) / T;
		}
		pages_fetched = ceil(pages_fetched);
	}
	return pages_fetched;
}
double relation_size(double tuples, int width)
{
	return tuples * (MAXALIGN(width) + MAXALIGN(sizeof(HeapTupleHeaderData)));
}
double clamp_row_est_rq(double nrows)
{
	/*
	 * Force estimate to be at least one row, to make explain output look
	 * better and to avoid possible divide-by-zero when interpolating costs.
	 * Make it an integer, too.
	 */
	if (nrows <= 1.0)
		nrows = 1.0;
	else
		nrows = rint(nrows);

	return nrows;
}
