/*-------------------------------------------------------------------------
 *
 * slotfuncs.c
 *	   Support functions for replication slots
 *
 * Copyright (c) 2012-2018, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  src/backend/replication/slotfuncs.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "funcapi.h"
#include "miscadmin.h"

#include "access/htup_details.h"
#include "replication/decode.h"
#include "replication/slot.h"
#include "replication/logical.h"
#include "replication/logicalfuncs.h"
#include "utils/builtins.h"
#include "utils/inval.h"
#include "utils/pg_lsn.h"
#include "utils/resowner.h"

/* YB includes. */
#include "commands/ybccmds.h"
#include "pg_yb_utils.h"
#include "utils/uuid.h"

static void
check_permissions(void)
{
	if (!superuser() && !has_rolreplication(GetUserId()))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("must be superuser or replication role to use replication slots"))));
}

/*
 * SQL function for creating a new physical (streaming replication)
 * replication slot.
 */
Datum
pg_create_physical_replication_slot(PG_FUNCTION_ARGS)
{
	Name		name = PG_GETARG_NAME(0);
	bool		immediately_reserve = PG_GETARG_BOOL(1);
	bool		temporary = PG_GETARG_BOOL(2);
	Datum		values[2];
	bool		nulls[2];
	TupleDesc	tupdesc;
	HeapTuple	tuple;
	Datum		result;

	Assert(!MyReplicationSlot);

	if (IsYugaByteEnabled())
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("YSQL only supports logical replication slots")));

	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	check_permissions();

	CheckSlotRequirements();

	/* acquire replication slot, this will check for conflicting names */
	ReplicationSlotCreate(NameStr(*name), false,
						  temporary ? RS_TEMPORARY : RS_PERSISTENT,
						  CRS_NOEXPORT_SNAPSHOT, NULL);

	values[0] = NameGetDatum(&MyReplicationSlot->data.name);
	nulls[0] = false;

	if (immediately_reserve)
	{
		/* Reserve WAL as the user asked for it */
		ReplicationSlotReserveWal();

		/* Write this slot to disk */
		ReplicationSlotMarkDirty();
		ReplicationSlotSave();

		values[1] = LSNGetDatum(MyReplicationSlot->data.restart_lsn);
		nulls[1] = false;
	}
	else
	{
		nulls[1] = true;
	}

	tuple = heap_form_tuple(tupdesc, values, nulls);
	result = HeapTupleGetDatum(tuple);

	ReplicationSlotRelease();

	PG_RETURN_DATUM(result);
}


/*
 * SQL function for creating a new logical replication slot.
 */
