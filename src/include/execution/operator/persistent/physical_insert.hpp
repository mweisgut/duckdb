//===----------------------------------------------------------------------===//
//                         DuckDB
//
// execution/operator/persistent/physical_insert.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "execution/physical_operator.hpp"

namespace duckdb {

//! Physically insert a set of data into a table
class PhysicalInsert : public PhysicalOperator {
public:
	PhysicalInsert(LogicalOperator &op, TableCatalogEntry *table, vector<vector<unique_ptr<Expression>>> insert_values,
	               vector<index_t> column_index_map, vector<unique_ptr<Expression>> bound_defaults)
	    : PhysicalOperator(PhysicalOperatorType::INSERT, op.types), column_index_map(column_index_map),
	      insert_values(move(insert_values)), table(table), bound_defaults(move(bound_defaults)) {
	}

	vector<index_t> column_index_map;
	vector<vector<unique_ptr<Expression>>> insert_values;
	TableCatalogEntry *table;
	vector<unique_ptr<Expression>> bound_defaults;

public:
	void GetChunkInternal(ClientContext &context, DataChunk &chunk, PhysicalOperatorState *state) override;
};

} // namespace duckdb
