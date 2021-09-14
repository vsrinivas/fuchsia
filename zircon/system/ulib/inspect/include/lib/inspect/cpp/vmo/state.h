// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CPP_VMO_STATE_H_
#define LIB_INSPECT_CPP_VMO_STATE_H_

#include <lib/fit/function.h>
#include <lib/fit/thread_safety.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/sequencer.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/inspect/cpp/vmo/block.h>
#include <lib/inspect/cpp/vmo/heap.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/stdcompat/optional.h>
#include <lib/stdcompat/span.h>
#include <lib/stdcompat/string_view.h>
#include <zircon/types.h>

#include <cstdint>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace inspect {

namespace internal {

// |State| wraps a |Heap| and implements the Inspect VMO API on top of
// that heap. This class contains the low-level operations necessary to
// deal with the various Inspect types and wrappers to denote ownership of
// those values.
//
// This class should not be used directly, prefer to use |Inspector|.
// |State| writes version 2 of the Inspect Format.
class State final {
 public:
  // Create a new State wrapping the given Heap.
  // On failure, returns nullptr.
  static std::shared_ptr<State> Create(std::unique_ptr<Heap> heap);

  // Create a new State wrapping a new heap of the given size.
  // On failure, returns an empty shared_ptr.
  static std::shared_ptr<State> CreateWithSize(size_t size);

  // Destructor for State, which performs necessary cleanup.
  ~State();

  // Disallow copy and assign.
  State(const State&) = delete;
  State(State&&) = delete;
  State& operator=(const State&) = delete;
  State& operator=(State&&) = delete;

  // Obtain a reference to the wrapped VMO.
  // This may be duplicated read-only to pass to a reader process.
  const zx::vmo& GetVmo() const;

  // Obtain a read-only duplicate of the VMO backing this State.
  bool DuplicateVmo(zx::vmo* vmo) const;

  // Obtain a copy of the VMO backing this state.
  //
  // Returns true on success, false otherwise.
  bool Copy(zx::vmo* vmo) const;

  // Obtain a copy of the bytes in the VMO backing this state.
  //
  // Returns true on success, false otherwise.
  bool CopyBytes(std::vector<uint8_t>* out) const;

  // Create a new |IntProperty| in the Inspect VMO. The returned value releases
  // the property when destroyed.
  IntProperty CreateIntProperty(BorrowedStringValue name, BlockIndex parent, int64_t value);

  // Create a new |UintProperty| in the Inspect VMO. The returned value releases
  // the property when destroyed.
  UintProperty CreateUintProperty(BorrowedStringValue name, BlockIndex parent, uint64_t value);

  // Create a new |DoubleProperty| in the Inspect VMO. The returned value releases
  // the property when destroyed.
  DoubleProperty CreateDoubleProperty(BorrowedStringValue name, BlockIndex parent, double value);

  // Create a new |BoolProperty| in the Inspect VMO. The returned value releases
  // the property when destroyed.
  BoolProperty CreateBoolProperty(BorrowedStringValue name, BlockIndex parent, bool value);

  // Create a new |IntArray| in the Inspect VMO. The returned value releases
  // the array when destroyed.
  IntArray CreateIntArray(BorrowedStringValue name, BlockIndex parent, size_t slots,
                          ArrayBlockFormat format);

  // Create a new |UintArray| in the Inspect VMO. The returned value releases
  // the array when destroyed.
  UintArray CreateUintArray(BorrowedStringValue name, BlockIndex parent, size_t slots,
                            ArrayBlockFormat format);

  // Create a new |DoubleArray| in the Inspect VMO. The returned value releases
  // the array when destroyed.
  DoubleArray CreateDoubleArray(BorrowedStringValue name, BlockIndex parent, size_t slots,
                                ArrayBlockFormat format);

  // Create a new |StringProperty| in the Inspect VMO. The returned value releases
  // the property when destroyed.
  StringProperty CreateStringProperty(BorrowedStringValue name, BlockIndex parent,
                                      const std::string& value);

  // Create a new |ByteVectorProperty| in the Inspect VMO. The returned value releases
  // the property when destroyed.
  ByteVectorProperty CreateByteVectorProperty(BorrowedStringValue name, BlockIndex parent,
                                              cpp20::span<const uint8_t> value);

