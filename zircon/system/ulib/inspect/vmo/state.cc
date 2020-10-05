// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/bridge.h>
#include <lib/fit/sequencer.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/vmo/block.h>
#include <lib/inspect/cpp/vmo/state.h>

#include <functional>
#include <sstream>

namespace inspect {
namespace internal {

namespace {
// Helper class to support RAII locking of the generation count.
class AutoGenerationIncrement final {
 public:
  AutoGenerationIncrement(BlockIndex target, Heap* heap);
  ~AutoGenerationIncrement();

  // Disallow copy assign and move.
  AutoGenerationIncrement(AutoGenerationIncrement&&) = delete;
  AutoGenerationIncrement(const AutoGenerationIncrement&) = delete;
  AutoGenerationIncrement& operator=(AutoGenerationIncrement&&) = delete;
  AutoGenerationIncrement& operator=(const AutoGenerationIncrement&) = delete;

 private:
  // Acquire the generation count lock.
  // This consists of atomically incrementing the count using
  // acquire-release ordering, ensuring readers see this increment before
  // any changes to the buffer.
  void Acquire(Block* block);

  // Release the generation count lock.
  // This consists of atomically incrementing the count using release
  // ordering, ensuring readers see this increment after all changes to
  // the buffer are committed.
  void Release(Block* block);

  BlockIndex target_;
  Heap* heap_;
};

AutoGenerationIncrement ::AutoGenerationIncrement(BlockIndex target, Heap* heap)
    : target_(target), heap_(heap) {
  Acquire(heap_->GetBlock(target_));
}
AutoGenerationIncrement::~AutoGenerationIncrement() { Release(heap_->GetBlock(target_)); }

void AutoGenerationIncrement ::Acquire(Block* block) {
  uint64_t* ptr = &block->payload.u64;
  __atomic_fetch_add(ptr, 1, std::memory_order_acq_rel);
}

void AutoGenerationIncrement::Release(Block* block) {
  uint64_t* ptr = &block->payload.u64;
  __atomic_fetch_add(ptr, 1, std::memory_order_release);
}

}  // namespace

template <typename NumericType, typename WrapperType, BlockType BlockTypeValue>
WrapperType State::InnerCreateArray(const std::string& name, BlockIndex parent, size_t slots,
                                    ArrayBlockFormat format) {
  size_t block_size_needed = slots * sizeof(NumericType) + kMinOrderSize;
  ZX_DEBUG_ASSERT_MSG(block_size_needed <= kMaxOrderSize,
                      "The requested array size cannot fit in a block");
  if (block_size_needed > kMaxOrderSize) {
    return WrapperType();
  }

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  BlockIndex name_index, value_index;
  zx_status_t status;
  status = InnerCreateValue(name, BlockType::kArrayValue, parent, &name_index, &value_index,
                            block_size_needed);
  if (status != ZX_OK) {
    return WrapperType();
  }

  auto* block = heap_->GetBlock(value_index);
  block->payload.u64 = ArrayBlockPayload::EntryType::Make(BlockTypeValue) |
                       ArrayBlockPayload::Flags::Make(format) |
                       ArrayBlockPayload::Count::Make(slots);

  return WrapperType(weak_self_ptr_.lock(), name_index, value_index);
}

template <typename NumericType, typename WrapperType, BlockType BlockTypeValue>
void State::InnerSetArray(WrapperType* metric, size_t index, NumericType value) {
  ZX_ASSERT(metric->state_.get() == this);
  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  auto* block = heap_->GetBlock(metric->value_index_);
  ZX_ASSERT(GetType(block) == BlockType::kArrayValue);
  auto entry_type = ArrayBlockPayload::EntryType::Get<BlockType>(block->payload.u64);
  ZX_ASSERT(entry_type == BlockTypeValue);
  auto* slot = GetArraySlot<NumericType>(block, index);
  if (slot != nullptr) {
    *slot = value;
  }
}

template <typename NumericType, typename WrapperType, BlockType BlockTypeValue, typename Operation>
void State::InnerOperationArray(WrapperType* metric, size_t index, NumericType value) {
  ZX_ASSERT(metric->state_.get() == this);
  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  auto* block = heap_->GetBlock(metric->value_index_);
  ZX_ASSERT(GetType(block) == BlockType::kArrayValue);
  auto entry_type = ArrayBlockPayload::EntryType::Get<BlockType>(block->payload.u64);
  ZX_ASSERT(entry_type == BlockTypeValue);
  auto* slot = GetArraySlot<NumericType>(block, index);
  if (slot != nullptr) {
    *slot = Operation()(*slot, value);
  }
}

template <typename WrapperType>
void State::InnerFreeArray(WrapperType* value) {
  ZX_DEBUG_ASSERT_MSG(value->state_.get() == this, "Array being freed from the wrong state");
  if (value->state_.get() != this) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  DecrementParentRefcount(value->value_index_);

  heap_->Free(value->name_index_);
  heap_->Free(value->value_index_);
  value->state_ = nullptr;
}

std::shared_ptr<State> State::Create(std::unique_ptr<Heap> heap) {
  BlockIndex header;
  if (heap->Allocate(sizeof(Block), &header) != ZX_OK) {
    return nullptr;
  }

  ZX_DEBUG_ASSERT_MSG(header == 0, "Header must be at index 0");
  if (header != 0) {
    return nullptr;
  }

  auto* block = heap->GetBlock(header);
  block->header = HeaderBlockFields::Order::Make(GetOrder(block)) |
                  HeaderBlockFields::Type::Make(BlockType::kHeader) |
                  HeaderBlockFields::Version::Make(kVersion);
  memcpy(&block->header_data[4], kMagicNumber, 4);
  block->payload.u64 = 0;

  std::shared_ptr<State> ret(new State(std::move(heap), header));
  ret->weak_self_ptr_ = ret;
  return ret;
}

std::shared_ptr<State> State::CreateWithSize(size_t size) {
  zx::vmo vmo;
  if (size == 0 || ZX_OK != zx::vmo::create(size, 0, &vmo)) {
    return nullptr;
  }
  static const char kName[] = "InspectHeap";
  vmo.set_property(ZX_PROP_NAME, kName, strlen(kName));
  return State::Create(std::make_unique<Heap>(std::move(vmo)));
}

State::~State() { heap_->Free(header_); }

const zx::vmo& State::GetVmo() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return heap_->GetVmo();
}

bool State::DuplicateVmo(zx::vmo* vmo) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ZX_OK == heap_->GetVmo().duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP, vmo);
}

