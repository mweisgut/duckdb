#include "catch.hpp"
#include "test_helpers.hpp"

using namespace duckdb;
using namespace std;

TEST_CASE("Test basic comparison statements", "[comparison]") {
	unique_ptr<QueryResult> result;
	DuckDB db(nullptr);
	Connection con(db);
	con.EnableQueryVerification();

	// = and == are the same
	result = con.Query("SELECT 1 == 1, 1 = 1, 1 == 0, 1 = 0, 1 == NULL");
	REQUIRE(CHECK_COLUMN(result, 0, {true}));
	REQUIRE(CHECK_COLUMN(result, 1, {true}));
	REQUIRE(CHECK_COLUMN(result, 2, {false}));
	REQUIRE(CHECK_COLUMN(result, 3, {false}));
	REQUIRE(CHECK_COLUMN(result, 4, {Value()}));

	// != and <> are the same
	result = con.Query("SELECT 1 <> 1, 1 != 1, 1 <> 0, 1 != 0, 1 <> NULL");
	REQUIRE(CHECK_COLUMN(result, 0, {false}));
	REQUIRE(CHECK_COLUMN(result, 1, {false}));
	REQUIRE(CHECK_COLUMN(result, 2, {true}));
	REQUIRE(CHECK_COLUMN(result, 3, {true}));
	REQUIRE(CHECK_COLUMN(result, 4, {Value()}));
}
