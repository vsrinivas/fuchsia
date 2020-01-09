// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IO_SCHEDULER_STREAM_H_
#define IO_SCHEDULER_STREAM_H_

#include <zircon/types.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <io-scheduler/scheduler-client.h>
#include <io-scheduler/stream-op.h>

namespace ioscheduler {

class Scheduler;
class Stream;
using StreamRef = fbl::RefPtr<Stream>;

// Stream - a logical sequence of ops.
// The Stream class is not thread safe, streams depend on the scheduler for synchronization.
class Stream : public fbl::RefCounted<Stream> {
 public:
  Stream(uint32_t id, uint32_t pri, Scheduler* sched);
  ~Stream();
  DISALLOW_COPY_ASSIGN_AND_MOVE(Stream);

  uint32_t id() { return id_; }
  uint32_t priority() { return priority_; }

  // Close a stream.
  // Returns:
  //    ZX_OK if stream is empty and ready for immediate release.
  //    ZX_ERR_SHOULD_WAIT is stream has pending operations. It will be released by worker threads
  //       or the shutdown routine.
  zx_status_t Close();

  // Insert an op into the tail of the stream (subject to reordering).
  // On error op's error status is set and it is moved to |*op_err|.
  zx_status_t Insert(UniqueOp op, UniqueOp* op_err) __TA_EXCLUDES(lock_);

  // Fetch a pointer to an op from the head of the stream.
  // The stream maintains ownership of the op. All fetched op must be returned via ReleaseOp().
  void GetNext(UniqueOp* op_out) __TA_EXCLUDES(lock_);

  // Releases an op obtained via GetNext().
  void ReleaseOp(UniqueOp op, SchedulerClient* client);

  // WAVL Tree support.
  using WAVLTreeNodeState = fbl::WAVLTreeNodeState<StreamRef>;
  struct WAVLTreeNodeTraitsSortById {
    static WAVLTreeNodeState& node_state(Stream& s) { return s.map_node_; }
  };

  struct KeyTraitsSortById {
    static const uint32_t& GetKey(const Stream& s) { return s.id_; }
    static bool LessThan(const uint32_t s1, const uint32_t s2) { return (s1 < s2); }
    static bool EqualTo(const uint32_t s1, const uint32_t s2) { return (s1 == s2); }
  };

  using WAVLTreeSortById =
      fbl::WAVLTree<uint32_t, StreamRef, KeyTraitsSortById, WAVLTreeNodeTraitsSortById>;

  // List support.
  using ListNodeState = fbl::DoublyLinkedListNodeState<StreamRef>;
  struct ListTraitsUnsorted {
    static ListNodeState& node_state(Stream& s) { return s.list_node_; }
  };

  using ListUnsorted = fbl::DoublyLinkedList<StreamRef, ListTraitsUnsorted>;

 private:
  friend struct WAVLTreeNodeTraitsSortById;
  friend struct KeyTraitsSortById;

  bool IsEmptyLocked() __TA_REQUIRES(lock_);

  uint32_t id_;
  uint32_t priority_;

  // Used by the scheduler's stream maps. Access is protected by the scheduler lock.
  WAVLTreeNodeState map_node_;

  // Used by the queue's priority list. Access is protected by the queue lock.
  ListNodeState list_node_;

  // Pointer to the scheduler. Streams may not exist beyond the lifetime of the scheduler, so
  // this pointer must always be valid.
  Scheduler* sched_ = nullptr;

  fbl::Mutex lock_;
  bool open_ __TA_GUARDED(lock_) = true;                  // Stream is open, can accept more ops.
  StreamOp::ReadyList ready_ops_ __TA_GUARDED(lock_);     // Ops ready to be issued.
  StreamOp::IssuedList issued_ops_ __TA_GUARDED(lock_);   // Issued ops.
};

}  // namespace ioscheduler

#endif  // IO_SCHEDULER_STREAM_H_
