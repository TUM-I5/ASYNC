# SPDX-FileCopyrightText: 2016 Technical University of Munich
#
# SPDX-License-Identifier: BSD-3-Clause

# cf. https://cmake.org/cmake/help/v3.8/module/FindCxxTest.html

# Get cxxtest
find_package( CxxTest REQUIRED )
include_directories( ${CXXTEST_INCLUDE_DIR} )

# Timeout for tests, to detect deadlocks
set( test_timeout 20 )

# Environment variables every test runs with
set( test_environment "" )

# Sanitizers
set( ASYNC_TEST_SANITIZERS "" CACHE STRING
    "Sanitizers to build and run the tests with, a list of address, undefined and thread" )
if( ASYNC_TEST_SANITIZERS )
    list( JOIN ASYNC_TEST_SANITIZERS "," sanitizers )
    add_compile_options( -fsanitize=${sanitizers} -fno-omit-frame-pointer )
    add_link_options( -fsanitize=${sanitizers} )

    if( "undefined" IN_LIST ASYNC_TEST_SANITIZERS )
        add_compile_options( -fno-sanitize-recover=undefined )
        list( APPEND test_environment "UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1" )
    endif()
    if( "address" IN_LIST ASYNC_TEST_SANITIZERS )
        list( APPEND test_environment "ASAN_OPTIONS=detect_leaks=1"
            "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp:print_suppressions=0" )
    endif()
    if( "thread" IN_LIST ASYNC_TEST_SANITIZERS )
        # The MPI library is not instrumented: what it does on behalf of the
        # tests is out of sight either way, and looking into it at all trips
        # over its components, which it loads and unloads at run time
        list( APPEND test_environment "TSAN_OPTIONS=halt_on_error=1:report_signal_unsafe=0:ignore_noninstrumented_modules=1:suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tsan.supp" )
    endif()

    # The shared-memory store of PMIx maps its segments at addresses the
    # sanitizers reserve for themselves
    list( APPEND test_environment "PMIX_MCA_gds=hash" )

    math( EXPR test_timeout "${test_timeout} * 6" )
endif()

set( cxx_template ${CMAKE_CURRENT_SOURCE_DIR}/mpirunner.tpl )

# Build a test executable from a cxxtest suite; with MPI, the runner
# initializes MPI around the tests
function( add_cxx_test_executable target source )
    set( CPP_FULL_NAME "${CMAKE_CURRENT_BINARY_DIR}/${target}.cpp" )
    set( template "" )
    if( USE_MPI )
        set( template --template "${cxx_template}" )
    endif()
    add_custom_command(
        OUTPUT "${CPP_FULL_NAME}"
        COMMAND ${CXXTEST_PYTHON_TESTGEN_EXECUTABLE} --error-printer --output "${CPP_FULL_NAME}"
            ${template} "${source}"
        DEPENDS "${source}" "${cxx_template}"
    )
    add_executable( ${target} ${CPP_FULL_NAME} )
    set_target_properties( ${target} PROPERTIES COMPILE_FLAGS "-Wno-effc++" )
    target_link_libraries( ${target} PRIVATE ASYNC )
endfunction( add_cxx_test_executable )

# Run a test executable as a test of its own: on the given number of MPI
# ranks (0: without MPI), with the environment variables that follow
function( add_cxx_test_run name executable ranks )
    if( ranks GREATER 0 )
        # TODO: make --oversubscribe optional here
        add_test( NAME ${name} COMMAND ${MPIEXEC} ${MPIEXEC_PREFLAGS} --oversubscribe
            ${MPIEXEC_NUMPROC_FLAG} ${ranks} $<TARGET_FILE:${executable}> )
    else()
        add_test( NAME ${name} COMMAND $<TARGET_FILE:${executable}> )
    endif()
    set( environment ${test_environment} ${ARGN} )
    set_tests_properties( ${name} PROPERTIES TIMEOUT ${test_timeout} )
    if( environment )
        set_tests_properties( ${name} PROPERTIES ENVIRONMENT "${environment}" )
    endif()
endfunction( add_cxx_test_run )

# A test that runs on a single rank (with MPI initialized, if available)
function( add_serial_cxx_test_run name executable )
    if( USE_MPI )
        add_cxx_test_run( ${name} ${executable} 1 ${ARGN} )
    else()
        add_cxx_test_run( ${name} ${executable} 0 ${ARGN} )
    endif()
endfunction( add_serial_cxx_test_run )

# Add sub tests
add_subdirectory( as )

