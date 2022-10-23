// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_CONTENT_SIZE_MANAGER_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_CONTENT_SIZE_MANAGER_H_

#include <lib/zx/result.h>

#include <fbl/intrusive_container_utils.h>
#include <fbl/intrusive_double_list.h>
#include <kernel/event.h>
#include <kernel/mutex.h>
#include <ktl/algorithm.h>
#include <ktl/atomic.h>
#include <ktl/limits.h>
#include <ktl/optional.h>
#include <ktl/variant.h>

// `ContentSizeManager` is a class that helps coordinate multiple, potentially concurrent changes
// to a VMO's content size without needing to serialize the I/O of those operations. This is done by
// maintaining queues of outstanding operations, allowing concurrent execution of the operations,
// and then committing the content size effects of those operations in a particular order. This idea
// is similar to the re-order buffer in Tomasulo's algorithm.
//
// There are 2 ordering queues: the read queue and the write queue. Both queues hold their
// respective namesake operations as well as shrink operations.
//
// Read operations are permitted to read up to the smallest outstanding content size, which can be
// found as the minimum of the current content size and all shrink operations. Upon completion,
// reads will always commit without blocking behind other operations.
//
// Write operations may extend content size. Upon completion, a write will block until it is the
// head of the write queue if the smallest outstanding content size is less than its target size.
//
// Set size operations are treated differently, depending on whether the operation will expand or
// shrink the content size. When expanding, set size ops are treated as write operations of the same
// target size (see above). When shrinking, set size ops are treated as shrink operations and will
// block until it is the head if any read or write operations that operate beyond the target size
// are queued in front of the set size.
class ContentSizeManager {
 private:
  // Forward declarations
  struct WriteQueueTag {};
  struct ReadQueueTag {};

 public:
  enum class OperationType {
    Write,
    Read,
    SetSize,
    Append,
  };

  // `ContentSizeManager::Operation` is a structure to ensure operations related to content size are
  // committed in order. `Operation` is intended to be used as a stack-allocated structure.
  //
  // Currently, an operation maps 1:1 with the thread it is executing on and thus, can be
  // considered owned by that thread.
  //
  // Notes:
  //  * The initialization, destruction, and immutable properties of this type are only
  //    thread-compatible.
  //  * The type must either be committed or cancelled before destruction. Otherwise, the destructor
  //    will panic.
  class Operation : public fbl::ContainableBaseClasses<
                        fbl::TaggedDoublyLinkedListable<Operation*, WriteQueueTag>,
                        fbl::TaggedDoublyLinkedListable<Operation*, ReadQueueTag>> {
   public:
    Operation() = default;

    ~Operation() {
      DEBUG_ASSERT_MSG(!IsValid(), "Operation destructed without cancelling or committing!");
    }

    // Disallow copy and move
    Operation(const Operation&) = delete;
    Operation& operator=(const Operation&) = delete;
    Operation(Operation&&) = delete;
    Operation& operator=(Operation&&) = delete;

    ContentSizeManager* parent() const { return parent_; }

    OperationType GetType() const { return type_; }

    // This function exists to satisfy Clang thread safety analysis since there are many
    // circumstances where the parent lock is not acquired through the parent pointer
    // (i.e. initialization). As of writing, Clang is unable to follow pointer aliasing.
    void AssertParentLockHeld() const TA_ASSERT(parent()->lock()) {
      DEBUG_ASSERT(IsValid());
      parent()->lock()->capability().AssertHeld();
    }

    // Gets the content size that the operation will expand to once it is completed.
    //
    // Note:
    //  * This may only be called on a valid operation.
    //  * This must only be called when holding the parent `ContentSizeManager` lock.
    uint64_t GetSizeLocked() const TA_REQ(parent()->lock());

    // Shrinks the size of the operation.
    //
    // Only size shrinks are allowed, since the concurrency of other operations are gated on the
    // largest potential size of operations in front of it.
    //
    // Note:
    //  * This may only be called on a valid operation.
    //  * This must only be called when holding the parent `ContentSizeManager` lock.
    //  * The `new_size` passed in must be greater than 0.
    //  * The `new_size` passed in must be less than or equal to the current size.
    //  * This must only be called for `OperationType::Append` and `OperationType::Write` ops.
    void ShrinkSizeLocked(uint64_t new_size) TA_REQ(parent()->lock());

    // Commits the operation's effects on the content size.
    //
    // Note:
    //  * This may only be called on a valid operation.
    //  * This must only be called when holding the parent `ContentSizeManager` lock.
    void CommitLocked() TA_REQ(parent()->lock());

    // Cancels the operation and does not commit any changes to the content size.
    //
    // Note:
    //  * This may only be called on a valid operation.
    //  * This must only be called when holding the parent `ContentSizeManager` lock.
    void CancelLocked() TA_REQ(parent()->lock());

    // Updates the content size when progress is made from the operation.
    //
    // Note:
    //  * This may only be called on a valid `Append` or `Write` operation.
    //  * The content size must be larger than the current content size.
    void UpdateContentSizeFromProgress(uint64_t new_content_size);

   private:
    friend class ContentSizeManager;

    // Indicates whether the operation is valid.
    bool IsValid() const { return parent_ != nullptr; }

    void Reset() { parent_ = nullptr; }

    void Initialize(ContentSizeManager* parent, uint64_t size, OperationType type);

    ContentSizeManager* parent_ = nullptr;
    OperationType type_;

    // Holds the target size. For appends, this will only be valid once the operation is at the head
    // of the queue.
    uint64_t size_;

    Event ready_event_;
  };

