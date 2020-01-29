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

constexpr uint32_t kStreamFlagIsClosed = (1u << 0);
constexpr uint32_t kStreamFlagHasDeferred = (1u << 1);

class Scheduler;
class Stream;
using StreamRef = fbl::RefPtr<Stream>;

// Stream - a logical sequence of ops.
// The Stream class is not thread safe, streams depend on the scheduler for synchronization.
class Stream : public fbl::RefCounted<Stream> {
 public:
  Stream() = delete;
  Stream(uint32_t id, uint32_t pri);
  ~Stream();
  DISALLOW_COPY_ASSIGN_AND_MOVE(Stream);

  uint32_t id() { return id_; }
  uint32_t priority() { return priority_; }
  bool is_closed() { return (flags_ & kStreamFlagIsClosed); }

  inline uint32_t flags() { return flags_; }
  inline void set_flags(uint32_t flags) { flags_ |= flags; }
  inline void clear_flags(uint32_t flags) { flags_ &= ~flags; }

  inline bool IsEmpty() { return ready_ops_.is_empty() && issued_ops_.is_empty(); }
  inline bool HasReady() { return !ready_ops_.is_empty(); }
  inline bool HasDefered() { return !deferred_ops_.is_empty(); }

  // Close a stream.
  // Returns:
  //    ZX_OK if stream is empty and ready for immediate release.
  //    ZX_ERR_SHOULD_WAIT is stream has pending operations. It will be released by worker threads
  //       or the shutdown routine.
  zx_status_t Close();

  // Insert an op into the tail of the stream (subject to reordering).
  // On error op's error status is set and it is moved to |*op_err|.
  zx_status_t Insert(UniqueOp op, UniqueOp* op_err);

  // Fetch a pointer to an op from the head of the stream.
  // The stream maintains ownership of the op. All fetched op must be returned via ReleaseOp().
  void GetNext(UniqueOp* op_out);

  // Set an op as deferred for later completion.
  void Defer(UniqueOp op);

  // Get an op pending completion.
  void GetDeferred(UniqueOp* op_out);

  // Marks an op obtained via GetNext() or GetDeferred() as complete.
  // Op is not consumed.
  void Complete(StreamOp* op);

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

  struct ReadyListTraits {
    static ListNodeState& node_state(Stream& s) { return s.ready_node_; }
  };
  using ReadyStreamList = fbl::DoublyLinkedList<StreamRef, ReadyListTraits>;

  struct DeferredListTraits {
    static ListNodeState& node_state(Stream& s) { return s.deferred_node_; }
  };
  using DeferredStreamList = fbl::DoublyLinkedList<StreamRef, DeferredListTraits>;

 private:
  friend struct WAVLTreeNodeTraitsSortById;
  friend struct KeyTraitsSortById;

  uint32_t id_;
  uint32_t priority_;

  WAVLTreeNodeState map_node_;

  ListNodeState ready_node_;
  ListNodeState deferred_node_;

  uint32_t flags_ = 0;
  StreamOp::OpList ready_ops_;            // Ops ready to be issued.
  StreamOp::OpList issued_ops_;           // Issued ops pending completion.
  StreamOp::DeferredList deferred_ops_;   // Ops whose completion has been deferred.
};

}  // namespace ioscheduler

#endif  // IO_SCHEDULER_STREAM_H_
