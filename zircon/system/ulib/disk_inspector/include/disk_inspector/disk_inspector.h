// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains apis needed for inspection of on-disk data structures.

#ifndef DISK_INSPECTOR_DISK_INSPECTOR_H_
#define DISK_INSPECTOR_DISK_INSPECTOR_H_

#include <zircon/types.h>

#include <cstdint>
#include <memory>

namespace disk_inspector {

// This class is a generic "DiskObject" interface which enables inspection and accessing of
// various on-disk structures.
class DiskObject {
 public:
  virtual ~DiskObject() {}

  // Gets the object name.
  virtual const char* GetName() const = 0;

  // Gets the number of elements in the object.
  // Returns 0 on a scalar data types and number of elements on composite data types.
  virtual uint32_t GetNumElements() const = 0;

  // Returns the element at a particular index.
  // If this element is a scalar data type simply return nullptr.
  virtual std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const = 0;

  // Returns the exact value and size of the scalar data type.
  // On return, out_buffer will point to an internal buffer so it should not be released or
  // accessed after this object is destroyed, while out_buffer_size returns the actual size
  // of the out_buffer. The out_buffer and out_buffer_size values are valid and in scope as
  // long as the encapsulating DiskObject is valid/in scope.
  // GetValue should only be called on scalar data types. Calling GetValue on composite data
  // types is an error.
  virtual void GetValue(const void** out_buffer, size_t* out_buffer_size) const = 0;
};

// This class is an interface to access the root of the filesystem,
// FVM et al.
class DiskInspector {
 public:
  virtual ~DiskInspector() {}

  // Get the root disk object.
  virtual zx_status_t GetRoot(std::unique_ptr<DiskObject>* out) = 0;
};

}  // namespace disk_inspector

#endif  // DISK_INSPECTOR_DISK_INSPECTOR_H_