Datum
pg_create_logical_replication_slot(PG_FUNCTION_ARGS)
{
	if (IsYugaByteEnabled() && !yb_enable_replication_commands)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("pg_create_logical_replication_slot is unavailable"),
				 errdetail("yb_enable_replication_commands is false or a "
						   "system upgrade is in progress")));

	Name		name = PG_GETARG_NAME(0);
	Name		plugin = PG_GETARG_NAME(1);
	bool		temporary = PG_GETARG_BOOL(2);

	LogicalDecodingContext *ctx = NULL;

	TupleDesc	tupdesc;
	HeapTuple	tuple;
	Datum		result;
	Datum		values[2];
	bool		nulls[2];

	Assert(!MyReplicationSlot);

	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	if (IsYugaByteEnabled())
	{
		if (temporary)
			ereport(ERROR, 
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("Temporary replication slot is not yet supported"),
					 errhint("See https://github.com/yugabyte/yugabyte-db/"
							 "issues/19263. React with thumbs up to raise its "
							 "priority")));
	
		/*
		 * Validate output plugin requirement early so that we can avoid the
		 * expensive call to yb-master.
		 *
		 * This is different from PG where the validation is done after creating
		 * the replication slot on disk which is cleaned up in case of errors.
		 *
		 * TODO(#20756): Support other plugins such as test_decoding once we
		 * store replication slot metadata in yb-master.
		 */
		if (plugin == NULL || strcmp(NameStr(*plugin), PG_OUTPUT_PLUGIN) != 0)
			ereport(ERROR, 
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid output plugin"),
					 errdetail("Only 'pgoutput' plugin is supported")));
	}

	check_permissions();

	CheckLogicalDecodingRequirements();

	/*
	 * Acquire a logical decoding slot, this will check for conflicting names.
	 * Initially create persistent slot as ephemeral - that allows us to
	 * nicely handle errors during initialization because it'll get dropped if
	 * this transaction fails. We'll make it persistent at the end. Temporary
	 * slots can be created as temporary from beginning as they get dropped on
	 * error as well.
	 *
	 * YB Note: We use CRS_NOEXPORT_SNAPSHOT here since it is the only supported
	 * mechanism via this function in PG. It is evident from the
	 * CreateInitDecodingContext call below where `need_full_snapshot` is passed
	 * as false indicating that no snapshot should be built.
	 */
	ReplicationSlotCreate(NameStr(*name), true,
						  temporary ? RS_TEMPORARY : RS_EPHEMERAL,
						  CRS_NOEXPORT_SNAPSHOT, NULL);

	memset(nulls, 0, sizeof(nulls));

	if (IsYugaByteEnabled())
	{
		values[0] = CStringGetTextDatum(name->data);
		/* Send lsn as NULL */
		nulls[1] = true;
	}
	else
	{
		/*
		 * Create logical decoding context, to build the initial snapshot.
		 */
		ctx = CreateInitDecodingContext(NameStr(*plugin), NIL,
										false,	/* do not build snapshot */
										logical_read_local_xlog_page, NULL, NULL,
										NULL);

		/* build initial snapshot, might take a while */
		DecodingContextFindStartpoint(ctx);
	
		values[0] = CStringGetTextDatum(NameStr(MyReplicationSlot->data.name));
		values[1] = LSNGetDatum(MyReplicationSlot->data.confirmed_flush);

		/* don't need the decoding context anymore */
		FreeDecodingContext(ctx);
	}

	tuple = heap_form_tuple(tupdesc, values, nulls);
	result = HeapTupleGetDatum(tuple);

	if (!IsYugaByteEnabled())
	{
		/* ok, slot is now fully created, mark it as persistent if needed */
		if (!temporary)
			ReplicationSlotPersist();
		ReplicationSlotRelease();
	}

	PG_RETURN_DATUM(result);
}


/*
 * SQL function for dropping a replication slot.
 */
Datum
pg_drop_replication_slot(PG_FUNCTION_ARGS)
{
	if (IsYugaByteEnabled() && !yb_enable_replication_commands)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("pg_drop_replication_slot is unavailable"),
				 errdetail("yb_enable_replication_commands is false or a "
				 		   "system upgrade is in progress")));

	Name		name = PG_GETARG_NAME(0);

	check_permissions();

	CheckSlotRequirements();

	ReplicationSlotDrop(NameStr(*name), true);

	PG_RETURN_VOID();
}

/*
 * pg_get_replication_slots - SQL SRF showing active replication slots.
 */
