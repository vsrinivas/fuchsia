// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <lib/inspect-vmo/state.h>

namespace inspect {
namespace vmo {
namespace internal {

// Helper class to support RAII locking of the generation count.
class AutoGenerationIncrement final {
public:
    AutoGenerationIncrement(BlockIndex target, Heap* heap);
    ~AutoGenerationIncrement();

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

    DISALLOW_COPY_ASSIGN_AND_MOVE(AutoGenerationIncrement);
};

AutoGenerationIncrement ::AutoGenerationIncrement(BlockIndex target, Heap* heap)
    : target_(target), heap_(heap) {
    Acquire(heap_->GetBlock(target_));
}
AutoGenerationIncrement::~AutoGenerationIncrement() {
    Release(heap_->GetBlock(target_));
}

void AutoGenerationIncrement ::Acquire(Block* block) {
    uint64_t* ptr = &block->payload.u64;
    __atomic_fetch_add(ptr, 1, std::memory_order_acq_rel);
}

void AutoGenerationIncrement::Release(Block* block) {
    uint64_t* ptr = &block->payload.u64;
    __atomic_fetch_add(ptr, 1, std::memory_order_release);
}

template <typename NumericType, typename WrapperType, BlockType BlockTypeValue>
WrapperType State::InnerCreateArray(fbl::StringPiece name, BlockIndex parent, size_t slots, ArrayFormat format) {
    size_t block_size_needed = slots * sizeof(NumericType) + kMinOrderSize;
    ZX_DEBUG_ASSERT_MSG(block_size_needed <= kMaxOrderSize, "The requested array size cannot fit in a block");
    if (block_size_needed > kMaxOrderSize) {
        return WrapperType();
    }

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    BlockIndex name_index, value_index;
    zx_status_t status;
    status = InnerCreateValue(name, BlockType::kArrayValue, parent, &name_index, &value_index, block_size_needed);
    if (status != ZX_OK) {
        return WrapperType();
    }

    auto* block = heap_->GetBlock(value_index);
    block->payload.u64 = ArrayBlockPayload::EntryType::Make(BlockTypeValue) |
                         ArrayBlockPayload::Flags::Make(format) |
                         ArrayBlockPayload::Count::Make(slots);

    return WrapperType(fbl::WrapRefPtr(this), name_index, value_index);
}

template <typename NumericType, typename WrapperType, BlockType BlockTypeValue>
void State::InnerSetArray(WrapperType* metric, size_t index, NumericType value) {
    ZX_ASSERT(metric->state_.get() == this);
    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

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
    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

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

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    DecrementParentRefcount(value->value_index_);

    heap_->Free(value->name_index_);
    heap_->Free(value->value_index_);
    value->state_ = nullptr;
}

fbl::RefPtr<State> State::Create(fbl::unique_ptr<Heap> heap) {
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
                    HeaderBlockFields::Version::Make(0);
    memcpy(&block->header_data[4], kMagicNumber, 4);
    block->payload.u64 = 0;

    fbl::AllocChecker ac;
    auto ret = fbl::AdoptRef(new (&ac) State(std::move(heap), header));
    if (!ac.check()) {
        return nullptr;
    }
    return ret;
}

State::~State() {
    heap_->Free(header_);
}

const zx::vmo& State::GetVmo() const {
    fbl::AutoLock lock(&mutex_);
    return heap_->GetVmo();
}

IntMetric State::CreateIntMetric(fbl::StringPiece name, BlockIndex parent, int64_t value) {
    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    BlockIndex name_index, value_index;
    zx_status_t status;
    status = InnerCreateValue(name, BlockType::kIntValue, parent, &name_index, &value_index);
    if (status != ZX_OK) {
        return IntMetric();
    }

    auto* block = heap_->GetBlock(value_index);
    block->payload.i64 = value;

    return IntMetric(fbl::WrapRefPtr(this), name_index, value_index);
}

UintMetric State::CreateUintMetric(fbl::StringPiece name, BlockIndex parent, uint64_t value) {
    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    BlockIndex name_index, value_index;
    zx_status_t status;
    status = InnerCreateValue(name, BlockType::kUintValue, parent, &name_index, &value_index);
    if (status != ZX_OK) {
        return UintMetric();
    }

    auto* block = heap_->GetBlock(value_index);
    block->payload.u64 = value;

    return UintMetric(fbl::WrapRefPtr(this), name_index, value_index);
}

DoubleMetric State::CreateDoubleMetric(fbl::StringPiece name, BlockIndex parent, double value) {
    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    BlockIndex name_index, value_index;
    zx_status_t status;
    status = InnerCreateValue(name, BlockType::kDoubleValue, parent, &name_index, &value_index);
    if (status != ZX_OK) {
        return DoubleMetric();
    }

    auto* block = heap_->GetBlock(value_index);
    block->payload.f64 = value;

    return DoubleMetric(fbl::WrapRefPtr(this), name_index, value_index);
}

IntArray State::CreateIntArray(fbl::StringPiece name, BlockIndex parent, size_t slots, ArrayFormat format) {
    return InnerCreateArray<int64_t, IntArray, BlockType::kIntValue>(name, parent, slots, format);
}

UintArray State::CreateUintArray(fbl::StringPiece name, BlockIndex parent, size_t slots, ArrayFormat format) {
    return InnerCreateArray<uint64_t, UintArray, BlockType::kUintValue>(name, parent, slots, format);
}

DoubleArray State::CreateDoubleArray(fbl::StringPiece name, BlockIndex parent, size_t slots, ArrayFormat format) {
    return InnerCreateArray<double, DoubleArray, BlockType::kDoubleValue>(name, parent, slots, format);
}

Property State::CreateProperty(fbl::StringPiece name, BlockIndex parent, fbl::StringPiece value, PropertyFormat format) {
    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    BlockIndex name_index, value_index;
    zx_status_t status;
    status = InnerCreateValue(name, BlockType::kPropertyValue, parent, &name_index, &value_index);
    if (status != ZX_OK) {
        return Property();
    }
    auto* block = heap_->GetBlock(value_index);
    block->payload.u64 = PropertyBlockPayload::Flags::Make(format);
    status = InnerSetStringExtents(value_index, value);
    if (status != ZX_OK) {
        heap_->Free(name_index);
        heap_->Free(value_index);
        return Property();
    }

    return Property(fbl::WrapRefPtr(this), name_index, value_index);
}

Object State::CreateObject(fbl::StringPiece name, BlockIndex parent) {
    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    BlockIndex name_index, value_index;
    zx_status_t status;
    status = InnerCreateValue(name, BlockType::kObjectValue, parent, &name_index, &value_index);
    if (status != ZX_OK) {
        return Object();
    }

    return Object(fbl::WrapRefPtr(this), name_index, value_index);
}

void State::SetIntMetric(IntMetric* metric, int64_t value) {
    ZX_ASSERT(metric->state_.get() == this);
    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    auto* block = heap_->GetBlock(metric->value_index_);
    ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kIntValue, "Expected int metric, got %d",
                        static_cast<int>(GetType(block)));
    block->payload.i64 = value;
}

void State::SetUintMetric(UintMetric* metric, uint64_t value) {
    ZX_ASSERT(metric->state_.get() == this);

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    auto* block = heap_->GetBlock(metric->value_index_);
    ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kUintValue, "Expected uint metric, got %d",
                        static_cast<int>(GetType(block)));
    block->payload.u64 = value;
}

void State::SetDoubleMetric(DoubleMetric* metric, double value) {
    ZX_ASSERT(metric->state_.get() == this);

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    auto* block = heap_->GetBlock(metric->value_index_);
    ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kDoubleValue, "Expected double metric, got %d",
                        static_cast<int>(GetType(block)));
    block->payload.f64 = value;
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

void State::AddIntMetric(IntMetric* metric, int64_t value) {
    ZX_ASSERT(metric->state_.get() == this);

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    auto* block = heap_->GetBlock(metric->value_index_);
    ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kIntValue, "Expected int metric, got %d",
                        static_cast<int>(GetType(block)));
    block->payload.i64 += value;
}

