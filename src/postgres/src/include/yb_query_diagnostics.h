#ifndef YB_QUERY_DIAGNOSTICS_H
#define YB_QUERY_DIAGNOSTICS_H

#include "postgres.h"
#include "hdr/hdr_histogram.h"
#include "storage/proc.h"
#include "utils/guc.h"
#include "utils/timestamp.h"
#include "pg_yb_utils.h"


extern void YbQueryDiagnosticsInstallHooks(void);


typedef struct Counters
{
	int64		calls;			/* # of times executed */
	double		total_time;		/* total execution time, in msec */
	double		min_time;		/* minimum execution time in msec */
	double		max_time;		/* maximum execution time in msec */
	double		mean_time;		/* mean execution time in msec */
	double		sum_var_time;	/* sum of variances in execution time in msec */
	int64		rows;			/* total # of retrieved or affected rows */
	int64		shared_blks_hit;	/* # of shared buffer hits */
	int64		shared_blks_read;	/* # of shared disk blocks read */
	int64		shared_blks_dirtied;	/* # of shared disk blocks dirtied */
	int64		shared_blks_written;	/* # of shared disk blocks written */
	int64		local_blks_hit; /* # of local buffer hits */
	int64		local_blks_read;	/* # of local disk blocks read */
	int64		local_blks_dirtied; /* # of local disk blocks dirtied */
	int64		local_blks_written; /* # of local disk blocks written */
	int64		temp_blks_read; /* # of temp blocks read */
	int64		temp_blks_written;	/* # of temp blocks written */
	double		blk_read_time;	/* time spent reading, in msec */
	double		blk_write_time; /* time spent writing, in msec */
	double		usage;			/* usage factor */
} Counters;


typedef struct
{
    bool isOn;
	int total;
} SharedBundleVariables;

typedef struct QueryDiagnosticsEntry
{
	//params.
	int64	   bind_var_query_min_duration_ms;
	int64	   diagnostics_interval_sec;
	int64	   explain_sample_rate;
	bool 	   explain_analyze;
	bool 	   explain_dist;
	bool 	   explain_debug;


	char       query[2024];
	TimestampTz start_time;
	char 	   log_path[1024];

	//for explain
	char        explain_str[8024];
	bool 		explain_printonce;

	//for schema
	char        schema_str[1024];
	
	//for pg_stat_statements
	Oid			userid;			/* user OID */
	Oid			dbid;			/* database OID */
	uint64		queryid;		/* query identifier */
	Counters    counters;		/* statistics counters */
	size_t yb_slow_executions; /* # of executions >= yb_hdr_max_value * yb_hdr_latency_res_ms */
	hdr_histogram yb_hdr_histogram; /* flexible array member at end - MUST BE LAST */
} QueryDiagnosticsEntry;


extern HTAB *query_diagnostics_hash;
extern SharedBundleVariables *shared_bundle_variables;
extern Size YbQueryDiagnosticsShmemSize(void);
extern void YbQueryDiagnosticsShmemInit(void);
typedef struct {
    int64 key;
    QueryDiagnosticsEntry value;
} HashEntry;
#endif                            /* YB_QUERY_DIAGNOSTICS_H */