  ContentSizeManager() = default;

  explicit ContentSizeManager(uint64_t content_size) : content_size_(content_size) {}

  Lock<Mutex>* lock() const TA_RET_CAP(lock_) { return &lock_; }

  // Returns the current content size.
  uint64_t GetContentSize() const {
    // Loads from the operation the content size must be ordered with the acquire ordering to ensure
    // that all memory operations from the VMO (i.e. reads) after the load are not reordered before
    // reading the content size. Otherwise, reads from the VMO before acquiring content size may not
    // see data that was written to the VMO just before content size was updated (via
    // `SetContentSize`).
    return content_size_.load(ktl::memory_order_acquire);
  }

  // Marks and registers the beginning of an append operation.
  //
  //
  // Notes:
  //  * This function may block until other conflicting operations complete.
  //  * This function may drop and reacquire the lock guarded by `lock_guard`.
  //  * `append_size` must be greater than 0.
  zx_status_t BeginAppendLocked(uint64_t append_size, Guard<Mutex>* lock_guard, Operation* out_op)
      TA_REQ(lock_);

  // Marks and registers the beginning of a write operation.
  //
  // If the write is results in an expansion of the content size, returns the previous content size
  // from which the write expands in `out_prev_content_size`. The gap from the previous content size
  // to where the write begins likely needs to be zeroed out.
  //
  // Notes:
  //  * This function may block until other conflicting operations complete.
  //  * This function may drop and reacquire the lock guarted by `lock_guard`.
  void BeginWriteLocked(uint64_t target_size, Guard<Mutex>* lock_guard,
                        ktl::optional<uint64_t>* out_prev_content_size, Operation* out_op)
      TA_REQ(lock_);

  // Marks and registers the beginning of a read operation.
  //
  // Returns the maximum size of the content that should be read in `out_content_size_limit`.
  void BeginReadLocked(uint64_t target_size, uint64_t* out_content_size_limit, Operation* out_op)
      TA_REQ(lock_);

  // Marks and registers the beginning of an operation to set the content size to a target size.
  //
  // Note that this function may drop and reacquire the lock guarded by `lock_guard`.
  void BeginSetContentSizeLocked(uint64_t target_size, Operation* out_op, Guard<Mutex>* lock_guard)
      TA_REQ(lock_);

 private:
  // Updates the content size to a new value.
  //
  // Note that this function should only be called by internal functions, as content size should
  // only be modified by one operation at a time. This is enforced by the queues.
  void SetContentSize(uint64_t new_content_size) {
    // Stores to the content size must be ordered with release ordering to ensure that all memory
    // operations (i.e. writes) to the VMO are visible *before* updating content size. Readers must
    // see valid data in the VMO if the region being read is within content size. See
    // `GetContentSize` as well.
    content_size_.store(new_content_size, ktl::memory_order_release);
  }

  // Blocks until the provided operation is at the head of the queue.
  //
  // Note that this function will drop the lock guarded by `lock_guard` while blocking and
  // reacquires the lock after.
  void BlockUntilHeadLocked(Operation* op, Guard<Mutex>* lock_guard) TA_REQ(lock_);

  void CommitAndDequeueOperationLocked(Operation* op) TA_REQ(lock_);

  // Dequeues an `Operation`. This must only be called internally, once an `Operation` is committed
  // or cancelled.
  void DequeueOperationLocked(Operation* op) TA_REQ(lock_);

  mutable DECLARE_MUTEX(ContentSizeManager) lock_;
  // These queues are usually very shallow, unless stream clients call many operations concurrently.
  fbl::DoublyLinkedList<Operation*, WriteQueueTag> write_q_ TA_GUARDED(lock_);
  fbl::DoublyLinkedList<Operation*, ReadQueueTag> read_q_ TA_GUARDED(lock_);

  // `content_size_` is not guarded by a lock because the queues above maintains that only one
  // operation can ever be mutating `content_size_` at any given point.
  //
  // Accessing this value should be done via `GetContentSize` and `SetContentSize`.
  ktl::atomic<uint64_t> content_size_ = 0;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_CONTENT_SIZE_MANAGER_H_
