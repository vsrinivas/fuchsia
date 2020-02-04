// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DISK_INSPECTOR_DISK_STRUCT_H_
#define DISK_INSPECTOR_DISK_STRUCT_H_

#include <zircon/assert.h>
#include <zircon/types.h>

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <disk_inspector/disk_obj.h>
#include <disk_inspector/supported_types.h>

namespace disk_inspector {

// Helper for implementers of the inspector to read and write fields to
// an in-memory struct through string field names and string values
// that bypass the need of field typing information. Weakly emulates
// reflection of a struct provided typing information is pre-initialized
// through the AddField method.
class DiskStruct : public DiskObj {
 public:
  // Create function that takes in the |name| to label the struct and the
  // |size| of the struct.
  static std::unique_ptr<DiskStruct> Create(std::string name, uint64_t size);
  DiskStruct(const DiskStruct&) = delete;
  DiskStruct& operator=(const DiskStruct&) = delete;
  DiskStruct(DiskStruct&& other) = default;
  DiskStruct& operator=(DiskStruct&& other) = default;
  ~DiskStruct() override = default;

  // DiskStruct interface:
  std::string GetTypeName() override { return name_; }
  uint64_t GetSize() override { return size_; }
  zx_status_t WriteField(void* position, std::vector<std::string> keys,
                         std::vector<uint64_t> indices, const std::string& value) override;
  std::string ToString(void* position, const PrintOptions& options) override;

  // Adds the field information to end of the list of fields to parse the struct.
  // Users should add all relevant fields through this method before calling ToString or
  // WriteField. Returns a debug error if re-adding an existing field name. If the field type
  // is FieldType::kStruct, |disk_struct| should be set to represents the structure of the
  // field struct type. |count| should be 0 for non-array fields and > 0 to represent the
  // number of elements in an array field. For unsupported field types, the field will still
  // be added to the struct, but its contents will not be able to be parsed.
  void AddField(std::string key, FieldType type, uint64_t field_offset, uint64_t count = 0,
                std::unique_ptr<DiskStruct> disk_struct = nullptr);

 private:
  explicit DiskStruct(std::string name, uint64_t size) : name_(std::move(name)), size_(size) {}
  std::string name_;
  uint64_t size_;

  std::vector<std::string> field_list_;

  // Helper struct to store information needed to parse each field.
  // |element|: DiskObj with field typing information. Can either be
  // a DiskStruct or a DiskPrimitive.
  // |element_size|: Byte size of the field.
  // |count|: Number of elements in the field. -1 if the field is unparsable. 0 if the field is
  // a single element. > 0 if the field is an array type.
  // |offset|: Byte offset of the field from the start of the struct.
  struct FieldInfo {
    std::unique_ptr<DiskObj> element;
    uint64_t element_size;
    int64_t count;
    uint64_t offset;
  };
  std::unordered_map<std::string, FieldInfo> fields_;
};

}  // namespace disk_inspector

#endif  // DISK_INSPECTOR_DISK_STRUCT_H_
