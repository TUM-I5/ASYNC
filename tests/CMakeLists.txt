# SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
#
# SPDX-License-Identifier: BSD-3-Clause

# cf. https://cmake.org/cmake/help/v3.8/module/FindCxxTest.html

# Get cxxtest
find_package( CxxTest REQUIRED )
include_directories( ${CXXTEST_INCLUDE_DIR} )

# Timout for tests
set( test_timeout 20 )

# Add test functions
function( add_cxx_test target source )
    get_filename_component(CPP_FILE_NAME ${source} NAME)
    string(REGEX REPLACE "h$|hpp$" "cpp" CPP_FILE_NAME ${CPP_FILE_NAME})
    set(CPP_FULL_NAME "${CMAKE_CURRENT_BINARY_DIR}/${CPP_FILE_NAME}")
    CXXTEST_ADD_TEST( ${target} "${CPP_FULL_NAME}" ${source} )
    target_link_libraries( ${target} PRIVATE ASYNC )
    set_tests_properties( ${target} PROPERTIES TIMEOUT ${test_timeout} ) # Detect deadlocks
endfunction( add_cxx_test )

set( cxx_template ${CMAKE_CURRENT_SOURCE_DIR}/mpirunner.tpl )
function( add_mpi_cxx_test target source ranks )
    get_filename_component(CPP_FILE_NAME ${source} NAME)
    string(REGEX REPLACE "h$|hpp$" "cpp" CPP_FILE_NAME ${CPP_FILE_NAME})
    set(CPP_FULL_NAME "${CMAKE_CURRENT_BINARY_DIR}/${CPP_FILE_NAME}")
    add_custom_command(
        OUTPUT "${CPP_FULL_NAME}"
        COMMAND ${CXXTEST_PYTHON_TESTGEN_EXECUTABLE} --error-printer --output "${CPP_FULL_NAME}"
         --template "${cxx_template}" "${source}"
        DEPENDS "${source}" "${cxx_template}"
    )
    add_executable(${target} ${CPP_FULL_NAME})
    set_target_properties(${target} PROPERTIES COMPILE_FLAGS "-Wno-effc++")
    target_link_libraries( ${target} PRIVATE ASYNC )

    # TODO: make --oversubscribe optional here
    add_test( NAME ${target} COMMAND ${MPIEXEC} ${MPIEXEC_PREFLAGS} --oversubscribe
        ${MPIEXEC_NUMPROC_FLAG} ${ranks} ${CMAKE_CURRENT_BINARY_DIR}/${target} )
    set_tests_properties( ${target} PROPERTIES TIMEOUT ${test_timeout} ) # Detect deadlocks
endfunction( add_mpi_cxx_test )

function( add_serial_cxx_test target source )
    # Makes sure that MPI is initialized even when only one rank is required
    if( USE_MPI )
        add_mpi_cxx_test( ${target} ${source} 1 )
    else( USE_MPI )
        add_cxx_test( ${target} ${source} )
    endif( USE_MPI )
endfunction( add_serial_cxx_test )

# Add sub tests
add_subdirectory( as )

# Add tests
add_serial_cxx_test( TestConfig ${CMAKE_CURRENT_SOURCE_DIR}/Config.t.h )
add_serial_cxx_test( TestDispatcher ${CMAKE_CURRENT_SOURCE_DIR}/Dispatcher.t.h )
add_serial_cxx_test( TestModuleSync ${CMAKE_CURRENT_SOURCE_DIR}/Module.t.h )
set_tests_properties( TestModuleSync PROPERTIES ENVIRONMENT "ASYNC_MODE=SYNC" )
add_serial_cxx_test( TestModuleThread ${CMAKE_CURRENT_SOURCE_DIR}/Module.t.h )
set_tests_properties( TestModuleThread PROPERTIES ENVIRONMENT "ASYNC_MODE=THREAD" )
if( USE_MPI )
    add_mpi_cxx_test( TestModuleMPI ${CMAKE_CURRENT_SOURCE_DIR}/Module.t.h 3 )
    set_tests_properties( TestModuleMPI PROPERTIES ENVIRONMENT "ASYNC_MODE=MPI" )
    add_mpi_cxx_test( TestModuleMPIAsync ${CMAKE_CURRENT_SOURCE_DIR}/Module.t.h 3 )
    set_tests_properties( TestModuleMPIAsync PROPERTIES ENVIRONMENT "ASYNC_MODE=MPI;ASYNC_MPI_COPY=1" )
endif( USE_MPI )
