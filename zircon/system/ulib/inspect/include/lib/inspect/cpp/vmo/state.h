// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CPP_VMO_STATE_H_
#define LIB_INSPECT_CPP_VMO_STATE_H_

#include <lib/fit/function.h>
#include <lib/inspect/cpp/vmo/block.h>
#include <lib/inspect/cpp/vmo/heap.h>
#include <lib/inspect/cpp/vmo/types.h>

#include <mutex>

namespace inspect {
namespace internal {

// |State| wraps a |Heap| and implements the Inspect File API on top of
// that heap. This class contains the low-level operations necessary to
// deal with the various Inspect types and wrappers to denote ownership of
// those values.
//
// This class should not be used directly, prefer to use |Inspector|.
class State final {
 public:
  // Create a new State wrapping the given Heap.
  // On failure, returns nullptr.
  static std::shared_ptr<State> Create(std::unique_ptr<Heap> heap);

  // Create a new State wrapping a new heap of the given size.
  // On failure, returns an empty shared_ptr.
  static std::shared_ptr<State> CreateWithSize(size_t size);

  ~State();

  // Disallow copy and assign.
  State(const State&) = delete;
  State(State&&) = delete;
  State& operator=(const State&) = delete;
  State& operator=(State&&) = delete;

  // Obtain a reference to the wrapped VMO.
  // This may be duplicated read-only to pass to a reader process.
  const zx::vmo& GetVmo() const;

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
  IntProperty CreateIntProperty(const std::string& name, BlockIndex parent, int64_t value);

  // Create a new |UintProperty| in the Inspect VMO. The returned value releases
  // the property when destroyed.
  UintProperty CreateUintProperty(const std::string& name, BlockIndex parent, uint64_t value);

  // Create a new |DoubleProperty| in the Inspect VMO. The returned value releases
  // the property when destroyed.
  DoubleProperty CreateDoubleProperty(const std::string& name, BlockIndex parent, double value);

  // Create a new |IntArray| in the Inspect VMO. The returned value releases
  // the array when destroyed.
  IntArray CreateIntArray(const std::string& name, BlockIndex parent, size_t slots,
                          ArrayBlockFormat format);

  // Create a new |UintArray| in the Inspect VMO. The returned value releases
  // the array when destroyed.
  UintArray CreateUintArray(const std::string& name, BlockIndex parent, size_t slots,
                            ArrayBlockFormat format);

  // Create a new |DoubleArray| in the Inspect VMO. The returned value releases
  // the array when destroyed.
  DoubleArray CreateDoubleArray(const std::string& name, BlockIndex parent, size_t slots,
                                ArrayBlockFormat format);

  // Create a new |StringProperty| in the Inspect VMO. The returned value releases
  // the property when destroyed.
  StringProperty CreateStringProperty(const std::string& name, BlockIndex parent,
                                      const std::string& value);

  // Create a new |ByteVectorProperty| in the Inspect VMO. The returned value releases
  // the property when destroyed.
  ByteVectorProperty CreateByteVectorProperty(const std::string& name, BlockIndex parent,
                                              const std::vector<uint8_t>& value);

  // Create a new [Link] in the Inspect VMO. The returned value releases the link when destroyed.
  Link CreateLink(const std::string& name, BlockIndex parent, const std::string& content,
                  LinkBlockDisposition disposition);

  // Create a new |Node| in the Inspect VMO. Nodes are refcounted such that values nested under the
  // node remain valid until all such values values are destroyed.
  Node CreateNode(const std::string& name, BlockIndex parent);

  // Create a special root |Node| in the Inspect VMO. This node is not backed by any storage, rather
  // it allows clients to use the |Node| iterface to add properties and children directly to the
  // root of the VMO.
  Node CreateRootNode();

  // Setters for various property types
  void SetIntProperty(IntProperty* property, int64_t value);
  void SetUintProperty(UintProperty* property, uint64_t value);
  void SetDoubleProperty(DoubleProperty* property, double value);
  void SetIntArray(IntArray* array, size_t index, int64_t value);
  void SetUintArray(UintArray* array, size_t index, uint64_t value);
  void SetDoubleArray(DoubleArray* array, size_t index, double value);
  void SetStringProperty(StringProperty* property, const std::string& value);
  void SetByteVectorProperty(ByteVectorProperty* property, const std::vector<uint8_t>& value);

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
  void FreeIntArray(IntArray* array);
  void FreeUintArray(UintArray* array);
  void FreeDoubleArray(DoubleArray* array);
  void FreeStringProperty(StringProperty* property);
  void FreeByteVectorProperty(ByteVectorProperty* property);
  void FreeLink(Link* link);
  void FreeNode(Node* node);

  // Create a unique name for children in this State.
  //
  // Returned strings are guaranteed to be unique and will start with the given prefix.
  std::string UniqueName(const std::string& prefix);

 private:
  State(std::unique_ptr<Heap> heap, BlockIndex header)
      : heap_(std::move(heap)), header_(header), next_unique_id_(0) {}

  void DecrementParentRefcount(BlockIndex value_index) __TA_REQUIRES(mutex_);

  // Helper method for creating a new VALUE block type.
  zx_status_t InnerCreateValue(const std::string& name, BlockType type, BlockIndex parent_index,
                               BlockIndex* out_name, BlockIndex* out_value,
                               size_t min_size_required = kMinOrderSize) __TA_REQUIRES(mutex_);

  // Returns true if the block is an extent, false otherwise.
  constexpr bool IsExtent(const Block* block) {
    return block && GetType(block) == BlockType::kExtent;
  }

  // Helper to set the value of a string across its extents.
  zx_status_t InnerSetStringExtents(BlockIndex string_index, const char* value, size_t length)
      __TA_REQUIRES(mutex_);

  // Helper to free all extents for a given string.
  // This leaves the string value allocated and empty.
  void InnerFreeStringExtents(BlockIndex string_index) __TA_REQUIRES(mutex_);

  // Helper to create a new name block with the given name.
  zx_status_t CreateName(const std::string& name, BlockIndex* out) __TA_REQUIRES(mutex_);

  // Helper function to create an array with the given name, number of slots, and format.
  template <typename NumericType, typename WrapperType, BlockType BlockTypeValue>
  WrapperType InnerCreateArray(const std::string& name, BlockIndex parent, size_t slots,
                               ArrayBlockFormat format);

  // Helper function to create a property with a byte format.
  template <typename WrapperType, typename ValueType>
  WrapperType InnerCreateProperty(const std::string& name, BlockIndex parent, const char* value,
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

  // Mutex wrapping all fields in the state.
  mutable std::mutex mutex_;

  // Weak pointer reference to this object, used to pass shared pointers to children.
  std::weak_ptr<State> weak_self_ptr_;

  // The wrapped |Heap|, protected by the mutex.
  std::unique_ptr<Heap> heap_ __TA_GUARDED(mutex_);

  // The index for the header block containing the generation count
  // to increment
  BlockIndex header_ __TA_GUARDED(mutex_);

  // The next unique ID to give out from UniqueName.
  std::atomic_uint_fast64_t next_unique_id_;
};

}  // namespace internal
}  // namespace inspect

#endif  // LIB_INSPECT_CPP_VMO_STATE_H_