  // Create a new [Link] in the Inspect VMO. The returned node releases the link when destroyed.
  //
  // A Link is a low-level reference to a new Inspector linked off of the one managed by this
  // state. A Link alone is not sufficient to populate the linked tree, see CreateLazyNode and
  // CreateLazyValues.
  Link CreateLink(BorrowedStringValue name, BlockIndex parent, BorrowedStringValue content,
                  LinkBlockDisposition disposition);

  // Create a new |Node| in the Inspect VMO. Nodes are refcounted such that values nested under the
  // node remain valid until all such values values are destroyed.
  Node CreateNode(BorrowedStringValue name, BlockIndex parent);

  // Create and allocate, if necessary, a StringReference block. The reference count will be 1
  // if the block is new, and incremented if it already existed.
  zx_status_t CreateAndIncrementStringReference(BorrowedStringValue value, BlockIndex* out);

  // Create a special root |Node| in the Inspect VMO. This node is not backed by any storage,
  // rather it allows clients to use the |Node| interface to add properties and children directly
  // to the root of the VMO.
  Node CreateRootNode();

  // Create a new |LazyNode| with a new named |Link| that calls the given callback with child
  // disposition.
  LazyNode CreateLazyNode(BorrowedStringValue name, BlockIndex parent, LazyNodeCallbackFn callback);

  // Create a new |LazyNode| with a new named |Link| that calls the given callback with inline
  // disposition.
  LazyNode CreateLazyValues(BorrowedStringValue name, BlockIndex parent,
                            LazyNodeCallbackFn callback);

  // Setters for various property types
  void SetIntProperty(IntProperty* property, int64_t value);
  void SetUintProperty(UintProperty* property, uint64_t value);
  void SetDoubleProperty(DoubleProperty* property, double value);
  void SetBoolProperty(BoolProperty* property, bool value);
  void SetIntArray(IntArray* array, size_t index, int64_t value);
  void SetUintArray(UintArray* array, size_t index, uint64_t value);
  void SetDoubleArray(DoubleArray* array, size_t index, double value);
  void SetStringProperty(StringProperty* property, const std::string& value);
  void SetByteVectorProperty(ByteVectorProperty* property, cpp20::span<const uint8_t> value);

  // Adders for various property types
  void AddIntProperty(IntProperty* property, int64_t value);
  void AddUintProperty(UintProperty* property, uint64_t value);
  void AddDoubleProperty(DoubleProperty* property, double value);
  void AddIntArray(IntArray* array, size_t index, int64_t value);
  void AddUintArray(UintArray* array, size_t index, uint64_t value);
  void AddDoubleArray(DoubleArray* array, size_t index, double value);

  // Subtractors for various property types
  void SubtractIntProperty(IntProperty* property, int64_t value);
  void SubtractUintProperty(UintProperty* property, uint64_t value);
  void SubtractDoubleProperty(DoubleProperty* property, double value);
  void SubtractIntArray(IntArray* array, size_t index, int64_t value);
  void SubtractUintArray(UintArray* array, size_t index, uint64_t value);
  void SubtractDoubleArray(DoubleArray* array, size_t index, double value);

  // Free various entities
  void FreeIntProperty(IntProperty* property);
  void FreeUintProperty(UintProperty* property);
  void FreeDoubleProperty(DoubleProperty* property);
  void FreeBoolProperty(BoolProperty* property);
  void FreeIntArray(IntArray* array);
  void FreeUintArray(UintArray* array);
  void FreeDoubleArray(DoubleArray* array);
  void FreeStringProperty(StringProperty* property);
  void FreeByteVectorProperty(ByteVectorProperty* property);
  void FreeLink(Link* link);
  void FreeNode(Node* node);
  void FreeLazyNode(LazyNode* lazy_node);
  void ReleaseStringReference(BlockIndex index);

  // Get the names of all links in this state.
  std::vector<std::string> GetLinkNames() const;

  // Call a specific link by name, return a promise for the Inspector it produces.
  fpromise::promise<Inspector> CallLinkCallback(const std::string& name);

  // Create a unique name for children in this State.
  //
  // Returned strings are guaranteed to be unique and will start with the given prefix.
  std::string UniqueName(const std::string& prefix);

  // Return stats about this state.
  InspectStats GetStats() const;

