// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vaapi_utils.h"

#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>

#include <filesystem>

#include <va/va_magma.h>

namespace {
std::unique_ptr<VADisplayWrapper> display_wrapper;
}

// static
bool VADisplayWrapper::InitializeSingleton(uint64_t required_vendor_id) {
  ZX_ASSERT(!display_wrapper);

  auto new_display_wrapper = std::make_unique<VADisplayWrapper>();

  for (auto& p : std::filesystem::directory_iterator("/dev/class/gpu")) {
    {
      zx::channel local, remote;
      zx_status_t zx_status = zx::channel::create(0 /*flags*/, &local, &remote);
      ZX_ASSERT(zx_status == ZX_OK);
      zx_status = fdio_service_connect(p.path().c_str(), remote.release());
      ZX_ASSERT(zx_status == ZX_OK);
      magma_status_t status =
          magma_device_import(local.release(), &new_display_wrapper->magma_device_);
      ZX_ASSERT(status == MAGMA_STATUS_OK);
      if (status != MAGMA_STATUS_OK)
        continue;
    }
    {
      uint64_t vendor_id;
      magma_status_t magma_status =
          magma_query2(new_display_wrapper->magma_device_, MAGMA_QUERY_VENDOR_ID, &vendor_id);
      if (magma_status == MAGMA_STATUS_OK && vendor_id == required_vendor_id) {
        break;
      }
    }

    magma_device_release(new_display_wrapper->magma_device_);
    new_display_wrapper->magma_device_ = {};
  }

  if (!new_display_wrapper->magma_device_)
    return false;

  if (!new_display_wrapper->Initialize())
    return false;
  display_wrapper = std::move(new_display_wrapper);
  return true;
}

// static
bool VADisplayWrapper::InitializeSingletonForTesting() {
  auto new_display_wrapper = std::make_unique<VADisplayWrapper>();
  if (!new_display_wrapper->Initialize())
    return false;
  display_wrapper = std::move(new_display_wrapper);
  return true;
}

bool VADisplayWrapper::Initialize() {
  display_ = vaGetDisplayMagma(magma_device_);
  if (!display_)
    return false;

  int major_ver, minor_ver;

  VAStatus va_status = vaInitialize(display_, &major_ver, &minor_ver);
  if (va_status != VA_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

// static
VADisplayWrapper* VADisplayWrapper::GetSingleton() { return display_wrapper.get(); }

static bool SupportsH264() {
  VADisplay display = VADisplayWrapper::GetSingleton()->display();
  constexpr VAProfile kProfile = VAProfileH264High;
  std::vector<VAEntrypoint> entrypoints(vaMaxNumEntrypoints(display));
  int num_entrypoints, vld_entrypoint;
  VAStatus va_status =
      vaQueryConfigEntrypoints(display, kProfile, entrypoints.data(), &num_entrypoints);
  if (va_status != VA_STATUS_SUCCESS)
    return false;

  for (vld_entrypoint = 0; vld_entrypoint < num_entrypoints; vld_entrypoint++) {
    if (entrypoints[vld_entrypoint] == VAEntrypointVLD)
      break;
  }
  if (vld_entrypoint == num_entrypoints) {
    return false;
  }

  VAConfigAttrib attrib;
  attrib.type = VAConfigAttribRTFormat;
  va_status = vaGetConfigAttributes(display, kProfile, VAEntrypointVLD, &attrib, 1);
  if (va_status != VA_STATUS_SUCCESS)
    return false;
  if ((attrib.value & VA_RT_FORMAT_YUV420) == 0) {
    return false;
  }
  return true;
}

std::vector<fuchsia::mediacodec::CodecDescription> GetCodecList() {
  std::vector<fuchsia::mediacodec::CodecDescription> descriptions;
  if (SupportsH264()) {
    fuchsia::mediacodec::CodecDescription description;
    description.codec_type = fuchsia::mediacodec::CodecType::DECODER;
    description.mime_type = "video/h264";
    descriptions.push_back(description);
    description.codec_type = fuchsia::mediacodec::CodecType::DECODER;
    description.mime_type = "video/h264-multi";
    descriptions.push_back(description);
  }
  return descriptions;
}
