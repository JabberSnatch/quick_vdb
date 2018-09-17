cmake_minimum_required(VERSION 3.0)

set( SOURCES
	 main.cc
	 )

project(quick_vdb)

add_compile_options(-std:c++latest)
add_compile_options(-W4 -WX)
add_executable(quick_vdb ${SOURCES})