bool State::Copy(zx::vmo* vmo) const {
  std::lock_guard<std::mutex> lock(mutex_);

  size_t size = heap_->size();
  if (zx::vmo::create(size, 0, vmo) != ZX_OK) {
    return false;
  }

  if (vmo->write(heap_->data(), 0, size) != ZX_OK) {
    return false;
  }

  return true;
}

bool State::CopyBytes(std::vector<uint8_t>* out) const {
  std::lock_guard<std::mutex> lock(mutex_);

  size_t size = heap_->size();
  if (size == 0) {
    return false;
  }

  out->resize(size);
  memcpy(out->data(), heap_->data(), size);

  return true;
}

IntProperty State::CreateIntProperty(const std::string& name, BlockIndex parent, int64_t value) {
  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  BlockIndex name_index, value_index;
  zx_status_t status;
  status = InnerCreateValue(name, BlockType::kIntValue, parent, &name_index, &value_index);
  if (status != ZX_OK) {
    return IntProperty();
  }

  auto* block = heap_->GetBlock(value_index);
  block->payload.i64 = value;

  return IntProperty(weak_self_ptr_.lock(), name_index, value_index);
}

UintProperty State::CreateUintProperty(const std::string& name, BlockIndex parent, uint64_t value) {
  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  BlockIndex name_index, value_index;
  zx_status_t status;
  status = InnerCreateValue(name, BlockType::kUintValue, parent, &name_index, &value_index);
  if (status != ZX_OK) {
    return UintProperty();
  }

  auto* block = heap_->GetBlock(value_index);
  block->payload.u64 = value;

  return UintProperty(weak_self_ptr_.lock(), name_index, value_index);
}

