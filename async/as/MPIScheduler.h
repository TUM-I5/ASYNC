// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifndef ASYNC_AS_MPISCHEDULER_H
#define ASYNC_AS_MPISCHEDULER_H

#include <mpi.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "utils/logger.h"

namespace async::as {

/**
 * A pair of MPI requests
 */
struct MPIRequest2 {
  std::array<MPI_Request, 2> r;
};

template <class Executor, typename InitParameter, typename Parameter>
class MPIBase;
template <class Executor, typename InitParameter, typename Parameter>
class MPI;
template <class Executor, typename InitParameter, typename Parameter>
class MPIAsync;
class MPIScheduler;

class Scheduled {
  friend class MPIScheduler;

  private:
  std::vector<char> m_paramBuffer;

  protected:
  Scheduled() = default;

  ~Scheduled() = default;

  private:
  void allocParamBuffer() { m_paramBuffer.resize(paramSize()); }

  auto paramBuffer() -> void* { return m_paramBuffer.data(); }

  [[nodiscard]] auto paramBuffer() const -> const void* { return m_paramBuffer.data(); }

  /**
   * @return The maximum buffer size required to store init parameter
   *  or parameter
   */
  [[nodiscard]] virtual auto paramSize() const -> unsigned int = 0;

  /**
   * @return True if this class uses async copying
   */
  [[nodiscard]] virtual auto useAsyncCopy() const -> bool = 0;

  /**
   * @return True if this class uses async copying for a specific buffer
   */
  [[nodiscard]] virtual auto useAsyncCopy(unsigned int id) const -> bool = 0;

  /**
   * Returns the number of buffer chunks send to the executor
   *
   * This might differ from the number of buffers since large buffers
   * have the be send in multiple iterations.
   */
  [[nodiscard]] virtual auto numBufferChunks() const -> unsigned int = 0;

  /**
   * @param sync True if the buffer is send sychronized
   */
  virtual void addBufferInternal(bool sync) = 0;

  virtual void resizeBufferInternal(unsigned int id) = 0;

  virtual void removeBufferInternal(unsigned int id) = 0;

  virtual void execInitInternal(const void* parameter) = 0;

  virtual auto getBufferPos(unsigned int id, int rank, int size) -> void* = 0;

  virtual void execInternal(const void* parameter) = 0;

  /**
   * Wait on the executor for the call to finish
   */
  virtual void waitInternal() = 0;

  virtual void finalizeInternal() = 0;
};

/**
 * Scheduler for asynchronous MPI calls
 *
 * @warning Only one instance of this class should be created
 */
class MPIScheduler {
  template <class Executor, typename InitParameter, typename Parameter>
  friend class MPIBase;
  template <class Executor, typename InitParameter, typename Parameter>
  friend class MPI;
  template <class Executor, typename InitParameter, typename Parameter>
  friend class MPIAsync;

  private:
  /** The group local communicator (incl. the executor) */
  MPI_Comm m_privateGroupComm;

  /** The public group local communicator (excl. the executor) */
  MPI_Comm m_groupComm;

  /** The rank of in the executor group */
  int m_groupRank{0};

  /** The size of the executor group (incl. the executor) */
  int m_groupSize{0};

  /** True of this task is an executor */
  bool m_isExecutor{false};

  /** True of this task is a compute rank */
  bool m_isCompute{};

  /** The "COMM_WORLD" communicator (communicator without executors) */
  MPI_Comm m_commWorld;

  /** All async objects */
  std::vector<Scheduled*> m_asyncCalls;

  /** Managed buffer counters for each size */
  std::map<size_t, unsigned int> m_managedBufferCounter;

  /** Managed buffer size */
  std::vector<uint8_t> m_managedBuffer;

  /**
   * A list of possible buffer ids
   *
   * With MPI_Isend we need to provide a memory location for buffer ids
   * that is not on the stack. We can use this vector which contains:
   * id[0] = 0, id[1] = 1, ...
   */
  std::vector<unsigned int> m_heapBufferIds;

  /** Class is finalized? */
  bool m_finalized{false};

  public:
  MPIScheduler()
      : m_privateGroupComm(MPI_COMM_NULL), m_groupComm(MPI_COMM_SELF), m_commWorld(MPI_COMM_WORLD) {
  }

  ~MPIScheduler() { finalize(); }

