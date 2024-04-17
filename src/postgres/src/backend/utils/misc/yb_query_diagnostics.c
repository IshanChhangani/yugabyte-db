#include "postgres.h"
#include "yb_query_diagnostics.h"
#include "access/htup_details.h"
#include "catalog/indexing.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_constraint_d.h"
#include "commands/tablespace.h"
#include "executor/execdebug.h"
#include "executor/instrument.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "utils/ruleutils.h"
#include "utils/timestamp.h"

PG_MODULE_MAGIC;

void YbQueryDiagnosticsInstallHooks(void);

static ExecutorStart_hook_type prev_ExecutorStart = NULL;
static ExecutorRun_hook_type prev_ExecutorRun = NULL;
static ExecutorFinish_hook_type prev_ExecutorFinish = NULL; 
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;

SharedBundleVariables *shared_bundle_variables = NULL;
HTAB *query_diagnostics_hash = NULL;

static void queryDiagnostics_ExecutorStart(QueryDesc *queryDesc, int eflags);
static void queryDiagnostics_ExecutorRun(QueryDesc *queryDesc, ScanDirection direction, uint64 count, bool execute_once);
static void queryDiagnostics_ExecutorFinish(QueryDesc *queryDesc); 
static void queryDiagnostics_ExecutorEnd(QueryDesc *queryDesc);
static void print_schema_details(List *rtable, QueryDiagnosticsEntry* queryDiagnosticsEntry);
static void print_table_columns(Oid relid);
static void print_table_indexes(Oid relid);
static void print_table_foreign_keys(Oid relid);
static void create_shared_hashtable(void);
static void insert_into_shared_hashtable(HTAB *htab, int64 key, QueryDiagnosticsEntry value);
static QueryDiagnosticsEntry* lookup_in_shared_hashtable(HTAB *htab, int64 key);
static void remove_from_shared_hashtable(HTAB *htab, int64 key);

void YbQueryDiagnosticsInstallHooks(void)
{
    prev_ExecutorStart = ExecutorStart_hook;
    ExecutorStart_hook = queryDiagnostics_ExecutorStart;

    prev_ExecutorRun = ExecutorRun_hook;
    ExecutorRun_hook = queryDiagnostics_ExecutorRun;

    prev_ExecutorFinish = ExecutorFinish_hook; 
    ExecutorFinish_hook = queryDiagnostics_ExecutorFinish; 

    prev_ExecutorEnd = ExecutorEnd_hook;
    ExecutorEnd_hook = queryDiagnostics_ExecutorEnd;
}
char *
format_params(ParamListInfo params)
{
    StringInfoData buf;
    int i;

    initStringInfo(&buf);

    for (i = 0; i < params->numParams; i++)
    {
        if (params->params[i].isnull)
        {
            appendStringInfo(&buf, "NULL");
        }
        else
        {
            Oid typoutput;
            bool typIsVarlena;
            char *val;

            getTypeOutputInfo(params->params[i].ptype, &typoutput, &typIsVarlena);
            val = OidOutputFunctionCall(typoutput, params->params[i].value);
            appendStringInfoString(&buf, val);
        }

        if (i < params->numParams - 1)
        {
            appendStringInfoChar(&buf, ',');
        }
    }

	//insert a new line character in buf.data
	appendStringInfoChar(&buf, ' ');

    return buf.data;
}

/*
 * YbAshShmemSize
 *		Compute space needed for ASH-related shared memory
 */
Size
YbQueryDiagnosticsShmemSize(void)
{
	Size		size;
	size = sizeof(SharedBundleVariables);
	return size;
}

/*
 * YbAshShmemInit
 *		Allocate and initialize ASH-related shared memory
 */
void
YbQueryDiagnosticsShmemInit(void)
{
	bool		found = false;

    shared_bundle_variables = (SharedBundleVariables *) ShmemInitStruct("Shared variables for query diagnostics",
                                                    YbQueryDiagnosticsShmemSize(),
                                                    &found);

	if (!found)
	{
        /* First time through ... */
        shared_bundle_variables->isOn = false;
        shared_bundle_variables->total = 0;
	}

    //Initialize shared hash table
    create_shared_hashtable();
}

