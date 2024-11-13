# SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.13)

project(ASYNC)

option(USE_MPI "Compile with MPI" ON)

add_library(ASYNC INTERFACE)

# MPI
if(USE_MPI)
  # Find MPI
  find_package(MPI REQUIRED)

  # Do not include OpenMPI C++ Bindings
  target_compile_definitions(ASYNC INTERFACE -DOMPI_SKIP_MPICXX)

  # Use MPI
  target_compile_definitions(ASYNC INTERFACE -DUSE_MPI)

  target_link_libraries(ASYNC INTERFACE MPI::MPI_C)
endif()

# Threads
find_package(Threads REQUIRED)
target_link_libraries(ASYNC INTERFACE Threads::Threads)

# Make sure we find the header files
target_include_directories(ASYNC INTERFACE ${CMAKE_SOURCE_DIR})

# Submodules
target_include_directories(ASYNC INTERFACE ${CMAKE_SOURCE_DIR}/submodules )

option(TESTING "Build unit tests" OFF)

if (TESTING)
  # Enable testing
  enable_testing()

  add_subdirectory( tests )
endif()
