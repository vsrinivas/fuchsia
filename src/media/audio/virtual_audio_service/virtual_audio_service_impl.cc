// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/virtual_audio_service/virtual_audio_service_impl.h"

#include <fcntl.h>
#include <fuchsia/virtualaudio/c/fidl.h>
#include <lib/fdio/fdio.h>

#include "src/lib/fxl/logging.h"
#include "src/media/audio/drivers/virtual_audio/virtual_audio.h"

namespace virtual_audio {

VirtualAudioServiceImpl::VirtualAudioServiceImpl(
    std::unique_ptr<sys::ComponentContext> startup_context)
    : startup_context_(std::move(startup_context)) {
  FXL_DCHECK(startup_context_);

  startup_context_->outgoing()
      ->AddPublicService<fuchsia::virtualaudio::Control>(
          [this](
              fidl::InterfaceRequest<fuchsia::virtualaudio::Control> request) {
            if (driver_open_) {
              ForwardControlRequest(std::move(request));
            }
          });

  startup_context_->outgoing()->AddPublicService<fuchsia::virtualaudio::Input>(
      [this](fidl::InterfaceRequest<fuchsia::virtualaudio::Input> request) {
        if (driver_open_) {
          ForwardInputRequest(std::move(request));
        }
      });

  startup_context_->outgoing()->AddPublicService<fuchsia::virtualaudio::Output>(
      [this](fidl::InterfaceRequest<fuchsia::virtualaudio::Output> request) {
        if (driver_open_) {
          ForwardOutputRequest(std::move(request));
        }
      });
}

VirtualAudioServiceImpl::~VirtualAudioServiceImpl() {
  if (driver_open_) {
    FXL_LOG(INFO) << "Closing '" << ::virtual_audio::kCtlNodeName << "'";
    zx_handle_close(channel_handle_);
  }
}

// If we couldn't open the control driver, the service isn't operational.
zx_status_t VirtualAudioServiceImpl::Init() {
  return (OpenControlDriver() ? ZX_OK : ZX_ERR_INTERNAL);
}

// Return a bool representing whether control driver was successfully opened.
bool VirtualAudioServiceImpl::OpenControlDriver() {
  if (driver_open_) {
    FXL_LOG(WARNING) << "Already connected to '"
                     << ::virtual_audio::kCtlNodeName << "'";
    return true;
  }

  int ctl_node_file_desc = ::open(::virtual_audio::kCtlNodeName, O_RDONLY);

  if (ctl_node_file_desc <= 0) {
    FXL_LOG(WARNING) << "Failed to open '" << ::virtual_audio::kCtlNodeName
                     << "' - result " << ctl_node_file_desc;
    return false;
  }

  zx_status_t status =
      fdio_get_service_handle(ctl_node_file_desc, &channel_handle_);
  if (status != ZX_OK || channel_handle_ == ZX_KOID_INVALID) {
    FXL_LOG(WARNING) << "fdio_get_service_handle returned " << status
                     << "; handle is " << channel_handle_
                     << "; we will exit now";
    return false;
  }

  driver_open_ = true;

  return true;
}

zx_status_t VirtualAudioServiceImpl::ForwardControlRequest(
    fidl::InterfaceRequest<fuchsia::virtualaudio::Control> control_request) {
  auto control_request_handle = control_request.TakeChannel().release();

  zx_status_t status = fuchsia_virtualaudio_ForwarderSendControl(
      channel_handle_, control_request_handle);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << __func__ << " returned " << status;
  }

  return status;
}

zx_status_t VirtualAudioServiceImpl::ForwardInputRequest(
    fidl::InterfaceRequest<fuchsia::virtualaudio::Input> input_request) {
  auto input_request_handle = input_request.TakeChannel().release();

  zx_status_t status = fuchsia_virtualaudio_ForwarderSendInput(
      channel_handle_, input_request_handle);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << __func__ << " returned " << status;
  }

  return status;
}

zx_status_t VirtualAudioServiceImpl::ForwardOutputRequest(
    fidl::InterfaceRequest<fuchsia::virtualaudio::Output> output_request) {
  auto output_request_handle = output_request.TakeChannel().release();

  zx_status_t status = fuchsia_virtualaudio_ForwarderSendOutput(
      channel_handle_, output_request_handle);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << __func__ << " returned " << status;
  }

  return status;
}

}  // namespace virtual_audio
