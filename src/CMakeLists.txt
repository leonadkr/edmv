cmake_minimum_required( VERSION 4.1 )

project( ${PROGRAM_NAME} LANGUAGES C )

include( GNUInstallDirs )

find_package( PkgConfig REQUIRED )
pkg_check_modules( GLIB2 REQUIRED glib-2.0 )
pkg_check_modules( GIO2 REQUIRED gio-2.0 )

add_executable( ${CMAKE_PROJECT_NAME} )
target_compile_features( ${PROJECT_NAME} PRIVATE c_std_17 )

target_sources( ${PROJECT_NAME}
	PRIVATE
		edmvapplication.c
		main.c

	PRIVATE
		FILE_SET privateHeaders
		TYPE HEADERS
		FILES
			edmvapplication.h
)

target_include_directories( ${PROJECT_NAME}
	PRIVATE
		${GLIB2_INCLUDE_DIRS}
		${GIO2_INCLUDE_DIRS}
)

target_link_directories( ${PROJECT_NAME}
	PRIVATE
		${GLIB2_LIBRARY_DIRS}
		${GIO2_LIBRARY_DIRS}
)

target_link_libraries( ${PROJECT_NAME}
	PRIVATE
		${GLIB2_LIBRARIES}
		${GIO2_LIBRARIES}
)

install( TARGETS ${PROJECT_NAME}
	RUNTIME
)
