// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_HW_ACCEL_TASK_TASK_H_
#define SRC_CAMERA_DRIVERS_HW_ACCEL_TASK_TASK_H_

#include <lib/fzl/pinned-vmo.h>
#include <lib/fzl/vmo-pool.h>
#include <lib/syslog/global.h>
#include <zircon/fidl.h>

#include <deque>

#include <ddktl/protocol/gdc.h>
#include <fbl/unique_ptr.h>

namespace generictask {
// The |Task| class store all the information pertaining to
// a task when registered. It maintains the VMO pool for the
// output buffer collections.
class GenericTask {
 public:
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

  image_format_2_t input_format() { return input_format_; }
  image_format_2_t output_format() {
    return output_image_format_list_[cur_output_image_format_index_];
  }
  const hw_accel_callback_t* callback() { return callback_; }

 protected:
  // Initializes a VMO pool from buffer collection for output buffer collection.
  // Pins the input buffer collection.
  zx_status_t InitBuffers(const buffer_collection_info_2_t* input_buffer_collection,
                          const buffer_collection_info_2_t* output_buffer_collection,
                          const image_format_2_t* input_image_format,
                          const image_format_2_t* output_image_format_table_list,
                          size_t output_image_format_table_count,
                          uint32_t output_image_format_index, const zx::bti& bti,
                          const hw_accel_callback_t* callback);

 private:
  size_t output_image_format_count_;
  std::unique_ptr<image_format_2_t[]> output_image_format_list_;
  uint32_t cur_output_image_format_index_;
  image_format_2_t input_format_;
  const hw_accel_callback_t* callback_;
  fzl::VmoPool output_buffers_;
  fbl::Array<fzl::PinnedVmo> input_buffers_;
  std::deque<fzl::VmoPool::Buffer> write_locked_buffers_;
};
}  // namespace generictask

#endif  // SRC_CAMERA_DRIVERS_HW_ACCEL_TASK_TASK_H_