DoubleProperty State::CreateDoubleProperty(const std::string& name, BlockIndex parent,
                                           double value) {
  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  BlockIndex name_index, value_index;
  zx_status_t status;
  status = InnerCreateValue(name, BlockType::kDoubleValue, parent, &name_index, &value_index);
  if (status != ZX_OK) {
    return DoubleProperty();
  }

  auto* block = heap_->GetBlock(value_index);
  block->payload.f64 = value;

  return DoubleProperty(weak_self_ptr_.lock(), name_index, value_index);
}

BoolProperty State::CreateBoolProperty(const std::string& name, BlockIndex parent, bool value) {
  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  BlockIndex name_index, value_index;
  zx_status_t status;
  status = InnerCreateValue(name, BlockType::kBoolValue, parent, &name_index, &value_index);
  if (status != ZX_OK) {
    return BoolProperty();
  }

  auto* block = heap_->GetBlock(value_index);
  block->payload.u64 = value;
  return BoolProperty(weak_self_ptr_.lock(), name_index, value_index);
}

IntArray State::CreateIntArray(const std::string& name, BlockIndex parent, size_t slots,
                               ArrayBlockFormat format) {
  return InnerCreateArray<int64_t, IntArray, BlockType::kIntValue>(name, parent, slots, format);
}

UintArray State::CreateUintArray(const std::string& name, BlockIndex parent, size_t slots,
                                 ArrayBlockFormat format) {
  return InnerCreateArray<uint64_t, UintArray, BlockType::kUintValue>(name, parent, slots, format);
}

DoubleArray State::CreateDoubleArray(const std::string& name, BlockIndex parent, size_t slots,
                                     ArrayBlockFormat format) {
  return InnerCreateArray<double, DoubleArray, BlockType::kDoubleValue>(name, parent, slots,
                                                                        format);
}

template <typename WrapperType, typename ValueType>
WrapperType State::InnerCreateProperty(const std::string& name, BlockIndex parent,
                                       const char* value, size_t length,
                                       PropertyBlockFormat format) {
  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  BlockIndex name_index, value_index;
  zx_status_t status;
  status = InnerCreateValue(name, BlockType::kBufferValue, parent, &name_index, &value_index);
  if (status != ZX_OK) {
    return WrapperType();
  }
  auto* block = heap_->GetBlock(value_index);
  block->payload.u64 = PropertyBlockPayload::Flags::Make(format);
  status = InnerSetStringExtents(value_index, value, length);
  if (status != ZX_OK) {
    heap_->Free(name_index);
    heap_->Free(value_index);
    return WrapperType();
  }

  return WrapperType(weak_self_ptr_.lock(), name_index, value_index);
}

StringProperty State::CreateStringProperty(const std::string& name, BlockIndex parent,
                                           const std::string& value) {
  return InnerCreateProperty<StringProperty, std::string>(
      name, parent, value.data(), value.length(), PropertyBlockFormat::kUtf8);
}

ByteVectorProperty State::CreateByteVectorProperty(const std::string& name, BlockIndex parent,
                                                   const std::vector<uint8_t>& value) {
  return InnerCreateProperty<ByteVectorProperty, std::vector<uint8_t>>(
      name, parent, reinterpret_cast<const char*>(value.data()), value.size(),
      PropertyBlockFormat::kBinary);
}

Link State::CreateLink(const std::string& name, BlockIndex parent, const std::string& content,
                       LinkBlockDisposition disposition) {
  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  BlockIndex name_index, value_index, content_index;
  zx_status_t status;
  status = InnerCreateValue(name, BlockType::kLinkValue, parent, &name_index, &value_index);
  if (status != ZX_OK) {
    return Link();
  }

  status = CreateName(content, &content_index);
  if (status != ZX_OK) {
    DecrementParentRefcount(value_index);
    heap_->Free(name_index);
    heap_->Free(value_index);
    return Link();
  }

  auto* block = heap_->GetBlock(value_index);

  block->payload.u64 = LinkBlockPayload::ContentIndex::Make(content_index) |
                       LinkBlockPayload::Flags::Make(disposition);

  return Link(weak_self_ptr_.lock(), name_index, value_index, content_index);
}

Node State::CreateRootNode() {
  std::lock_guard<std::mutex> lock(mutex_);
  return Node(weak_self_ptr_.lock(), 0, 0);
}

