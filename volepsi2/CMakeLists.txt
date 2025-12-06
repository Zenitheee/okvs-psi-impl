cmake_minimum_required(VERSION 3.20)
project(volepsi2 LANGUAGES CXX)

set(VOLEPSI2_SOURCES
    src/gf128.cpp
    src/row_hasher.cpp
    src/paxos_like.cpp
    src/encoder.cpp
)

add_library(volepsi2 STATIC ${VOLEPSI2_SOURCES})
target_include_directories(volepsi2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(volepsi2 PUBLIC cxx_std_20)

add_executable(volepsi2_cli src/main.cpp)
target_link_libraries(volepsi2_cli PRIVATE volepsi2)

enable_testing()
add_executable(volepsi2_tests tests/okvs_roundtrip.cpp)
target_link_libraries(volepsi2_tests PRIVATE volepsi2)
add_test(NAME okvs_roundtrip COMMAND volepsi2_tests)


