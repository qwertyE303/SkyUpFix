cmake_minimum_required(VERSION 3.20)

project(SkyUpFix VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build against BML+ 0.3.4, the oldest SDK available here.  BML+ keeps
# loading mods built for older versions, so one .bmodp covers 0.3.4+.
set(BML_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../deps/BMLPlus-SDK-v0.3.4-Release"
        CACHE PATH "BML+ 0.3.4 SDK root")
set(VT_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../deps/Virtools-SDK-2.1"
        CACHE PATH "Virtools 2.1 SDK root")

if (NOT EXISTS "${BML_SDK_DIR}/include/BML/IMod.h")
    message(FATAL_ERROR "BML SDK not found at ${BML_SDK_DIR}")
endif ()
if (NOT EXISTS "${VT_SDK_DIR}/Include/CKAll.h")
    message(FATAL_ERROR "Virtools SDK not found at ${VT_SDK_DIR}")
endif ()

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
            "Ballance and BML+ are 32-bit. Configure with -A Win32 (Visual Studio generator).")
endif ()

add_library(SkyUpFix SHARED
        src/Mod.cpp
)

target_include_directories(SkyUpFix PRIVATE
        "${BML_SDK_DIR}/include"
        "${VT_SDK_DIR}/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(SkyUpFix PRIVATE
        "${BML_SDK_DIR}/lib/BMLPlus.lib"
        "${VT_SDK_DIR}/Lib/CK2.lib"
        "${VT_SDK_DIR}/Lib/VxMath.lib"
)

target_compile_definitions(SkyUpFix PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        SKYUPFIX_VERSION="${PROJECT_VERSION}"
)

target_compile_options(SkyUpFix PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
        $<$<CXX_COMPILER_ID:MSVC>:/W3>
        $<$<CXX_COMPILER_ID:MSVC>:/Zc:strictStrings->
        $<$<CXX_COMPILER_ID:MSVC>:/Zc:char8_t->
)

set_target_properties(SkyUpFix PROPERTIES
        OUTPUT_NAME "SkyUpFix"
        PREFIX ""
        SUFFIX ".bmodp"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