LazyNode State::InnerCreateLazyLink(const std::string& name, BlockIndex parent,
                                    LazyNodeCallbackFn callback, LinkBlockDisposition disposition) {
  auto content = UniqueLinkName(name);
  auto link = CreateLink(name, parent, content, disposition);

  {
    std::lock_guard<std::mutex> lock(mutex_);

    link_callbacks_.emplace(content, LazyNodeCallbackHolder(std::move(callback)));

    return LazyNode(weak_self_ptr_.lock(), std::move(content), std::move(link));
  }
}

LazyNode State::CreateLazyNode(const std::string& name, BlockIndex parent,
                               LazyNodeCallbackFn callback) {
  return InnerCreateLazyLink(name, parent, std::move(callback), LinkBlockDisposition::kChild);
}

LazyNode State::CreateLazyValues(const std::string& name, BlockIndex parent,
                                 LazyNodeCallbackFn callback) {
  return InnerCreateLazyLink(name, parent, std::move(callback), LinkBlockDisposition::kInline);
}

Node State::CreateNode(const std::string& name, BlockIndex parent) {
  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  BlockIndex name_index, value_index;
  zx_status_t status;
  status = InnerCreateValue(name, BlockType::kNodeValue, parent, &name_index, &value_index);
  if (status != ZX_OK) {
    return Node();
  }

  return Node(weak_self_ptr_.lock(), name_index, value_index);
}

void State::SetIntProperty(IntProperty* metric, int64_t value) {
  ZX_ASSERT(metric->state_.get() == this);
  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  auto* block = heap_->GetBlock(metric->value_index_);
  ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kIntValue, "Expected int metric, got %d",
                      static_cast<int>(GetType(block)));
  block->payload.i64 = value;
}

void State::SetUintProperty(UintProperty* metric, uint64_t value) {
  ZX_ASSERT(metric->state_.get() == this);

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  auto* block = heap_->GetBlock(metric->value_index_);
  ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kUintValue, "Expected uint metric, got %d",
                      static_cast<int>(GetType(block)));
  block->payload.u64 = value;
}

void State::SetDoubleProperty(DoubleProperty* metric, double value) {
  ZX_ASSERT(metric->state_.get() == this);

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  auto* block = heap_->GetBlock(metric->value_index_);
  ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kDoubleValue, "Expected double metric, got %d",
                      static_cast<int>(GetType(block)));
  block->payload.f64 = value;
}

void State::SetBoolProperty(BoolProperty* metric, bool value) {
  ZX_ASSERT(metric->state_.get() == this);
  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  auto* block = heap_->GetBlock(metric->value_index_);
  ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kBoolValue, "Expected bool metric, got %d",
                      static_cast<int>(GetType(block)));
  block->payload.u64 = value;
}

void State::SetIntArray(IntArray* array, size_t index, int64_t value) {
  InnerSetArray<int64_t, IntArray, BlockType::kIntValue>(array, index, value);
}

void State::SetUintArray(UintArray* array, size_t index, uint64_t value) {
  InnerSetArray<uint64_t, UintArray, BlockType::kUintValue>(array, index, value);
}

void State::SetDoubleArray(DoubleArray* array, size_t index, double value) {
  InnerSetArray<double, DoubleArray, BlockType::kDoubleValue>(array, index, value);
}

void State::AddIntProperty(IntProperty* metric, int64_t value) {
  ZX_ASSERT(metric->state_.get() == this);

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  auto* block = heap_->GetBlock(metric->value_index_);
  ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kIntValue, "Expected int metric, got %d",
                      static_cast<int>(GetType(block)));
  block->payload.i64 += value;
}

void State::AddUintProperty(UintProperty* metric, uint64_t value) {
  ZX_ASSERT(metric->state_.get() == this);

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  auto* block = heap_->GetBlock(metric->value_index_);
  ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kUintValue, "Expected uint metric, got %d",
                      static_cast<int>(GetType(block)));
  block->payload.u64 += value;
}

void State::AddDoubleProperty(DoubleProperty* metric, double value) {
  ZX_ASSERT(metric->state_.get() == this);

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  auto* block = heap_->GetBlock(metric->value_index_);
  ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kDoubleValue, "Expected double metric, got %d",
                      static_cast<int>(GetType(block)));
  block->payload.f64 += value;
}

