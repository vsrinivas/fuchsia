// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DISK_INSPECTOR_DISK_OBJ_H_
#define DISK_INSPECTOR_DISK_OBJ_H_

#include <zircon/types.h>

#include <memory>
#include <string>

#include <disk_inspector/supported_types.h>

namespace disk_inspector {

// Base class to represent a disk structure or a primitive field.
class DiskObj {
 public:
  virtual ~DiskObj() = default;

  // Return the typename of the struct as a string.
  virtual std::string GetTypeName() = 0;

  // Return the size in bytes of the primitive or struct the object represents.
  virtual uint64_t GetSize() = 0;

  // Treating the |obj_start| as the start of the object represented by DiskObj,
  // sets the primitive value of the object field identified by |keys| and |indices|.
  // In the case in which the field to write is within a nested struct in the object,
  // the full list of field names at each layer should be stored in |keys|.
  // In the case in which the field to write is within an array field, the corresponding
  // index at the right nesting layer should be set in |indices|.
  // For example, assume the following nested struct format:
  // struct Bar { uint8_t bar_value; };
  // struct Foo { Bar bar[10]; uint8_t foo_value; }
  // struct Root { Foo foo; };
  // From a Root object, to write into the "bar_value" field of the 7th bar element, the list
  // of keys and indices would be {"foo", "bar", "bar_value"} and {0, 6, 0} respectively.
  // From a Root object, to write into the "foo_value" field, the list of keys and indices would
  // be {"foo", "foo_value"} and {0, 0} respectively.
  // In the case of primitives, since the object represents a single value without fields
  // both keys and indices should be empty ({} and {}).
  // By the above usage, |keys| and |indices| should always be the same length.
  virtual zx_status_t WriteField(void* obj_start, std::vector<std::string> keys,
                                 std::vector<uint64_t> indices, const std::string& value) = 0;

  // Returns a string serialization of the object.
  virtual std::string ToString(void* obj_start, const PrintOptions& options) = 0;
};

}  // namespace disk_inspector

#endif  // DISK_INSPECTOR_DISK_OBJ_H_