 private:
  // Holder for a LazyNodeCallbackFn.
  //
  // This class ensures that the callback function is only called once at a time, and it allows
  // future calls to the callback to be cancelled to prevent calling it when the corresponding
  // LazyNode has been deleted.
  //
  // This class is copyable and thread-safe. Each copy refers to the same underlying callback, and
  // cancelling one copy cancels all copies.
  class LazyNodeCallbackHolder {
   public:
    LazyNodeCallbackHolder() = default;
    explicit LazyNodeCallbackHolder(LazyNodeCallbackFn callback)
        : inner_(new Inner(std::move(callback))) {}

    // This class is copyable but not movable. This ensures LazyNodeCallbackHolder objects are
    // always in a valid state.
    LazyNodeCallbackHolder(const LazyNodeCallbackHolder&) = default;
    LazyNodeCallbackHolder(LazyNodeCallbackHolder&&) = delete;
    LazyNodeCallbackHolder& operator=(const LazyNodeCallbackHolder&) = default;
    LazyNodeCallbackHolder& operator=(LazyNodeCallbackHolder&&) = delete;

    // Cancel and release the callback. Future attempts to call the callback will do nothing.
    void cancel() {
      std::lock_guard<std::mutex> lock(inner_->mutex);
      inner_->callback = {};
    }

    // Call the callback if it is not cancelled.
    fpromise::promise<Inspector> call() {
      std::lock_guard<std::mutex> lock(inner_->mutex);
      if (inner_->callback) {
        return inner_->callback();
      } else {
        return fpromise::make_result_promise<Inspector>(fpromise::error());
      }
    }

   private:
    // Inner structure to share a mutex and a callback.
    struct Inner {
      explicit Inner(LazyNodeCallbackFn fn) : callback(std::move(fn)) {}

      std::mutex mutex;
      LazyNodeCallbackFn callback FIT_GUARDED(mutex);
    };
    std::shared_ptr<Inner> inner_;
  };

  State(std::unique_ptr<Heap> heap, BlockIndex header)
      : heap_(std::move(heap)), header_(header), next_unique_id_(0), next_unique_link_number_(0) {}

  void DecrementParentRefcount(BlockIndex value_index) __TA_REQUIRES(mutex_);

  // Helper method for creating a new VALUE block type.
  zx_status_t InnerCreateValue(BorrowedStringValue name, BlockType type, BlockIndex parent_index,
                               BlockIndex* out_name, BlockIndex* out_value,
                               size_t min_size_required = kMinOrderSize) __TA_REQUIRES(mutex_);

  // Helper method to create a new LINK block that calls a callback when followed.
  LazyNode InnerCreateLazyLink(BorrowedStringValue name, BlockIndex parent,
                               LazyNodeCallbackFn callback, LinkBlockDisposition disposition);

  // Returns true if the block is an extent, false otherwise.
  constexpr bool IsExtent(const Block* block) {
    return block && GetType(block) == BlockType::kExtent;
  }

  // Helper to set the value of a string across its extents.
  std::pair<BlockIndex, zx_status_t> InnerCreateExtentChain(const char* value, size_t length)
      __TA_REQUIRES(mutex_);

  // Helper to free all extents for a given first index in the extent chain.
  void InnerFreeExtentChain(BlockIndex extent_index) __TA_REQUIRES(mutex_);

  // Helper to create a new name block with the given name.
  zx_status_t InnerCreateAndIncrementStringReference(BorrowedStringValue name, BlockIndex* out)
      __TA_REQUIRES(mutex_);

  // Helper function to create an array with the given name, number of slots, and format.
  template <typename NumericType, typename WrapperType, BlockType BlockTypeValue>
  WrapperType InnerCreateArray(BorrowedStringValue name, BlockIndex parent, size_t slots,
                               ArrayBlockFormat format);

  // Helper function to create a property with a byte format.
  template <typename WrapperType, typename ValueType>
  WrapperType InnerCreateProperty(BorrowedStringValue name, BlockIndex parent, const char* value,
                                  size_t length, PropertyBlockFormat format);

  template <typename WrapperType>
  void InnerSetProperty(WrapperType* property, const char* value, size_t length);

  // Helper function to delete String or ByteVector properties.
  template <typename WrapperType>
  void InnerFreePropertyWithExtents(WrapperType* property);

  // Helper function to set the value of a specific index in an array.
  template <typename NumericType, typename WrapperType, BlockType BlockTypeValue>
  void InnerSetArray(WrapperType* property, size_t index, NumericType value);