void State::SubtractIntProperty(IntProperty* metric, int64_t value) {
  ZX_ASSERT(metric->state_.get() == this);

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  auto* block = heap_->GetBlock(metric->value_index_);
  ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kIntValue, "Expected int metric, got %d",
                      static_cast<int>(GetType(block)));
  block->payload.i64 -= value;
}

void State::SubtractUintProperty(UintProperty* metric, uint64_t value) {
  ZX_ASSERT(metric->state_.get() == this);

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  auto* block = heap_->GetBlock(metric->value_index_);
  ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kUintValue, "Expected uint metric, got %d",
                      static_cast<int>(GetType(block)));
  block->payload.u64 -= value;
}

void State::SubtractDoubleProperty(DoubleProperty* metric, double value) {
  ZX_ASSERT(metric->state_.get() == this);

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  auto* block = heap_->GetBlock(metric->value_index_);
  ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kDoubleValue, "Expected double metric, got %d",
                      static_cast<int>(GetType(block)));
  block->payload.f64 -= value;
}

void State::AddIntArray(IntArray* array, size_t index, int64_t value) {
  InnerOperationArray<int64_t, IntArray, BlockType::kIntValue, std::plus<int64_t>>(array, index,
                                                                                   value);
}

void State::SubtractIntArray(IntArray* array, size_t index, int64_t value) {
  InnerOperationArray<int64_t, IntArray, BlockType::kIntValue, std::minus<int64_t>>(array, index,
                                                                                    value);
}

void State::AddUintArray(UintArray* array, size_t index, uint64_t value) {
  InnerOperationArray<uint64_t, UintArray, BlockType::kUintValue, std::plus<uint64_t>>(array, index,
                                                                                       value);
}

void State::SubtractUintArray(UintArray* array, size_t index, uint64_t value) {
  InnerOperationArray<uint64_t, UintArray, BlockType::kUintValue, std::minus<uint64_t>>(
      array, index, value);
}

void State::AddDoubleArray(DoubleArray* array, size_t index, double value) {
  InnerOperationArray<double, DoubleArray, BlockType::kDoubleValue, std::plus<double>>(array, index,
                                                                                       value);
}

void State::SubtractDoubleArray(DoubleArray* array, size_t index, double value) {
  InnerOperationArray<double, DoubleArray, BlockType::kDoubleValue, std::minus<double>>(
      array, index, value);
}

template <typename WrapperType>
void State::InnerSetProperty(WrapperType* property, const char* value, size_t length) {
  ZX_ASSERT(property->state_.get() == this);

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  InnerSetStringExtents(property->value_index_, value, length);
}

void State::SetStringProperty(StringProperty* property, const std::string& value) {
  InnerSetProperty(property, value.data(), value.size());
}

void State::SetByteVectorProperty(ByteVectorProperty* property, const std::vector<uint8_t>& value) {
  InnerSetProperty(property, reinterpret_cast<const char*>(value.data()), value.size());
}

void State::DecrementParentRefcount(BlockIndex value_index) {
  Block* value = heap_->GetBlock(value_index);
  ZX_ASSERT(value);

  BlockIndex parent_index = ValueBlockFields::ParentIndex::Get<BlockIndex>(value->header);
  Block* parent;
  while ((parent = heap_->GetBlock(parent_index)) != nullptr) {
    ZX_ASSERT(parent);
    switch (GetType(parent)) {
      case BlockType::kHeader:
        return;
      case BlockType::kNodeValue:
        // Stop decrementing parent refcounts when we observe a live object.
        ZX_ASSERT(parent->payload.u64 != 0);
        --parent->payload.u64;
        return;
      case BlockType::kTombstone:
        ZX_ASSERT(parent->payload.u64 != 0);
        if (--parent->payload.u64 == 0) {
          // The tombstone parent is no longer referenced and can be deleted.
          // Continue decrementing refcounts.
          BlockIndex next_parent_index =
              ValueBlockFields::ParentIndex::Get<BlockIndex>(parent->header);
          heap_->Free(ValueBlockFields::NameIndex::Get<BlockIndex>(parent->header));
          heap_->Free(parent_index);
          parent_index = next_parent_index;
          break;
        }
        // The tombstone parent is still referenced. Done decrementing refcounts.
        return;
      default:
        ZX_DEBUG_ASSERT_MSG(false, "Invalid parent type %u",
                            static_cast<uint32_t>(GetType(parent)));
        return;
    }
  }
}