void State::AddUintMetric(UintMetric* metric, uint64_t value) {
    ZX_ASSERT(metric->state_.get() == this);

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    auto* block = heap_->GetBlock(metric->value_index_);
    ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kUintValue, "Expected uint metric, got %d",
                        static_cast<int>(GetType(block)));
    block->payload.u64 += value;
}

void State::AddDoubleMetric(DoubleMetric* metric, double value) {
    ZX_ASSERT(metric->state_.get() == this);

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    auto* block = heap_->GetBlock(metric->value_index_);
    ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kDoubleValue, "Expected double metric, got %d",
                        static_cast<int>(GetType(block)));
    block->payload.f64 += value;
}

void State::SubtractIntMetric(IntMetric* metric, int64_t value) {
    ZX_ASSERT(metric->state_.get() == this);

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    auto* block = heap_->GetBlock(metric->value_index_);
    ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kIntValue, "Expected int metric, got %d",
                        static_cast<int>(GetType(block)));
    block->payload.i64 -= value;
}

void State::SubtractUintMetric(UintMetric* metric, uint64_t value) {
    ZX_ASSERT(metric->state_.get() == this);

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    auto* block = heap_->GetBlock(metric->value_index_);
    ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kUintValue, "Expected uint metric, got %d",
                        static_cast<int>(GetType(block)));
    block->payload.u64 -= value;
}