Datum
pg_get_replication_slots(PG_FUNCTION_ARGS)
{
#define PG_GET_REPLICATION_SLOTS_COLS 11
/* YB specific fields in pg_get_replication_slots */
#define YB_PG_GET_REPLICATION_SLOTS_COLS 1

	if (IsYugaByteEnabled() && !yb_enable_replication_commands)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("pg_get_replication_slots is unavailable"),
				 errdetail("yb_enable_replication_commands is false or a "
				 		   "system upgrade is in progress")));

	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	int			slotno;

	int			yb_totalslots;

	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not " \
						"allowed in this context")));

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	/*
	 * We don't require any special permission to see this function's data
	 * because nothing should be sensitive. The most critical being the slot
	 * name, which shouldn't contain anything particularly sensitive.
	 */

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	YBCReplicationSlotDescriptor *yb_replication_slots = NULL;
	size_t yb_numreplicationslots = 0;

	/*
	 * Fetch the replication slots from yb-master.
	 *
	 * For YSQL, the source of truth is yb-master and the
	 * ReplicationSlotCtl->replication_slots array just acts as a cache.
	 */
	if (IsYugaByteEnabled())
		YBCListReplicationSlots(&yb_replication_slots, &yb_numreplicationslots);

	yb_totalslots = (IsYugaByteEnabled()) ? yb_numreplicationslots :
										 max_replication_slots;

	LWLockAcquire(ReplicationSlotControlLock, LW_SHARED);
	for (slotno = 0; slotno < yb_totalslots; slotno++)
	{
		ReplicationSlot *slot = &ReplicationSlotCtl->replication_slots[slotno];
		Datum		values[PG_GET_REPLICATION_SLOTS_COLS +
						   YB_PG_GET_REPLICATION_SLOTS_COLS];
		bool		nulls[PG_GET_REPLICATION_SLOTS_COLS +
						  YB_PG_GET_REPLICATION_SLOTS_COLS];

		ReplicationSlotPersistency persistency;
		TransactionId xmin;
		TransactionId catalog_xmin;
		XLogRecPtr	restart_lsn;
		XLogRecPtr	confirmed_flush_lsn;
		pid_t		active_pid;
		Oid			database;
		NameData	slot_name;
		NameData	plugin;
		int			i;

		const char	*yb_stream_id;
		bool		yb_stream_active;

		if (IsYugaByteEnabled())
		{
			YBCReplicationSlotDescriptor *slot = &yb_replication_slots[slotno];

			database = slot->database_oid;
			namestrcpy(&slot_name, slot->slot_name);
			namestrcpy(&plugin, PG_OUTPUT_PLUGIN);
			yb_stream_id = slot->stream_id;
			yb_stream_active = slot->active;

			/* Fill in the dummy values. */
			xmin = InvalidXLogRecPtr;
			catalog_xmin = InvalidXLogRecPtr;
			restart_lsn = InvalidXLogRecPtr;
			confirmed_flush_lsn = InvalidXLogRecPtr;
			active_pid = 0;
			persistency = RS_PERSISTENT;
		}
		else
		{
			if (!slot->in_use)
				continue;

			SpinLockAcquire(&slot->mutex);

			xmin = slot->data.xmin;
			catalog_xmin = slot->data.catalog_xmin;
			database = slot->data.database;
			restart_lsn = slot->data.restart_lsn;
			confirmed_flush_lsn = slot->data.confirmed_flush;
			namecpy(&slot_name, &slot->data.name);
			namecpy(&plugin, &slot->data.plugin);
			active_pid = slot->active_pid;
			persistency = slot->data.persistency;

			SpinLockRelease(&slot->mutex);
		}

		memset(nulls, 0, sizeof(nulls));

		i = 0;
		values[i++] = NameGetDatum(&slot_name);

		if (database == InvalidOid)
			nulls[i++] = true;
		else
			values[i++] = NameGetDatum(&plugin);

		if (database == InvalidOid)
			values[i++] = CStringGetTextDatum("physical");
		else
			values[i++] = CStringGetTextDatum("logical");

		if (database == InvalidOid)
			nulls[i++] = true;
		else
			values[i++] = database;

		values[i++] = BoolGetDatum(persistency == RS_TEMPORARY);

		if (IsYugaByteEnabled())
			values[i++] = BoolGetDatum(yb_stream_active);
		else
			values[i++] = BoolGetDatum(active_pid != 0);

		if (active_pid != 0)
			values[i++] = Int32GetDatum(active_pid);
		else
			nulls[i++] = true;

		if (xmin != InvalidTransactionId)
			values[i++] = TransactionIdGetDatum(xmin);
		else
			nulls[i++] = true;

		if (catalog_xmin != InvalidTransactionId)
			values[i++] = TransactionIdGetDatum(catalog_xmin);
		else
			nulls[i++] = true;

		if (restart_lsn != InvalidXLogRecPtr)
			values[i++] = LSNGetDatum(restart_lsn);
		else
			nulls[i++] = true;

		if (confirmed_flush_lsn != InvalidXLogRecPtr)
			values[i++] = LSNGetDatum(confirmed_flush_lsn);
		else
			nulls[i++] = true;

		if (IsYugaByteEnabled())
			values[i++] = CStringGetTextDatum(yb_stream_id);
		else
			nulls[i++] = true;

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}
	LWLockRelease(ReplicationSlotControlLock);

	tuplestore_donestoring(tupstore);

	return (Datum) 0;
}

