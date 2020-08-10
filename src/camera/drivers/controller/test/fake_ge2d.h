// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_GE2D_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_GE2D_H_

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/sys/cpp/component_context.h>

#include <ddktl/protocol/ge2d.h>
class FakeGe2d;

class FakeGe2d {
 public:
  FakeGe2d() {
    ge2d_protocol_ops_.init_task_resize = Ge2dInitTaskResize;
    ge2d_protocol_ops_.init_task_water_mark = Ge2dInitTaskWaterMark;
    ge2d_protocol_ops_.init_task_in_place_water_mark = Ge2dInitTaskInPlaceWaterMark;
    ge2d_protocol_ops_.process_frame = Ge2dProcessFrame;
    ge2d_protocol_ops_.remove_task = Ge2dRemoveTask;
    ge2d_protocol_ops_.release_frame = Ge2dReleaseFrame;
    ge2d_protocol_ops_.set_output_resolution = Ge2dSetOutputResolution;
    ge2d_protocol_ops_.set_input_and_output_resolution = Ge2dSetInputAndOutputResolution;
    ge2d_protocol_ops_.set_crop_rect = Ge2dSetCropRect;
    ge2d_protocol_.ctx = this;
    ge2d_protocol_.ops = &ge2d_protocol_ops_;
  }

  ddk::Ge2dProtocolClient client() { return ddk::Ge2dProtocolClient(&ge2d_protocol_); }

  fake_ddk::ProtocolEntry ProtocolEntry() const {
    return {ZX_PROTOCOL_GE2D, *reinterpret_cast<const fake_ddk::Protocol*>(&ge2d_protocol_)};
  }

  // ZX_PROTOCOL_GE2DC (Refer to ge2d.banjo for documentation).
  zx_status_t Ge2dInitTaskResize(const buffer_collection_info_2_t* /*input_buffer_collection*/,
                                 const buffer_collection_info_2_t* /*output_buffer_collection*/,
                                 const resize_info_t* /*info*/,
                                 const image_format_2_t* /*input_image_format*/,
                                 const image_format_2_t* /*output_image_format_table_list*/,
                                 size_t /*output_image_format_table_count*/,
                                 uint32_t output_image_format_index,
                                 const hw_accel_frame_callback_t* frame_callback,
                                 const hw_accel_res_change_callback_t* res_callback,
                                 const hw_accel_remove_task_callback_t* remove_task_callback,
                                 uint32_t* /*out_task_index*/) {
    remove_task_callback_ = remove_task_callback;
    frame_callback_ = frame_callback;
    res_change_callback_ = res_callback;
    image_format_index_ = output_image_format_index;
    return ZX_OK;
  }
  zx_status_t Ge2dInitTaskInPlaceWaterMark(
      const buffer_collection_info_2_t* /*output_buffer_collection*/,
      const water_mark_info_t* /*info_list*/, size_t /*info_count*/,
      const image_format_2_t* /*image_format_table_list*/, size_t /*image_format_table_count*/,
      uint32_t image_format_index, const hw_accel_frame_callback_t* frame_callback,
      const hw_accel_res_change_callback_t* res_callback,
      const hw_accel_remove_task_callback_t* remove_task_callback, uint32_t* /*out_task_index*/) {
    remove_task_callback_ = remove_task_callback;
    frame_callback_ = frame_callback;
    res_change_callback_ = res_callback;
    image_format_index_ = image_format_index;
    return ZX_OK;
  }

  zx_status_t Ge2dInitTaskWaterMark(const buffer_collection_info_2_t* /*input_buffer_collection*/,
                                    const buffer_collection_info_2_t* /*output_buffer_collection*/,
                                    const water_mark_info_t* /*info_list*/, size_t /*info_count*/,
                                    const image_format_2_t* /*image_format_table_list*/,
                                    size_t /*image_format_table_count*/,
                                    uint32_t image_format_index,
                                    const hw_accel_frame_callback_t* frame_callback,
                                    const hw_accel_res_change_callback_t* res_callback,
                                    const hw_accel_remove_task_callback_t* remove_task_callback,
                                    uint32_t* /*out_task_index*/) {
    remove_task_callback_ = remove_task_callback;
    frame_callback_ = frame_callback;
    res_change_callback_ = res_callback;
    image_format_index_ = image_format_index;
    return ZX_OK;
  }
  zx_status_t Ge2dProcessFrame(uint32_t /*task_index*/, uint32_t input_buffer_index) {
    frame_available_info info = {
        .frame_status = FRAME_STATUS_OK,
        .buffer_id = input_buffer_index,
        .metadata.input_buffer_index = input_buffer_index,
        .metadata.image_format_index = image_format_index_,
    };
    frame_callback_->frame_ready(frame_callback_->ctx, &info);
    return ZX_OK;
  }
  void Ge2dRemoveTask(uint32_t task_index) {}
  void Ge2dReleaseFrame(uint32_t task_index, uint32_t buffer_index) {}
  zx_status_t Ge2dSetInputAndOutputResolution(uint32_t /*task_index*/,
                                              uint32_t new_image_format_index) {
    image_format_index_ = new_image_format_index;
    image_format_index_ = new_image_format_index;
    frame_available_info info = {
        .frame_status = FRAME_STATUS_OK,
        .buffer_id = 0,
        .metadata.input_buffer_index = 0,
        .metadata.image_format_index = image_format_index_,
    };
    res_change_callback_->frame_resolution_changed(res_change_callback_->ctx, &info);
    return ZX_OK;
  }
  zx_status_t Ge2dSetOutputResolution(uint32_t /*task_index*/,
                                      uint32_t new_output_image_format_index) {
    image_format_index_ = new_output_image_format_index;
    image_format_index_ = new_output_image_format_index;
    frame_available_info info = {
        .frame_status = FRAME_STATUS_OK,
        .buffer_id = 0,
        .metadata.input_buffer_index = 0,
        .metadata.image_format_index = image_format_index_,
    };
    res_change_callback_->frame_resolution_changed(res_change_callback_->ctx, &info);
    return ZX_OK;
  }