void State::FreeIntProperty(IntProperty* metric) {
  ZX_DEBUG_ASSERT_MSG(metric->state_.get() == this, "Property being freed from the wrong state");
  if (metric->state_.get() != this) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  DecrementParentRefcount(metric->value_index_);

  heap_->Free(metric->name_index_);
  heap_->Free(metric->value_index_);
  metric->state_ = nullptr;
}

void State::FreeUintProperty(UintProperty* metric) {
  ZX_DEBUG_ASSERT_MSG(metric->state_.get() == this, "Property being freed from the wrong state");
  if (metric->state_.get() != this) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  DecrementParentRefcount(metric->value_index_);

  heap_->Free(metric->name_index_);
  heap_->Free(metric->value_index_);
  metric->state_ = nullptr;
}

void State::FreeDoubleProperty(DoubleProperty* metric) {
  ZX_DEBUG_ASSERT_MSG(metric->state_.get() == this, "Property being freed from the wrong state");
  if (metric->state_.get() != this) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  DecrementParentRefcount(metric->value_index_);

  heap_->Free(metric->name_index_);
  heap_->Free(metric->value_index_);
  metric->state_ = nullptr;
}

void State::FreeBoolProperty(BoolProperty* metric) {
  ZX_DEBUG_ASSERT_MSG(metric->state_.get() == this, "Property being freed from wrong state");
  if (metric->state_.get() != this) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  DecrementParentRefcount(metric->value_index_);

  heap_->Free(metric->name_index_);
  heap_->Free(metric->value_index_);
  metric->state_ = nullptr;
}

void State::FreeIntArray(IntArray* array) { InnerFreeArray<IntArray>(array); }

void State::FreeUintArray(UintArray* array) { InnerFreeArray<UintArray>(array); }

void State::FreeDoubleArray(DoubleArray* array) { InnerFreeArray<DoubleArray>(array); }

template <typename WrapperType>
void State::InnerFreePropertyWithExtents(WrapperType* property) {
  ZX_DEBUG_ASSERT_MSG(property->state_.get() == this, "Property being freed from the wrong state");
  if (property->state_.get() != this) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  DecrementParentRefcount(property->value_index_);

  InnerFreeStringExtents(property->value_index_);

  heap_->Free(property->name_index_);
  heap_->Free(property->value_index_);
  property->state_ = nullptr;
}

void State::FreeStringProperty(StringProperty* property) { InnerFreePropertyWithExtents(property); }

void State::FreeByteVectorProperty(ByteVectorProperty* property) {
  InnerFreePropertyWithExtents(property);
}

void State::FreeLink(Link* link) {
  ZX_DEBUG_ASSERT_MSG(link->state_.get() == this, "Link being freed from the wrong state");
  if (link->state_.get() != this) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  DecrementParentRefcount(link->value_index_);

  heap_->Free(link->name_index_);
  heap_->Free(link->value_index_);
  heap_->Free(link->content_index_);
  link->state_ = nullptr;
}

