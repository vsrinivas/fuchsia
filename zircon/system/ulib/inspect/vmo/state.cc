// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fpromise/bridge.h>
#include <lib/fpromise/sequencer.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/vmo/block.h>
#include <lib/inspect/cpp/vmo/state.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/stdcompat/optional.h>
#include <lib/stdcompat/string_view.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

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
  __atomic_fetch_add(ptr, 1, static_cast<int>(std::memory_order_acq_rel));
}

void AutoGenerationIncrement::Release(Block* block) {
  uint64_t* ptr = &block->payload.u64;
  __atomic_fetch_add(ptr, 1, static_cast<int>(std::memory_order_release));
}

}  // namespace

template <typename NumericType, typename WrapperType, BlockType BlockTypeValue>
WrapperType State::InnerCreateArray(BorrowedStringValue name, BlockIndex parent, size_t slots,
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

  InnerReleaseStringReference(value->name_index_);
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

IntProperty State::CreateIntProperty(BorrowedStringValue name, BlockIndex parent, int64_t value) {
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

UintProperty State::CreateUintProperty(BorrowedStringValue name, BlockIndex parent,
                                       uint64_t value) {
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

DoubleProperty State::CreateDoubleProperty(BorrowedStringValue name, BlockIndex parent,
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

BoolProperty State::CreateBoolProperty(BorrowedStringValue name, BlockIndex parent, bool value) {
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

IntArray State::CreateIntArray(BorrowedStringValue name, BlockIndex parent, size_t slots,
                               ArrayBlockFormat format) {
  return InnerCreateArray<int64_t, IntArray, BlockType::kIntValue>(name, parent, slots, format);
}

UintArray State::CreateUintArray(BorrowedStringValue name, BlockIndex parent, size_t slots,
                                 ArrayBlockFormat format) {
  return InnerCreateArray<uint64_t, UintArray, BlockType::kUintValue>(name, parent, slots, format);
}

DoubleArray State::CreateDoubleArray(BorrowedStringValue name, BlockIndex parent, size_t slots,
                                     ArrayBlockFormat format) {
  return InnerCreateArray<double, DoubleArray, BlockType::kDoubleValue>(name, parent, slots,
                                                                        format);
}

template <typename WrapperType, typename ValueType>
WrapperType State::InnerCreateProperty(BorrowedStringValue name, BlockIndex parent,
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
  BlockIndex first_extent_index;
  std::tie(first_extent_index, status) = InnerCreateExtentChain(value, length);
  block->payload.u64 = PropertyBlockPayload::TotalLength::Make(length) |
                       PropertyBlockPayload::ExtentIndex::Make(first_extent_index) |
                       PropertyBlockPayload::Flags::Make(format);

  if (status != ZX_OK) {
    InnerReleaseStringReference(name_index);
    heap_->Free(value_index);
    return WrapperType();
  }

  return WrapperType(weak_self_ptr_.lock(), name_index, value_index);
}

StringProperty State::CreateStringProperty(BorrowedStringValue name, BlockIndex parent,
                                           const std::string& value) {
  return InnerCreateProperty<StringProperty, std::string>(
      name, parent, value.data(), value.length(), PropertyBlockFormat::kUtf8);
}

ByteVectorProperty State::CreateByteVectorProperty(BorrowedStringValue name, BlockIndex parent,
                                                   cpp20::span<const uint8_t> value) {
  return InnerCreateProperty<ByteVectorProperty, cpp20::span<const uint8_t>>(
      name, parent, reinterpret_cast<const char*>(value.data()), value.size(),
      PropertyBlockFormat::kBinary);
}

Link State::CreateLink(BorrowedStringValue name, BlockIndex parent, BorrowedStringValue content,
                       LinkBlockDisposition disposition) {
  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());

  BlockIndex name_index, value_index, content_index;
  zx_status_t status;
  status = InnerCreateValue(name, BlockType::kLinkValue, parent, &name_index, &value_index);
  if (status != ZX_OK) {
    return Link();
  }

  status = InnerCreateAndIncrementStringReference(content, &content_index);
  if (status != ZX_OK) {
    DecrementParentRefcount(value_index);
    InnerReleaseStringReference(name_index);
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

LazyNode State::InnerCreateLazyLink(BorrowedStringValue name, BlockIndex parent,
                                    LazyNodeCallbackFn callback, LinkBlockDisposition disposition) {
  cpp17::string_view data;
  switch (name.index()) {
    case internal::isStringReference:
      data = cpp17::get<internal::isStringReference>(name).Data();
      break;
    case internal::isStringLiteral:
      data = cpp17::get<internal::isStringLiteral>(name);
      break;
  }
  std::string content = UniqueLinkName(data);
  auto link = CreateLink(name, parent, content, disposition);

  {
    std::lock_guard<std::mutex> lock(mutex_);

    link_callbacks_.emplace(content, LazyNodeCallbackHolder(std::move(callback)));

    return LazyNode(weak_self_ptr_.lock(), std::move(content), std::move(link));
  }
}

LazyNode State::CreateLazyNode(BorrowedStringValue name, BlockIndex parent,
                               LazyNodeCallbackFn callback) {
  return InnerCreateLazyLink(name, parent, std::move(callback), LinkBlockDisposition::kChild);
}

LazyNode State::CreateLazyValues(BorrowedStringValue name, BlockIndex parent,
                                 LazyNodeCallbackFn callback) {
  return InnerCreateLazyLink(name, parent, std::move(callback), LinkBlockDisposition::kInline);
}

Node State::CreateNode(BorrowedStringValue name, BlockIndex parent) {
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

  auto* block = heap_->GetBlock(property->value_index_);
  InnerFreeExtentChain(PropertyBlockPayload::ExtentIndex::Get<BlockIndex>(block->payload.u64));

  BlockIndex first_extent_index;
  zx_status_t status;
  std::tie(first_extent_index, status) = InnerCreateExtentChain(value, length);

  const auto length_maybe_zeroed = status == ZX_OK ? length : 0;

  block->payload.u64 = PropertyBlockPayload::TotalLength::Make(length_maybe_zeroed) |
                       PropertyBlockPayload::ExtentIndex::Make(first_extent_index) |
                       PropertyBlockPayload::Flags::Make(
                           PropertyBlockPayload::Flags::Get<uint8_t>(block->payload.u64));
}

void State::SetStringProperty(StringProperty* property, const std::string& value) {
  InnerSetProperty(property, value.data(), value.size());
}

void State::SetByteVectorProperty(ByteVectorProperty* property, cpp20::span<const uint8_t> value) {
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
          InnerReleaseStringReference(ValueBlockFields::NameIndex::Get<BlockIndex>(parent->header));
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

  InnerReleaseStringReference(metric->name_index_);
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

  InnerReleaseStringReference(metric->name_index_);
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

  InnerReleaseStringReference(metric->name_index_);
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

  InnerReleaseStringReference(metric->name_index_);
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

  const auto* block = heap_->GetBlock(property->value_index_);
  InnerFreeExtentChain(PropertyBlockPayload::ExtentIndex::Get<BlockIndex>(block->payload.u64));

  InnerReleaseStringReference(property->name_index_);
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

  InnerReleaseStringReference(link->name_index_);
  heap_->Free(link->value_index_);
  InnerReleaseStringReference(link->content_index_);
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
      InnerReleaseStringReference(object->name_index_);
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

  // Cancel the Holder without State locked. This avoids a deadlock in which we could be locking
  // the holder with the state lock held, meanwhile the callback itself is modifying state (with
  // holder locked).
  //
  // At this point in time, the LazyNode is still *live* and the callback may be getting executed.
  // Following this cancel call, the LazyNode is no longer live and the callback will never be
  // called again.
  holder.cancel();
}

void State::ReleaseStringReference(const BlockIndex index) {
  std::lock_guard<std::mutex> lock(mutex_);
  AutoGenerationIncrement gen(header_, heap_.get());
  InnerReleaseStringReference(index);
}

void State::InnerReleaseStringReference(const BlockIndex index) {
  auto* const block = heap_->GetBlock(index);
  const auto reference_count =
      StringReferenceBlockFields::ReferenceCount::Get<uint64_t>(block->header);
  StringReferenceBlockFields::ReferenceCount::Set(&block->header,
                                                  reference_count > 0 ? reference_count - 1 : 0);
  InnerMaybeFreeStringReference(index, block);
}

void State::InnerMaybeFreeStringReference(BlockIndex index, Block* block) {
  const auto reference_count =
      StringReferenceBlockFields::ReferenceCount::Get<uint64_t>(block->header);
  if (reference_count != 0) {
    return;
  }

  // If a reference ID is used again, it will just be re-allocated to the VMO.
  // Additionally, though the index might not have been mapped to a state ID,
  // failing to erase isn't an error.
  string_reference_ids_.EraseByIndex(index);

  const auto first_extent_index =
      StringReferenceBlockFields::NextExtentIndex::Get<BlockIndex>(block->header);
  heap_->Free(index);
  InnerFreeExtentChain(first_extent_index);
}

void State::InnerReadExtents(BlockIndex head_extent, size_t remaining_length,
                             std::vector<uint8_t>* buf) const {
  auto* extent = heap_->GetBlock(head_extent);
  while (remaining_length > 0) {
    if (!extent || GetType(extent) != BlockType::kExtent) {
      break;
    }
    size_t len = std::min(remaining_length, PayloadCapacity(GetOrder(extent)));
    buf->insert(buf->cend(), extent->payload_ptr(), extent->payload_ptr() + len);
    remaining_length -= len;

    BlockIndex next_extent = ExtentBlockFields::NextExtentIndex::Get<BlockIndex>(extent->header);

    extent = heap_->GetBlock(next_extent);
  }
}

std::vector<std::string> State::GetLinkNames() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> ret;
  for (const auto& entry : link_callbacks_) {
    ret.push_back(entry.first);
  }
  return ret;
}

fpromise::promise<Inspector> State::CallLinkCallback(const std::string& name) {
  LazyNodeCallbackHolder holder;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = link_callbacks_.find(name);
    if (it == link_callbacks_.end()) {
      return fpromise::make_result_promise<Inspector>(fpromise::error());
    }
    // Copy out the holder.
    holder = it->second;
  }

  // Call the callback.
  // This occurs without state locked, but deletion of the LazyNode synchronizes on the internal
  // mutex in the Holder. If the LazyNode is deleted before this call, the callback will not be
  // executed. If the LazyNode is being deleted concurrent with this call, it will be delayed
  // until after the callback returns.
  return holder.call();
}

zx_status_t State::InnerCreateValue(BorrowedStringValue name, BlockType type,
                                    BlockIndex parent_index, BlockIndex* out_name,
                                    BlockIndex* out_value, size_t min_size_required) {
  BlockIndex value_index, name_index;
  zx_status_t status;
  status = heap_->Allocate(min_size_required, &value_index);
  if (status != ZX_OK) {
    return status;
  }

  status = InnerCreateAndIncrementStringReference(name, &name_index);
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
      InnerReleaseStringReference(name_index);
      heap_->Free(value_index);
      return ZX_ERR_INVALID_ARGS;
  }

  *out_name = name_index;
  *out_value = value_index;
  return ZX_OK;
}

// This function accepts either a BufferValue index or an Extent index.
// If passed a BufferValue, it will proceed to the extent in the ExtentIndex field.
void State::InnerFreeExtentChain(BlockIndex index) {
  auto* extent = heap_->GetBlock(index);
  ZX_DEBUG_ASSERT_MSG(IsExtent(extent) || index == 0,
                      "must pass extent index to InnerFreeExtentChain");

  while (IsExtent(extent)) {
    auto next_extent = ExtentBlockFields::NextExtentIndex::Get<BlockIndex>(extent->header);
    heap_->Free(index);
    index = next_extent;
    extent = heap_->GetBlock(index);
  }
}

std::pair<BlockIndex, zx_status_t> State::InnerCreateExtentChain(const char* value, size_t length) {
  if (length == 0)
    return {0, ZX_OK};

  BlockIndex extent_index;
  zx_status_t status;
  status = heap_->Allocate(std::min(kMaxOrderSize, BlockSizeForPayload(length)), &extent_index);
  if (status != ZX_OK) {
    return {0, status};
  }

  // Thread the value through extents, creating new extents as needed.
  size_t offset = 0;
  const BlockIndex first_extent_index = extent_index;
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
        InnerFreeExtentChain(first_extent_index);
        return {0, status};
      }
      ExtentBlockFields::NextExtentIndex::Set(&extent->header, extent_index);
    }
  }

  return {first_extent_index, ZX_OK};
}