void State::SubtractDoubleMetric(DoubleMetric* metric, double value) {
    ZX_ASSERT(metric->state_.get() == this);

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    auto* block = heap_->GetBlock(metric->value_index_);
    ZX_DEBUG_ASSERT_MSG(GetType(block) == BlockType::kDoubleValue, "Expected double metric, got %d",
                        static_cast<int>(GetType(block)));
    block->payload.f64 -= value;
}

void State::AddIntArray(IntArray* array, size_t index, int64_t value) {
    InnerOperationArray<int64_t, IntArray, BlockType::kIntValue, std::plus<int64_t>>(array, index, value);
}

void State::SubtractIntArray(IntArray* array, size_t index, int64_t value) {
    InnerOperationArray<int64_t, IntArray, BlockType::kIntValue, std::minus<int64_t>>(array, index, value);
}

void State::AddUintArray(UintArray* array, size_t index, uint64_t value) {
    InnerOperationArray<uint64_t, UintArray, BlockType::kUintValue, std::plus<uint64_t>>(array, index, value);
}

void State::SubtractUintArray(UintArray* array, size_t index, uint64_t value) {
    InnerOperationArray<uint64_t, UintArray, BlockType::kUintValue, std::minus<uint64_t>>(array, index, value);
}

void State::AddDoubleArray(DoubleArray* array, size_t index, double value) {
    InnerOperationArray<double, DoubleArray, BlockType::kDoubleValue, std::plus<double>>(array, index, value);
}

void State::SubtractDoubleArray(DoubleArray* array, size_t index, double value) {
    InnerOperationArray<double, DoubleArray, BlockType::kDoubleValue, std::minus<double>>(array, index, value);
}

void State::SetProperty(Property* property, fbl::StringPiece value) {
    ZX_ASSERT(property->state_.get() == this);

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    InnerSetStringExtents(property->value_index_, value);
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
        case BlockType::kObjectValue:
            // Stop decrementing parent refcounts when we observe a live object.
            ZX_ASSERT(parent->payload.u64 != 0);
            --parent->payload.u64;
            return;
        case BlockType::kTombstone:
            ZX_ASSERT(parent->payload.u64 != 0);
            if (--parent->payload.u64 == 0) {
                BlockIndex next_parent_index =
                    ValueBlockFields::ParentIndex::Get<BlockIndex>(parent->header);
                heap_->Free(ValueBlockFields::NameIndex::Get<BlockIndex>(parent->header));
                heap_->Free(parent_index);
                parent_index = next_parent_index;
            }
            break;
        default:
            ZX_DEBUG_ASSERT_MSG(false, "Invalid parent type %u",
                                static_cast<uint32_t>(GetType(parent)));
            return;
        }
    }
}

void State::FreeIntMetric(IntMetric* metric) {
    ZX_DEBUG_ASSERT_MSG(metric->state_.get() == this, "Metric being freed from the wrong state");
    if (metric->state_.get() != this) {
        return;
    }

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    DecrementParentRefcount(metric->value_index_);

    heap_->Free(metric->name_index_);
    heap_->Free(metric->value_index_);
    metric->state_ = nullptr;
}

void State::FreeUintMetric(UintMetric* metric) {
    ZX_DEBUG_ASSERT_MSG(metric->state_.get() == this, "Metric being freed from the wrong state");
    if (metric->state_.get() != this) {
        return;
    }

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    DecrementParentRefcount(metric->value_index_);

    heap_->Free(metric->name_index_);
    heap_->Free(metric->value_index_);
    metric->state_ = nullptr;
}

void State::FreeDoubleMetric(DoubleMetric* metric) {
    ZX_DEBUG_ASSERT_MSG(metric->state_.get() == this, "Metric being freed from the wrong state");
    if (metric->state_.get() != this) {
        return;
    }

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    DecrementParentRefcount(metric->value_index_);

    heap_->Free(metric->name_index_);
    heap_->Free(metric->value_index_);
    metric->state_ = nullptr;
}

void State::FreeIntArray(IntArray* array) {
    InnerFreeArray<IntArray>(array);
}

void State::FreeUintArray(UintArray* array) {
    InnerFreeArray<UintArray>(array);
}

