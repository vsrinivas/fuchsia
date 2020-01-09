// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IO_SCHEDULER_STREAM_OP_H_
#define IO_SCHEDULER_STREAM_OP_H_

#include <stdint.h>
#include <zircon/types.h>

#include <fbl/intrusive_double_list.h>

namespace ioscheduler {

// Operation type.
// These are used to determine respective ordering restrictions of the ops in a stream.
enum class OpType : uint32_t {
  // Operations that can optionally be reordered.

  kOpTypeUnknown = 0,  // Always reordered.
  kOpTypeRead = 1,     // Read ordering.
  kOpTypeWrite = 2,    // Write order.
  kOpTypeDiscard = 3,  // Write order.
  kOpTypeRename = 4,   // Read and Write order.
  kOpTypeSync = 5,     // Write order.
  kOpTypeCommand = 6,  // Read and Write order.

  // Operations that cannot be reordered.

  kOpTypeOrderedUnknown = 32,  // Always ordered.

  // Barrier operations.

  // Prevent reads from being reordered ahead of this barrier op. No read
  // after this barrier can be issued until this operation has completed.
  kOpTypeReadBarrier = 64,

  // Prevent writes from being reordered after this barrier op. This
  // operation completes after all previous writes in the stream have been
  // issued.
  kOpTypeWriteBarrier = 65,

  // Prevent writes from being reordered after this barrier op. This
  // instruction completes after all previous writes in the stream have been
  // completed.
  kOpTypeWriteCompleteBarrier = 66,

  // Combined effects of kOpTypeReadBarrier and kOpTypeWriteBarrier.
  kOpTypeFullBarrier = 67,

  // Combined effects of kOpTypeReadBarrier and kOpTypeWriteCompleteBarrier.
  kOpTypeFullCompleteBarrier = 68,
};

constexpr uint32_t kOpFlagComplete = (1u << 0);
constexpr uint32_t kOpFlagDeferred = (1u << 1);
constexpr uint32_t kOpFlagGroupLeader = (1u << 8);

constexpr uint32_t kOpGroupNone = 0;

class Stream;
class StreamOp;

// UniqueOp is a wrapper around StreamOp designed to clarify the ownership of an op pointer.
// It supports move-only semantics, and must be either move()'d or release()'d before destruction.
// Since StreamOp is allocated by the client, it cannot be deleted by this wrapper. Notably,
// UniqueOp's destructor DOES NOT delete and will assert if its container is non-null.
class UniqueOp {
 public:
  // Constructors
  constexpr UniqueOp() : op_(nullptr) {}
  constexpr UniqueOp(decltype(nullptr)) : UniqueOp() {}
  explicit UniqueOp(StreamOp* op) : op_(op) {}

  // Copy construction.
  UniqueOp(const UniqueOp& r) = delete;
  // Assignment
  UniqueOp& operator=(const UniqueOp& r) = delete;

  // Move construction.
  UniqueOp(UniqueOp&& r) : op_(r.op_) { r.op_ = nullptr; }

  ~UniqueOp() { ZX_DEBUG_ASSERT(op_ == nullptr); }

  // Move assignment.
  UniqueOp& operator=(UniqueOp&& r) {
    ZX_DEBUG_ASSERT(op_ == nullptr);
    op_ = r.op_;
    r.op_ = nullptr;
    return *this;
  }

  void set(StreamOp* op) {
    ZX_DEBUG_ASSERT(op_ == nullptr);
    op_ = op;
  }

  StreamOp* release() {
    StreamOp* old = op_;
    op_ = nullptr;
    return old;
  }

  StreamOp* get() const { return op_; }
  StreamOp& operator*() const { return *op_; }
  StreamOp* operator->() const { return op_; }
  explicit operator bool() const { return !!op_; }
  bool operator==(decltype(nullptr)) const { return (op_ == nullptr); }
  bool operator!=(decltype(nullptr)) const { return (op_ != nullptr); }
  bool operator==(const UniqueOp& other) const { return (op_ == other.op_); }
  bool operator!=(const UniqueOp& other) const { return (op_ != other.op_); }

