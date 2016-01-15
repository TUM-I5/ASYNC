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

# Add cxxtest pathes
set( CXXTEST_PATH ${CMAKE_SOURCE_DIR}/submodules/cxxtest )
set( CMAKE_INCLUDE_PATH
    ${CMAKE_SOURCE_DIR}/submodules/cxxtest
    ${CMAKE_INCLUDE_PATH} )
set( CMAKE_MODULE_PATH
	${CMAKE_SOURCE_DIR}/submodules/cxxtest/build_tools/cmake
	${CMAKE_MODULE_PATH} )

# Get cxxtest
find_package( CxxTest REQUIRED )
include_directories( ${CXXTEST_INCLUDES} )

# Add test functions
function( add_cxx_test target source )
    cxx_test( ${target} ${source} )
    target_link_libraries( ${target} ${CMAKE_THREAD_LIBS_INIT} )
endfunction( add_cxx_test )

function( add_mpi_cxx_test target source ranks )
    get_filename_component(CPP_FILE_NAME ${source} NAME)
    string(REGEX REPLACE "h$|hpp$" "cpp" CPP_FILE_NAME ${CPP_FILE_NAME})
    set(CPP_FULL_NAME "${CMAKE_CURRENT_BINARY_DIR}/${CPP_FILE_NAME}")
    add_custom_command(
        OUTPUT "${CPP_FULL_NAME}"
        COMMAND ${CXXTESTGEN} --runner=ErrorPrinter --output "${CPP_FULL_NAME}"
         --template ${CMAKE_CURRENT_SOURCE_DIR}/mpirunner.tpl "${source}"
        DEPENDS "${source}"
    )
    add_executable(${target} ${CPP_FULL_NAME})
    set_target_properties(${target} PROPERTIES COMPILE_FLAGS "-Wno-effc++")
    target_link_libraries( ${target} ${CMAKE_THREAD_LIBS_INIT} )
    add_test( NAME ${target} COMMAND ${MPIEXEC} ${MPIEXEC_PREFLAGS}
        ${MPIEXEC_NUMPROC_FLAG} ${ranks} ${CMAKE_CURRENT_BINARY_DIR}/${target} )
endfunction( add_mpi_cxx_test )

# Add tests
add_cxx_test( TestAsyncSync ${CMAKE_CURRENT_SOURCE_DIR}/AsyncSync.t.h )
add_cxx_test( TestAsyncThread ${CMAKE_CURRENT_SOURCE_DIR}/AsyncThread.t.h )
if( USE_MPI )
    add_mpi_cxx_test( TestAsyncMPIScheduler ${CMAKE_CURRENT_SOURCE_DIR}/AsyncMPIScheduler.t.h 5 )
    add_mpi_cxx_test( TestAsyncMPI ${CMAKE_CURRENT_SOURCE_DIR}/AsyncMPI.t.h 5 )
endif( USE_MPI )