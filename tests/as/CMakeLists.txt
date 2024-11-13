# SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
#
# SPDX-License-Identifier: BSD-3-Clause

# Add tests
add_cxx_test( TestSync ${CMAKE_CURRENT_SOURCE_DIR}/Sync.t.h )
add_cxx_test( TestThread ${CMAKE_CURRENT_SOURCE_DIR}/Thread.t.h )
add_cxx_test( TestThreadBuffer ${CMAKE_CURRENT_SOURCE_DIR}/Thread_buffer.t.h )
set_tests_properties( TestThreadBuffer PROPERTIES ENVIRONMENT "ASYNC_BUFFER_ALIGNMENT=65536" )
if( USE_MPI )
    add_mpi_cxx_test( TestMPIScheduler ${CMAKE_CURRENT_SOURCE_DIR}/MPIScheduler.t.h 5 )
    add_mpi_cxx_test( TestMPI ${CMAKE_CURRENT_SOURCE_DIR}/MPI.t.h 5 )
    set_tests_properties( TestMPI PROPERTIES ENVIRONMENT "ASYNC_MPI_MAX_SEND=1048576" )
    add_mpi_cxx_test( TestMPIAsync ${CMAKE_CURRENT_SOURCE_DIR}/MPI.t.h 5 )
    set_target_properties( TestMPIAsync PROPERTIES COMPILE_DEFINITIONS "ASYNC_MPI_COPY" )
    set_tests_properties( TestMPIAsync PROPERTIES ENVIRONMENT "ASYNC_MPI_MAX_SEND=1048576;" )
endif( USE_MPI )