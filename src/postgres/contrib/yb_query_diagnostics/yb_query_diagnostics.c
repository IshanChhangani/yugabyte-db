#include "postgres.h"
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

PG_MODULE_MAGIC;

void _PG_init(void);
void _PG_fini(void);

static ExecutorStart_hook_type prev_ExecutorStart = NULL;
static ExecutorRun_hook_type prev_ExecutorRun = NULL;
static ExecutorFinish_hook_type prev_ExecutorFinish = NULL; 
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;

static void my_ExecutorStart(QueryDesc *queryDesc, int eflags);
static void my_ExecutorRun(QueryDesc *queryDesc, ScanDirection direction, uint64 count, bool execute_once);
static void my_ExecutorFinish(QueryDesc *queryDesc); 
static void my_ExecutorEnd(QueryDesc *queryDesc);
static void print_schema_details(List *rtable);
static void print_table_columns(Oid relid);
static void print_table_indexes(Oid relid);
static void print_table_foreign_keys(Oid relid);
void _PG_init(void)
{
    prev_ExecutorStart = ExecutorStart_hook;
    ExecutorStart_hook = my_ExecutorStart;
    prev_ExecutorRun = ExecutorRun_hook;
    ExecutorRun_hook = my_ExecutorRun;
    prev_ExecutorFinish = ExecutorFinish_hook; 
    ExecutorFinish_hook = my_ExecutorFinish; 
    prev_ExecutorEnd = ExecutorEnd_hook;
    ExecutorEnd_hook = my_ExecutorEnd;
}

void _PG_fini(void)
{
    ExecutorStart_hook = prev_ExecutorStart;
    ExecutorRun_hook = prev_ExecutorRun;
    ExecutorFinish_hook = prev_ExecutorFinish; 
    ExecutorEnd_hook = prev_ExecutorEnd;
}

static void my_ExecutorStart(QueryDesc *queryDesc, int eflags)
{
    if (prev_ExecutorStart)
        prev_ExecutorStart(queryDesc, eflags);
    else
        standard_ExecutorStart(queryDesc, eflags);
}

static void my_ExecutorRun(QueryDesc *queryDesc, ScanDirection direction, uint64 count, bool execute_once)
{
    if (prev_ExecutorRun)
        prev_ExecutorRun(queryDesc, direction, count, execute_once);
    else
        standard_ExecutorRun(queryDesc, direction, count, execute_once);
}
static void my_ExecutorFinish(QueryDesc *queryDesc) 
{
    if (prev_ExecutorFinish)
        prev_ExecutorFinish(queryDesc);
    else
        standard_ExecutorFinish(queryDesc);
}
static void my_ExecutorEnd(QueryDesc *queryDesc)
{
    if (prev_ExecutorEnd)
        prev_ExecutorEnd(queryDesc);
    else
        standard_ExecutorEnd(queryDesc);

    print_schema_details(queryDesc->plannedstmt->rtable);
}

void
print_schema_details(List *rtable)
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