static void queryDiagnostics_ExecutorStart(QueryDesc *queryDesc, int eflags)
{
    if (prev_ExecutorStart)
        prev_ExecutorStart(queryDesc, eflags);
    else
        standard_ExecutorStart(queryDesc, eflags);
}

static void queryDiagnostics_ExecutorRun(QueryDesc *queryDesc, ScanDirection direction, uint64 count, bool execute_once)
{
    if (prev_ExecutorRun)
        prev_ExecutorRun(queryDesc, direction, count, execute_once);
    else
        standard_ExecutorRun(queryDesc, direction, count, execute_once);
}
static void queryDiagnostics_ExecutorFinish(QueryDesc *queryDesc) 
{
    if (prev_ExecutorFinish)
        prev_ExecutorFinish(queryDesc);
    else
        standard_ExecutorFinish(queryDesc);
}
static void queryDiagnostics_ExecutorEnd(QueryDesc *queryDesc)
{
    if (prev_ExecutorEnd)
        prev_ExecutorEnd(queryDesc);
    else
        standard_ExecutorEnd(queryDesc);

    if(queryDesc->params){

        FILE* fptr = fopen("/Users/ishanchhangani/test.txt","a");
        fprintf(fptr, "QueryId: %ld\n" , queryDesc->plannedstmt->queryId);
        fprintf(fptr, "PARAMS: %s\n", format_params(queryDesc->params));
        fclose(fptr);
    }

    if(shared_bundle_variables && shared_bundle_variables->isOn){
        QueryDiagnosticsEntry* queryDiagnosticsEntry = lookup_in_shared_hashtable(query_diagnostics_hash, queryDesc->plannedstmt->queryId);
        if(queryDiagnosticsEntry)
            print_schema_details(queryDesc->plannedstmt->rtable, queryDiagnosticsEntry);
    }
}

void
print_schema_details(List *rtable, QueryDiagnosticsEntry* queryDiagnosticsEntry)
{
    ListCell   *lc;

    /* Iterate over each element in the rtable list */
    foreach(lc, rtable)
    {
        RangeTblEntry *rte = (RangeTblEntry *) lfirst(lc);

        /* Check if the element is a table */
        if (rte->rtekind == RTE_RELATION)
        {
            /* Get the table name */
            char *table_name = get_rel_name(rte->relid);
            char* database_name = get_tablespace_name(get_rel_namespace(rte->relid));
            char* tablespace_name = get_tablespace_name(get_rel_tablespace(rte->relid));

            /* Print the table name */
            if (table_name != NULL){
                FILE* fptr = fopen("/Users/ishanchhangani/table.txt","a");
                fprintf(fptr, "\n----------------------------------\nTable name : %s\n" , table_name);
                fprintf(fptr, "Database name : %s\n" , database_name);
                fprintf(fptr, "Tablespace name : %s\n\n" , tablespace_name);
                fclose(fptr);
            }

            print_table_columns(rte->relid);
            print_table_indexes(rte->relid);
            print_table_foreign_keys(rte->relid);
        }
    }
}

void
print_table_columns(Oid relid)
{
    Relation    rel;
    TupleDesc   desc;
    int         i;

    /* Open the table */
    rel = relation_open(relid, AccessShareLock);
    /* Get the tuple descriptor */
    desc = RelationGetDescr(rel);
    /* Print each column name */
    for (i = 0; i < desc->natts; i++)
    {
        Form_pg_attribute attr = TupleDescAttr(desc, i);

        /* Ignore dropped columns */
        if (attr->attisdropped)
            continue;

        // attr->
        FILE* fptr = fopen("/Users/ishanchhangani/table.txt","a");
        fprintf(fptr, "Column: %s, " , NameStr(attr->attname));
        fprintf(fptr, "Type: %s\n" , format_type_be(attr->atttypid));
        fclose(fptr);
    }

    /* Close the table */
    relation_close(rel, AccessShareLock);
}

