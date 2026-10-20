# SPDX-FileCopyrightText: 2016 Technical University of Munich
#
# SPDX-License-Identifier: BSD-3-Clause

# Add tests
add_cxx_test_executable( TestSync ${CMAKE_CURRENT_SOURCE_DIR}/Sync.t.h )
add_serial_cxx_test_run( TestSync TestSync )
add_cxx_test_executable( TestThread ${CMAKE_CURRENT_SOURCE_DIR}/Thread.t.h )
add_serial_cxx_test_run( TestThread TestThread )
add_cxx_test_executable( TestThreadBuffer ${CMAKE_CURRENT_SOURCE_DIR}/Thread_buffer.t.h )
add_serial_cxx_test_run( TestThreadBuffer TestThreadBuffer "ASYNC_BUFFER_ALIGNMENT=65536" )
if( USE_MPI )
    add_cxx_test_executable( TestMPIScheduler ${CMAKE_CURRENT_SOURCE_DIR}/MPIScheduler.t.h )
    add_cxx_test_run( TestMPIScheduler TestMPIScheduler 5 )

    # The groups of the low-level tests are {0, 1, 2} and {3, 4}
    foreach( copy 0 1 )
        if( copy )
            set( executable TestMPIAsync )
        else()
            set( executable TestMPI )
        endif()
        add_cxx_test_executable( ${executable} ${CMAKE_CURRENT_SOURCE_DIR}/MPI.t.h )
        target_compile_definitions( ${executable} PRIVATE ASYNC_MPI_COPY=${copy} )

        add_cxx_test_run( ${executable} ${executable} 5 "ASYNC_MPI_MAX_SEND=1048576" )
    endforeach()
endif( USE_MPI )
