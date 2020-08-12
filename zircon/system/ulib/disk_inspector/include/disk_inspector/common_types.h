// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DISK_INSPECTOR_COMMON_TYPES_H_
#define DISK_INSPECTOR_COMMON_TYPES_H_

#include <zircon/assert.h>
#include <zircon/types.h>

#include <fbl/string.h>

#include "disk_inspector.h"

namespace disk_inspector {

template <typename T>
class PrimitiveType : public DiskObject {
 public:
  PrimitiveType() = delete;
  PrimitiveType(const PrimitiveType&) = delete;
  PrimitiveType(PrimitiveType&&) = delete;
  PrimitiveType& operator=(const PrimitiveType&) = delete;
  PrimitiveType& operator=(PrimitiveType&&) = delete;

  PrimitiveType(fbl::String name, const T* value);

  // DiskObject interface:
  const char* GetName() const override { return name_.c_str(); }

  void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

  uint32_t GetNumElements() const override { return 0; }

  std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override { return nullptr; }

 private:
  // Name of the field of the on-disk structure this object represents.
  fbl::String name_;

  // Pointer to memory location where the value is stored.
  const T* value_;
};

using DiskObjectUint64 = PrimitiveType<uint64_t>;
using DiskObjectUint32 = PrimitiveType<uint32_t>;
using DiskObjectChar = PrimitiveType<char>;

template <typename T>
class PrimitiveTypeArray : public DiskObject {
 public:
  PrimitiveTypeArray() = delete;
  PrimitiveTypeArray(const PrimitiveTypeArray&) = delete;
  PrimitiveTypeArray(PrimitiveTypeArray&&) = delete;
  PrimitiveTypeArray& operator=(const PrimitiveTypeArray&) = delete;
  PrimitiveTypeArray& operator=(PrimitiveTypeArray&&) = delete;

  PrimitiveTypeArray(fbl::String name, const T* value, size_t size);

  // DiskObject interface:
  const char* GetName() const override { return name_.c_str(); }

  void GetValue(const void** out_buffer, size_t* out_buffer_size) const override {
    // Invalid call for an array.
    ZX_DEBUG_ASSERT_MSG(false, "Invalid GetValue call for an array.");
  }

  uint32_t GetNumElements() const override { return static_cast<uint32_t>(size_); }

  std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

 private:
  // Name of the field of the on-disk structure this object represents.
  fbl::String name_;

  // Pointer to memory location where the value is stored.
  const T* value_;

  size_t size_;
};

using DiskObjectUint64Array = PrimitiveTypeArray<uint64_t>;
using DiskObjectUint32Array = PrimitiveTypeArray<uint32_t>;
using DiskObjectCharArray = PrimitiveTypeArray<char>;

}  // namespace disk_inspector

#endif  // DISK_INSPECTOR_COMMON_TYPES_H_
