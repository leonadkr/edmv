cmake_minimum_required( VERSION 3.16 )

project( edmv VERSION 1.1.5 )

set( PROGRAM_NAME ${CMAKE_PROJECT_NAME} )
set( PROGRAM_VERSION ${CMAKE_PROJECT_VERSION} )
set( PROGRAM_APP_ID "com.github.leonadkr.edmv" )
set( PROGRAM_AUTHOR "leonadkr@gmail.com" )

set( PROGRAM_CONFIGURE_FILE "config" )

if( CMAKE_HOST_WIN32 )
	set( PROGRAM_LINE_BREAKER "\\r\\n" )
else()
	set( PROGRAM_LINE_BREAKER "\\n" )
endif()

configure_file( config.h.in config.h )
include_directories( ${CMAKE_CURRENT_BINARY_DIR} )

add_subdirectory( src )
