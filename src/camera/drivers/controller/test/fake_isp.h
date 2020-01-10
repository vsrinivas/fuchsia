// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_ISP_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_ISP_H_

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/sys/cpp/component_context.h>

#include <ddktl/protocol/isp.h>

class FakeIsp {
 public:
  FakeIsp() {
    isp_protocol_ops_.create_output_stream = IspCreateOutputStream;
    isp_protocol_.ctx = this;
    isp_protocol_.ops = &isp_protocol_ops_;
  }

  ddk::IspProtocolClient client() { return ddk::IspProtocolClient(&isp_protocol_); }

  fake_ddk::ProtocolEntry ProtocolEntry() const {
    return {ZX_PROTOCOL_ISP, *reinterpret_cast<const fake_ddk::Protocol*>(&isp_protocol_)};
  }

  void PopulateStreamProtocol(output_stream_protocol_t* out_s) {
    out_s->ctx = this;
    out_s->ops->start = Start;
    out_s->ops->stop = Stop;
    out_s->ops->release_frame = ReleaseFrame;
  }

  zx_status_t Start() { return ZX_OK; }
  zx_status_t Stop() { return ZX_OK; }
  zx_status_t ReleaseFrame(uint32_t buffer_index) {
    frame_released_ = true;
    return ZX_OK;
  }

  // |ZX_PROTOCOL_ISP|
  zx_status_t IspCreateOutputStream(const buffer_collection_info_2_t* buffer_collection,
                                    const image_format_2_t* image_format, const frame_rate_t* rate,
                                    stream_type_t type,
                                    const hw_accel_frame_callback_t* frame_callback,
                                    output_stream_protocol_t* out_s) {
    frame_callback_ = frame_callback;
    out_s->ctx = this;
    out_s->ops->start = Start;
    out_s->ops->stop = Stop;
    out_s->ops->release_frame = ReleaseFrame;
    return ZX_OK;
  }

  bool frame_released() { return frame_released_; }

 private:
  static zx_status_t IspCreateOutputStream(void* ctx,
                                           const buffer_collection_info_2_t* buffer_collection,
                                           const image_format_2_t* image_format,
                                           const frame_rate_t* rate, stream_type_t type,
                                           const hw_accel_frame_callback_t* frame_callback,
                                           output_stream_protocol_t* out_st) {
    return static_cast<FakeIsp*>(ctx)->IspCreateOutputStream(buffer_collection, image_format, rate,
                                                             type, frame_callback, out_st);
  }

  static zx_status_t Start(void* ctx) { return static_cast<FakeIsp*>(ctx)->Start(); }
  static zx_status_t Stop(void* ctx) { return static_cast<FakeIsp*>(ctx)->Stop(); }
  static zx_status_t ReleaseFrame(void* ctx, uint32_t index) {
    return static_cast<FakeIsp*>(ctx)->ReleaseFrame(index);
  }

  const hw_accel_frame_callback_t* frame_callback_;
  isp_protocol_t isp_protocol_ = {};
  isp_protocol_ops_t isp_protocol_ops_ = {};
  bool frame_released_ = false;
};

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_ISP_H_