/*
 * Helper function for advancing our physical replication slot forward.
 *
 * The LSN position to move to is compared simply to the slot's restart_lsn,
 * knowing that any position older than that would be removed by successive
 * checkpoints.
 */
static XLogRecPtr
pg_physical_replication_slot_advance(XLogRecPtr moveto)
{
	XLogRecPtr	startlsn = MyReplicationSlot->data.restart_lsn;
	XLogRecPtr	retlsn = startlsn;

	if (startlsn < moveto)
	{
		SpinLockAcquire(&MyReplicationSlot->mutex);
		MyReplicationSlot->data.restart_lsn = moveto;
		SpinLockRelease(&MyReplicationSlot->mutex);
		retlsn = moveto;
	}

	return retlsn;
}

/*
 * Helper function for advancing our logical replication slot forward.
 *
 * The slot's restart_lsn is used as start point for reading records,
 * while confirmed_lsn is used as base point for the decoding context.
 *
 * We cannot just do LogicalConfirmReceivedLocation to update confirmed_flush,
 * because we need to digest WAL to advance restart_lsn allowing to recycle
 * WAL and removal of old catalog tuples.  As decoding is done in fast_forward
 * mode, no changes are generated anyway.
 */
static XLogRecPtr
pg_logical_replication_slot_advance(XLogRecPtr moveto)
{
	LogicalDecodingContext *ctx;
	ResourceOwner old_resowner = CurrentResourceOwner;
	XLogRecPtr	startlsn;
	XLogRecPtr	retlsn;

	PG_TRY();
	{
		/*
		 * Create our decoding context in fast_forward mode, passing start_lsn
		 * as InvalidXLogRecPtr, so that we start processing from my slot's
		 * confirmed_flush.
		 */
		ctx = CreateDecodingContext(InvalidXLogRecPtr,
									NIL,
									true,	/* fast_forward */
									logical_read_local_xlog_page,
									NULL, NULL, NULL);

		/*
		 * Start reading at the slot's restart_lsn, which we know to point to
		 * a valid record.
		 */
		startlsn = MyReplicationSlot->data.restart_lsn;

		/* Initialize our return value in case we don't do anything */
		retlsn = MyReplicationSlot->data.confirmed_flush;

		/* invalidate non-timetravel entries */
		InvalidateSystemCaches();

		/* Decode at least one record, until we run out of records */
		while ((!XLogRecPtrIsInvalid(startlsn) &&
				startlsn < moveto) ||
			   (!XLogRecPtrIsInvalid(ctx->reader->EndRecPtr) &&
				ctx->reader->EndRecPtr < moveto))
		{
			char	   *errm = NULL;
			XLogRecord *record;

			/*
			 * Read records.  No changes are generated in fast_forward mode,
			 * but snapbuilder/slot statuses are updated properly.
			 */
			record = XLogReadRecord(ctx->reader, startlsn, &errm);
			if (errm)
				elog(ERROR, "%s", errm);

			/* Read sequentially from now on */
			startlsn = InvalidXLogRecPtr;

			/*
			 * Process the record.  Storage-level changes are ignored in
			 * fast_forward mode, but other modules (such as snapbuilder)
			 * might still have critical updates to do.
			 */
			if (record)
				LogicalDecodingProcessRecord(ctx, ctx->reader);

			/* Stop once the requested target has been reached */
			if (moveto <= ctx->reader->EndRecPtr)
				break;

			CHECK_FOR_INTERRUPTS();
		}

		/*
		 * Logical decoding could have clobbered CurrentResourceOwner during
		 * transaction management, so restore the executor's value.  (This is
		 * a kluge, but it's not worth cleaning up right now.)
		 */
		CurrentResourceOwner = old_resowner;

		if (ctx->reader->EndRecPtr != InvalidXLogRecPtr)
		{
			LogicalConfirmReceivedLocation(moveto);

			/*
			 * If only the confirmed_flush LSN has changed the slot won't get
			 * marked as dirty by the above. Callers on the walsender
			 * interface are expected to keep track of their own progress and
			 * don't need it written out. But SQL-interface users cannot
			 * specify their own start positions and it's harder for them to
			 * keep track of their progress, so we should make more of an
			 * effort to save it for them.
			 *
			 * Dirty the slot so it's written out at the next checkpoint.
			 * We'll still lose its position on crash, as documented, but it's
			 * better than always losing the position even on clean restart.
			 */
			ReplicationSlotMarkDirty();
		}

		retlsn = MyReplicationSlot->data.confirmed_flush;

		/* free context, call shutdown callback */
		FreeDecodingContext(ctx);

		InvalidateSystemCaches();
	}
	PG_CATCH();
	{
		/* clear all timetravel entries */
		InvalidateSystemCaches();

		PG_RE_THROW();
	}
	PG_END_TRY();

	return retlsn;
}