# Add tests
add_cxx_test_executable( TestConfig ${CMAKE_CURRENT_SOURCE_DIR}/Config.t.h )
add_serial_cxx_test_run( TestConfig TestConfig )
add_cxx_test_executable( TestDispatcher ${CMAKE_CURRENT_SOURCE_DIR}/Dispatcher.t.h )
add_serial_cxx_test_run( TestDispatcher TestDispatcher )

add_cxx_test_executable( TestModule ${CMAKE_CURRENT_SOURCE_DIR}/Module.t.h )
add_serial_cxx_test_run( TestModuleSync TestModule "ASYNC_MODE=SYNC" )
add_serial_cxx_test_run( TestModuleThread TestModule "ASYNC_MODE=THREAD" )
# AUTO settles on NODE where ranks share a node and every node holds consecutive ranks, and on
# THREAD otherwise; ASYNC_TEST_AUTO_MODE is what the tests expect
add_serial_cxx_test_run( TestModuleAuto TestModule "ASYNC_MODE=AUTO" "ASYNC_TEST_AUTO_MODE=THREAD" )
if( USE_MPI )
    # executors that compute as well (the default) and exclusive ones, each with and without the
    # asynchronous copy
    add_cxx_test_run( TestModuleMPI TestModule 3 "ASYNC_MODE=MPI" "ASYNC_MPI_COPY=0" )
    add_cxx_test_run( TestModuleMPIAsync TestModule 3 "ASYNC_MODE=NODE" "ASYNC_MPI_COPY=1" )
    add_cxx_test_run( TestModuleMPIExclusive TestModule 3
        "ASYNC_MODE=NODE" "ASYNC_MPI_COPY=0" "ASYNC_EXECUTOR_EXCLUSIVE=1" )
    add_cxx_test_run( TestModuleMPIAsyncExclusive TestModule 3
        "ASYNC_MODE=NODE" "ASYNC_MPI_COPY=1" "ASYNC_EXECUTOR_EXCLUSIVE=1" )
    # a group of one rank, which is its own executor
    add_cxx_test_run( TestModuleMPISingle TestModule 1 "ASYNC_MODE=NODE" "ASYNC_MPI_COPY=1" )
    # messages instead of shared windows
    add_cxx_test_run( TestModuleMPINoShared TestModule 3
        "ASYNC_MODE=NODE" "ASYNC_MPI_COPY=1" "ASYNC_SHARED_MEMORY=0" )
    add_cxx_test_run( TestModuleMPINoSharedSyncCopy TestModule 3
        "ASYNC_MODE=NODE" "ASYNC_MPI_COPY=0" "ASYNC_SHARED_MEMORY=0" )
    # one group over two nodes {0, 1} and {2, 3}, which the tests draw on a single machine
    foreach( variant "" "SyncCopy" "Exclusive" "ExclusiveSyncCopy" )
        set( environment "ASYNC_MODE=NODE" "ASYNC_GROUP_NODES=2" "ASYNC_TEST_NODE_RANKS=2" )
        if( variant MATCHES "SyncCopy" )
            list( APPEND environment "ASYNC_MPI_COPY=0" )
        endif()
        if( variant MATCHES "Exclusive" )
            list( APPEND environment "ASYNC_EXECUTOR_EXCLUSIVE=1" )
        endif()
        add_cxx_test_run( TestModuleMPINodes${variant} TestModule 4 ${environment} )
    endforeach()
    # AUTO on a single machine, on two nodes of consecutive ranks, on nodes of a single rank each,
    # and on nodes the ranks are dealt to round-robin
    add_cxx_test_run( TestModuleAutoNode TestModule 3 "ASYNC_MODE=AUTO" "ASYNC_TEST_AUTO_MODE=NODE" )
    add_cxx_test_run( TestModuleAutoNodes TestModule 4 "ASYNC_MODE=AUTO" "ASYNC_GROUP_NODES=2"
        "ASYNC_TEST_NODE_RANKS=2" "ASYNC_TEST_AUTO_MODE=NODE" )
    add_cxx_test_run( TestModuleAutoSingleRanks TestModule 3 "ASYNC_MODE=AUTO"
        "ASYNC_TEST_NODE_RANKS=1" "ASYNC_TEST_AUTO_MODE=THREAD" )
    add_cxx_test_run( TestModuleAutoRoundRobin TestModule 4 "ASYNC_MODE=AUTO"
        "ASYNC_TEST_ROUND_ROBIN_NODES=2" "ASYNC_TEST_AUTO_MODE=THREAD" )
endif( USE_MPI )
