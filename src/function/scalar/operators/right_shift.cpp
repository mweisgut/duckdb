#include "function/scalar/operators.hpp"
#include "common/vector_operations/vector_operations.hpp"

using namespace duckdb;
using namespace std;

static void bitwise_rshift_function(ExpressionExecutor &exec, Vector inputs[], index_t input_count, BoundFunctionExpression &expr,
                  Vector &result) {
	result.Initialize(inputs[0].type);
	VectorOperations::BitwiseShiftRight(inputs[0], inputs[1], result);
}

void RightShift::RegisterFunction(BuiltinFunctions &set) {
	ScalarFunctionSet functions(">>");
	for(auto &type : SQLType::INTEGRAL) {
		functions.AddFunction(ScalarFunction({ type, type }, type, bitwise_rshift_function));
	}
	set.AddFunction(functions);
}
