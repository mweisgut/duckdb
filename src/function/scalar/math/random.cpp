#include "function/scalar/math_functions.hpp"
#include "common/vector_operations/vector_operations.hpp"
#include "execution/expression_executor.hpp"
#include "planner/expression/bound_function_expression.hpp"
#include <random>

using namespace duckdb;
using namespace std;

struct RandomBindData : public FunctionData {
	default_random_engine gen;
	uniform_real_distribution<double> dist;

	RandomBindData(default_random_engine gen, uniform_real_distribution<double> dist) : gen(gen), dist(dist) {
	}

	unique_ptr<FunctionData> Copy() override {
		return make_unique<RandomBindData>(gen, dist);
	}
};

static void random_function(ExpressionExecutor &exec, Vector inputs[], index_t input_count, BoundFunctionExpression &expr,
                     Vector &result) {
	auto &info = (RandomBindData &) *expr.bind_info;
	assert(input_count == 0);
	result.Initialize(TypeId::DOUBLE);

	result.count = 1;
	if (exec.chunk) {
		result.count = exec.chunk->size();
		result.sel_vector = exec.chunk->sel_vector;
	}

	double *result_data = (double *) result.data;
	VectorOperations::Exec(result, [&](index_t i, index_t k) {
		result_data[i] = info.dist(info.gen);
	});
}

unique_ptr<FunctionData> random_bind(BoundFunctionExpression &expr, ClientContext &context) {
	random_device rd;
	default_random_engine gen(rd());
	uniform_real_distribution<double> dist(0, 1);
	return make_unique<RandomBindData>(move(gen), move(dist));
}

void Random::RegisterFunction(BuiltinFunctions &set) {
	set.AddFunction(ScalarFunction("random", { }, SQLType::DOUBLE, random_function, true, random_bind));
}