std::string State::UniqueLinkName(cpp17::string_view prefix) {
  return std::string(prefix.data(), prefix.size()) + "-" +
         std::to_string(next_unique_link_number_.fetch_add(1, std::memory_order_relaxed));
}

zx_status_t State::CreateAndIncrementStringReference(BorrowedStringValue value, BlockIndex* out) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Since InnerCreateStringReferenceWithCount might not actually allocate, a potential
  // optimzation here is to only conditionally increment the generation count.
  AutoGenerationIncrement gen(header_, heap_.get());
  return InnerCreateAndIncrementStringReference(value, out);
}

zx_status_t State::InnerCreateStringReference(BorrowedStringValue value, BlockIndex* const out) {
  zx_status_t status;
  cpp17::optional<BlockIndex> maybe_block_index;

  switch (value.index()) {
    case internal::isStringReference:
      maybe_block_index =
          string_reference_ids_.GetBlockIndex(cpp17::get<internal::isStringReference>(value).ID());
      if (maybe_block_index.has_value()) {
        *out = maybe_block_index.value();
        return ZX_OK;
      }

      status = InnerDoStringReferenceAllocations(
          cpp17::get<internal::isStringReference>(value).Data(), out);
      if (status != ZX_OK) {
        return status;
      }

      string_reference_ids_.Insert(*out, cpp17::get<internal::isStringReference>(value).ID());
      break;

    case internal::isStringLiteral:
      return InnerDoStringReferenceAllocations(cpp17::get<internal::isStringLiteral>(value), out);
  }

  return ZX_OK;
}

