// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifdef USE_MPI
#include <mpi.h>
#endif
#include <cxxtest/ErrorPrinter.h>

int main(int argc, char** argv)
{
#ifdef USE_MPI
    MPI_Init(&argc, &argv);
#endif

    CxxTest::ErrorPrinter tester;
    int status = CxxTest::Main<CxxTest::ErrorPrinter>(tester, argc, argv);

#ifdef USE_MPI
    MPI_Finalize();
#endif

    return status;
}

<CxxTest world>
