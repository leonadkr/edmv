cmake_minimum_required( VERSION 3.16 )

project( edname VERSION 1.0.0 LANGUAGES C )

find_package( PkgConfig REQUIRED )
pkg_check_modules( GLIB2 REQUIRED glib-2.0 )

set( PROGRAM_NAME ${CMAKE_PROJECT_NAME} )
configure_file( config.h.in config.h )
include_directories( ${CMAKE_CURRENT_BINARY_DIR} )

add_compile_options( "-Wall" "-pedantic" )

add_executable( edname
	main.c )

install( TARGETS edname
	RUNTIME )

target_include_directories( edname
	PRIVATE
	${GLIB2_INCLUDE_DIRS} )

target_link_directories( edname
	PRIVATE
	${GLIB2_LIBRARY_DIRS} )

target_link_libraries( edname
	PRIVATE
	${GLIB2_LIBRARIES} )