namespace {
constexpr size_t GetOrderForSizeOfStringReference(const size_t data_size) {
  return std::min(
      BlockSizeForPayload(data_size + StringReferenceBlockPayload::TotalLength::SizeInBytes()),
      BlockSizeForPayload(kMaxPayloadSize));
}
}  // namespace

zx_status_t State::InnerDoStringReferenceAllocations(cpp17::string_view data,
                                                     BlockIndex* const out) {
  const auto order_for_size = GetOrderForSizeOfStringReference(data.size());
  auto status = heap_->Allocate(order_for_size, out);
  if (status != ZX_OK) {
    return status;
  }

  auto* block = heap_->GetBlock(*out);
  block->header = StringReferenceBlockFields::Order::Make(GetOrder(block)) |
                  StringReferenceBlockFields::Type::Make(BlockType::kStringReference) |
                  StringReferenceBlockFields::NextExtentIndex::Make(
                      0 /* this is potentially reset in WriteStringReferencePayload */) |
                  StringReferenceBlockFields::ReferenceCount::Make(0);
  block->payload.u64 = StringReferenceBlockPayload::TotalLength::Make(data.size());
  return WriteStringReferencePayload(block, data);
}

zx_status_t State::WriteStringReferencePayload(Block* const block, cpp17::string_view data) {
  // write the inline-portion first:
  auto inline_length =
      std::min(data.size(), PayloadCapacity(GetOrder(block)) -
                                StringReferenceBlockPayload::TotalLength::SizeInBytes());
  memcpy(block->payload.data + StringReferenceBlockPayload::TotalLength::SizeInBytes(), data.data(),
         inline_length);
  // this implies the whole piece of data fit inline, and we are done
  if (inline_length == data.size()) {
    return ZX_OK;
  }

  // allocate necessary extents, copying data
  BlockIndex first_extent_index;
  zx_status_t status;
  std::tie(first_extent_index, status) =
      InnerCreateExtentChain(std::cbegin(data) + inline_length, data.size() - inline_length);

  if (status != ZX_OK) {
    return status;
  }

  block->header =
      block->header | StringReferenceBlockFields::NextExtentIndex::Make(first_extent_index);
  return ZX_OK;
}