  auto operator=(MPIScheduler&&) -> MPIScheduler& = delete;
  auto operator=(const MPIScheduler&) -> MPIScheduler& = delete;
  MPIScheduler(const MPIScheduler&) = delete;
  MPIScheduler(MPIScheduler&&) = delete;

  /**
   * Set the MPI configuration.
   *
   * Has to be called before {@link init()}
   *
   * @param comm The MPI communicator that should be used
   * @param groupSize Number of ranks per group (excluding the executor)
   */
  void setCommunicator(MPI_Comm comm, unsigned int groupSize) {
    int rank = 0;
    MPI_Comm_rank(comm, &rank);

    MPI_Comm groupComm = nullptr;

    // Create group communicator
    MPI_Comm_split(comm, static_cast<int>(rank / (groupSize + 1)), 0, &groupComm);

    MPI_Comm_rank(groupComm, &m_groupRank);
    MPI_Comm_size(groupComm, &m_groupSize);

    const auto isExecutor = m_groupRank == m_groupSize - 1;

    setCommunicator(comm, groupComm, isExecutor, true);
  }

  /**
   * Set the MPI configuration.
   *
   * Has to be called before {@link init()}
   *
   * @param comm The global MPI communicator that should be used
   * @param groupComm The group MPI communicator that should be used
   * @param isExecutor Indicates if the current rank is an executor
   * @param executorExclusive Indicates if the executor should be excluded from the group
   */
  void setCommunicator(MPI_Comm comm, MPI_Comm groupComm, bool isExecutor, bool executorExclusive) {
    m_privateGroupComm = groupComm;

    // Get group rank/size
    MPI_Comm_rank(groupComm, &m_groupRank);
    MPI_Comm_size(groupComm, &m_groupSize);

    // Is an executor?
    m_isExecutor = isExecutor;
    m_isCompute = !(executorExclusive && isExecutor);

    int executorCount = isExecutor ? 1 : 0;
    MPI_Allreduce(MPI_IN_PLACE, &executorCount, 1, MPI_INT, MPI_SUM, groupComm);

    if (executorCount != 1) {
      logError() << "";
    }

    if (executorExclusive) {
      // Create the new comm world communicator
      MPI_Comm_split(comm, (m_isExecutor ? 1 : 0), 0, &m_commWorld);

      // Create the public group communicator (excl. the executor)
      MPI_Comm_split(m_privateGroupComm, (m_isExecutor ? MPI_UNDEFINED : 0), 0, &m_groupComm);
    } else {
      m_commWorld = comm;
      m_privateGroupComm = groupComm;
    }
  }

  [[nodiscard]] auto groupRank() const -> int { return m_groupRank; }

  [[nodiscard]] auto isExecutor() const -> bool { return m_isExecutor; }

  [[nodiscard]] auto commWorld() const -> MPI_Comm { return m_commWorld; }

  [[nodiscard]] auto groupComm() const -> MPI_Comm { return m_groupComm; }