  // Helper function to perform an operation on a specific index in an array.
  // Common operations are std::plus and std::minus.
  template <typename NumericType, typename WrapperType, BlockType BlockTypeValue,
            typename Operation>
  void InnerOperationArray(WrapperType* property, size_t index, NumericType value);

  // Helper function to free an array type.
  template <typename WrapperType>
  void InnerFreeArray(WrapperType* value);

  // Helper function to generate a unique name for a link.
  std::string UniqueLinkName(cpp17::string_view prefix);

  // Create a new |StringReference| in the Inspect VMO, or get the existing index.
  // If the StringReference is new, it will have a reference count of 0. If it already
  // existed, the reference count will not be incremented.
  zx_status_t InnerCreateStringReference(BorrowedStringValue value, BlockIndex* out)
      __TA_REQUIRES(mutex_);

  zx_status_t InnerDoStringReferenceAllocations(cpp17::string_view data, BlockIndex* out)
      __TA_REQUIRES(mutex_);

  // Free the block pointed to by |index| if and only if it has a reference count of 0.
  // Attempting to free a reference that has a non-zero reference count is not an error,
  // and will simply do nothing.
  void InnerMaybeFreeStringReference(BlockIndex index, Block* block) __TA_REQUIRES(mutex_);

  void InnerReleaseStringReference(BlockIndex index) __TA_REQUIRES(mutex_);

  void InnerReadExtents(BlockIndex head_extent, size_t remaining_length,
                        std::vector<uint8_t>* buf) const __TA_REQUIRES(mutex_);

  zx_status_t WriteStringReferencePayload(Block* block, cpp17::string_view data)
      __TA_REQUIRES(mutex_);

  friend cpp17::optional<std::string> TesterLoadStringReference(const State& state,
                                                                BlockIndex index);

  // Mutex wrapping all fields in the state.
  // The mutex is mutable to support locking when reading fields of a
  // const reference to state.
  mutable std::mutex mutex_;

  // Weak pointer reference to this object, used to pass shared pointers to children.
  std::weak_ptr<State> weak_self_ptr_;

  // The wrapped |Heap|, protected by the mutex.
  std::unique_ptr<Heap> heap_ FIT_GUARDED(mutex_);

  // Map from the key of a linked inspect tree to the callback that populates that tree.
  //
  // An ordered map is used to ensure consistent iteration ordering for clients reading this data.
  std::map<std::string, LazyNodeCallbackHolder> link_callbacks_ FIT_GUARDED(mutex_);

  // The index for the header block containing the generation count
  // to increment
  BlockIndex header_ FIT_GUARDED(mutex_);

  // The next unique ID to give out from UniqueName.
  //
  // Uses the fastest available atomic uint64 type for fetch_and_add.
  std::atomic_uint_fast64_t next_unique_id_;

  // Next value to be used as a suffix for links.
  //
  // Uses the fastest available atomic uint64 type for fetch_and_add.
  std::atomic_uint_fast64_t next_unique_link_number_;

  // Map StringReference.ID to an index in the VMO and vice-versa.
  class {
   public:
    void Insert(BlockIndex index, uint64_t id) {
      state_id_to_block_index_.insert({id, index});
      block_index_to_state_id_.insert({index, id});
    }

    cpp17::optional<BlockIndex> GetBlockIndex(uint64_t id) {
      const auto index = state_id_to_block_index_.find(id);
      if (index == std::cend(state_id_to_block_index_)) {
        return {};
      }
      return index->second;
    }

    cpp17::optional<uint64_t> GetStateId(BlockIndex index) {
      const auto state_id = block_index_to_state_id_.find(index);
      if (state_id == std::cend(block_index_to_state_id_)) {
        return {};
      }

      return state_id->second;
    }

    void EraseByIndex(BlockIndex index) {
      const auto id = GetStateId(index);
      if (!id.has_value()) {
        return;
      }

      state_id_to_block_index_.erase(*id);
      block_index_to_state_id_.erase(index);
    }

   private:
    std::unordered_map<uint64_t, BlockIndex> state_id_to_block_index_;
    std::unordered_map<BlockIndex, uint64_t> block_index_to_state_id_;

  } string_reference_ids_;
};

}  // namespace internal
}  // namespace inspect

#endif  // LIB_INSPECT_CPP_VMO_STATE_H_
