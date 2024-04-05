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
    BEGIN;
}
step "update_in_s1"	{ UPDATE test SET v=20 WHERE k=1; }
step "pgss_reset" { SELECT pg_stat_statements_reset(); }
step "wait5s_s1" { SELECT pg_sleep(5); }
step "c1" { COMMIT; }
step "pgss_check_update_s1"
{ 
    SELECT rows FROM pg_stat_statements WHERE query LIKE '%UPDATE test%' and mean_time > 5000; 
}
step "pgss_check_explain_analyze_s1"
{ 
    SELECT rows FROM pg_stat_statements WHERE query LIKE '%get_execution_time%' and mean_time > 5000; 
}
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



session "s2"
setup
{
    SET yb_max_query_layer_retries = 60;
    BEGIN;
}
step "update_in_s2"	{ UPDATE test SET v=40 WHERE k=1; }
step "c2" { COMMIT; }
step "explain_analyze_check_exec_time_s2"
{ 
    select count(*) from (select get_execution_time() as exec_time) as func where exec_time > 5000; 
}
step "explain_analyze_s2"
{ 
    select count(*) from (select get_execution_time() as exec_time) as func;
}


# Check pgss time.
permutation "update_in_s1" "pgss_reset" "update_in_s2" "wait5s_s1" "c1" "c2" "pgss_check_update_s1"
# Check explain analyze time.
permutation "create_function" "update_in_s1" "explain_analyze_check_exec_time_s2" "wait5s_s1" "c1" "c2"
# Check explain analyze entry within pgss.
permutation "create_function" "update_in_s1" "pgss_reset" "explain_analyze_s2" "wait5s_s1" "c1" "c2" "pgss_check_explain_analyze_s1"