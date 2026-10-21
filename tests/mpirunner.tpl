// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#ifdef USE_MPI
#include <mpi.h>
#endif
#include <cxxtest/ErrorPrinter.h>

int main(int argc, char** argv)
{
#ifdef USE_MPI
    // MPI mode calls MPI from the scheduler loop and the executor thread at once
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
#endif

    CxxTest::ErrorPrinter tester;
    int status = CxxTest::Main<CxxTest::ErrorPrinter>(tester, argc, argv);

#ifdef USE_MPI
    MPI_Finalize();
#endif

    return status;
}

<CxxTest world>
