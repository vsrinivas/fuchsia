// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_TASK_H_
#define SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_TASK_H_

#include <ddktl/protocol/gdc.h>
#include <fbl/unique_ptr.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/fzl/vmo-pool.h>
#include <lib/syslog/global.h>
#include <zircon/fidl.h>

#include <deque>

namespace gdc {
// The |Task| class store all the information pertaining to
// a task when registered. It maintains the VMO pool for the
// output buffer collections.
class Task {
 public:
  Task() = default;

  ~Task() = default;

  // Returns the physical address for the config VMO.
  zx_paddr_t GetConigVmoPhysAddr() const { return config_vmo_pinned_.region(0).phys_addr; }

  // Returns the physical address for the config VMO.
  uint64_t GetConigVmoPhysSize() const { return config_vmo_pinned_.region(0).size; }

  // Returns the physical address for the input buffer.
  // |input_buffer_index| : Index of the input buffer for which the address is
  // requested. |out| : Returns the physical address if the index provided is
  // valid.
  zx_status_t GetInputBufferPhysAddr(uint32_t input_buffer_index, zx_paddr_t* out) const;

  // Returns the size of the input buffer.
  // |input_buffer_index| : Index of the input buffer for which the address is
  // requested. |out| : Returns the size if the index provided is
  // valid.
  zx_status_t GetInputBufferPhysSize(uint32_t input_buffer_index, uint64_t* out) const;

  // Validates input buffer index.
  bool IsInputBufferIndexValid(uint32_t input_buffer_index) const {
    return input_buffer_index < input_buffers_.size();
  }

  // Returns the physical address for the output buffer which is
  // picked from the pool of free buffers.
  uint32_t GetOutputBufferPhysAddr() {
    auto buffer = output_buffers_.LockBufferForWrite();
    ZX_ASSERT(buffer);
    auto addr = static_cast<uint32_t>(buffer->physical_address());
    write_locked_buffers_.push_front(std::move(*buffer));
    return addr;
  }

  // Releases the write lock of the output buffer and returns back and index.
  uint32_t GetOutputBufferIndex() {
    auto index = write_locked_buffers_.back().ReleaseWriteLockAndGetIndex();
    write_locked_buffers_.pop_back();
    return index;
  }

  // Returns the output buffer back to the VMO pool to be reused again.
  zx_status_t ReleaseOutputBuffer(uint32_t index) { return output_buffers_.ReleaseBuffer(index); }

  image_format_t input_format() { return input_format_; }
  image_format_t output_format() { return output_format_; }
  const gdc_callback_t* callback() { return callback_; }

  // Static function to create a task object.
  // |input_buffer_collection|              : Input buffer collection.
  // |output_buffer_collection|             : Output buffer collection.
  // |config_vmo|                           : Configuration is stored in this
  // VMO. |callback|                             : Callback function to call for
  // this task. |out|                                  : Pointer to a task
  // object returned to the caller.
  static zx_status_t Create(const buffer_collection_info_t* input_buffer_collection,
                            const buffer_collection_info_t* output_buffer_collection,
                            const zx::vmo& config_vmo, const gdc_callback_t* callback,
                            const zx::bti& bti, std::unique_ptr<Task>* out);

 private:
  // Initializes a VMO pool from buffer collection for output buffer collection.
  // Pins the input buffer collection.
  zx_status_t InitBuffers(const buffer_collection_info_t* input_buffer_collection,
                          const buffer_collection_info_t* output_buffer_collection,
                          const zx::vmo& config_vmo, const zx::bti& bti);

  fzl::PinnedVmo config_vmo_pinned_;
  fzl::VmoPool output_buffers_;
  fbl::Array<fzl::PinnedVmo> input_buffers_;
  image_format_t input_format_;
  image_format_t output_format_;
  const gdc_callback_t* callback_;
  std::deque<fzl::VmoPool::Buffer> write_locked_buffers_;
};
}  // namespace gdc

#endif  // SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_TASK_H_
