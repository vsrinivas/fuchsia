// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <type_traits>

namespace fs_metrics {
// This library provides the mechanisms for calculating the offsets or positions of an object, given
// their attributes. An object is struct that inhertics from structs providing the Attribtue compile
// time contract.
//
// The benefit comes when dealing with large fixed numbers of objects(e.g. metrics), that are stored
// in a contiguous buffer and there is a need to map a set of attribtues to individual objects.
//
// These objects must provide the following compile-time interface:
//
//     OperationInfo::kStart -> Position where the first object is stored.
//
// Note: that for consistency, AttributeList must remain in the same order in all instantiations.

namespace internal {
template <typename OperationInfo>
constexpr uint64_t CalculateRelativeOffset(const typename OperationInfo::AttributeData& attributes,
                                           uint64_t aggregated_offset,
                                           uint64_t accumulated_dimensions) {
    return aggregated_offset;
}

// Recursive step, for every attribute in the argument pack, or |AttributeList| calculate and
// aggregatre the respective offsets.
template <typename OperationInfo, typename Attribute, typename... RemainingAttributes>
constexpr uint64_t CalculateRelativeOffset(const typename OperationInfo::AttributeData& attributes,
                                           uint64_t aggregated_offset,
                                           uint64_t accumulated_dimensions) {

    if constexpr (std::is_base_of<Attribute, OperationInfo>::value) {
        uint64_t offset = Attribute::OffsetOf(attributes.*Attribute::kAttributeValue);
        aggregated_offset += offset * accumulated_dimensions;
        accumulated_dimensions *= Attribute::kSize;
    }

    return CalculateRelativeOffset<OperationInfo, RemainingAttributes...>(
        attributes, aggregated_offset, accumulated_dimensions);
}

template <typename OperationInfo>
constexpr uint64_t CountInstances(uint64_t accumulated_count) {
    return accumulated_count;
}

template <typename OperationInfo, typename Attribute, typename... RemainingAttributes>
constexpr uint64_t CountInstances(uint64_t accumulated_count) {
    if constexpr (std::is_base_of<Attribute, OperationInfo>::value) {
        accumulated_count *= Attribute::kSize;
    }

    return CountInstances<OperationInfo, RemainingAttributes...>(accumulated_count);
}
} // namespace internal

// Provides specialiazation based on the parameter pack, keeping it consistent across calls and
// a single point of update for adding and changing attribtues.
// Recommended use is:
// using MyOffsets = fs_metrics::MyOffsets<Attribute_1, Attribute_2, ...>;
template <typename... AttributeList>
struct Offsets {

    template <typename OperationInfo>
    static constexpr uint64_t
    RelativeOffset(const typename OperationInfo::AttributeData& attributes) {
        return internal::CalculateRelativeOffset<OperationInfo, AttributeList...>(attributes, 0, 1);
    }

    template <typename OperationInfo>
    static constexpr uint64_t
    AbsoluteOffset(const typename OperationInfo::AttributeData& attributes) {
        return OperationInfo::kStart + RelativeOffset<OperationInfo>(attributes);
    }

    // Returns the number of objects that are being tracked for a single OperationInfo. This is how
    // many different objects can be mapped to all possible attribute values in AttributeData for a
    // specific element.
    template <typename OperationInfo>
    static constexpr uint64_t Count() {
        return internal::CountInstances<OperationInfo, AttributeList...>(1);
    }

    template <typename OperationInfo>
    static constexpr uint64_t Begin() {
        return OperationInfo::kStart;
    }

    template <typename OperationInfo>
    static constexpr uint64_t End() {
        return OperationInfo::kStart + Count<OperationInfo>();
    }
};

} // namespace fs_metrics
