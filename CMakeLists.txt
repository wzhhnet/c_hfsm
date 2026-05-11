cmake_minimum_required(VERSION 3.10)

project(hfsm
  LANGUAGES C CXX
  VERSION "1.0.0"
)

option(TEST "building test code" OFF)

set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")

set(SRC_PATH ${PROJECT_SOURCE_DIR})

include_directories(
	${SRC_PATH}/inc
	${SRC_PATH}/src/inc
)

aux_source_directory(./src SRC_LIBS)

if (TEST)
include_directories(src)
aux_source_directory(./test TEST_SRCS)
#file(GLOB SRC_TEST test.c)
endif ()

set(STATIC_LIB_NAME ${PROJECT_NAME}-static)
set(SHARED_LIB_NAME ${PROJECT_NAME}-shared)

if (TEST)
set(TEST_EXEC_NAME ${PROJECT_NAME}-unit)
endif ()

add_library(${STATIC_LIB_NAME} STATIC ${SRC_LIBS})
add_library(${SHARED_LIB_NAME} SHARED ${SRC_LIBS})
target_link_libraries(${STATIC_LIB_NAME} INTERFACE pthread)
target_link_libraries(${SHARED_LIB_NAME} LINK_PUBLIC -lpthread)

set_target_properties(${STATIC_LIB_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
set_target_properties(${SHARED_LIB_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})

if (TEST)
add_executable(${TEST_EXEC_NAME} ${TEST_SRCS} ${GTEST_SRCS})
target_link_libraries(${TEST_EXEC_NAME} LINK_PUBLIC ${STATIC_LIB_NAME} gtest)
endif ()
