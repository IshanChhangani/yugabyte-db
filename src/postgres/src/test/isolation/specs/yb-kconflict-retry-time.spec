setup
{
    create table test (k int primary key, v int);
    insert into test values (1, 1);
}

teardown
{
    DROP TABLE test;
}

session "s1"
setup 
{ 
    SET yb_max_query_layer_retries = 60;
    SELECT pg_stat_statements_reset();
}
step "s1_begin"     { BEGIN; }
step "s1_update"	{ UPDATE test SET v=20 WHERE k=1; }
step "s1_wait5s"    { SELECT pg_sleep(5); }
step "c1"           { COMMIT; }
step "create_function"
{
    CREATE OR REPLACE FUNCTION get_execution_time() RETURNS FLOAT
    AS $$
    DECLARE 
        time_json jsonb;
        execution_time FLOAT;
    BEGIN
        EXECUTE 'EXPLAIN (ANALYZE, FORMAT JSON) UPDATE test SET v=20 WHERE k=1' INTO time_json;
        execution_time := (time_json->0->'Execution Time')::FLOAT;
        RETURN execution_time;
    END;
    $$  LANGUAGE plpgsql;
}
step "s1_reduce_retries" { SET yb_max_query_layer_retries = 1; }


session "s2"
setup
{
    SET yb_max_query_layer_retries = 60;
}
step "s2_begin"     { BEGIN; }
step "s2_update"	{ UPDATE test SET v=40 WHERE k=1; }
step "c2"           { COMMIT; }
step "s2_pgss_check_update"
{ 
    SELECT calls, rows, query, mean_time FROM pg_stat_statements WHERE query LIKE '%UPDATE test%' and mean_time > 5000; 
}
step "s2_pgss_check_explain_analyze"
{ 
    SELECT calls, rows, query, mean_time FROM pg_stat_statements WHERE query LIKE '%get_execution_time%' and mean_time > 5000; 
}
step "s2_explain_analyze_check_exec_time"
{ 
    select count(*) from (select get_execution_time() as exec_time) as func where exec_time > 5000; 
}
step "s2_explain_analyze"
{ 
    select count(*) from (select get_execution_time() as exec_time) as func;
}
step "s2_reduce_retries" { SET yb_max_query_layer_retries = 1; }

# Check pgss time.
permutation "s1_begin" "s2_begin" "s1_update" "s2_update" "s1_wait5s" "c1" "c2" "s2_pgss_check_update"
# Exhausting the retries
permutation "s1_begin" "s2_begin" "s1_reduce_retries" "s2_reduce_retries" "s1_update" "s2_update" "s1_wait5s" "c1" "c2" "s2_pgss_check_update"
# Check explain analyze time.
permutation "s1_begin" "s2_begin" "create_function" "s1_update" "s2_explain_analyze_check_exec_time" "s1_wait5s" "c1" "c2"
# Check explain analyze entry within pgss.
permutation "s1_begin" "s2_begin" "create_function" "s1_update" "s2_explain_analyze" "s1_wait5s" "c1" "c2" "s2_pgss_check_explain_analyze"
