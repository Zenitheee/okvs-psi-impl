include_guard(GLOBAL)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

function(volepsi2_configure_vendored_libote vendor_root)
    set(vendor_root "${vendor_root}")
    set(vendor_build_root "${CMAKE_BINARY_DIR}/vendored")

    set(required_paths
        "${vendor_root}/function2/CMakeLists.txt"
        "${vendor_root}/macoro/CMakeLists.txt"
        "${vendor_root}/coproto/coproto/CMakeLists.txt"
        "${vendor_root}/libOTe/cryptoTools/cryptoTools/CMakeLists.txt"
        "${vendor_root}/libOTe/libOTe/CMakeLists.txt"
        "${vendor_root}/libdivide/libdivide.h"
    )

    foreach(required_path IN LISTS required_paths)
        if(NOT EXISTS "${required_path}")
            message(FATAL_ERROR
                "Vendored libOTe fallback is incomplete. Missing required path: ${required_path}")
        endif()
    endforeach()

    message(STATUS "libOTe not found via find_package(); using vendored fallback from ${vendor_root}")

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
        set(vendor_enable_arm_aes ON)
        set(vendor_enable_sse OFF)
        set(vendor_enable_avx OFF)
    else()
        set(vendor_enable_arm_aes OFF)
        set(vendor_enable_sse ON)
        set(vendor_enable_avx ON)
    endif()

    find_package(Threads REQUIRED)

    if(NOT TARGET function2::function2)
        add_subdirectory("${vendor_root}/function2" "${vendor_build_root}/function2" EXCLUDE_FROM_ALL)
    endif()

    if(NOT TARGET macoro)
        set(MACORO_TESTS OFF CACHE BOOL "" FORCE)
        set(MACORO_CPP_VER 20 CACHE STRING "" FORCE)
        set(MACORO_PIC OFF CACHE BOOL "" FORCE)
        set(MACORO_ASAN OFF CACHE BOOL "" FORCE)
        add_subdirectory("${vendor_root}/macoro" "${vendor_build_root}/macoro" EXCLUDE_FROM_ALL)
    endif()
    if(TARGET macoro AND NOT TARGET macoro::macoro)
        add_library(macoro::macoro ALIAS macoro)
    endif()

    if(NOT TARGET coproto)
        set(coproto_VERSION 1.0.0)
        set(coproto_VERSION_MAJOR 1)
        set(coproto_VERSION_MINOR 0)
        set(coproto_VERSION_PATCH 0)
        set(COPROTO_CPP_VER 20)
        set(COPROTO_CPP20 ON)
        set(COPROTO_ENABLE_BOOST OFF)
        set(COPROTO_ENABLE_OPENSSL OFF)
        set(COPROTO_ASAN OFF)
        add_subdirectory("${vendor_root}/coproto/coproto" "${vendor_build_root}/coproto/coproto" EXCLUDE_FROM_ALL)
    endif()
    if(TARGET coproto AND NOT TARGET coproto::coproto)
        add_library(coproto::coproto ALIAS coproto)
    endif()

    if(NOT TARGET sodium)
        find_path(VOLEPSI2_SODIUM_INCLUDE_DIR sodium.h)
        find_library(VOLEPSI2_SODIUM_LIBRARY NAMES sodium libsodium)

        if(NOT VOLEPSI2_SODIUM_INCLUDE_DIR OR NOT VOLEPSI2_SODIUM_LIBRARY)
            message(FATAL_ERROR
                "Vendored libOTe fallback requires libsodium. Install libsodium development files "
                "or make them discoverable to CMake.")
        endif()

        add_library(sodium UNKNOWN IMPORTED)
        set_target_properties(sodium PROPERTIES
            IMPORTED_LOCATION "${VOLEPSI2_SODIUM_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${VOLEPSI2_SODIUM_INCLUDE_DIR}")
        if(VOLEPSI2_SODIUM_LIBRARY MATCHES "\\.a$")
            target_compile_definitions(sodium INTERFACE SODIUM_STATIC=1)
        endif()
    endif()

    if(NOT TARGET libdivide)
        add_library(libdivide INTERFACE)
        target_include_directories(libdivide INTERFACE
            $<BUILD_INTERFACE:${vendor_root}/libdivide>
            $<INSTALL_INTERFACE:include>)
    endif()

    if(NOT TARGET cryptoTools)
        set(cryptoTools_VERSION 1.10.1)
        set(cryptoTools_VERSION_MAJOR 1)
        set(cryptoTools_VERSION_MINOR 10)
        set(cryptoTools_VERSION_PATCH 1)

        set(ENABLE_SPAN_LITE OFF)
        set(ENABLE_GMP OFF)
        set(ENABLE_RELIC OFF)
        set(ENABLE_SODIUM ON)
        set(SODIUM_MONTGOMERY OFF)
        set(ENABLE_CIRCUITS OFF)
        set(ENABLE_NET_LOG OFF)
        set(ENABLE_WOLFSSL OFF)
        set(ENABLE_BOOST OFF)
        set(ENABLE_OPENSSL OFF)
        set(ENABLE_COPROTO TRUE)
        set(ENABLE_ARM_AES ${vendor_enable_arm_aes})
        set(ENABLE_SSE ${vendor_enable_sse})
        set(ENABLE_AVX ${vendor_enable_avx})
        set(ENABLE_PIC OFF)
        set(ENABLE_ASAN OFF)
        set(ENABLE_CPP_14 OFF)
        if(ENABLE_SSE OR ENABLE_ARM_AES)
            set(ENABLE_PORTABLE_AES OFF)
        else()
            set(ENABLE_PORTABLE_AES ON)
        endif()
        set(CRYPTO_TOOLS_STD_VER 20)

        file(MAKE_DIRECTORY "${vendor_build_root}/cryptoTools/cryptoTools/Common")
        configure_file(
            "${vendor_root}/libOTe/cryptoTools/cryptoTools/Common/config.h.in"
            "${vendor_build_root}/cryptoTools/cryptoTools/Common/config.h"
            @ONLY)

        add_subdirectory(
            "${vendor_root}/libOTe/cryptoTools/cryptoTools"
            "${vendor_build_root}/cryptoTools/cryptoTools"
            EXCLUDE_FROM_ALL)
    endif()
    if(TARGET cryptoTools AND NOT TARGET oc::cryptoTools)
        add_library(oc::cryptoTools ALIAS cryptoTools)
    endif()

    if(NOT TARGET libOTe)
        set(libOTe_VERSION 2.2.0)
        set(libOTe_VERSION_MAJOR 2)
        set(libOTe_VERSION_MINOR 2)
        set(libOTe_VERSION_PATCH 0)

        set(LIBOTE_STD_VER 20)
        set(ENABLE_BITPOLYMUL OFF)
        set(ENABLE_MOCK_OT OFF)
        set(ENABLE_SIMPLESTOT OFF)
        set(ENABLE_SIMPLESTOT_ASM OFF)
        set(ENABLE_MRR ON)
        set(ENABLE_MRR_TWIST OFF)
        set(ENABLE_MR OFF)
        set(ENABLE_MR_KYBER OFF)
        set(ENABLE_KOS ON)
        set(ENABLE_IKNP ON)
        set(ENABLE_SILENTOT ON)
        set(ENABLE_SOFTSPOKEN_OT ON)
        set(ENABLE_DELTA_KOS OFF)
        set(ENABLE_OOS OFF)
        set(ENABLE_KKRT OFF)
        set(ENABLE_SILENT_VOLE ON)
        set(ENABLE_FOLEAGE OFF)
        set(ENABLE_REGULAR_DPF OFF)
        set(ENABLE_TERNARY_DPF OFF)
        set(ENABLE_SPARSE_DPF OFF)
        set(ENABLE_PPRF ON)
        set(ENABLE_INSECURE_SILVER OFF)
        set(ENABLE_LDPC OFF)
        set(NO_KOS_WARNING ON)

        file(MAKE_DIRECTORY "${vendor_build_root}/libOTe/libOTe")
        configure_file(
            "${vendor_root}/libOTe/libOTe/config.h.in"
            "${vendor_build_root}/libOTe/libOTe/config.h"
            @ONLY)

        add_subdirectory(
            "${vendor_root}/libOTe/libOTe"
            "${vendor_build_root}/libOTe/libOTe"
            EXCLUDE_FROM_ALL)
    endif()

    if(TARGET libOTe AND NOT TARGET oc::libOTe)
        add_library(oc::libOTe ALIAS libOTe)
    endif()
endfunction()