 private:
  StreamOp* op_ = nullptr;
};

// class StreamOp.
// The library schedules operations, or ops of type StreamOp. An IO operation is a discrete
// unit of IO that is meaningful to the client. StreamOps are allocated and freed by the client.
// The Scheduler interacts with these via the SchedulerClient interface. A reference to each op
// acquired through this interface is retained until the Release() method is called.
class StreamOp {
 public:
  StreamOp() { StreamOp(OpType::kOpTypeUnknown, 0, kOpGroupNone, 0, nullptr); }

  StreamOp(OpType type, uint32_t stream_id, uint32_t group_id, uint32_t group_members, void* cookie)
      : type_(type),
        stream_id_(stream_id),
        group_id_(group_id),
        group_members_(group_members),
        result_(ZX_OK),
        cookie_(cookie),
        flags_(0),
        stream_(nullptr) {}

  DISALLOW_COPY_ASSIGN_AND_MOVE(StreamOp);

  OpType type() { return type_; }
  void set_type(OpType type) { type_ = type; }

  uint32_t stream_id() { return stream_id_; }
  void set_stream_id(uint32_t stream_id) { stream_id_ = stream_id; }

  uint32_t group() { return group_id_; }
  void set_group(uint32_t gid) { group_id_ = gid; }

  uint32_t members() { return group_members_; }
  void set_members(uint32_t group_members) { group_members_ = group_members; }

  zx_status_t result() { return result_; }
  void set_result(zx_status_t result) { result_ = result; }

  void* cookie() { return cookie_; }
  void set_cookie(void* cookie) { cookie_ = cookie; }

  uint32_t flags() { return flags_; }
  void set_flags(uint32_t flags) { flags_ = flags; }

  Stream* stream() { return stream_; }
  void set_stream(Stream* stream) { stream_ = stream; }
  bool is_deferred() { return flags_ & kOpFlagDeferred; }

  // List support.
  using ListNodeState = fbl::DoublyLinkedListNodeState<StreamOp*>;
  struct AllListTraits {
    static ListNodeState& node_state(StreamOp& s) { return s.all_node_; }
  };
  using AllList = fbl::DoublyLinkedList<StreamOp*, AllListTraits>;

  struct ReadyListTraits {
    static ListNodeState& node_state(StreamOp& s) { return s.ready_node_; }
  };
  using ReadyList = fbl::DoublyLinkedList<StreamOp*, ReadyListTraits>;

  struct IssuedListTraits {
    static ListNodeState& node_state(StreamOp& s) { return s.issued_node_; }
  };
  using IssuedList = fbl::DoublyLinkedList<StreamOp*, IssuedListTraits>;

  struct DeferredListTraits {
    static ListNodeState& node_state(StreamOp& s) { return s.deferred_node_; }
  };
  using DeferredList = fbl::DoublyLinkedList<StreamOp*, DeferredListTraits>;

 private:
  ListNodeState all_node_;
  ListNodeState ready_node_;
  ListNodeState issued_node_;
  ListNodeState deferred_node_;

  OpType type_;             // Type of operation.
  uint32_t stream_id_;      // Stream into which this op is queued.
  uint32_t group_id_;       // Group of operations.
  uint32_t group_members_;  // Number of members in the group.
  zx_status_t result_;      // Status code of the released operation.
  void* cookie_;            // User-defined per-op cookie.

  uint32_t flags_;
  // Pointer to stream containing this op.
  // This pointer is valid as long as the op is retained by the stream, from insertion to release.
  // Effectively this is the lifetime of the op inside the io scheduler.
  Stream* stream_;
};

}  // namespace ioscheduler

#endif  // IO_SCHEDULER_STREAM_OP_H_