/*
 * SQL function for moving the position in a replication slot.
 */
Datum
pg_replication_slot_advance(PG_FUNCTION_ARGS)
{
	Name		slotname = PG_GETARG_NAME(0);
	XLogRecPtr	moveto = PG_GETARG_LSN(1);
	XLogRecPtr	endlsn;
	XLogRecPtr	minlsn;
	TupleDesc	tupdesc;
	Datum		values[2];
	bool		nulls[2];
	HeapTuple	tuple;
	Datum		result;

	Assert(!MyReplicationSlot);

	check_permissions();

	if (XLogRecPtrIsInvalid(moveto))
		ereport(ERROR,
				(errmsg("invalid target wal lsn")));

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	/*
	 * We can't move slot past what's been flushed/replayed so clamp the
	 * target position accordingly.
	 */
	if (!RecoveryInProgress())
		moveto = Min(moveto, GetFlushRecPtr());
	else
		moveto = Min(moveto, GetXLogReplayRecPtr(&ThisTimeLineID));

	/* Acquire the slot so we "own" it */
	ReplicationSlotAcquire(NameStr(*slotname), true);

	/* A slot whose restart_lsn has never been reserved cannot be advanced */
	if (XLogRecPtrIsInvalid(MyReplicationSlot->data.restart_lsn))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot advance replication slot that has not previously reserved WAL")));

	/*
	 * Check if the slot is not moving backwards.  Physical slots rely simply
	 * on restart_lsn as a minimum point, while logical slots have confirmed
	 * consumption up to confirmed_lsn, meaning that in both cases data older
	 * than that is not available anymore.
	 */
	if (OidIsValid(MyReplicationSlot->data.database))
		minlsn = MyReplicationSlot->data.confirmed_flush;
	else
		minlsn = MyReplicationSlot->data.restart_lsn;

	if (moveto < minlsn)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot advance replication slot to %X/%X, minimum is %X/%X",
						(uint32) (moveto >> 32), (uint32) moveto,
						(uint32) (minlsn >> 32), (uint32) minlsn)));

	/* Do the actual slot update, depending on the slot type */
	if (OidIsValid(MyReplicationSlot->data.database))
		endlsn = pg_logical_replication_slot_advance(moveto);
	else
		endlsn = pg_physical_replication_slot_advance(moveto);

	values[0] = NameGetDatum(&MyReplicationSlot->data.name);
	nulls[0] = false;

	/* Update the on disk state when lsn was updated. */
	if (XLogRecPtrIsInvalid(endlsn))
	{
		ReplicationSlotMarkDirty();
		ReplicationSlotsComputeRequiredXmin(false);
		ReplicationSlotsComputeRequiredLSN();
		ReplicationSlotSave();
	}

	ReplicationSlotRelease();

	/* Return the reached position. */
	values[1] = LSNGetDatum(endlsn);
	nulls[1] = false;

	tuple = heap_form_tuple(tupdesc, values, nulls);
	result = HeapTupleGetDatum(tuple);

	PG_RETURN_DATUM(result);
}
