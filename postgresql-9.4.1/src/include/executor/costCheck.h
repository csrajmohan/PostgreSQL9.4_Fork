#include "postgres.h"


#define check_query_cost 				\
		do								\
		{								\
			if(cost_limit != -1 && query_cost > cost_limit)		\
			{								\
				query_continue_flag=false;	\
				return(NULL);				\
			}								\
		}while(0)

/*
 * This macro is used in functions with return type void
 */
#define check_query_cost2				\
		do								\
		{								\
			if(cost_limit!=-1 && query_cost>cost_limit)		\
			{								\
				query_continue_flag=false;	\
				return;						\
			}								\
		}while(0)


#define check_query_time				\
		do								\
		{								\
			INSTR_TIME_SET_CURRENT(current_time);				\
			current_time.tv_sec = current_time.tv_sec - query_start_time.tv_sec;			\
			current_time.tv_usec = current_time.tv_usec - query_start_time.tv_usec;			\
			while (current_time.tv_usec < 0)					\
			{													\
				current_time.tv_usec += 1000000;				\
				current_time.tv_sec--;						\
			}													\
			while (current_time.tv_usec >= 1000000)			\
			{													\
				current_time.tv_usec -= 1000000;				\
				current_time.tv_sec++;						\
			}													\
			query_exec_time = INSTR_TIME_GET_DOUBLE(current_time)*1000.0;					\
			if(time_limit != -1 && query_exec_time > time_limit)		\
			{								\
				query_continue_flag=false;	\
				return(NULL);				\
			}								\
		}while(0)


#define check_query_time2				\
		do								\
		{								\
			INSTR_TIME_SET_CURRENT(current_time);				\
			current_time.tv_sec = current_time.tv_sec - query_start_time.tv_sec;			\
			current_time.tv_usec = current_time.tv_usec - query_start_time.tv_usec;			\
			while (current_time.tv_usec < 0)					\
			{													\
				current_time.tv_usec += 1000000;				\
				current_time.tv_sec--;						\
			}													\
			while (current_time.tv_usec >= 1000000)			\
			{													\
				current_time.tv_usec -= 1000000;				\
				current_time.tv_sec++;						\
			}													\
			query_exec_time = INSTR_TIME_GET_DOUBLE(current_time)*1000.0;					\
			if(time_limit!=-1 && query_exec_time > time_limit)		\
			{								\
				query_continue_flag=false;	\
				return;				\
			}								\
		}while(0)