void State::FreeNode(Node* object) {
  ZX_DEBUG_ASSERT_MSG(object->state_.get() == this, "Node being freed from the wrong state");
  if (object->state_.get() != this) {
    return;
  }

  if (object->value_index_ == 0) {
    // This is a special "root" node, it cannot be deleted.
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  auto* block = heap_->GetBlock(object->value_index_);
  if (block) {
    if (block->payload.u64 == 0) {
      // Actually free the block, decrementing parent refcounts.
      DecrementParentRefcount(object->value_index_);
      // Node has no refs, free it.
      heap_->Free(object->name_index_);
      heap_->Free(object->value_index_);
    } else {
      // Node has refs, change type to tombstone so it can be removed
      // when the last ref is gone.
      ValueBlockFields::Type::Set(&block->header, static_cast<uint64_t>(BlockType::kTombstone));
    }
  }
}

void State::FreeLazyNode(LazyNode* object) {
  ZX_DEBUG_ASSERT_MSG(object->state_.get() == this, "Node being freed from the wrong state");
  if (object->state_.get() != this) {
    return;
  }

  // Free the contained link, which removes the reference to the value in the map.
  FreeLink(&object->link_);

  LazyNodeCallbackHolder holder;

  {
    // Separately lock the current state, and remove the callback for this lazy node.
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = link_callbacks_.find(object->content_value_);
    if (it != link_callbacks_.end()) {
      holder = it->second;
      link_callbacks_.erase(it);
    }
    object->state_ = nullptr;
  }

  // Cancel the Holder without State locked. This avoids a deadlock in which we could be locking the
  // holder with the state lock held, meanwhile the callback itself is modifying state (with holder
  // locked).
  //
  // At this point in time, the LazyNode is still *live* and the callback may be getting executed.
  // Following this cancel call, the LazyNode is no longer live and the callback will never be
  // called again.
  holder.cancel();
}

std::vector<std::string> State::GetLinkNames() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> ret;
  for (const auto& entry : link_callbacks_) {
    ret.push_back(entry.first);
  }
  return ret;
}

fit::promise<Inspector> State::CallLinkCallback(const std::string& name) {
  LazyNodeCallbackHolder holder;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = link_callbacks_.find(name);
    if (it == link_callbacks_.end()) {
      return fit::make_result_promise<Inspector>(fit::error());
    }
    // Copy out the holder.
    holder = it->second;
  }

  // Call the callback.
  // This occurs without state locked, but deletion of the LazyNode synchronizes on the internal
  // mutex in the Holder. If the LazyNode is deleted before this call, the callback will not be
  // executed. If the LazyNode is being deleted concurrent with this call, it will be delayed until
  // after the callback returns.
  return holder.call();
}

zx_status_t State::InnerCreateValue(const std::string& name, BlockType type,
                                    BlockIndex parent_index, BlockIndex* out_name,
                                    BlockIndex* out_value, size_t min_size_required) {
  BlockIndex value_index, name_index;
  zx_status_t status;
  status = heap_->Allocate(min_size_required, &value_index);
  if (status != ZX_OK) {
    return status;
  }

  status = CreateName(name, &name_index);
  if (status != ZX_OK) {
    heap_->Free(value_index);
    return status;
  }

  auto* block = heap_->GetBlock(value_index);
  block->header = ValueBlockFields::Order::Make(GetOrder(block)) |
                  ValueBlockFields::Type::Make(type) |
                  ValueBlockFields::ParentIndex::Make(parent_index) |
                  ValueBlockFields::NameIndex::Make(name_index);
  memset(&block->payload, 0, min_size_required - sizeof(block->header));

  // Increment the parent refcount.
  Block* parent = heap_->GetBlock(parent_index);
  ZX_DEBUG_ASSERT_MSG(parent, "Index %lu is invalid", parent_index);
  // In release mode, do cleanup if parent is invalid.
  BlockType parent_type = (parent) ? GetType(parent) : BlockType::kFree;
  switch (parent_type) {
    case BlockType::kHeader:
      break;
    case BlockType::kNodeValue:
    case BlockType::kTombstone:
      // Increment refcount.
      parent->payload.u64++;
      break;
    default:
      ZX_DEBUG_ASSERT_MSG(false, "Invalid parent block type %u for 0x%lx",
                          static_cast<uint32_t>(GetType(parent)), parent_index);
      heap_->Free(name_index);
      heap_->Free(value_index);
      return ZX_ERR_INVALID_ARGS;
  }

  *out_name = name_index;
  *out_value = value_index;
  return ZX_OK;
}

void State::InnerFreeStringExtents(BlockIndex string_index) {
  auto* str = heap_->GetBlock(string_index);
  if (!str || GetType(str) != BlockType::kBufferValue) {
    return;
  }

  auto extent_index = PropertyBlockPayload::ExtentIndex::Get<BlockIndex>(str->payload.u64);
  auto* extent = heap_->GetBlock(extent_index);
  while (IsExtent(extent)) {
    auto next_extent = ExtentBlockFields::NextExtentIndex::Get<BlockIndex>(extent->header);
    heap_->Free(extent_index);
    extent_index = next_extent;
    extent = heap_->GetBlock(extent_index);
  }

  // Leave the string value allocated (and empty).
  str->payload.u64 = PropertyBlockPayload::TotalLength::Make(0) |
                     PropertyBlockPayload::ExtentIndex::Make(0) |
                     PropertyBlockPayload::Flags::Make(
                         PropertyBlockPayload::Flags::Get<uint8_t>(str->payload.u64));
}