void
print_table_indexes(Oid relid)
{
    Relation    rel;
    List        *indexlist;
    ListCell    *lc;
    rel = relation_open(relid, AccessShareLock);

    /* Get the list of indexes for the table */
    indexlist = RelationGetIndexList(rel);

    /* Print each index */
    foreach(lc, indexlist)
    {   
        Oid         indexoid = lfirst_oid(lc);
        char        *indexname = get_rel_name(indexoid);

        if (indexname != NULL){

            FILE* fptr = fopen("/Users/ishanchhangani/table.txt","a");
            fprintf(fptr, "Index: %s\n" , indexname);
            fclose(fptr);
        }
    }

    /* Free the index list */
    list_free(indexlist);

     /* Close the table */
    relation_close(rel, AccessShareLock);
}

void
print_table_foreign_keys(Oid relid)
{
    SysScanDesc scan;
    ScanKeyData skey;
    HeapTuple   tuple;
    Relation    conrel;


    /* Open the pg_constraint catalog */
    conrel = heap_open(ConstraintRelationId, AccessShareLock);

    /* Initialize a scan key to search for constraints on the table */
    ScanKeyInit(&skey,
                Anum_pg_constraint_conrelid,
                BTEqualStrategyNumber, F_OIDEQ,
                ObjectIdGetDatum(relid));

    /* Begin a sequential scan */
    scan = systable_beginscan(conrel, ConstraintRelidTypidNameIndexId, true,
                              NULL, 1, &skey);

    /* Loop over each tuple returned by the scan */
    while ((tuple = systable_getnext(scan)) != NULL)
    {
        Form_pg_constraint con = (Form_pg_constraint) GETSTRUCT(tuple);

        /* Check if the constraint is a foreign key */
        if (con->contype == CONSTRAINT_FOREIGN)
        {
            /* Get the foreign key name */
            // char *fkeyname = NameStr(con->conname); this is some internal naming convention
            /* Get the referenced table */
            Oid refTableOid = con->confrelid;
            char *refTableName = get_rel_name(refTableOid);
            /* Get the referenced column */
            int16 conkey[INDEX_MAX_KEYS];
            int16 confkey[INDEX_MAX_KEYS];
            int numfks;
            DeconstructFkConstraintRow(tuple, &numfks, conkey, confkey, NULL,NULL,NULL);
            int refColumnIndex = confkey[0] - 1;  // Subtract 1 because array is 0-based
            int refOriginalColIndex = conkey[0] - 1;  // Subtract 1 because array is 0-based
            char *refColumnName = get_attname(refTableOid, refColumnIndex + 1,false);  // Add 1 because function is 1-based
            char *thisTableColumnName = get_attname(relid, refOriginalColIndex + 1,false);  // Add 1 because function is 1-based

            // elog(LOG, "Foreign Key: %s", fkeyname);
            FILE* fptr = fopen("/Users/ishanchhangani/table.txt","a");
            fprintf(fptr, "    FOREIGN KEY (%s) REFERENCES %s(%s)\n", thisTableColumnName, refTableName, refColumnName);
            // fprintf(fptr, "numfks: %d\n", numfks); Number of foreign keys
            // fprintf(fptr, "conkey: %s\n", thisTableColumnName);
            fclose(fptr);
        }

         if (con->contype == CONSTRAINT_PRIMARY)
        {
            // Bitmapset  *pkattnos;
            Oid constraintOid;
            // int attno;

            get_primary_key_attnos(relid, false, &constraintOid);


            // This function is compatible to get unique full unique key data also given contrainstOid which shouldn;t be that hard.
            char* constraintDef = pg_get_constraintdef_command(constraintOid);
            FILE* fptr = fopen("/Users/ishanchhangani/table.txt","a");
            fprintf(fptr, "    PRIMARY KEY (%s)\n", constraintDef);
            fclose(fptr);
        }
    }

    /* End the sequential scan */
    systable_endscan(scan);

    /* Close the pg_constraint catalog */
    heap_close(conrel, AccessShareLock);
}



void
create_shared_hashtable(void)
{
    HASHCTL ctl;

    /* Initialize the hash table control structure */
    memset(&ctl, 0, sizeof(ctl));

    /* Set the key size and entry size */
    ctl.keysize = sizeof(int64);
    ctl.entrysize = sizeof(HashEntry);
	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
    /* Create the hash table in shared memory */
    query_diagnostics_hash = ShmemInitHash("QueryDiagnostics shared hash table", 100, 100, &ctl, HASH_ELEM | HASH_BLOBS);
	
	LWLockRelease(AddinShmemInitLock);
}

