package org.yb.pgsql;

import java.io.PrintWriter;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Collections;
import java.util.Map;
import org.junit.Assert;
import org.junit.Test;

public class TestRetryTime extends BasePgSQLTest {

    private void setConfigAndRestartCluster() throws Exception {
        Map<String, String> flagMap = super.getTServerFlags();
        flagMap.put("enable_wait_queues", "false");
        restartClusterWithFlags(Collections.emptyMap(), flagMap);
    }

    @Test
    public void testRetryTime() throws Exception {
        setConfigAndRestartCluster();

        try (Connection c1 = getConnectionBuilder().withTServer(0).connect();
                Connection c2 = getConnectionBuilder().withTServer(0).connect();
                Statement s1 = c1.createStatement();
                Statement s2 = c2.createStatement();
                PrintWriter writer = new PrintWriter("/Users/ishanchhangani/test.txt", "UTF-8");) {

            s1.execute("CREATE TABLE test (k int, v int)");
            waitForTServerHeartbeat();
            assertQuery(s1, "SELECT * FROM test");
            s1.execute("INSERT INTO test VALUES (1, 1)");
            s1.execute("SELECT pg_stat_statements_reset()");
            s1.execute("UPDATE test SET v = 2 WHERE k = 1");
            ResultSet rs = s1
                    .executeQuery("SELECT mean_time,calls FROM pg_stat_statements WHERE query like '%UPDATE test%'");
            rs.next();
            double meanTime = rs.getDouble(1);
            int calls = rs.getInt(2);
            assert (calls == 1);
            writer.println("--------------- normal ---------------");
            writer.println("meanTime:" + meanTime);

            // for (int i = 0; i < 10; i++) {
            // Now lets do the confilct
            s1.execute("BEGIN");
            s2.execute("BEGIN");
            s1.execute("UPDATE test SET v = 3 WHERE k = 1");
            s1.execute("select pg_stat_statements_reset()");
            writer.println("waiting for 5 seconds");
            new Thread(() -> {
                try {
                    s2.execute("UPDATE test SET v = 55 WHERE k = 1");
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }).start();
            Thread.sleep(5000);
            writer.println("done waiting");
            s1.execute("COMMIT");
            s2.execute("COMMIT");
            rs = s1.executeQuery(
                    "SELECT max_time,min_time,mean_time,calls FROM pg_stat_statements WHERE query like '%UPDATE test%'");
            rs.next();
            double maxTime = rs.getDouble(1);
            double minTime = rs.getDouble(2);
            double meanTimeConflict = rs.getDouble(3);
            calls = rs.getInt(4);
            assert (calls == 1);
            Assert.assertEquals("calls != 1", calls, 1);
            Assert.assertTrue("Time is 5 times of meanTime", meanTimeConflict > meanTime * 5);
            Assert.assertTrue("meanTime is not less than 50 or meanTimeConflict is not greater than 50",
                    meanTime < 50 && meanTimeConflict > 50);
            writer.println("--------------- kconflict ---------------");
            writer.println("meanTime:" + meanTimeConflict);
            writer.println("maxTime:" + maxTime);
            writer.println("minTime:" + minTime);
            s1.execute("DROP TABLE test");
            // }
        }
    }
}