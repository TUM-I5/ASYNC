<!--
    SPDX-FileCopyrightText: 2016 Technical University of Munich

    SPDX-License-Identifier: BSD-3-Clause
-->

# ASYNC

ASYNC is a header-only library for asynchronous I/O. It takes the output of
an MPI application off the path of the computation: a module describes the
buffers the application fills, and an executor that writes them. ASYNC hands
the buffers to the executor and runs it while the application goes on.

## Modules

A module derives from `async::Module` and is driven by the application:

```cpp
class Writer : public async::Module<Writer, InitParam, Param> {
  public:
  void setUp() override { setExecutor(*this); }
  void execInit(const async::ExecInfo& info, const InitParam& param);
  void exec(const async::ExecInfo& info, const Param& param);
  void tearDown() override {}
};

async::Dispatcher dispatcher;
Writer writer;
dispatcher.init();
if (dispatcher.dispatch()) {
  writer.init();
  const auto id = writer.addBuffer(data, size);
  writer.callInit(initParam);
  writer.wait();
  for (/* every output step */) {
    writer.sendBuffer(id);
    writer.call(param);
    // compute
    writer.wait();
  }
  writer.finalize();
}
dispatcher.finalize();
```

The buffers belong to the application between `wait()` and `call()`, and to
ASYNC between `call()` and `wait()`. Every `call()` follows a `wait()`, the
first one included. `addBuffer(nullptr, size)` lets ASYNC manage the memory
of a buffer, which `managedBuffer()` hands out for the application to fill.
In the executor, `ExecInfo` describes the buffers as the executor holds them,
and `bufferParts()` the contributions a buffer is made of: one for every rank
in `NODE` mode. Each contribution is a block of its own, so data distributed
along another dimension than its leading one has to be put together from them.

## Modes

- `SYNC` runs the executor right in `call()`.
- `THREAD` runs the executor of every rank on a thread of its own.
- `NODE` hands the buffers of the ranks of a node to an executor rank on that
  node, which runs the executors for all of them: it holds the buffers of its
  group one after another, in the order of the ranks. `MPI` is accepted as a
  deprecated name for it. The mode requires `MPI_THREAD_MULTIPLE`.
- `AUTO` lets `Dispatcher::init()` choose: `NODE` where ranks share a node,
  every node holds consecutive ranks and MPI provides `MPI_THREAD_MULTIPLE`,
  `THREAD` otherwise. Whether the threads of ASYNC have room at all is for the
  application to know; where they have none, it settles `AUTO` on `SYNC` with
  `Dispatcher::setMode()` beforehand. A module created with `AUTO` takes the
  mode of the dispatcher, in `init()`, or in `attach()` if it comes later.

In `NODE` mode, the executor rank computes as well by default, and
coordinates its group from a thread of its own; `Dispatcher::setAffinity`
places that thread, and `setAffinityIfNecessary` the executor thread. An
exclusive executor leaves the computation and splits the communicator of the
application, which `Dispatcher::commWorld()` then returns.

The ranks of a node aggregate their buffers in shared memory windows: every
rank writes its part straight into the aggregate, and a buffer ASYNC manages
lives there already. A group may span several nodes; the last rank of every
other node then sends the parts of its node to the executor in one piece.
Without shared memory, every rank sends its part itself.

`ExecInfo::comm()` spans the ranks that run executors, for output that the
executors write together. `Dispatcher::layout()` reports how the groups were
formed; `rankOrdered` tells whether the executors, taken in their order, hold
the buffers in the order of the ranks that compute.

## Settings

An `async::Config` reads its defaults from environment variables. Their prefix
is `ASYNC_` unless the application chooses a chain of its own, as in
`utils::Env{"MYAPP_ASYNC_", "ASYNC_"}`; setters override every value.

- `MODE`: `SYNC` (default), `THREAD`, `NODE` or `AUTO`.
- `PIN_CORE`: the core of the executor thread in `THREAD` mode, counted
  among the cores the rank may use; negative values count from the end
  (default -1).
- `EXECUTORS_PER_NODE`: executors, and with them groups, on every node
  (default 1).
- `GROUP_NODES`: whole nodes that share one set of executors (default 1).
- `MAX_GROUP_RANKS`: the most ranks an executor collects the buffers of;
  more executors per node where needed (default 0, no limit). `GROUP_SIZE` is
  a deprecated name for it.
- `EXECUTOR_EXCLUSIVE`: keep the executor ranks out of the computation
  (default false).
- `MPI_COPY`: let `call()` return without waiting for the other ranks of the
  group (default true). Otherwise, `call()` returns once the whole group has
  called it, and without shared memory, `sendBuffer()` sends a buffer right
  away instead of copying it.
- `SHARED_MEMORY`: aggregate in shared memory windows where the ranks of a
  node share memory (default true).
- `POLL_INTERVAL_US`: the longest an idle executor sleeps between two looks
  for work, in microseconds; 0 keeps it spinning (default: 1000 for an
  executor that computes as well, 0 for an exclusive one).
- `MPI_MAX_SEND`: the largest message, in bytes (default 1 GiB).
- `BUFFER_ALIGNMENT`: the alignment of the buffers of the executor, in bytes
  (default 0, none).

## Tests

```sh
cmake -B build -DTESTING=ON
cmake --build build
ctest --test-dir build
```

With `-DASYNC_TEST_SANITIZERS="address;undefined"` or
`-DASYNC_TEST_SANITIZERS=thread`, the tests are built and run with
sanitizers. The tests stand in for several nodes on a single machine by
drawing the node boundaries themselves.
