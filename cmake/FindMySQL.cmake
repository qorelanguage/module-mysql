# This file is put in the public domain by
# it's author Niclas Rosenvik

include(FindPackageHandleStandardArgs)

set(MySQL_PATH_SUFFIXES mysql mariadb mariadb/mysql percona/mysql)

find_path(MySQL_INCLUDE_DIR mysql.h 
          PATH_SUFFIXES ${MySQL_PATH_SUFFIXES})

find_library(MySQL_LIBS 
             NAMES mysqlclient mariadbclient
             PATH_SUFFIXES ${MySQL_PATH_SUFFIXES})

find_library(MySQL_LIBS_R 
             NAMES mysqlclient_r mariadbclient_r
             PATH_SUFFIXES ${MySQL_PATH_SUFFIXES})

find_library(MySQL_LIBS_D
             NAMES mysqld mariadbd
             PATH_SUFFIXES ${MySQL_PATH_SUFFIXES})


find_package_handle_standard_args(MySQL
  FOUND_VAR MySQL_FOUND
  REQUIRED_VARS MySQL_INCLUDE_DIR MySQL_LIBS MySQL_LIBS_R)