/* Insert a value into the shared hash table */
void
insert_into_shared_hashtable(HTAB *htab, int64 key, QueryDiagnosticsEntry value)
{
    bool found;
    HashEntry *entry;

    /* Try to find the key in the hash table */
    entry = (HashEntry *) hash_search(htab, &key, HASH_ENTER, &found);

    /* Insert the value into the hash table */
	entry->key = key;
    entry->value = value;
}

/* Look up a value in the shared hash table */
QueryDiagnosticsEntry*
lookup_in_shared_hashtable(HTAB *htab, int64 key)
{
    // bool found;
    HashEntry *entry;


	// LWLockAcquire(pgss->lock, LW_SHARED);
    /* Try to find the key in the hash table */
    entry = (HashEntry *) hash_search(htab, &key, HASH_FIND, NULL);
	// LWLockRelease(pgss->lock);	

    if (entry) {
        /* The key was found in the hash table */
        return &entry->value;
    } else {
        /* The key was not found in the hash table */
        return NULL;
    }
}

void remove_from_shared_hashtable(HTAB *htab, int64 key) {
    /* Try to find the key in the hash table and remove it */
    hash_search(htab, &key, HASH_REMOVE, NULL);
}

Datum
yb_query_diagnostics(PG_FUNCTION_ARGS) //allows geneartion of bundle for a specific query id
{
    bool is_queryid_null = PG_ARGISNULL(0);
	int64 query_id = is_queryid_null ? 0 : PG_GETARG_INT64(0);

    bool is_explain_sample_rate_null = PG_ARGISNULL(1);
	int64 explain_sample_rate = is_explain_sample_rate_null ? 1 : PG_GETARG_INT64(1);

    bool is_explain_analyze_null = PG_ARGISNULL(2);
	bool explain_analyze = is_explain_analyze_null ? false : PG_GETARG_BOOL(2);

    bool is_explain_dist_null = PG_ARGISNULL(3);
	bool explain_dist = is_explain_dist_null ? false : PG_GETARG_BOOL(3);

    bool is_explain_debug_null = PG_ARGISNULL(4);
	bool explain_debug = is_explain_debug_null ? false : PG_GETARG_BOOL(4);

    bool is_bind_var_query_min_duration_ms_null = PG_ARGISNULL(5);
	int64 bind_var_query_min_duration_ms = is_bind_var_query_min_duration_ms_null ? 1 : PG_GETARG_INT64(5);


    bool is_diagnostics_interval_sec_null = PG_ARGISNULL(6);
	int64 diagnostics_interval_sec = is_diagnostics_interval_sec_null ? 300 : PG_GETARG_INT64(6);

    if(query_id && bind_var_query_min_duration_ms && explain_sample_rate && diagnostics_interval_sec && explain_analyze && explain_dist && explain_debug)
    ;

	shared_bundle_variables->isOn = true;
	shared_bundle_variables->total++;

    QueryDiagnosticsEntry* result = lookup_in_shared_hashtable(query_diagnostics_hash, query_id);
	if(result){
		ereport(LOG, (errmsg("Cannot start the bundle for the queryid[ %ld ] as it is already running", query_id)));
		 PG_RETURN_TEXT_P(cstring_to_text("Cannot start the bundle for the this queryid as it is already running"));
	}

    QueryDiagnosticsEntry entry = {0};
    
    //set params
    entry.bind_var_query_min_duration_ms = bind_var_query_min_duration_ms;
    entry.diagnostics_interval_sec = diagnostics_interval_sec;
    entry.explain_sample_rate = explain_sample_rate;
    entry.explain_analyze = explain_analyze;
    entry.explain_dist = explain_dist;
    entry.explain_debug = explain_debug;
    entry.start_time = GetCurrentTimestamp();

    insert_into_shared_hashtable(query_diagnostics_hash, query_id, entry);

    PG_RETURN_TEXT_P(cstring_to_text("Give Folder path here!"));
}