  /**
   * Should be called on the executor to start the execution loop
   */
  void loop() {
    if (!m_isExecutor) {
      return;
    }

    // Save ready tasks for each call
    std::vector<unsigned int> readyTasks(m_asyncCalls.size());
    std::vector<unsigned int> asyncReadyTasks(m_asyncCalls.size());

    // Distinguish between init and param tag (required for asynchronous copies)
    std::vector<int> lastTag(m_asyncCalls.size());

    // Number of finalized aync calls
    unsigned int finalized = 0;

    while (finalized < m_asyncCalls.size()) {
      MPI_Status status;
      int id = 0;
      int tag = 0;

      int sync = 0;              // Required for add tag
      unsigned int bufferId = 0; // Required for remove tag and buffer tag

      do {
        MPI_Message message = nullptr;
        MPI_Mprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, m_privateGroupComm, &message, &status);

        if (status.MPI_TAG == KillTag) {
          // Dummy recv
          assert(status.MPI_SOURCE == 0);
          MPI_Mrecv(nullptr, 0, MPI_CHAR, &message, MPI_STATUS_IGNORE);

          // Stop everything immediately (probably some finalizes were missing)
          for (auto& call : m_asyncCalls) {
            if (call != nullptr) {
              call->finalizeInternal();
              call = nullptr;
            }
          }
          return;
        }

        tag = status.MPI_TAG - NumStaticTags;
        id = tag / NumTags;
        tag = tag % NumTags;

        if (id > static_cast<int>(m_asyncCalls.size()) || m_asyncCalls[id] == nullptr) {
          logError() << "ASYNC: Invalid id" << id << "received";
        }

        int size = 0;
        void* buf = nullptr;

        switch (tag) {
        case AddTag:
          MPI_Mrecv(&sync, 1, MPI_INT, &message, MPI_STATUS_IGNORE);

          readyTasks[id]++;
          break;
        case ResizeTag:
          MPI_Mrecv(&bufferId, 1, MPI_UNSIGNED, &message, MPI_STATUS_IGNORE);

          readyTasks[id]++;
          break;
        case RemoveTag:
          MPI_Mrecv(&bufferId, 1, MPI_UNSIGNED, &message, MPI_STATUS_IGNORE);

          readyTasks[id]++;
          break;
        case InitTag:
        case ParamTag:
          MPI_Get_count(&status, MPI_CHAR, &size);
          MPI_Mrecv(m_asyncCalls[id]->paramBuffer(), size, MPI_CHAR, &message, MPI_STATUS_IGNORE);

          lastTag[id] = tag;
          if (m_asyncCalls[id]->useAsyncCopy()) {
            asyncReadyTasks[id]++;
          } else {
            readyTasks[id]++;
          }
          break;
        case BufferTag:
          // Select a buffer
          MPI_Mrecv(&bufferId, 1, MPI_UNSIGNED, &message, MPI_STATUS_IGNORE);

          // Probe buffer receive from some source/tag
          MPI_Mprobe(status.MPI_SOURCE, status.MPI_TAG, m_privateGroupComm, &message, &status);

          // Get the size that is received
          MPI_Get_count(&status, MPI_CHAR, &size);

          // Receive the buffer
          buf = m_asyncCalls[id]->getBufferPos(bufferId, status.MPI_SOURCE, size);
          MPI_Mrecv(buf, size, MPI_CHAR, &message, MPI_STATUS_IGNORE);

          if (m_asyncCalls[id]->useAsyncCopy(bufferId)) {
            asyncReadyTasks[id]++;
          }
          break;
        case WaitTag:
        case FinalizeTag:
          MPI_Mrecv(nullptr, 0, MPI_CHAR, &message, MPI_STATUS_IGNORE);

          readyTasks[id]++;
          break;
        default:
          logError() << "ASYNC: Unknown tag" << tag << "received";
        }

      } while ((static_cast<int>(readyTasks[id]) < m_groupSize - 1) &&
               (asyncReadyTasks[id] < m_groupSize - 1 + m_asyncCalls[id]->numBufferChunks()));

      if (tag == BufferTag) {
        assert(lastTag[id] == InitTag || lastTag[id] == ParamTag);

        tag = lastTag[id];
      }

      switch (tag) {
      case AddTag:
        MPI_Barrier(m_privateGroupComm);
        m_asyncCalls[id]->addBufferInternal(sync != 0);
        break;
      case ResizeTag:
        MPI_Barrier(m_privateGroupComm);
        m_asyncCalls[id]->resizeBufferInternal(bufferId);
        break;
      case RemoveTag:
        MPI_Barrier(m_privateGroupComm);
        m_asyncCalls[id]->removeBufferInternal(bufferId);
        break;
      case InitTag:
        MPI_Barrier(m_privateGroupComm);
        m_asyncCalls[id]->execInitInternal(m_asyncCalls[id]->paramBuffer());
        break;
      case ParamTag:
        if (!m_asyncCalls[id]->useAsyncCopy()) {
          MPI_Barrier(m_privateGroupComm);
        }
        m_asyncCalls[id]->execInternal(m_asyncCalls[id]->paramBuffer());
        break;
      case WaitTag:
        MPI_Barrier(m_privateGroupComm);
        m_asyncCalls[id]->waitInternal();
        // This barrier can probably be called before waitInternal()
        // Just leave it here to be 100% sure that everything is done
        // when the wait returns
        MPI_Barrier(m_privateGroupComm);
        break;
      case FinalizeTag:
        // Forget the async call
        m_asyncCalls[id]->finalizeInternal();
        m_asyncCalls[id] = nullptr;
        finalized++;
        break;
      default:
        logError() << "ASYNC: Unknown tag" << tag;
      }

      if (static_cast<int>(readyTasks[id]) >= m_groupSize - 1) {
        readyTasks[id] = 0;
      } else {
        asyncReadyTasks[id] = 0;
      }
    }
  }