zx_status_t State::InnerSetStringExtents(BlockIndex string_index, const char* value,
                                         size_t length) {
  InnerFreeStringExtents(string_index);

  auto* block = heap_->GetBlock(string_index);

  if (length == 0) {
    // The extent index is 0 if no extents were needed (the value is empty).
    block->payload.u64 = PropertyBlockPayload::TotalLength::Make(length) |
                         PropertyBlockPayload::ExtentIndex::Make(0) |
                         PropertyBlockPayload::Flags::Make(
                             PropertyBlockPayload::Flags::Get<uint8_t>(block->payload.u64));
    return ZX_OK;
  }

  BlockIndex extent_index;
  zx_status_t status;
  status = heap_->Allocate(std::min(kMaxOrderSize, BlockSizeForPayload(length)), &extent_index);
  if (status != ZX_OK) {
    return status;
  }

  block->payload.u64 = PropertyBlockPayload::TotalLength::Make(length) |
                       PropertyBlockPayload::ExtentIndex::Make(extent_index) |
                       PropertyBlockPayload::Flags::Make(
                           PropertyBlockPayload::Flags::Get<uint8_t>(block->payload.u64));

  // Thread the value through extents, creating new extents as needed.
  size_t offset = 0;
  while (offset < length) {
    auto* extent = heap_->GetBlock(extent_index);

    extent->header = ExtentBlockFields::Order::Make(GetOrder(extent)) |
                     ExtentBlockFields::Type::Make(BlockType::kExtent) |
                     ExtentBlockFields::NextExtentIndex::Make(0);

    size_t len = std::min(PayloadCapacity(GetOrder(extent)), length - offset);
    memcpy(extent->payload.data, value + offset, len);
    offset += len;

    if (offset < length) {
      status = heap_->Allocate(std::min(kMaxOrderSize, BlockSizeForPayload(length - offset)),
                               &extent_index);
      if (status != ZX_OK) {
        InnerFreeStringExtents(string_index);
        return status;
      }
      ExtentBlockFields::NextExtentIndex::Set(&extent->header, extent_index);
    }
  }

  return ZX_OK;
}

std::string State::UniqueLinkName(const std::string& prefix) {
  return prefix + "-" +
         std::to_string(next_unique_link_number_.fetch_add(1, std::memory_order_relaxed));
}

zx_status_t State::CreateName(const std::string& name, BlockIndex* out) {
  ZX_DEBUG_ASSERT_MSG(name.size() <= kMaxPayloadSize, "Name too long (length is %lu)", name.size());
  if (name.size() > kMaxPayloadSize) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status;
  status = heap_->Allocate(BlockSizeForPayload(name.size()), out);
  if (status != ZX_OK) {
    return status;
  }

  auto* block = heap_->GetBlock(*out);
  block->header = NameBlockFields::Order::Make(GetOrder(block)) |
                  NameBlockFields::Type::Make(BlockType::kName) |
                  NameBlockFields::Length::Make(name.size());
  memset(block->payload.data, 0, PayloadCapacity(GetOrder(block)));
  memcpy(block->payload.data, name.data(), name.size());
  return ZX_OK;
}

std::string State::UniqueName(const std::string& prefix) {
  std::ostringstream out;
  uint64_t value = next_unique_id_.fetch_add(1, std::memory_order_relaxed);
  out << prefix << "0x" << std::hex << value;
  return out.str();
}

InspectStats State::GetStats() const {
  InspectStats ret = {};
  std::lock_guard<std::mutex> lock(mutex_);

  ret.dynamic_child_count = link_callbacks_.size();
  ret.maximum_size = heap_->maximum_size();
  ret.size = heap_->size();
  return ret;
}

}  // namespace internal
}  // namespace inspect
