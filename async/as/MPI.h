/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 *
 * @copyright Copyright (c) 2016, Technische Universitaet Muenchen.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *
 *  3. Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ASYNC_AS_MPI_H
#define ASYNC_AS_MPI_H

#include <mpi.h>

#include <cassert>
#include <vector>

#include "MPIBase.h"

namespace async
{

namespace as
{

/**
 * Asynchronous call via MPI
 */
template<class Executor, typename InitParameter, typename Parameter>
class MPI : public MPIBase<Executor, InitParameter, Parameter>
{
public:
	MPI()
	{
	}

	~MPI()
	{
	}

	unsigned int addBuffer(const void* buffer, size_t size)
	{
		MPIBase<Executor, InitParameter, Parameter>::addBuffer(buffer, size);
		return Base<Executor, InitParameter, Parameter>::_addBuffer(buffer, size, false);
	}

	const void* buffer(unsigned int id) const
	{
		if (MPIBase<Executor, InitParameter, Parameter>::scheduler().isExecutor())
			return MPIBase<Executor, InitParameter, Parameter>::buffer(id);

		return Base<Executor, InitParameter, Parameter>::origin(id);
	}

	/**
	 * @param id The id of the buffer
	 */
	void sendBuffer(unsigned int id, size_t size)
	{
		if (size == 0)
			return;

		assert(id < (Base<Executor, InitParameter, Parameter>::numBuffers()));

		// We need to send the buffer in 1 GB chunks
		for (size_t done = 0; done < size; done += MPIBase<Executor, InitParameter, Parameter>::maxSend()) {
			size_t send = std::min(MPIBase<Executor, InitParameter, Parameter>::maxSend(), size-done);

			MPIBase<Executor, InitParameter, Parameter>::scheduler().sendBuffer(
				MPIBase<Executor, InitParameter, Parameter>::id(),
				id,
				Base<Executor, InitParameter, Parameter>::origin(id) +
					MPIBase<Executor, InitParameter, Parameter>::bufferPos(id),
				send);
			MPIBase<Executor, InitParameter, Parameter>::incBufferPos(id, send);
		}
	}

	/**
	 * @warning Only the parameter from one task will be considered
	 */
	void call(const Parameter &parameters)
	{
		MPIBase<Executor, InitParameter, Parameter>::scheduler().sendParam(
			MPIBase<Executor, InitParameter, Parameter>::id(), parameters);
	}

private:
	bool useAsyncCopy() const
	{
		return false;
	}
};

}

}

#endif // ASYNC_AS_MPI_H