  void finalize() {
    if (m_finalized) {
      return;
    }

    if (!m_asyncCalls.empty() && m_groupRank == 0) {
      // Some async calls are left, -> send the kill switch
      MPI_Ssend(nullptr, 0, MPI_CHAR, m_groupSize - 1, KillTag, m_privateGroupComm);
    }

    if (m_privateGroupComm != MPI_COMM_NULL) {
      MPI_Comm_free(&m_privateGroupComm);
    }
    if (m_groupComm != MPI_COMM_NULL && m_groupComm != MPI_COMM_SELF) {
      MPI_Comm_free(&m_groupComm);
    }
    if (m_commWorld != MPI_COMM_WORLD) {
      MPI_Comm_free(&m_commWorld);
    }

    m_finalized = true;
  }

  private:
  [[nodiscard]] auto privateGroupComm() const -> MPI_Comm { return m_privateGroupComm; }

  [[nodiscard]] auto groupSize() const -> int { return m_groupSize; }

  auto addScheduled(Scheduled* scheduled) -> int {
    const int id = static_cast<int>(m_asyncCalls.size());
    m_asyncCalls.push_back(scheduled);

    scheduled->allocParamBuffer();

    return id;
  }

  void addBuffer(int id, unsigned int bufferId, bool sync = true) {
    assert(id >= 0);

    if (bufferId >= m_heapBufferIds.size()) {
      assert(bufferId == m_heapBufferIds.size()); // IDs always increment by 1

      m_heapBufferIds.push_back(bufferId);
    }

    int syncInt = static_cast<int>(sync);
    MPI_Send(&syncInt,
             1,
             MPI_INT,
             m_groupSize - 1,
             id * NumTags + AddTag + NumStaticTags,
             m_privateGroupComm);

    MPI_Barrier(m_privateGroupComm);
  }

  void resizeBuffer(int id, unsigned int bufferId) {
    assert(id >= 0);

    MPI_Send(&bufferId,
             1,
             MPI_UNSIGNED,
             m_groupSize - 1,
             id * NumTags + ResizeTag + NumStaticTags,
             m_privateGroupComm);

    MPI_Barrier(m_privateGroupComm);
  }

  void removeBuffer(int id, unsigned int bufferId) {
    assert(id >= 0);

    MPI_Send(&bufferId,
             1,
             MPI_UNSIGNED,
             m_groupSize - 1,
             id * NumTags + RemoveTag + NumStaticTags,
             m_privateGroupComm);

    MPI_Barrier(m_privateGroupComm);
  }

  void sendBuffer(int id, unsigned int bufferId, const void* buffer, int size) {
    assert(id >= 0);

    // Select the buffer
    MPI_Send(&bufferId,
             1,
             MPI_UNSIGNED,
             m_groupSize - 1,
             id * NumTags + BufferTag + NumStaticTags,
             m_privateGroupComm);

    // Send the buffer (synchronous to avoid overtaking of other messages)
    MPI_Ssend(buffer,
              size,
              MPI_CHAR,
              m_groupSize - 1,
              id * NumTags + BufferTag + NumStaticTags,
              m_privateGroupComm);
  }

  auto iSendBuffer(int id, unsigned int bufferId, const void* buffer, int size) -> MPIRequest2 {
    assert(id >= 0);

    MPIRequest2 requests{};

    // Select the buffer
    MPI_Isend(&m_heapBufferIds[bufferId],
              1,
              MPI_UNSIGNED,
              m_groupSize - 1,
              id * NumTags + BufferTag + NumStaticTags,
              m_privateGroupComm,
              requests.r.data());

    // Send the buffer
    MPI_Isend(buffer,
              size,
              MPI_CHAR,
              m_groupSize - 1,
              id * NumTags + BufferTag + NumStaticTags,
              m_privateGroupComm,
              &requests.r[1]);

    return requests;
  }

