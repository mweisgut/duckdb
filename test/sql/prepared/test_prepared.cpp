#include "catch.hpp"
#include "common/file_system.hpp"
#include "common/types/date.hpp"
#include "test_helpers.hpp"

using namespace duckdb;
using namespace std;

TEST_CASE("Basic prepared statements", "[prepared]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("PREPARE s1 AS SELECT CAST($1 AS INTEGER), CAST($2 AS STRING)"));
	result = con.Query("EXECUTE s1(42, 'dpfkg')");

	REQUIRE(CHECK_COLUMN(result, 0, {42}));
	REQUIRE(CHECK_COLUMN(result, 1, {"dpfkg"}));

	result = con.Query("EXECUTE s1(43, 'asdf')");
	REQUIRE(CHECK_COLUMN(result, 0, {43}));
	REQUIRE(CHECK_COLUMN(result, 1, {"asdf"}));

	// not enough params
	REQUIRE_FAIL(con.Query("EXECUTE s1(43)"));
	// too many
	REQUIRE_FAIL(con.Query("EXECUTE s1(43, 'asdf', 42)"));
	// wrong non-castable types
	REQUIRE_FAIL(con.Query("EXECUTE s1('asdf', 'asdf')"));

	REQUIRE_NO_FAIL(con.Query("DEALLOCATE s1"));

	// we can deallocate non-existing statements
	REQUIRE_NO_FAIL(con.Query("DEALLOCATE s2"));

	// now its gone
	REQUIRE_FAIL(con.Query("EXECUTE s1(42, 'dpfkg')"));

	// prepare a statement that cannot be prepared
	REQUIRE_FAIL(con.Query("PREPARE EXPLAIN SELECT 42"));

	REQUIRE_FAIL(con.Query("PREPARE CREATE TABLE a(i INTEGER)"));
	REQUIRE_FAIL(con.Query("SELECT * FROM a;"));

	// type will be resolved to "double"
	REQUIRE_NO_FAIL(con.Query("PREPARE s1 AS SELECT $1+$2"));
	REQUIRE_NO_FAIL(con.Query("DEALLOCATE s1"));

	// but this works
	REQUIRE_NO_FAIL(con.Query("PREPARE s1 AS SELECT NOT($1), 10+$2, $3+20, 4 IN (2, 3, $4), $5 IN (2, 3, 4)"));

	result = con.Query("EXECUTE s1(1, 2, 3, 4, 2)");
	REQUIRE(CHECK_COLUMN(result, 0, {false}));
	REQUIRE(CHECK_COLUMN(result, 1, {12}));
	REQUIRE(CHECK_COLUMN(result, 2, {23}));
	REQUIRE(CHECK_COLUMN(result, 3, {true}));
	REQUIRE(CHECK_COLUMN(result, 4, {true}));

	// cannot resolve these types
	REQUIRE_FAIL(con.Query("PREPARE s1 AS SELECT $1"));
	REQUIRE_FAIL(con.Query("PREPARE s1 AS SELECT (SELECT $1)"));
	REQUIRE_FAIL(con.Query("PREPARE s1 AS SELECT $1=$2"));
}

TEST_CASE("Prepared statements and subqueries", "[prepared]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	// simple subquery
	REQUIRE_NO_FAIL(con.Query("PREPARE v1 AS SELECT * FROM (SELECT $1::INTEGER) sq1;"));

	result = con.Query("EXECUTE v1(42)");
	REQUIRE(CHECK_COLUMN(result, 0, {42}));

	// subquery with non-fulfillable predicate
	REQUIRE_NO_FAIL(con.Query("PREPARE v2 AS SELECT * FROM (SELECT $1::INTEGER WHERE 1=0) sq1;"));

	result = con.Query("EXECUTE v2(42)");
	REQUIRE(CHECK_COLUMN(result, 0, {}));

	// prepared statement in correlated scalar subquery
	REQUIRE_NO_FAIL(con.Query("PREPARE v3 AS SELECT (SELECT $1::INT+sq1.i) FROM (SELECT 42 AS i) sq1;"));

	result = con.Query("EXECUTE v3(42)");
	REQUIRE(CHECK_COLUMN(result, 0, {84}));

	// prepared statement in nested correlated scalar subquery
	REQUIRE_NO_FAIL(
	    con.Query("PREPARE v4 AS SELECT (SELECT (SELECT $1::INT+sq1.i)+$2::INT+sq1.i) FROM (SELECT 42 AS i) sq1;"));

	result = con.Query("EXECUTE v4(20, 20)");
	REQUIRE(CHECK_COLUMN(result, 0, {124}));
}

