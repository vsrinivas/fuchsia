// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DISK_INSPECTOR_TYPE_UTILS_H_
#define DISK_INSPECTOR_TYPE_UTILS_H_

#include <disk_inspector/disk_struct.h>
#include <disk_inspector/supported_types.h>

namespace disk_inspector {

// Template specialization to convert from regular types to FieldType.
template <typename T>
constexpr FieldType GetFieldType() {
  return FieldType::kNotSupported;
}
template <>
constexpr FieldType GetFieldType<uint8_t>() {
  return FieldType::kUint8;
}
template <>
constexpr FieldType GetFieldType<uint16_t>() {
  return FieldType::kUint16;
}
template <>
constexpr FieldType GetFieldType<uint32_t>() {
  return FieldType::kUint32;
}
template <>
constexpr FieldType GetFieldType<uint64_t>() {
  return FieldType::kUint64;
}

}  // namespace disk_inspector

// Helper macros to add fields to a DiskStruct object from field typing information. See the
// associated test for usage example.
#define ADD_FIELD(object, struct, field)                                                     \
  object->AddField(                                                                          \
      #field,                                                                                \
      disk_inspector::GetFieldType<                                                          \
          typename std::remove_pointer<std::decay<decltype(struct ::field)>::type>::type>(), \
      offsetof(struct, field), 0);

#define ADD_ARRAY_FIELD(object, struct, field, count)                                        \
  object->AddField(                                                                          \
      #field,                                                                                \
      disk_inspector::GetFieldType<                                                          \
          typename std::remove_pointer<std::decay<decltype(struct ::field)>::type>::type>(), \
      offsetof(struct, field), count);

#define ADD_STRUCT_FIELD(object, struct, field, field_disk_struct)                             \
  object->AddField(#field, disk_inspector::FieldType::kDiskStruct, offsetof(struct, field), 0, \
                   field_disk_struct);

#define ADD_STRUCT_ARRAY_FIELD(object, struct, field, count, field_disk_struct)                    \
  object->AddField(#field, disk_inspector::FieldType::kDiskStruct, offsetof(struct, field), count, \
                   field_disk_struct);

#endif  // DISK_INSPECTOR_TYPE_UTILS_H_
