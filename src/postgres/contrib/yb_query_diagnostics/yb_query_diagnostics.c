#include "postgres.h"
#include "executor/execdebug.h"
#include "executor/instrument.h"
#include "utils/guc.h"

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
}