TEST_CASE("PREPARE for SELECT clause", "[prepared]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE a (i TINYINT)"));
	REQUIRE_NO_FAIL(con.Query("INSERT INTO a VALUES (42)"));
	REQUIRE_NO_FAIL(con.Query("PREPARE s3 AS SELECT * FROM a WHERE i=$1"));

	REQUIRE_FAIL(con.Query("EXECUTE s3(10000)"));

	result = con.Query("EXECUTE s3(42)");
	REQUIRE(result->success);
	REQUIRE(CHECK_COLUMN(result, 0, {42}));

	result = con.Query("EXECUTE s3(84)");
	REQUIRE(result->success);
	REQUIRE(CHECK_COLUMN(result, 0, {}));

	REQUIRE_NO_FAIL(con.Query("DEALLOCATE s3"));

	// can't run a query with a param without PREPARE
	REQUIRE_FAIL(con.Query("SELECT * FROM a WHERE i=$1"));
	// also can't run a query with a param when casting
	REQUIRE_FAIL(con.Query("SELECT * FROM a WHERE i=CAST($1 AS VARCHAR)"));
}

TEST_CASE("PREPARE for INSERT", "[prepared]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE b (i TINYINT)"));
	REQUIRE_NO_FAIL(con.Query("PREPARE s1 AS INSERT INTO b VALUES (cast($1 as tinyint)), ($2 + 1), ($3)"));
	REQUIRE_NO_FAIL(con.Query("EXECUTE s1 (42, 41, 42)"));

	result = con.Query("SELECT * FROM b");
	REQUIRE(CHECK_COLUMN(result, 0, {42, 42, 42}));
	REQUIRE_FAIL(con.Query("EXECUTE s1 (42, 41, 10000)"));

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE c (i INTEGER)"));
	REQUIRE_NO_FAIL(con.Query("PREPARE s2 AS INSERT INTO c VALUES ($1)"));

	for (size_t i = 0; i < 1000; i++) {
		REQUIRE_NO_FAIL(con.Query("EXECUTE s2(" + to_string(i) + ")"));
	}

	result = con.Query("SELECT COUNT(*), MIN(i), MAX(i) FROM c");
	REQUIRE(CHECK_COLUMN(result, 0, {1000}));
	REQUIRE(CHECK_COLUMN(result, 1, {0}));
	REQUIRE(CHECK_COLUMN(result, 2, {999}));

	// can't drop table because we still have a prepared statement on it
	REQUIRE_FAIL(con.Query("DROP TABLE b"));
	REQUIRE_FAIL(con.Query("DROP TABLE c"));

	REQUIRE_NO_FAIL(con.Query("DEALLOCATE s2"));
	REQUIRE_NO_FAIL(con.Query("DEALLOCATE s1"));

	// now we can
	REQUIRE_NO_FAIL(con.Query("DROP TABLE b"));
	REQUIRE_NO_FAIL(con.Query("DROP TABLE c"));
}

