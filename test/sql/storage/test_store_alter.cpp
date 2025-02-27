#include "catch.hpp"
#include "common/file_system.hpp"
#include "test_helpers.hpp"

using namespace duckdb;
using namespace std;

TEST_CASE("Test storage of alter table", "[storage]") {
	unique_ptr<QueryResult> result;
	auto storage_database = TestCreatePath("storage_test");
	auto config = GetTestConfig();

	// make sure the database does not exist
	DeleteDatabase(storage_database);
	{
		// create a database and insert values
		DuckDB db(storage_database, config.get());
		Connection con(db);
		REQUIRE_NO_FAIL(con.Query("CREATE TABLE test (a INTEGER, b INTEGER);"));
		REQUIRE_NO_FAIL(con.Query("INSERT INTO test VALUES (11, 22), (13, 22), (12, 21)"));
		for (index_t i = 0; i < 2; i++) {
			REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
			result = con.Query("SELECT a FROM test ORDER BY a");
			REQUIRE(CHECK_COLUMN(result, 0, {11, 12, 13}));

			REQUIRE_NO_FAIL(con.Query("ALTER TABLE test RENAME COLUMN a TO k"));

			result = con.Query("SELECT k FROM test ORDER BY k");
			REQUIRE(CHECK_COLUMN(result, 0, {11, 12, 13}));
			REQUIRE_NO_FAIL(con.Query(i == 0 ? "ROLLBACK" : "COMMIT"));
		}
	}
	// reload the database from disk
	{
		DuckDB db(storage_database, config.get());
		Connection con(db);
		result = con.Query("SELECT k FROM test ORDER BY k");
		REQUIRE(CHECK_COLUMN(result, 0, {11, 12, 13}));
	}
	// reload the database from disk
	{
		DuckDB db(storage_database, config.get());
		Connection con(db);
		result = con.Query("SELECT k FROM test ORDER BY k");
		REQUIRE(CHECK_COLUMN(result, 0, {11, 12, 13}));
	}
	DeleteDatabase(storage_database);
}
