# SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
#
# SPDX-License-Identifier: BSD-3-Clause

# Copyright (c) 2016, Technische Universitaet Muenchen
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from this
#    software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

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
