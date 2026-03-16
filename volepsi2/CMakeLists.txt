cmake_minimum_required(VERSION 3.20)
project(volepsi2 LANGUAGES CXX)

if(NOT TARGET oc::libOTe)
    list(PREPEND CMAKE_PREFIX_PATH
        ${CMAKE_CURRENT_SOURCE_DIR}/../volepsi/out/function2/out/build/linux
        ${CMAKE_CURRENT_SOURCE_DIR}/../volepsi/out/install/linux)
    set(Boost_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../volepsi/out/install/linux)
    set(BOOST_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../volepsi/out/install/linux)
    set(Boost_NO_SYSTEM_PATHS ON)
    set(NO_SYSTEM_PATH ON)
    set(SODIUM_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../volepsi/out/install/linux/include CACHE PATH "" FORCE)
    set(SODIUM_LIBRARIES ${CMAKE_CURRENT_SOURCE_DIR}/../volepsi/out/install/linux/lib/libsodium.a CACHE FILEPATH "" FORCE)
    find_package(
        libOTe
        QUIET
        CONFIG
        HINTS
            ${CMAKE_CURRENT_SOURCE_DIR}/../volepsi/out/libOTe
    )
endif()

if(NOT TARGET oc::libOTe)
    message(FATAL_ERROR
        "volepsi2 now requires libOTe for the real VOLE backend. "
        "Build through volepsi/build.py or make libOTe discoverable to CMake.")
endif()

find_package(Threads REQUIRED)

set(VOLEPSI2_SOURCES
    src/gf128.cpp
    src/row_hasher.cpp
    src/paxos.cpp
    src/encoder.cpp
    src/binned_encoder.cpp
    src/psi.cpp
)

add_library(volepsi2 STATIC ${VOLEPSI2_SOURCES})
target_include_directories(volepsi2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(volepsi2 PUBLIC oc::libOTe)
target_compile_features(volepsi2 PUBLIC cxx_std_20)
target_compile_definitions(volepsi2 PUBLIC OKVS_ENABLE_REAL_VOLE=1)
target_compile_options(volepsi2 PRIVATE -march=native -maes -mpclmul)

add_executable(volepsi2_cli src/main.cpp)
target_link_libraries(volepsi2_cli PRIVATE volepsi2)

add_executable(volepsi2_bench src/benchmark.cpp)
target_link_libraries(volepsi2_bench PRIVATE volepsi2)

add_executable(volepsi2_demo src/demo_main.cpp)
target_link_libraries(volepsi2_demo PRIVATE volepsi2 Threads::Threads)
target_compile_definitions(
    volepsi2_demo
    PRIVATE VOLEPSI2_DEMO_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/demo")

enable_testing()
add_executable(volepsi2_tests tests/okvs_roundtrip.cpp)
target_link_libraries(volepsi2_tests PRIVATE volepsi2)
add_test(NAME okvs_roundtrip COMMAND volepsi2_tests)

add_executable(gf128_square_test tests/gf128_square_test.cpp)
target_link_libraries(gf128_square_test PRIVATE volepsi2)
add_test(NAME gf128_square_test COMMAND gf128_square_test)

add_executable(binned_encoder_test tests/binned_encoder_test.cpp)
target_link_libraries(binned_encoder_test PRIVATE volepsi2)
add_test(NAME binned_encoder_test COMMAND binned_encoder_test)

add_executable(psi_test tests/psi_test.cpp)
target_link_libraries(psi_test PRIVATE volepsi2)
add_test(NAME psi_test COMMAND psi_test)
