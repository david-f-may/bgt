# CMakeLists.txt - top level CMake file for the DCDB project.

project ( BGT C )

set ( BGT_VER 0.6 )
MESSAGE (STATUS "Setting BGT_VER to ${BGT_VER} ...")

cmake_minimum_required(VERSION 2.6)

if  ( NOT $ENV{CFLAGS} MATCHES ".+" )
  MESSAGE (STATUS "You do not have the CFLAGS environment variable set.  Using defaults.")
  set (CFLAGS "-s -Wall -W -O2")
endif  ( NOT $ENV{CFLAGS} MATCHES ".+" )

if ( $ENV{CFLAGS} MATCHES ".+" )
  MESSAGE (STATUS "Adding \"$ENV{CFLAGS}\" to compile flags ...")
  set ( CMAKE_REQUIRED_FLAGS $ENV{CFLAGS} )
  add_definitions ( $ENV{CFLAGS} )
endif ( $ENV{CFLAGS} MATCHES ".+" )

MESSAGE (STATUS "Removing \"-g\" just in case ...")
remove_definitions ( -g )

include_directories ( . )

include( CheckFunctionExists )
include( CheckLibraryExists )
include( CheckIncludeFile )
include( CheckIncludeFiles )

MESSAGE (STATUS "Checking for gcc ...")
include ( CMakeDetermineCCompiler )
include( CheckCSourceCompiles )
# Compiler - set the default to gcc
#set (CMAKE_GENERATOR_CC gcc)


# Set system specific variables.
MESSAGE (STATUS "Determining system ...")
include ( CMakeDetermineSystem )
MESSAGE (STATUS "   System type = ${CMAKE_SYSTEM_NAME} ...")

if (CMAKE_SYSTEM_NAME MATCHES SunOS)
  MESSAGE (STATUS "   Adding \"-D__solaris__\" to the compile flags ...")
  add_definitions ( -D__solaris__ )
endif (CMAKE_SYSTEM_NAME MATCHES SunOS)

if (CMAKE_SYSTEM_NAME MATCHES CYGWIN)
  MESSAGE (STATUS "   Adding \"-D__cygwin__\" to the compile flags ...")
  add_definitions ( -D__cygwin__ )
endif (CMAKE_SYSTEM_NAME MATCHES CYGWIN)

# Check for threads
MESSAGE ( STATUS "Checking for threads library ...")
include ( FindThreads )
if ( CMAKE_THREAD_LIBS_INIT MATCHES ".+" )
  MESSAGE ( STATUS "   ${CMAKE_THREAD_LIBS_INIT}" )
else ( CMAKE_THREAD_LIBS_INIT MATCHES ".+" )
  MESSAGE ( FATAL_ERROR "   No threads library found." )
endif ( CMAKE_THREAD_LIBS_INIT MATCHES ".+" )