void State::FreeDoubleArray(DoubleArray* array) {
    InnerFreeArray<DoubleArray>(array);
}

void State::FreeProperty(Property* property) {
    ZX_DEBUG_ASSERT_MSG(property->state_.get() == this,
                        "Property being freed from the wrong state");
    if (property->state_.get() != this) {
        return;
    }

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    DecrementParentRefcount(property->value_index_);

    InnerFreeStringExtents(property->value_index_);

    heap_->Free(property->name_index_);
    heap_->Free(property->value_index_);
    property->state_ = nullptr;
}

void State::FreeObject(Object* object) {
    ZX_DEBUG_ASSERT_MSG(object->state_.get() == this, "Object being freed from the wrong state");
    if (object->state_.get() != this) {
        return;
    }

    fbl::AutoLock lock(&mutex_);
    auto gen = AutoGenerationIncrement(header_, heap_.get());

    auto* block = heap_->GetBlock(object->value_index_);
    if (block) {
        if (block->payload.u64 == 0) {
            // Actually free the block, decrementing parent refcounts.
            DecrementParentRefcount(object->value_index_);
            // Object has no refs, free it.
            heap_->Free(object->name_index_);
            heap_->Free(object->value_index_);
        } else {
            // Object has refs, change type to tombstone so it can be removed
            // when the last ref is gone.
            ValueBlockFields::Type::Set(&block->header,
                                        static_cast<uint64_t>(BlockType::kTombstone));
        }
    }
}

zx_status_t State::InnerCreateValue(fbl::StringPiece name, BlockType type, BlockIndex parent_index,
                                    BlockIndex* out_name, BlockIndex* out_value,
                                    size_t min_size_required) {
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
    case BlockType::kObjectValue:
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
    if (!str || GetType(str) != BlockType::kPropertyValue) {
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
    str->payload.u64 =
        PropertyBlockPayload::TotalLength::Make(0) | PropertyBlockPayload::ExtentIndex::Make(0) |
        PropertyBlockPayload::Flags::Make(PropertyBlockPayload::Flags::Get<uint8_t>(str->payload.u64));
}

zx_status_t State::InnerSetStringExtents(BlockIndex string_index, fbl::StringPiece value) {
    InnerFreeStringExtents(string_index);

    auto* block = heap_->GetBlock(string_index);

    if (value.size() == 0) {
        // The extent index is 0 if no extents were needed (the value is empty).
        block->payload.u64 =
            PropertyBlockPayload::TotalLength::Make(value.size()) |
            PropertyBlockPayload::ExtentIndex::Make(0) |
            PropertyBlockPayload::Flags::Make(PropertyBlockPayload::Flags::Get<uint8_t>(block->payload.u64));
        return ZX_OK;
    }

    BlockIndex extent_index;
    zx_status_t status;
    status = heap_->Allocate(
        fbl::min(kMaxOrderSize, BlockSizeForPayload(value.size())), &extent_index);
    if (status != ZX_OK) {
        return status;
    }

    block->payload.u64 =
        PropertyBlockPayload::TotalLength::Make(value.size()) |
        PropertyBlockPayload::ExtentIndex::Make(extent_index) |
        PropertyBlockPayload::Flags::Make(PropertyBlockPayload::Flags::Get<uint8_t>(block->payload.u64));

    // Thread the value through extents, creating new extents as needed.
    size_t offset = 0;
    while (offset < value.size()) {
        auto* extent = heap_->GetBlock(extent_index);

        extent->header = ExtentBlockFields::Order::Make(GetOrder(extent)) |
                         ExtentBlockFields::Type::Make(BlockType::kExtent) |
                         ExtentBlockFields::NextExtentIndex::Make(0);

        size_t len = fbl::min(PayloadCapacity(GetOrder(extent)), value.size() - offset);
        memcpy(extent->payload.data, value.begin() + offset, len);
        offset += len;

        if (offset < value.size()) {
            status = heap_->Allocate(fbl::min(kMaxOrderSize, BlockSizeForPayload(value.size() - offset)), &extent_index);
            if (status != ZX_OK) {
                InnerFreeStringExtents(string_index);
                return status;
            }
            ExtentBlockFields::NextExtentIndex::Set(&extent->header, extent_index);
        }
    }

    return ZX_OK;
}

zx_status_t State::CreateName(fbl::StringPiece name, BlockIndex* out) {
    ZX_DEBUG_ASSERT_MSG(name.size() <= kMaxPayloadSize, "Name too long (length is %lu)",
                        name.size());
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

} // namespace internal
} // namespace vmo
} // namespace inspect
