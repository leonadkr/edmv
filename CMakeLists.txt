cmake_minimum_required( VERSION 3.16 )

project( edmv VERSION 1.1.2 )

if( CMAKE_HOST_WIN32 )
	set( TEXT_FILE_LINE_BREAKER "\\r\\n" )
else()
	set( TEXT_FILE_LINE_BREAKER "\\n" )
endif()

set( PROGRAM_NAME ${CMAKE_PROJECT_NAME} )
set( PROGRAM_VERSION ${CMAKE_PROJECT_VERSION} )
configure_file( config.h.in config.h )
include_directories( ${CMAKE_CURRENT_BINARY_DIR} )

add_subdirectory( src )