# Check for readline
MESSAGE ( STATUS "Checking for readline library ...")
set (Readline_FIND_REQUIRED TRUE)
FIND_PATH(READLINE_INCLUDE_DIR readline/readline.h)
FIND_LIBRARY(READLINE_LIBRARY NAMES readline) 
IF (READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
   SET(READLINE_FOUND TRUE)
ENDIF (READLINE_INCLUDE_DIR AND READLINE_LIBRARY)

IF (READLINE_FOUND)
   IF (NOT Readline_FIND_QUIETLY)
      MESSAGE(STATUS "Found GNU readline: ${READLINE_LIBRARY}")
   ENDIF (NOT Readline_FIND_QUIETLY)
ELSE (READLINE_FOUND)
   IF (Readline_FIND_REQUIRED)
      MESSAGE(FATAL_ERROR "Could not find GNU readline")
   ENDIF (Readline_FIND_REQUIRED)
ENDIF (READLINE_FOUND)

MACRO(CHECK_INCLUDE_FILE_CONCAT FILE VARIABLE)
  MESSAGE ( STATUS "Looking for include file ${FILE} ..." )
  CHECK_INCLUDE_FILES("${BGT_INCLUDES};${FILE}" ${VARIABLE})
  IF(${VARIABLE})
    SET(DCDB_INCLUDES ${BGT_INCLUDES} ${FILE})
  ENDIF(${VARIABLE})
ENDMACRO(CHECK_INCLUDE_FILE_CONCAT)

include ( CheckSymbolExists )

# Check some needed includes.
check_include_file_concat ( "stdio.h"  HAVE_STDIO_H )
check_include_file_concat ( "stddef.h"  HAVE_STDDEF_H )
check_include_file_concat ( "string.h"  HAVE_STRING_H )
check_include_file_concat ( "strings.h"  HAVE_STRING_H )
check_include_file_concat ( "sys/stat.h"  HAVE_SYS_STAT_H )
check_include_file_concat ( "sys/types.h"  HAVE_SYS_TYPES_H )
check_include_file_concat ( "sys/time.h"  HAVE_SYS_TIME_H )
check_include_file_concat ( "time.h"  HAVE_TIME_H )
check_include_file_concat ( "stdlib.h"  HAVE_STDLIB_H )
check_include_file_concat ( "alloca.h"  HAVE_ALLOCA_H )
check_include_file_concat ( "dir.h"  HAVE_DIR_H )
check_include_file_concat ( "fcntl.h"  HAVE_FCNTL_H )
check_include_file_concat ( "inttypes.h"  HAVE_INTTYPES_H )
check_include_file_concat ( "io.h"  HAVE_IO_H )
check_include_file_concat ( "libintl.h"  HAVE_LIBINTL_H )
check_include_file_concat ( "locale.h"  HAVE_LOCALE_H )
check_include_file_concat ( "malloc.h"  HAVE_MALLOC_H )
check_include_file_concat ( "memory.h"  HAVE_MEMORY_H )
check_include_file_concat ( "stdint.h"  HAVE_STDINT_H )
check_include_file_concat ( "unistd.h"  HAVE_UNISTD_H )
check_include_file_concat ( "wchar.h"  HAVE_WCHAR_H )
check_include_file_concat ( "pthread.h"  HAVE_PTHREAD_H )
check_include_file_concat ( "getopt.h"  HAVE_GETOPT_H )
check_include_file_concat ( "ctype.h"  HAVE_CTYPE_H )
check_include_file_concat ( "readline/readline.h"  HAVE_READLINE_H )

# Check for snprintf
CHECK_FUNCTION_EXISTS(snprintf HAVE_SNPRINTF)
IF(NOT HAVE_SNPRINTF)
    CHECK_FUNCTION_EXISTS(_snprintf HAVE__SNPRINTF)
    IF(NOT HAVE__SNPRINTF)
        MESSAGE(FATAL_ERROR "No snprintf function found, please report!")
    ENDIF()

    ADD_DEFINITIONS(-Dsnprintf=_snprintf)
ENDIF()

# Check for strncpy
CHECK_FUNCTION_EXISTS(strncpy HAVE_STRNCPY)
IF(NOT HAVE_STRNCPY)
    MESSAGE(FATAL_ERROR "No strncpy function found, please report!")
ENDIF()

# Assume stdc_headers.
set ( STDC_HEADERS 1 )

add_definitions ( -DHAVE_CONFIG_H )
configure_file ( ${BGT_SOURCE_DIR}/config.h.in
  ${BGT_SOURCE_DIR}/config.h )

MESSAGE ( STATUS "BGT_SOURCE_DIR = ${BGT_SOURCE_DIR}" )

SET (SOURCES
  number.c
  sqlite3.c
)

SET (OBJECTS
  number.o
  sqlite3.o
)

SET (HEADERS
  number.h
  sqlite3.h
  config.h
)

add_executable ( bgt bgt.c ${SOURCES} )
target_link_libraries ( bgt ${READLINE_LIBRARY} ${CMAKE_THREAD_LIBS_INIT} )

add_custom_target ( all
  DEPENDS bgt bgt.1 )

SET (POD2MAN_FOUND FALSE)
FIND_PROGRAM (POD2MAN pod2man)
if (NOT POD2MAN)
  MESSAGE (STATUS "pod2man not found... Not building man page")
else (NOT POD2MAN)
  SET (POD2MAN_FOUND TRUE)
  MESSAGE (STATUS "Using pod2man to create documentation.")
endif (NOT POD2MAN)

if (POD2MAN_FOUND)
  add_custom_command (OUTPUT bgt.1
    COMMAND ${POD2MAN}
    ARGS bgt.pod > bgt.1
    MAIN_DEPENDENCY bgt.pod
    VERBATIM
  )
endif (POD2MAN_FOUND)

#add_definitions ( -DMAIN )
#add_executable ( qif_parser qif_parser.c )
