// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_HW_ACCEL_TASK_TASK_H_
#define SRC_CAMERA_DRIVERS_HW_ACCEL_TASK_TASK_H_

#include <lib/fit/result.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/fzl/vmo-pool.h>
#include <zircon/device/sysmem.h>
#include <zircon/fidl.h>

#include <deque>

#include <ddktl/protocol/camerahwaccel.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

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
  fit::result<uint32_t, zx_status_t> GetOutputBufferPhysAddr() {
    fbl::AutoLock lock(&output_vmo_pool_lock_);
    auto buffer = output_buffers_.LockBufferForWrite();
    if (!buffer) {
      return fit::error(ZX_ERR_NO_MEMORY);
    }
    auto addr = static_cast<uint32_t>(buffer->physical_address());
    write_locked_buffers_.push_front(std::move(*buffer));
    return fit::ok(addr);
  }

  std::optional<fzl::VmoPool::Buffer> WriteLockOutputBuffer() {
    fbl::AutoLock lock(&output_vmo_pool_lock_);
    auto buffer = output_buffers_.LockBufferForWrite();
    if (buffer)
      return std::move(*buffer);
    return {};
  }

  // Releases the write lock of the output buffer and returns back and index.
  uint32_t GetOutputBufferIndex() {
    fbl::AutoLock lock(&output_vmo_pool_lock_);
    auto index = write_locked_buffers_.back().ReleaseWriteLockAndGetIndex();
    write_locked_buffers_.pop_back();
    return index;
  }

  // Returns the output buffer back to the VMO pool to be reused again.
  zx_status_t ReleaseOutputBuffer(uint32_t index) {
    fbl::AutoLock lock(&output_vmo_pool_lock_);
    return output_buffers_.ReleaseBuffer(index);
  }

  // Returns the output buffer back to the VMO pool to be reused again.
  zx_status_t ReleaseOutputBuffer(fzl::VmoPool::Buffer buffer) {
    fbl::AutoLock lock(&output_vmo_pool_lock_);
    return buffer.Release();
  }

  // Validates input buffer index.
  bool IsInputFormatIndexValid(uint32_t input_format_index) const {
    return input_format_index < input_image_format_count_;
  }
  // Validates output buffer index.
  bool IsOutputFormatIndexValid(uint32_t output_format_index) const {
    return output_format_index < output_image_format_count_;
  }
  uint32_t input_format_index() const { return cur_input_image_format_index_; }
  uint32_t output_format_index() const { return cur_output_image_format_index_; }
  void set_input_format_index(uint32_t new_index) { cur_input_image_format_index_ = new_index; }
  void set_output_format_index(uint32_t new_index) { cur_output_image_format_index_ = new_index; }
  image_format_2_t input_format() {
    return input_image_format_list_[cur_input_image_format_index_];
  }
  image_format_2_t output_format() {
    return output_image_format_list_[cur_output_image_format_index_];
  }

  void FrameReadyCallback(const frame_available_info_t* info) {
    frame_callback_->frame_ready(frame_callback_->ctx, info);
  }

  void ResolutionChangeCallback(const frame_available_info_t* info) {
    res_callback_->frame_resolution_changed(res_callback_->ctx, info);
  }

  void RemoveTaskCallback(task_remove_status_t status) {
    remove_task_callback_->task_removed(remove_task_callback_->ctx, status);
  }

 protected:
  // Initializes a VMO pool from buffer collection for output buffer collection.
  // Pins the input buffer collection.
  zx_status_t InitBuffers(const buffer_collection_info_2_t* input_buffer_collection,
                          const buffer_collection_info_2_t* output_buffer_collection,
                          const image_format_2_t* input_image_format_table_list,
                          size_t input_image_format_table_count, uint32_t input_image_format_index,
                          const image_format_2_t* output_image_format_table_list,
                          size_t output_image_format_table_count,
                          uint32_t output_image_format_index, const zx::bti& bti,
                          const hw_accel_frame_callback_t* frame_callback,
                          const hw_accel_res_change_callback_t* res_callback,
                          const hw_accel_remove_task_callback_t* remove_task_callback);

  // Just pins the input buffer collection.
  zx_status_t InitInputBuffers(const buffer_collection_info_2_t* input_buffer_collection,
                               const image_format_2_t* input_image_format_table_list,
                               size_t input_image_format_table_count,
                               uint32_t input_image_format_index, const zx::bti& bti,
                               const hw_accel_frame_callback_t* frame_callback,
                               const hw_accel_res_change_callback_t* res_callback,
                               const hw_accel_remove_task_callback_t* remove_task_callback);

  // Guards Allocations and Frees of buffers in the output pool.

 private:
  fbl::Mutex output_vmo_pool_lock_;
  size_t input_image_format_count_;
  std::unique_ptr<image_format_2_t[]> input_image_format_list_;
  uint32_t cur_input_image_format_index_;
  size_t output_image_format_count_;
  std::unique_ptr<image_format_2_t[]> output_image_format_list_;
  uint32_t cur_output_image_format_index_ = UINT32_MAX;
  const hw_accel_frame_callback_t* frame_callback_;
  const hw_accel_res_change_callback_t* res_callback_;
  const hw_accel_remove_task_callback_t* remove_task_callback_;
  fzl::VmoPool output_buffers_;
  fbl::Array<fzl::PinnedVmo> input_buffers_;
  std::deque<fzl::VmoPool::Buffer> write_locked_buffers_;
};
}  // namespace generictask

#endif  // SRC_CAMERA_DRIVERS_HW_ACCEL_TASK_TASK_H_