TEST_CASE("PREPARE for DELETE/UPDATE", "[prepared]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	// DELETE
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE b (i TINYINT)"));
	REQUIRE_NO_FAIL(con.Query("INSERT INTO b VALUES (1), (2), (3), (4), (5)"));
	REQUIRE_NO_FAIL(con.Query("PREPARE s1 AS DELETE FROM b WHERE i=$1"));

	result = con.Query("SELECT * FROM b ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {1, 2, 3, 4, 5}));
	REQUIRE_NO_FAIL(con.Query("EXECUTE s1(3)"));

	result = con.Query("SELECT * FROM b ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {1, 2, 4, 5}));

	// cannot drop table now
	REQUIRE_FAIL(con.Query("DROP TABLE b"));
	// but we can with cascade
	REQUIRE_NO_FAIL(con.Query("DROP TABLE b CASCADE"));

	// UPDATE
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE b (i TINYINT)"));
	REQUIRE_NO_FAIL(con.Query("INSERT INTO b VALUES (1), (2), (3), (4), (5)"));
	REQUIRE_NO_FAIL(con.Query("PREPARE s1 AS UPDATE b SET i=$1 WHERE i=$2"));

	result = con.Query("SELECT * FROM b ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {1, 2, 3, 4, 5}));
	REQUIRE_NO_FAIL(con.Query("EXECUTE s1(6, 3)"));

	result = con.Query("SELECT * FROM b ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {1, 2, 4, 5, 6}));

	// cannot drop table now
	REQUIRE_FAIL(con.Query("DROP TABLE b"));
	// but we can with cascade
	REQUIRE_NO_FAIL(con.Query("DROP TABLE b CASCADE"));
}

TEST_CASE("PREPARE for UPDATE", "[prepared]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE b (i TINYINT)"));
	REQUIRE_NO_FAIL(con.Query("INSERT INTO b VALUES (1), (2), (3), (4), (5)"));
	REQUIRE_NO_FAIL(con.Query("PREPARE s1 AS DELETE FROM b WHERE i=$1"));

	result = con.Query("SELECT * FROM b ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {1, 2, 3, 4, 5}));
	REQUIRE_NO_FAIL(con.Query("EXECUTE s1(3)"));

	result = con.Query("SELECT * FROM b ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {1, 2, 4, 5}));

	// cannot drop table now
	REQUIRE_FAIL(con.Query("DROP TABLE b"));
	// but we can with cascade
	REQUIRE_NO_FAIL(con.Query("DROP TABLE b CASCADE"));
}

TEST_CASE("PREPARE many types for INSERT", "[prepared]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	// prepare different types in insert
	REQUIRE_NO_FAIL(con.Query(
	    "CREATE TABLE test(a TINYINT, b SMALLINT, c INTEGER, d BIGINT, e REAL, f DOUBLE, g DATE, h VARCHAR)"));
	REQUIRE_NO_FAIL(con.Query("PREPARE s1 AS INSERT INTO test VALUES ($1,$2,$3,$4,$5,$6,$7,$8);"));
	REQUIRE_NO_FAIL(con.Query("EXECUTE s1(1,2,3,4,1.5,2.5,'1992-10-20', 'hello world');"));
	result = con.Query("SELECT * FROM test");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));
	REQUIRE(CHECK_COLUMN(result, 1, {2}));
	REQUIRE(CHECK_COLUMN(result, 2, {3}));
	REQUIRE(CHECK_COLUMN(result, 3, {4}));
	REQUIRE(CHECK_COLUMN(result, 4, {(float)1.5}));
	REQUIRE(CHECK_COLUMN(result, 5, {2.5}));
	REQUIRE(CHECK_COLUMN(result, 6, {Value::DATE(1992, 10, 20)}));
	REQUIRE(CHECK_COLUMN(result, 7, {"hello world"}));
}

TEST_CASE("PREPARE and DROPping tables", "[prepared]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con1(db);
	Connection con2(db);

	REQUIRE_NO_FAIL(con1.Query("CREATE TABLE a (i TINYINT)"));
	REQUIRE_NO_FAIL(con2.Query("PREPARE p1 AS SELECT * FROM a"));

	REQUIRE_NO_FAIL(con2.Query("EXECUTE p1"));

	// only the conn which did the prepare can execute
	REQUIRE_FAIL(con1.Query("EXECUTE p1"));

	// but someone else cannot drop the table
	REQUIRE_FAIL(con1.Query("DROP TABLE a"));

	// but when we take the statement away
	REQUIRE_NO_FAIL(con2.Query("DEALLOCATE p1"));

	// we can drop
	REQUIRE_NO_FAIL(con1.Query("DROP TABLE a"));
}

TEST_CASE("PREPARE and WAL", "[prepared][.]") {
	unique_ptr<QueryResult> result;
	auto prepare_database = TestCreatePath("prepare_test");

	// make sure the database does not exist
	DeleteDatabase(prepare_database);
	{
		// create a database and insert values
		DuckDB db(prepare_database);
		Connection con(db);
		REQUIRE_NO_FAIL(con.Query("CREATE TABLE t (a INTEGER)"));
		REQUIRE_NO_FAIL(con.Query("PREPARE p1 AS INSERT INTO t VALUES ($1)"));
		REQUIRE_NO_FAIL(con.Query("EXECUTE p1(42)"));
		REQUIRE_NO_FAIL(con.Query("EXECUTE p1(43)"));
		REQUIRE_NO_FAIL(con.Query("DEALLOCATE p1"));
		result = con.Query("SELECT a FROM t");
		REQUIRE(CHECK_COLUMN(result, 0, {42, 43}));
	}
	{
		DuckDB db(prepare_database);
		Connection con(db);

		result = con.Query("SELECT a FROM t");
		REQUIRE(CHECK_COLUMN(result, 0, {42, 43}));

		// unhelpfully use the same statement name again, it should be available, but do nothing with it
		REQUIRE_NO_FAIL(con.Query("PREPARE p1 AS DELETE FROM t WHERE a=$1"));
	}
	// reload the database from disk
	{
		DuckDB db(prepare_database);
		Connection con(db);
		REQUIRE_NO_FAIL(con.Query("PREPARE p1 AS DELETE FROM t WHERE a=$1"));
		REQUIRE_NO_FAIL(con.Query("EXECUTE p1(43)"));

		result = con.Query("SELECT a FROM t");
		REQUIRE(CHECK_COLUMN(result, 0, {42}));
	}
	// reload again

	{
		DuckDB db(prepare_database);
		Connection con(db);

		result = con.Query("SELECT a FROM t");
		REQUIRE(CHECK_COLUMN(result, 0, {42}));
	}

	{
		DuckDB db(prepare_database);
		Connection con(db);

		result = con.Query("SELECT a FROM t");
		REQUIRE(CHECK_COLUMN(result, 0, {42}));

		REQUIRE_NO_FAIL(con.Query("PREPARE p1 AS UPDATE t SET a = $1"));
		REQUIRE_NO_FAIL(con.Query("EXECUTE p1(43)"));

		result = con.Query("SELECT a FROM t");
		REQUIRE(CHECK_COLUMN(result, 0, {43}));
	}
	{
		DuckDB db(prepare_database);
		Connection con(db);

		result = con.Query("SELECT a FROM t");
		REQUIRE(CHECK_COLUMN(result, 0, {43}));
	}
	{
		DuckDB db(prepare_database);
		Connection con(db);

		result = con.Query("SELECT a FROM t");
		REQUIRE(CHECK_COLUMN(result, 0, {43}));
	}
	DeleteDatabase(prepare_database);
}

TEST_CASE("PREPARE with NULL", "[prepared]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE b (i TINYINT)"));
	REQUIRE_NO_FAIL(con.Query("PREPARE s1 AS INSERT INTO b VALUES ($1)"));
	REQUIRE_NO_FAIL(con.Query("EXECUTE s1 (NULL)"));

	result = con.Query("SELECT i FROM b");
	REQUIRE(CHECK_COLUMN(result, 0, {Value()}));

	REQUIRE_NO_FAIL(con.Query("PREPARE s2 AS UPDATE b SET i=$1"));
	REQUIRE_NO_FAIL(con.Query("EXECUTE s2 (NULL)"));

	result = con.Query("SELECT i FROM b");
	REQUIRE(CHECK_COLUMN(result, 0, {Value()}));

	REQUIRE_NO_FAIL(con.Query("PREPARE s3 AS DELETE FROM b WHERE i=$1"));
	REQUIRE_NO_FAIL(con.Query("EXECUTE s3 (NULL)"));

	result = con.Query("SELECT i FROM b");
	REQUIRE(CHECK_COLUMN(result, 0, {Value()}));
}
