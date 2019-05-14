// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include <fbl/string_buffer.h>
#include <lib/inspect-vmo/types.h>

namespace fs_metrics {
// This library provides mechanisms for auto generating inspect::vmo::objects. In order to so,
// operations tracked by |Offsets| that desire to take advantge of this helper class, need to
// provide the following compile time interface:
//
// Each operations' |OperationInfo| must provide:
//     OperationInfo::kPrefix        -> Unique identifier for the operation.
//     OperationInfo::CreateTracker  -> Callable type with signature (void(const char* name,
//                                     zx::vmo::Object* root, std::vector<InspectObjectType>*
//                                     collection);
//
// Each |Attribute| must provide:
//     Attribute::ToString(size_t index) -> Human readablle string of bucket[index] or the value
//                                          at index.
//
// The generated objects are added as childs to root |zx::vmo::Object| with the following name:
//    -> kPrefix_ToString(Offset(AttributeValue))... applies for each interesting attribute for the
//       operation.
//
// The recommended use is an alias with:
// using ObjectGenerator = ObjectGenerator<Attribute1, Attribute2, ...>
//
// When using in conjunction with |Offset|, prefer |ObjectOffset<>|;

// Maximum size for object names. We limit this, so an intermediate buffer can be used as scratch
// surface when generating all objects for all attributes, effectively reducing the amount of
// allocations.
constexpr uint64_t kNameMaxLength = 80;

namespace internal {

template <typename OperationInfo, typename ObjectType>
void AddObjects(inspect::vmo::Object* root, std::vector<ObjectType>* object_collection,
                fbl::StringBuffer<kNameMaxLength>* name_buffer, size_t last_character) {
    OperationInfo::CreateTracker(name_buffer->c_str(), root, object_collection);
}

template <typename OperationInfo, typename ObjectType, typename Attribute,
          typename... RemainingAttributes>
void AddObjects(inspect::vmo::Object* root, std::vector<ObjectType>* object_collection,
                fbl::StringBuffer<kNameMaxLength>* name_buffer, size_t last_character) {
    // Skip if this is not an attribute being tracked for the given operation.
    if constexpr (!std::is_base_of<Attribute, OperationInfo>::value) {
        AddObjects<OperationInfo, ObjectType, RemainingAttributes...>(
            root, object_collection, name_buffer, name_buffer->length());
        return;
    }

    // Clear any remainder from other invocations.
    for (size_t i = 0; i < Attribute::kSize; ++i) {
        name_buffer->Resize(last_character);
        name_buffer->AppendPrintf("_%s", Attribute::ToString(i).c_str());
        AddObjects<OperationInfo, ObjectType, RemainingAttributes...>(
            root, object_collection, name_buffer, name_buffer->length());
    }
}

} // namespace internal

template <typename... Attributes>
struct ObjectGenerator {

    // Adds all tracking objects for the |OperationInfo|. The tracking objects can be counters,
    // histograms, etc.
    template <typename OperationInfo, typename ObjectType>
    static void AddObjects(inspect::vmo::Object* root, std::vector<ObjectType>* object_collection) {
        fbl::StringBuffer<kNameMaxLength> name_buffer;
        name_buffer.AppendPrintf("%s", OperationInfo::kPrefix);
        internal::AddObjects<OperationInfo, ObjectType, Attributes...>(
            root, object_collection, &name_buffer, name_buffer.length());
    }
};

} // namespace fs_metrics
