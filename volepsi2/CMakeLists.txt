cmake_minimum_required(VERSION 3.20)
project(volepsi2 LANGUAGES CXX)

option(VOLEPSI2_USE_VENDORED_LIBOTE "Force the vendored thirdparty/libOTe fallback." OFF)
option(VOLEPSI2_USE_NATIVE_ARCH "Enable -march=native for maximum local performance at the cost of portability." OFF)
option(VOLEPSI2_ENABLE_X86_CRYPTO_FLAGS "Enable x86 AES/PCLMUL compile flags required by the current GF128 fast path." ON)
set(VOLEPSI2_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty" CACHE PATH
    "Path to vendored third-party dependencies used when libOTe is not installed externally.")

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(VOLEPSI2_LIBOTE_PROVIDER "")

if(TARGET oc::libOTe)
    set(VOLEPSI2_LIBOTE_PROVIDER "preexisting target")
elseif(NOT VOLEPSI2_USE_VENDORED_LIBOTE)
    find_package(
        libOTe
        QUIET
        CONFIG
        COMPONENTS
            silent_vole
    )

    if(NOT TARGET oc::libOTe AND NOT TARGET libOTe)
        # Some libOTe package variants export the target without component checks.
        find_package(libOTe QUIET CONFIG)
    endif()

    if(TARGET libOTe AND NOT TARGET oc::libOTe)
        add_library(oc::libOTe ALIAS libOTe)
    endif()

    if(TARGET oc::libOTe)
        set(VOLEPSI2_LIBOTE_PROVIDER "find_package(libOTe)")
    endif()
endif()

if(NOT TARGET oc::libOTe)
    include(VendoredLibOTe)
    volepsi2_configure_vendored_libote("${VOLEPSI2_THIRDPARTY_DIR}")
    if(TARGET oc::libOTe)
        set(VOLEPSI2_LIBOTE_PROVIDER "vendored thirdparty/libOTe")
    endif()
endif()

if(NOT TARGET oc::libOTe)
    message(FATAL_ERROR
        "volepsi2 requires libOTe for the real VOLE backend. "
        "Either make an installed libOTe package discoverable via CMAKE_PREFIX_PATH or libOTe_DIR, "
        "or provide the vendored fallback under ${VOLEPSI2_THIRDPARTY_DIR}.")
endif()

message(STATUS "volepsi2 will use libOTe from ${VOLEPSI2_LIBOTE_PROVIDER}")

find_package(Threads REQUIRED)

set(VOLEPSI2_SOURCES
    src/gf128.cpp
    src/input_dataset.cpp
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

if(VOLEPSI2_ENABLE_X86_CRYPTO_FLAGS)
    target_compile_options(volepsi2 PRIVATE -maes -mpclmul)
endif()

if(VOLEPSI2_USE_NATIVE_ARCH)
    target_compile_options(volepsi2 PRIVATE -march=native)
endif()

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
add_executable(core_test tests/core_test.cpp)
target_link_libraries(core_test PRIVATE volepsi2)
add_test(NAME core_test COMMAND core_test)

add_executable(psi_test tests/psi_test.cpp)
target_link_libraries(psi_test PRIVATE volepsi2)
add_test(NAME psi_test COMMAND psi_test)