  void Ge2dSetCropRect(uint32_t task_index, const rect_t* crop) {}

 private:
  static zx_status_t Ge2dInitTaskResize(
      void* ctx, const buffer_collection_info_2_t* input_buffer_collection,
      const buffer_collection_info_2_t* output_buffer_collection, const resize_info_t* info,
      const image_format_2_t* input_image_format,
      const image_format_2_t* output_image_format_table_list,
      size_t output_image_format_table_count, uint32_t output_image_format_index,
      const hw_accel_frame_callback_t* frame_callback,
      const hw_accel_res_change_callback_t* res_callback,
      const hw_accel_remove_task_callback_t* task_remove_callback, uint32_t* out_task_index) {
    return static_cast<FakeGe2d*>(ctx)->Ge2dInitTaskResize(
        input_buffer_collection, output_buffer_collection, info, input_image_format,
        output_image_format_table_list, output_image_format_table_count, output_image_format_index,
        frame_callback, res_callback, task_remove_callback, out_task_index);
  }

  static zx_status_t Ge2dInitTaskWaterMark(
      void* ctx, const buffer_collection_info_2_t* input_buffer_collection,
      const buffer_collection_info_2_t* output_buffer_collection,
      const water_mark_info_t* info_list, size_t info_count,
      const image_format_2_t* image_format_table_list, size_t image_format_table_count,
      uint32_t image_format_index, const hw_accel_frame_callback_t* frame_callback,
      const hw_accel_res_change_callback_t* res_callback,
      const hw_accel_remove_task_callback_t* task_remove_callback, uint32_t* out_task_index) {
    return static_cast<FakeGe2d*>(ctx)->Ge2dInitTaskWaterMark(
        input_buffer_collection, output_buffer_collection, info_list, info_count,
        image_format_table_list, image_format_table_count, image_format_index, frame_callback,
        res_callback, task_remove_callback, out_task_index);
  }

  static zx_status_t Ge2dInitTaskInPlaceWaterMark(
      void* ctx, const buffer_collection_info_2_t* buffer_collection,
      const water_mark_info_t* info_list, size_t info_count,
      const image_format_2_t* image_format_table_list, size_t image_format_table_count,
      uint32_t image_format_index, const hw_accel_frame_callback_t* frame_callback,
      const hw_accel_res_change_callback_t* res_callback,
      const hw_accel_remove_task_callback_t* task_remove_callback, uint32_t* out_task_index) {
    return static_cast<FakeGe2d*>(ctx)->Ge2dInitTaskInPlaceWaterMark(
        buffer_collection, info_list, info_count, image_format_table_list, image_format_table_count,
        image_format_index, frame_callback, res_callback, task_remove_callback, out_task_index);
  }

  static zx_status_t Ge2dProcessFrame(void* ctx, uint32_t task_index, uint32_t input_buffer_index) {
    return static_cast<FakeGe2d*>(ctx)->Ge2dProcessFrame(task_index, input_buffer_index);
  }

  static void Ge2dRemoveTask(void* ctx, uint32_t task_index) {
    return static_cast<FakeGe2d*>(ctx)->Ge2dRemoveTask(task_index);
  }

  static void Ge2dReleaseFrame(void* ctx, uint32_t task_index, uint32_t buffer_index) {
    return static_cast<FakeGe2d*>(ctx)->Ge2dReleaseFrame(task_index, buffer_index);
  }

  static zx_status_t Ge2dSetInputAndOutputResolution(void* ctx, uint32_t task_index,
                                                     uint32_t new_image_format_index) {
    return static_cast<FakeGe2d*>(ctx)->Ge2dSetInputAndOutputResolution(task_index,
                                                                        new_image_format_index);
  }

  static zx_status_t Ge2dSetOutputResolution(void* ctx, uint32_t task_index,
                                             uint32_t new_output_image_format_index) {
    return static_cast<FakeGe2d*>(ctx)->Ge2dSetOutputResolution(task_index,
                                                                new_output_image_format_index);
  }

  static void Ge2dSetCropRect(void* ctx, uint32_t task_index, const rect_t* crop) {
    return static_cast<FakeGe2d*>(ctx)->Ge2dSetCropRect(task_index, crop);
  }

  ge2d_protocol_t ge2d_protocol_ = {};
  ge2d_protocol_ops_t ge2d_protocol_ops_ = {};
  const hw_accel_remove_task_callback_t* remove_task_callback_;
  const hw_accel_frame_callback_t* frame_callback_;
  const hw_accel_res_change_callback_t* res_change_callback_;
  uint32_t image_format_index_ = -1;
};

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_GE2D_H_
