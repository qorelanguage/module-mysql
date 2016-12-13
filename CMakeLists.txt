cmake_minimum_required(VERSION 3.0.0)

project(qore-mysql-module VERSION 2.0.2)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
include(CheckCXXSymbolExists)

find_package(Qore REQUIRED)

# Check for C++11.
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
else()
    message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
if(CMAKE_USE_PTHREADS_INIT)
message(STATUS "Found POSIX Threads: TRUE")
else(CMAKE_USE_PTHREADS_INIT)
message(STATUS "Found POSIX Threads: FALSE")
message(FATAL_ERROR "POSIX threads does not seem to be supported on this platform, aborting")
endif()

find_package(MySQL REQUIRED)

set(CMAKE_REQUIRED_INCLUDES ${MySQL_INCLUDE_DIR})
set(CMAKE_REQUIRED_LIBRARIES ${MySQL_LIBS_R})
check_cxx_symbol_exists(mysql_stmt_prepare mysql.h HAVE_MYSQL_STMT)
check_cxx_symbol_exists(mysql_commit mysql.h HAVE_MYSQL_COMMIT)
check_cxx_symbol_exists(mysql_set_character_set mysql.h HAVE_MYSQL_SET_CHARACTER_SET)
check_cxx_symbol_exists(mysql_library_init mysql.h HAVE_MYSQL_LIBRARY_INIT)
unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_REQUIRED_LIBRARIES)

set(CMAKE_REQUIRED_INCLUDES ${QORE_INCLUDE_DIR})
set(CMAKE_REQUIRED_LIBRARIES ${QORE_LIBRARY})
check_cxx_source_compiles("
#include <qore/Qore.h>
int main(void){
#if (QORE_MODULE_API_MAJOR == 0 && QORE_MODULE_API_MINOR < 7) 
#error
#endif
return 0;
}" QORE_HAS_DATASOURCE_PORT)
unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_REQUIRED_LIBRARIES)

check_cxx_compiler_flag(-fvisibility=hidden HAVE_GCC_VISIBILITY)

configure_file(${CMAKE_SOURCE_DIR}/cmake/config.h.cmake config.h)

include_directories(${MySQL_INCLUDE_DIR})

set(CPP_SRC src/mysql.cpp)

set(module_name "mysql")

add_library(${module_name} SHARED ${CPP_SRC})

qore_binary_module(${module_name} ${PROJECT_VERSION} ${MySQL_LIBS_R} Threads::Threads)

qore_dist(${PROJECT_VERSION})

qore_config_info()
