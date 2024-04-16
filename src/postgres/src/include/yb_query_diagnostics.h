#ifndef YB_QUERY_DIAGNOSTICS_H
#define YB_QUERY_DIAGNOSTICS_H

#include "postgres.h"
#include "storage/proc.h"
#include "utils/guc.h"
#include "utils/timestamp.h"

/* GUC variables */
extern bool yb_enable_query_diagnostics;


extern void YbQueryDiagnosticsInstallHooks(void);
extern bool yb_enable_query_diagnostics_check_hook(bool *newval,
									 void **extra,
									 GucSource source);

#endif                            /* YB_QUERY_DIAGNOSTICS_H */
