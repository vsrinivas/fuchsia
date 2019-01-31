// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <lib/inspect/block.h>
#include <lib/inspect/heap.h>
#include <lib/inspect/types.h>

namespace inspect {
namespace internal {

// |State| wraps a |Heap| and implements the Inspect VMO API on top of
// that heap. This class contains the low-level operations necessary to
// deal with the various Inspect types and wrappers to denote ownership of
// those values.
// This class should not be used directly, prefer to use |Inspector|.
class State final : public fbl::RefCounted<State> {
public:
    // Create a new State wrapping the given Heap.
    // On failure, returns nullptr.
    static fbl::RefPtr<State> Create(fbl::unique_ptr<Heap> heap);
    ~State();

    // Disallow copy and assign.
    State(State&) = delete;
    State(State&&) = delete;
    State& operator=(State&) = delete;
    State& operator=(State&&) = delete;

    // Obtain a read-only clone of the VMO.
    // This may be passed to reader processes.
    zx::vmo GetReadOnlyVmoClone() const;

    // Create a new |IntMetric| in the Inspect VMO. The returned object releases
    // the metric when destroyed.
    IntMetric CreateIntMetric(fbl::StringPiece name, BlockIndex parent, int64_t value);

    // Create a new |UintMetric| in the Inspect VMO. The returned object releases
    // the metric when destroyed.
    UintMetric CreateUintMetric(fbl::StringPiece name, BlockIndex parent, uint64_t value);

    // Create a new |DoubleMetric| in the Inspect VMO. The returned object releases
    // the metric when destroyed.
    DoubleMetric CreateDoubleMetric(fbl::StringPiece name, BlockIndex parent, double value);

    // Create a new |Property| in the Inspect VMO. The returned object releases
    // the metric when destroyed.
    Property CreateProperty(fbl::StringPiece name, BlockIndex parent, fbl::StringPiece value);

    // Create a new |Object| in the Inspect VMO. Objects are refcounted such that
    // Metrics and Properties nested under the object remain valid until all
    // entities using the Object are destroyed.
    Object CreateObject(fbl::StringPiece name, BlockIndex parent);

    // Setters for various metric types
    void SetIntMetric(IntMetric* metric, int64_t value);
    void SetUintMetric(UintMetric* metric, uint64_t value);
    void SetDoubleMetric(DoubleMetric* metric, double value);

    // Adders for various metric types
    void AddIntMetric(IntMetric* metric, int64_t value);
    void AddUintMetric(UintMetric* metric, uint64_t value);
    void AddDoubleMetric(DoubleMetric* metric, double value);

    // Subtractors for various metric types
    void SubtractIntMetric(IntMetric* metric, int64_t value);
    void SubtractUintMetric(UintMetric* metric, uint64_t value);
    void SubtractDoubleMetric(DoubleMetric* metric, double value);

    // Set the value of a property.
    void SetProperty(Property* property, fbl::StringPiece value);

    // Free various entities
    void FreeIntMetric(IntMetric* metric);
    void FreeUintMetric(UintMetric* metric);
    void FreeDoubleMetric(DoubleMetric* metric);
    void FreeProperty(Property* property);
    void FreeObject(Object* object);

private:
    State(fbl::unique_ptr<Heap> heap, BlockIndex header)
        : heap_(std::move(heap)), header_(header) {}

    void DecrementParentRefcount(BlockIndex value_index) __TA_REQUIRES(mutex_);

    // Helper method for creating a new VALUE block type.
    zx_status_t InnerCreateValue(fbl::StringPiece name, BlockType type, BlockIndex parent_index,
                                 BlockIndex* out_name, BlockIndex* out_value) __TA_REQUIRES(mutex_);

    // Returns true if the block is an extent, false otherwise.
    constexpr bool IsExtent(const Block* block) {
        return block && GetType(block) == BlockType::kExtent;
    }

    // Helper to set the value of a string across its extents.
    zx_status_t InnerSetStringExtents(BlockIndex string_index, fbl::StringPiece value)
        __TA_REQUIRES(mutex_);

    // Helper to free all extents for a given string.
    // This leaves the string value allocated and empty.
    void InnerFreeStringExtents(BlockIndex string_index) __TA_REQUIRES(mutex_);

    // Helper to create a new name block with the given name.
    zx_status_t CreateName(fbl::StringPiece name, BlockIndex* out) __TA_REQUIRES(mutex_);

    // Mutex wrapping all fields in the state.
    mutable fbl::Mutex mutex_;

    // The wrapped |Heap|, protected by the mutex.
    fbl::unique_ptr<Heap> heap_ __TA_GUARDED(mutex_);

    // The index for the header block containing the generation count
    // to increment
    BlockIndex header_ __TA_GUARDED(mutex_);
};

} // namespace internal
} // namespace inspect