  template <typename Parameter>
  void sendInitParam(int id, const Parameter& param) {
    assert(id >= 0);

    // For simplification, we send the Parameter struct as a buffer
    // TODO find a nice way for hybrid systems
    MPI_Send(const_cast<Parameter*>(&param),
             sizeof(Parameter),
             MPI_CHAR,
             m_groupSize - 1,
             id * NumTags + InitTag + NumStaticTags,
             m_privateGroupComm);

    MPI_Barrier(m_privateGroupComm);
  }

  /**
   * Send message to the executor task
   */
  template <typename Parameter>
  void sendParam(int id, const Parameter& param) {
    assert(id >= 0);

    // For simplification, we send the Parameter struct as a buffer
    // TODO find a nice way for hybrid systems
    MPI_Send(const_cast<Parameter*>(&param),
             sizeof(Parameter),
             MPI_CHAR,
             m_groupSize - 1,
             id * NumTags + ParamTag + NumStaticTags,
             m_privateGroupComm);

    MPI_Barrier(m_privateGroupComm);
  }

  /**
   * Send message asynchronous to the executor task
   */
  template <typename Parameter>
  auto iSendParam(int id, const Parameter& param) -> MPI_Request {
    assert(id >= 0);

    MPI_Request request = nullptr;

    MPI_Isend(const_cast<Parameter*>(&param),
              sizeof(Parameter),
              MPI_CHAR,
              m_groupSize - 1,
              id * NumTags + ParamTag + NumStaticTags,
              m_privateGroupComm,
              &request);

    return request;
  }

  void wait(int id) {
    assert(id >= 0);

    MPI_Send(nullptr,
             0,
             MPI_CHAR,
             m_groupSize - 1,
             id * NumTags + WaitTag + NumStaticTags,
             m_privateGroupComm);

    // Wait for the return of the async call
    MPI_Barrier(m_privateGroupComm);
  }

  void sendFinalize(int id) {
    assert(id >= 0);

    if (m_privateGroupComm == MPI_COMM_NULL) {
      // Can happen if the dispatcher was already finalized
      // We can ignore this here because, we have the kill switch
      return;
    }

    MPI_Send(nullptr,
             0,
             MPI_CHAR,
             m_groupSize - 1,
             id * NumTags + FinalizeTag + NumStaticTags,
             m_privateGroupComm);

    // Remove one async call from the list (it does not matter which one,
    // since we do not need this list on non-executors anyway
    m_asyncCalls.pop_back();
  }

  void addManagedBuffer(size_t size) {
    m_managedBufferCounter[size]++;
    if (m_managedBuffer.size() < size) {
      m_managedBuffer.resize(size);
    }
  }

  void resizeManagedBuffer(size_t oldSize, size_t newSize) {
    assert(oldSize != newSize);

    m_managedBufferCounter[newSize]++;
    m_managedBufferCounter.at(oldSize)--;
    if (m_managedBufferCounter.at(oldSize) == 0) {
      m_managedBufferCounter.erase(oldSize);
    }
    if (m_managedBuffer.size() == oldSize) {
      if (newSize > oldSize) {
        m_managedBuffer.resize(newSize);
      } else {
        const auto it = m_managedBufferCounter.rbegin();
        assert(it != m_managedBufferCounter.rend());
        m_managedBuffer.resize(it->first);
      }
    }
  }

  void removeManagedBuffer(size_t size) {
    m_managedBufferCounter.at(size)--;
    if (m_managedBufferCounter.at(size) == 0) {
      m_managedBufferCounter.erase(size);
      if (m_managedBuffer.size() == size) {
        // Last element removed that required this size
        const auto it = m_managedBufferCounter.rbegin();
        if (it == m_managedBufferCounter.rend()) {
          m_managedBuffer.clear();
        } else {
          m_managedBuffer.resize(it->first);
        }
      }
    }
  }

  /**
   * @return Current pointer to the managed buffer
   */
  auto managedBuffer() -> uint8_t* { return m_managedBuffer.data(); }

  static const int KillTag = 0;
  static const int NumStaticTags = KillTag + 1;

  static const int AddTag = 0;
  static const int ResizeTag = 1;
  static const int RemoveTag = 2;
  static const int InitTag = 3;
  static const int BufferTag = 4;
  static const int ParamTag = 5;
  static const int WaitTag = 6;
  static const int FinalizeTag = 7;
  /** The number of tags required for each async module */
  static const int NumTags = FinalizeTag + 1;
};

} // namespace async::as

#endif // ASYNC_AS_MPISCHEDULER_H
