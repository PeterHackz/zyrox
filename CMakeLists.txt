cmake_minimum_required(VERSION 3.10)
project(zyrox DESCRIPTION "Zyrox llvm pass")

set(CMAKE_CXX_STANDARD 20)

find_package(LLVM REQUIRED CONFIG)

find_package(LLVM 18.1.3 CONFIG REQUIRED)

include_directories(
        ${LLVM_INCLUDE_DIRS}
        include
)

add_library(zyrox SHARED
        src/ZyroxPlugin.cpp

        src/core/ZyroxCore.cpp
        src/core/ZyroxPassOptions.cpp
        src/core/ZyroxMetaData.cpp

        src/quickjs/QuickRt.cpp
        src/quickjs/QuickConfig.cpp

        src/util/BasicBlockUtils.cpp
        src/util/FunctionUtils.cpp
        src/util/OpaqueTransformer.cpp
        src/util/ModuleUtils.cpp
        src/util/HashUtils.cpp
        src/util/CryptoUtils.cpp
        src/util/Random.cpp

        src/passes/BasicBlockSplitter.cpp
        src/passes/MBASub.cpp
        src/passes/IndirectBranch.cpp
        src/passes/ControlFlowFlattening.cpp
        src/passes/SimpleIndirectBranch.cpp
        src/passes/StringEncryption.cpp
)

set(ZYROX_ROOT ${CMAKE_CURRENT_LIST_DIR})

set(QUICKJS_SRC_DIR ${ZYROX_ROOT}/deps/quickjs)
set(QUICKJS_CMAKE_SRC ${ZYROX_ROOT}/cmake/quickjs.cmake)
set(QUICKJS_CMAKE_DST ${QUICKJS_SRC_DIR}/CMakeLists.txt)

if(NOT EXISTS ${QUICKJS_CMAKE_DST})
    file(COPY ${QUICKJS_CMAKE_SRC}
         DESTINATION ${QUICKJS_SRC_DIR})
    file(RENAME
         ${QUICKJS_SRC_DIR}/quickjs.cmake
         ${QUICKJS_CMAKE_DST})
endif()

add_subdirectory(deps/quickjs)

target_link_libraries(zyrox PRIVATE quickjs)
