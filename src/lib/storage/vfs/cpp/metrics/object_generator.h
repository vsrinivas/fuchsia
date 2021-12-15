// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_METRICS_OBJECT_GENERATOR_H_
#define SRC_LIB_STORAGE_VFS_CPP_METRICS_OBJECT_GENERATOR_H_

#include <lib/inspect/cpp/inspect.h>

#include <vector>

#include <fbl/string_buffer.h>

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

namespace internal {

template <typename Attribute>
std::vector<std::string> GetAttributeLabels() {
  std::vector<std::string> labels;
  for (size_t i = 0; i < Attribute::kSize; ++i) {
    labels.emplace_back(Attribute::ToString(i));
  }
  return labels;
}

template <typename OperationInfo>
void GetOperationLabels(std::vector<std::vector<std::string>>&) {
  // No more attributes to process.
}

template <typename OperationInfo, typename Attribute, typename... RemainingAttributes>
void GetOperationLabels(std::vector<std::vector<std::string>>& labels) {
  // Process remaining attributes first.
  GetOperationLabels<OperationInfo, RemainingAttributes...>(labels);

  // Ensure this attribute is being tracked for the given operation.
  if constexpr (std::is_base_of<Attribute, OperationInfo>::value) {
    labels.emplace_back(GetAttributeLabels<Attribute>());
  }
}

inline std::vector<std::string> CombineLabels(const std::vector<std::string>& base_labels,
                                              const std::vector<std::string>& new_labels) {
  std::vector<std::string> result;
  for (const std::string& base_label : base_labels) {
    for (const std::string& new_label : new_labels) {
      result.emplace_back(base_label + "_" + new_label);
    }
  }
  return result;
}

[[maybe_unused]] inline std::vector<std::string> GenerateHistogramLabels(
    const std::vector<std::vector<std::string>>& labels, std::string prefix) {
  std::vector<std::string> result;
  result.emplace_back(std::move(prefix));

  for (const auto& label : labels) {
    result = CombineLabels(result, label);
  }
  return result;
}

template <typename OperationInfo, typename Attribute, typename... RemainingAttributes>
std::vector<std::string> GetHistogramNames() {
  std::vector<std::vector<std::string>> all_labels;
  std::vector<std::string> result;
  GetOperationLabels<OperationInfo, Attribute, RemainingAttributes...>(all_labels);
  return GenerateHistogramLabels(all_labels, OperationInfo::kPrefix);
}

}  // namespace internal

template <typename... Attributes>
struct ObjectGenerator {
  // Adds all tracking objects for the |OperationInfo|. The tracking objects can be counters,
  // histograms, etc.
  template <typename OperationInfo, typename ObjectType>
  static void AddObjects(inspect::Node* root, std::vector<ObjectType>* object_collection) {
    // We need to make sure the order that the histograms are generated matches the indices
    // which are calculated in `object_offsets.h`.
    auto histogram_names = internal::GetHistogramNames<OperationInfo, Attributes...>();
    for (const std::string& name : histogram_names) {
      OperationInfo::CreateTracker(name.c_str(), root, object_collection);
    }
  }
};

}  // namespace fs_metrics

#endif  // SRC_LIB_STORAGE_VFS_CPP_METRICS_OBJECT_GENERATOR_H_
