// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <disk_inspector/common_types.h>

namespace disk_inspector {

template <typename T>
PrimitiveType<T>::PrimitiveType(fbl::String name, const T* value) : name_(name), value_(value) {
  ZX_DEBUG_ASSERT_MSG(value, "Null input buffer value is not allowed");
}

template <typename T>
void PrimitiveType<T>::GetValue(const void** out_buffer, size_t* out_buffer_size) const {
  ZX_DEBUG_ASSERT_MSG(out_buffer, "Null output buffer value is not allowed");
  ZX_DEBUG_ASSERT_MSG(out_buffer_size, "Zero-sized output buffer is not allowed");

  *out_buffer = value_;
  *out_buffer_size = sizeof(*value_);
}

template class PrimitiveType<uint32_t>;
template class PrimitiveType<uint64_t>;
template class PrimitiveType<char>;

template <typename T>
PrimitiveTypeArray<T>::PrimitiveTypeArray(fbl::String name, const T* value, size_t size)
    : name_(name), value_(value), size_(size) {
  ZX_DEBUG_ASSERT_MSG(value, "Null input buffer value is not allowed");
  ZX_DEBUG_ASSERT_MSG(size, "Zero-sized arrays are not allowed");
}

template <typename T>
std::unique_ptr<DiskObject> PrimitiveTypeArray<T>::GetElementAt(uint32_t index) const {
  if (index >= size_) {
    return nullptr;
  }
  return std::make_unique<PrimitiveType<T>>(name_, value_ + index);
}

template class PrimitiveTypeArray<uint32_t>;
template class PrimitiveTypeArray<uint64_t>;
template class PrimitiveTypeArray<char>;

}  // namespace disk_inspector
