// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_USB_DEVICE_UVC_HACK_H_
#define SRC_CAMERA_BIN_USB_DEVICE_UVC_HACK_H_

#include <fuchsia/camera/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

// These are all of the constants required for the NUC+UVC+Meet proof-of-concept. All of these
// should eventually be removed and replaced with structured code querying the actual physical
// device.
namespace camera {

constexpr uint32_t kUvcHackWidth = 640;
constexpr uint32_t kUvcHackHeight = 480;
constexpr uint32_t kUvcHackFrameRateNumerator = 10000000;
constexpr uint32_t kUvcHackFrameRateDenominator = 333333;

// Client-facing side (NV12)
constexpr auto kUvcHackClientPixelFormatType = fuchsia::sysmem::PixelFormatType::NV12;
constexpr auto kUvcHackClientColorSpaceType = fuchsia::sysmem::ColorSpaceType::REC709;

constexpr uint32_t kUvcHackClientCodedWidth = kUvcHackWidth;
constexpr uint32_t kUvcHackClientCodedHeight = kUvcHackHeight;
constexpr uint32_t kUvcHackClientBytesPerRow = kUvcHackWidth;
constexpr uint32_t kUvcHackClientDisplayWidth = kUvcHackWidth;
constexpr uint32_t kUvcHackClientDisplayHeight = kUvcHackHeight;
constexpr uint32_t kUvcHackClientLayers = 1;

constexpr uint32_t kUvcHackClientCodedWidthDivisor = 128;
constexpr uint32_t kUvcHackClientCodedHeightDivisor = 4;
constexpr uint32_t kUvcHackClientBytesPerRowDivisor = 128;
constexpr uint32_t kUvcHackClientStartOffsetDivisor = 128;
constexpr uint32_t kUvcHackClientDisplayWidthDivisor = 128;
constexpr uint32_t kUvcHackClientDisplayHeightDivisor = 4;

constexpr uint32_t kUvcHackClientMinBufferCountForCamping = 2;
constexpr uint32_t kUvcHackClientMaxBufferCountForCamping = 8;
constexpr uint32_t kUvcHackClientMinBufferCountForDedicatedSlack = 2;
constexpr uint32_t kUvcHackClientMinBufferCountForSharedSlack = 2;
constexpr uint32_t kUvcHackClientMinBufferCount = 16;
constexpr uint32_t kUvcHackClientMaxBufferCount = 16;

// Driver-facing side (YUY2)
constexpr auto kUvcHackDriverPixelFormatType = fuchsia::sysmem::PixelFormatType::YUY2;
constexpr auto kUvcHackDriverColorSpaceType = fuchsia::sysmem::ColorSpaceType::REC709;

constexpr uint32_t kUvcHackDriverWidth = kUvcHackWidth;
constexpr uint32_t kUvcHackDriverHeight = kUvcHackHeight;
constexpr uint32_t kUvcHackDriverBytesPerRow = kUvcHackWidth * 2;
constexpr uint32_t kUvcHackDriverLayers = 1;

void UvcHackGetClientBufferImageFormatConstraints(
    fuchsia::sysmem::ImageFormatConstraints* image_format_constraints);
void UvcHackGetClientBufferCollectionConstraints(
    fuchsia::sysmem::BufferCollectionConstraints* buffer_collection_constraints);
void UvcHackGetClientStreamProperties(fuchsia::camera3::StreamProperties* stream_properties);
void UvcHackGetClientStreamProperties2(fuchsia::camera3::StreamProperties2* stream_properties);

void UvcHackGetServerFrameRate(fuchsia::camera::FrameRate* frame_rate);
void UvcHackGetServerBufferVideoFormat(fuchsia::camera::VideoFormat* video_format);
void UvcHackConvertYUY2ToNV12(uint8_t* client_frame, const uint8_t* driver_frame);

}  // namespace camera

#endif  // SRC_CAMERA_BIN_USB_DEVICE_UVC_HACK_H_
