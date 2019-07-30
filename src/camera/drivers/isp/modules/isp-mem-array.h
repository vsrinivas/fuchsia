// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_ISP_MODULES_ISP_MEM_ARRAY_H_
#define SRC_CAMERA_DRIVERS_ISP_MODULES_ISP_MEM_ARRAY_H_

#include <lib/mmio/mmio.h>
#include <zircon/types.h>

#include <vector>

#include <hwreg/bitfields.h>

namespace camera {

// Register definition for generic 32 bit data field in array.
class IspMemArray32Reg : public hwreg::RegisterBase<IspMemArray32Reg, uint32_t> {
 public:
  DEF_FIELD(31, 0, value);
};

// Wrapper around data types that would be stored in an array.
// An example of this would be the ihist stats mem array.
template <typename T>
class IspMemArray {
 public:
  // Constructs and IspMemArray.
  // Args:
  // |mmio|: Mmio to access the registers.
  // |start_addr|: Starting address for chunk of memory.
  // |size|: Number of elements represented in array (not number of registers).
  IspMemArray(ddk::MmioView mmio, uint32_t start_addr, uint32_t size)
      : start_addr_(start_addr), data_size_(size), data_(size, 0), mmio_(mmio) {}

  virtual ~IspMemArray() {}

  // R/W access to the data in the array.
  // This accesses the class vector and not the registers.
  // Args:
  // |idx|: The index of the data to access (not index of register).
  T& operator[](uint32_t idx) { return data_.at(idx); }

  // R only access to the data in the array.
  // This pulls the data from the registers.
  // Args:
  // |idx|: The index of the data to access (not index of register).
  virtual T operator[](uint32_t idx) const = 0;

  // Writes the data from the to the registers.
  virtual void WriteRegisters() {}

 protected:
  uint32_t start_addr_;
  uint32_t data_size_;
  std::vector<T> data_;
  ddk::MmioView mmio_;
};

// |IspMemArray| for 32 bit data.
class IspMemArray32 : public IspMemArray<uint32_t> {
 public:
  // Constructs and IspMemArray.
  // Args:
  // |mmio|: Mmio to access the registers.
  // |start_addr|: Starting address for chunk of memory.
  // |size|: Number of elements represented in array (not number of registers).
  IspMemArray32(ddk::MmioView mmio, uint32_t start_addr, uint32_t size)
      : IspMemArray(mmio, start_addr, size) {}

  ~IspMemArray32() {}

  // R/W access to the data in the array.
  // This accesses the class vector and not the registers.
  // Args:
  // |idx|: The index of the data to access (not index of register).
  uint32_t& operator[](uint32_t idx) { return data_.at(idx); }

  // Read only access to the data in the array.
  // This pulls the data from the registers.
  // Args:
  // |idx|: The index of the data to access (non index of register).
  uint32_t operator[](uint32_t idx) const override;

  // Writes the data from the to the registers.
  void WriteRegisters() override;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_ISP_MODULES_ISP_MEM_ARRAY_H_