zx_status_t State::InnerCreateAndIncrementStringReference(BorrowedStringValue name,
                                                          BlockIndex* out) {
  const auto status = InnerCreateStringReference(name, out);
  if (status != ZX_OK) {
    return status;
  }

  auto* const block = heap_->GetBlock(*out);

  // you must look up the reference count, because if the block already exists,
  // InnerCreateStringReference does not notify you in any way
  const auto count = StringReferenceBlockFields::ReferenceCount::Get<uint64_t>(block->header);
  StringReferenceBlockFields::ReferenceCount::Set(&block->header, count + 1);

  return status;
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
  ret.allocated_blocks = heap_->TotalAllocatedBlocks();
  ret.deallocated_blocks = heap_->TotalDeallocatedBlocks();
  ret.failed_allocations = heap_->TotalFailedAllocations();
  return ret;
}

cpp17::optional<std::string> TesterLoadStringReference(const State& state, const BlockIndex index) {
  std::lock_guard<std::mutex> lock(state.mutex_);
  const auto* const block = state.heap_->GetBlock(index);
  if (!block) {
    return {};
  }

  std::vector<uint8_t> buffer;

  const auto total_length =
      StringReferenceBlockPayload::TotalLength::Get<size_t>(block->payload.u64);
  buffer.reserve(total_length);
  const auto max_inlinable_length =
      PayloadCapacity(GetOrder(block)) - StringReferenceBlockPayload::TotalLength::SizeInBytes();
  buffer.insert(buffer.cend(),
                block->payload_ptr() + StringReferenceBlockPayload::TotalLength::SizeInBytes(),
                block->payload_ptr() + StringReferenceBlockPayload::TotalLength::SizeInBytes() +
                    std::min(total_length, max_inlinable_length));

  if (total_length == buffer.size()) {
    return std::string{buffer.cbegin(), buffer.cend()};
  }

  state.InnerReadExtents(
      StringReferenceBlockFields::NextExtentIndex::Get<BlockIndex>(block->header),
      total_length - max_inlinable_length, &buffer);

  return std::string{buffer.cbegin(), buffer.cend()};
}

}  // namespace internal
}  // namespace inspect
