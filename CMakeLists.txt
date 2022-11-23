cmake_minimum_required(VERSION 3.0.0)

project(qore-mysql-module VERSION 2.1)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
include(CheckCXXSymbolExists)

find_package(Qore 0.9 REQUIRED)

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

if (DEFINED ENV{MySQL_INCLUDE_DIR})
    set(MySQL_INCLUDE_DIR $ENV{MySQL_INCLUDE_DIR})
endif()
if (DEFINED ENV{MySQL_LIBS})
    set(MySQL_LIBS $ENV{MySQL_LIBS})
endif()
if (DEFINED ENV{MySQL_LIBS_R})
    set(MySQL_LIBS_R $ENV{MySQL_LIBS_R})
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

set(QORE_DOX_TMPL_SRC
  docs/mainpage.dox.tmpl
)

add_library(${module_name} MODULE ${CPP_SRC})

if (DEFINED ENV{DOXYGEN_EXECUTABLE})
    set(DOXYGEN_EXECUTABLE $ENV{DOXYGEN_EXECUTABLE})
endif()

qore_external_binary_module(${module_name} ${PROJECT_VERSION} ${MySQL_LIBS_R} Threads::Threads)

qore_dist(${PROJECT_VERSION})

qore_config_info()

# NOTE: find_package(Doxygen) called in qore_external_binary_module()
if (DOXYGEN_FOUND)
    qore_wrap_dox(QORE_DOX_SRC ${QORE_DOX_TMPL_SRC})
    add_custom_target(QORE_MOD_DOX_FILES DEPENDS ${QORE_DOX_SRC})
    add_dependencies(docs-module QORE_MOD_DOX_FILES)
endif()
