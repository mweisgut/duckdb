include_directories(../../third_party/libpg_query/include)
include_directories(../../third_party/libpg_query)

add_subdirectory(constraints)
add_subdirectory(expression)
add_subdirectory(query_node)
add_subdirectory(statement)
add_subdirectory(tableref)
add_subdirectory(transform)

add_library(duckdb_parser OBJECT
            base_expression.cpp
            constraint.cpp
            parsed_expression.cpp
            parsed_expression_iterator.cpp
            parser.cpp
            query_node.cpp
            tableref.cpp
            transformer.cpp)
set(ALL_OBJECT_FILES
    ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:duckdb_parser>
    PARENT_SCOPE)
