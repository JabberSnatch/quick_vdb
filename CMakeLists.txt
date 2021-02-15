cmake_minimum_required(VERSION 3.0)

project(quick_vdb)

option(QVDB_BUILD_TESTS "Build unit tests executable" ON)
option(QVDB_ENABLE_CACHE "Enable VDB internal caching mechanism." ON)

if (${QVDB_BUILD_TESTS})
   add_compile_definitions(QVDB_BUILD_TESTS)
endif()

if (${QVDB_ENABLE_CACHE})
  add_compile_definitions(QVDB_ENABLE_CACHE)
endif()

add_library(qvdb INTERFACE)
target_include_directories(qvdb INTERFACE include)

if (${QVDB_BUILD_TESTS})

	if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
		add_compile_options(-std:c++latest)
		add_compile_options(-W4 -WX)

	elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR
		   "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		add_compile_options(-std=c++17)

	endif()

   add_executable(qvdb_tests main.cc)
   target_link_libraries(qvdb_tests PRIVATE qvdb)

endif()
