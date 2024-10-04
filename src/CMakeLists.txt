cmake_minimum_required( VERSION 3.16 )

project( ${PROGRAM_NAME} LANGUAGES C )

find_package( PkgConfig REQUIRED )
pkg_check_modules( GLIB2 REQUIRED glib-2.0 )
pkg_check_modules( GIO2 REQUIRED gio-2.0 )

add_compile_options( "-Wall" "-pedantic" )

add_executable( ${CMAKE_PROJECT_NAME}
	edmvapplication.c
	main.c )

install( TARGETS ${CMAKE_PROJECT_NAME}
	RUNTIME )

target_include_directories( ${CMAKE_PROJECT_NAME}
	PRIVATE
	${GLIB2_INCLUDE_DIRS}
	${GIO2_INCLUDE_DIRS} )

target_link_directories( ${CMAKE_PROJECT_NAME}
	PRIVATE
	${GLIB2_LIBRARY_DIRS}
	${GIO2_LIBRARY_DIRS} )

target_link_libraries( ${CMAKE_PROJECT_NAME}
	PRIVATE
	${GLIB2_LIBRARIES}
	${GIO2_LIBRARIES} )
