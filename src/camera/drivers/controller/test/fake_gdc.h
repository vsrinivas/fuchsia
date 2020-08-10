// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_GDC_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_GDC_H_

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/sys/cpp/component_context.h>

#include <ddktl/protocol/gdc.h>

class FakeGdc {
 public:
  FakeGdc() {
    gdc_protocol_ops_.init_task = GdcInitTask;
    gdc_protocol_ops_.process_frame = GdcProcessFrame;
    gdc_protocol_ops_.release_frame = GdcReleaseFrame;
    gdc_protocol_ops_.remove_task = GdcRemoveTask;
    gdc_protocol_ops_.set_output_resolution = GdcSetOutputResolution;
    gdc_protocol_.ctx = this;
    gdc_protocol_.ops = &gdc_protocol_ops_;
  }

  ddk::GdcProtocolClient client() { return ddk::GdcProtocolClient(&gdc_protocol_); }

  fake_ddk::ProtocolEntry ProtocolEntry() const {
    return {ZX_PROTOCOL_GDC, *reinterpret_cast<const fake_ddk::Protocol*>(&gdc_protocol_)};
  }

  // |ZX_PROTOCOL_GDC|
  zx_status_t GdcInitTask(const buffer_collection_info_2_t* /*input_buffer_collection*/,
                          const buffer_collection_info_2_t* /*output_buffer_collection*/,
                          const image_format_2_t* /*input_image_format*/,
                          const image_format_2_t* /*output_image_format_table_list*/,
                          size_t /*output_image_format_table_count*/,
                          uint32_t output_image_format_index,
                          const gdc_config_info* /*config_vmo_list*/, size_t /*config_vmo_count*/,
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
  zx_status_t GdcProcessFrame(uint32_t /*task_index*/, uint32_t input_buffer_index) {
    frame_available_info info = {
        .frame_status = FRAME_STATUS_OK,
        .buffer_id = input_buffer_index,
        .metadata.input_buffer_index = input_buffer_index,
        .metadata.image_format_index = image_format_index_,
    };
    frame_callback_->frame_ready(frame_callback_->ctx, &info);
    return ZX_OK;
  }
  void GdcRemoveTask(uint32_t /*task_index*/) {
    remove_task_callback_->task_removed(remove_task_callback_->ctx, ZX_OK);
  }
  void GdcReleaseFrame(uint32_t /*task_index*/, uint32_t /*buffer_index*/) {
    frame_released_ = true;
  }

  zx_status_t GdcSetOutputResolution(uint32_t /*task_index*/,
                                     uint32_t new_output_image_format_index) {
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

  bool frame_released() const { return frame_released_; }

 private:
  static zx_status_t GdcInitTask(void* ctx,
                                 const buffer_collection_info_2_t* input_buffer_collection,
                                 const buffer_collection_info_2_t* output_buffer_collection,
                                 const image_format_2_t* input_image_format,
                                 const image_format_2_t* output_image_format_table_list,
                                 size_t output_image_format_table_count,
                                 uint32_t output_image_format_index,
                                 const gdc_config_info* config_vmo_list, size_t config_vmo_count,
                                 const hw_accel_frame_callback_t* frame_callback,
                                 const hw_accel_res_change_callback_t* res_callback,
                                 const hw_accel_remove_task_callback_t* task_remove_callback,
                                 uint32_t* out_task_index) {
    return static_cast<FakeGdc*>(ctx)->GdcInitTask(
        input_buffer_collection, output_buffer_collection, input_image_format,
        output_image_format_table_list, output_image_format_table_count, output_image_format_index,
        config_vmo_list, config_vmo_count, frame_callback, res_callback, task_remove_callback,
        out_task_index);
  }

  static zx_status_t GdcProcessFrame(void* ctx, uint32_t task_index, uint32_t input_buffer_index) {
    return static_cast<FakeGdc*>(ctx)->GdcProcessFrame(task_index, input_buffer_index);
  }

  static void GdcRemoveTask(void* ctx, uint32_t task_index) {
    return static_cast<FakeGdc*>(ctx)->GdcRemoveTask(task_index);
  }

  static void GdcReleaseFrame(void* ctx, uint32_t task_index, uint32_t buffer_index) {
    return static_cast<FakeGdc*>(ctx)->GdcReleaseFrame(task_index, buffer_index);
  }

  static zx_status_t GdcSetOutputResolution(void* ctx, uint32_t task_index,
                                            uint32_t new_output_image_format_index) {
    return static_cast<FakeGdc*>(ctx)->GdcSetOutputResolution(task_index,
                                                              new_output_image_format_index);
  }

  gdc_protocol_t gdc_protocol_ = {};
  gdc_protocol_ops_t gdc_protocol_ops_ = {};
  const hw_accel_remove_task_callback_t* remove_task_callback_;
  const hw_accel_res_change_callback_t* res_change_callback_;
  const hw_accel_frame_callback_t* frame_callback_;
  bool frame_released_ = false;
  uint32_t image_format_index_ = -1;
};

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_GDC_H_
