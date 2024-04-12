-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION yb_query_diagnostics" to load this file. \quit

CREATE OR REPLACE FUNCTION yb_query_diagnostics() RETURNS VOID AS '$libdir/yb_query_diagnostics', 'yb_query_diagnostics' LANGUAGE C STRICT;