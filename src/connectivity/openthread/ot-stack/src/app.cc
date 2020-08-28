// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include <fcntl.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/epitaph.h>
#include <lib/svc/dir.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

namespace otstack {

OtStackApp::LowpanSpinelDeviceFidlImpl::LowpanSpinelDeviceFidlImpl(OtStackApp& app) : app_(app) {}

void OtStackApp::LowpanSpinelDeviceFidlImpl::Bind(async_dispatcher_t* dispatcher,
                                                  const char* service_name,
                                                  zx_handle_t service_request) {
  fidl::OnUnboundFn<OtStackApp::LowpanSpinelDeviceFidlImpl> on_unbound =
      [](LowpanSpinelDeviceFidlImpl*, fidl::UnbindInfo info, zx::channel channel) {
        FX_LOGS(INFO) << "channel handle " << channel.get() << " unbound with reason "
                      << static_cast<uint32_t>(info.reason);
      };
  auto res =
      fidl::BindServer(dispatcher, zx::channel(service_request), this, std::move(on_unbound));
  if (res.is_error()) {
    FX_LOGS(ERROR) << "Failed to bind FIDL server with status: " << res.error();
    return;
  }
  app_.binding_ = res.take_value();
}

void OtStackApp::LowpanSpinelDeviceFidlImpl::Open(OpenCompleter::Sync completer) {
  zx_status_t status = ZX_OK;
  if (app_.connected_to_device_ == false) {
    FX_LOGS(INFO) << "ot-radio not connected, trying to reconnect";
    status = app_.ConnectToOtRadioDev();
  }
  if (status != ZX_OK) {
    FX_LOGS(INFO) << "error connecting to ot-radio";
    completer.ReplyError(fidl_spinel::Error::UNSPECIFIED);
    app_.Shutdown();
    return;
  }
  FX_LOGS(INFO) << "FIDL request Open got";
  auto fidl_result = app_.device_client_ptr_->Open();
  if (fidl_result.status() != ZX_OK) {
    FX_LOGS(ERROR) << "FIDL error while sending req to ot-radio";
    completer.ReplyError(fidl_spinel::Error::UNSPECIFIED);
    app_.Shutdown();
    return;
  }
  auto* result = fidl_result.Unwrap();
  completer.Reply(std::move(result->result));
}

void OtStackApp::LowpanSpinelDeviceFidlImpl::Close(CloseCompleter::Sync completer) {
  if (app_.connected_to_device_ == false) {
    FX_LOGS(ERROR) << "ot-radio not connected";
    completer.ReplyError(fidl_spinel::Error::UNSPECIFIED);
    app_.Shutdown();
    return;
  }
  auto fidl_result = app_.device_client_ptr_->Close();
  if (fidl_result.status() != ZX_OK) {
    FX_LOGS(ERROR) << "FIDL error while sending req to ot-radio";
    completer.ReplyError(fidl_spinel::Error::UNSPECIFIED);
    app_.Shutdown();
    return;
  }
  auto* result = fidl_result.Unwrap();
  completer.Reply(std::move(result->result));
}

void OtStackApp::LowpanSpinelDeviceFidlImpl::GetMaxFrameSize(
    GetMaxFrameSizeCompleter::Sync completer) {
  if (app_.connected_to_device_ == false) {
    FX_LOGS(ERROR) << "ot-radio not connected";
    app_.Shutdown();
    return;
  }
  auto fidl_result = app_.device_client_ptr_->GetMaxFrameSize();
  if (fidl_result.status() != ZX_OK) {
    FX_LOGS(ERROR) << "FIDL error while sending req to ot-radio";
    app_.Shutdown();
    return;
  }
  auto* result = fidl_result.Unwrap();
  completer.Reply(result->size);
}

void OtStackApp::LowpanSpinelDeviceFidlImpl::SendFrame(::fidl::VectorView<uint8_t> data,
                                                       SendFrameCompleter::Sync completer) {
  if (app_.connected_to_device_ == false) {
    FX_LOGS(ERROR) << "ot-radio not connected";
    return;
  }
  auto fidl_result = app_.device_client_ptr_->SendFrame(std::move(data));
  if (fidl_result.status() != ZX_OK) {
    FX_LOGS(ERROR) << "FIDL error while sending req to ot-radio";
    return;
  }
}

void OtStackApp::LowpanSpinelDeviceFidlImpl::ReadyToReceiveFrames(
    uint32_t number_of_frames, ReadyToReceiveFramesCompleter::Sync completer) {
  if (app_.connected_to_device_ == false) {
    FX_LOGS(ERROR) << "ot-radio not connected";
    return;
  }
  auto fidl_result = app_.device_client_ptr_->ReadyToReceiveFrames(number_of_frames);
  if (fidl_result.status() != ZX_OK) {
    FX_LOGS(ERROR) << "FIDL error while sending req to ot-radio";
    return;
  }
}

static void connect(void* untyped_context, const char* service_name, zx_handle_t service_request) {
  auto app = static_cast<OtStackApp*>(untyped_context);
  app->AddFidlRequestHandler(service_name, service_request);
}

void OtStackApp::AddFidlRequestHandler(const char* service_name, zx_handle_t service_request) {
  if (binding_) {  // TODO (jiamingw) add support for multiple clients
    FX_LOGS(ERROR) << "FIDL connect request rejected: already bound";
    return;
  }
  fidl_request_handler_ptr_->Bind(loop_.dispatcher(), service_name, service_request);
}

// Setup FIDL server side which handle requests from upper layer components.
zx_status_t OtStackApp::SetupFidlService() {
  zx_handle_t directory_request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);

  if (directory_request == ZX_HANDLE_INVALID) {
    FX_LOGS(ERROR) << "Got invalid directory_request channel";
    return ZX_ERR_INTERNAL;
  }

  svc_dir_t* dir = nullptr;
  zx_status_t status = svc_dir_create(loop_.dispatcher(), directory_request, &dir);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Error creating service directory";
    return status;
  }

  fidl_request_handler_ptr_ = std::make_unique<LowpanSpinelDeviceFidlImpl>(*this);

  status = svc_dir_add_service(dir, "svc", "fuchsia.lowpan.spinel.Device", this, connect);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Error adding service in ot-stack";
    return status;
  }
  return ZX_OK;
}

zx_status_t OtStackApp::Init(const std::string& path, bool is_test_env) {
  is_test_env_ = is_test_env;
  device_path_ = path;
  zx_status_t result = ConnectToOtRadioDev();
  if (result != ZX_OK) {
    return result;
  }
  return SetupFidlService();
}

// Connect to ot-radio device driver which allows ot-stack to talk to lower layer
zx_status_t OtStackApp::ConnectToOtRadioDev() {
  zx_status_t result = ZX_OK;
  if (is_test_env_) {
    result = SetDeviceSetupClientInIsolatedDevmgr(device_path_);
  } else {
    result = SetDeviceSetupClientInDevmgr(device_path_);
  }
  if (result != ZX_OK) {
    FX_LOGS(ERROR) << "failed to set device setup client";
    return result;
  }
  return SetupOtRadioDev();
}

// Get the spinel setup client from a file path. Set `client_ptr_` on success.
zx_status_t OtStackApp::SetDeviceSetupClientInDevmgr(const std::string& path) {
  fbl::unique_fd fd(open(path.c_str(), O_RDWR));
  if (!fd.is_valid()) {
    FX_LOGS(ERROR) << "failed to connect to device\n";
    return ZX_ERR_INTERNAL;
  }

  zx::channel chan;
  zx_status_t status = fdio_get_service_handle(fd.release(), chan.reset_and_get_address());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Fdio get handle failed";
    return status;
  }

  device_setup_client_ptr_ =
      std::make_unique<fidl_spinel::DeviceSetup::SyncClient>(std::move(chan));
  return ZX_OK;
}

zx_status_t OtStackApp::ConnectServiceByName(const char name[], zx::channel* out) {
  static zx_handle_t service_root;

  {
    static std::once_flag once;
    static zx_status_t status;
    std::call_once(once, [&]() {
      zx::channel client_end, server_end;
      status = zx::channel::create(0, &client_end, &server_end);
      if (status != ZX_OK) {
        return;
      }
      status = fdio_service_connect("/svc/.", server_end.release());
      if (status != ZX_OK) {
        return;
      }
      service_root = client_end.release();
    });
    if (status != ZX_OK) {
      return status;
    }
  }

  zx::channel client_end, server_end;
  zx_status_t status = zx::channel::create(0, &client_end, &server_end);
  if (status != ZX_OK) {
    return status;
  }

  status = fdio_service_connect_at(service_root, name, server_end.release());
  if (status != ZX_OK) {
    return status;
  }
  *out = std::move(client_end);
  return ZX_OK;
}

// Get the spinel setup client from a file path. Set `client_ptr_` on success.
zx_status_t OtStackApp::SetDeviceSetupClientInIsolatedDevmgr(const std::string& path) {
  zx_status_t res = ZX_OK;

  res = ConnectServiceByName("fuchsia.openthread.devmgr.IsolatedDevmgr", &isolated_devfs_);
  if (res != ZX_OK) {
    FX_LOGS(ERROR) << "fdio_service_connect_by_name failed";
    return res;
  }

  zx::channel fake_device_server_side, fake_device_client_side;
  res = zx::channel::create(0, &fake_device_server_side, &fake_device_client_side);
  if (res != ZX_OK) {
    FX_LOGS(ERROR) << "zx::channel::create failed";
    return res;
  }

  res = fdio_service_connect_at(isolated_devfs_.get(), path.c_str(),
                                fake_device_server_side.release());
  if (res != ZX_OK) {
    FX_LOGS(ERROR) << "fdio_service_connect_at failed";
    return res;
  }

  device_setup_client_ptr_ =
      std::make_unique<fidl_spinel::DeviceSetup::SyncClient>(std::move(fake_device_client_side));
  return ZX_OK;
}

zx_status_t OtStackApp::SetupOtRadioDev() {
  if (device_setup_client_ptr_.get() == nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  zx::channel server_end;
  zx::channel client_end;

  zx_status_t status = zx::channel::create(0, &client_end, &server_end);
  if (status != ZX_OK) {
    return status;
  }

  auto fidl_result = device_setup_client_ptr_->SetChannel(std::move(server_end));
  if (fidl_result.status() != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot set the channel to device";
    return fidl_result.status();
  }

  auto* result = fidl_result.Unwrap();
  if (result->result.is_err()) {
    return ZX_ERR_INTERNAL;
  }
  FX_LOGS(INFO) << "successfully connected to driver";

  event_thread_ = std::thread(
      [](void* cookie) { return reinterpret_cast<OtStackApp*>(cookie)->EventThread(); }, this);
  status = zx::port::create(0, &port_);
  if (status != ZX_OK) {
    return status;
  }

  device_channel_ = zx::unowned_channel(client_end);
  device_client_ptr_ = std::make_unique<fidl_spinel::Device::SyncClient>(std::move(client_end));
  connected_to_device_ = true;

  status = zx_object_wait_async(device_channel_->get(), port_.get(), kPortPktChannelRead,
                                ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, 0);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to wait for events";
  }
  return status;
}

void OtStackApp::EventThread() {
  fidl_spinel::Device::EventHandlers event_handlers{
      .on_ready_for_send_frames =
          [this](fidl_spinel::Device::OnReadyForSendFramesResponse* message) {
            return (*binding_)->OnReadyForSendFrames(message->number_of_frames);
          },
      .on_receive_frame =
          [this](fidl_spinel::Device::OnReceiveFrameResponse* message) {
            return (*binding_)->OnReceiveFrame(std::move(message->data));
          },
      .on_error =
          [this](fidl_spinel::Device::OnErrorResponse* message) {
            return (*binding_)->OnError(message->error, message->did_close);
          },
      .unknown =
          [this]() {
            (*binding_)->OnError(fidl_spinel::Error::IO_ERROR, true);
            DisconnectDevice();
            return ZX_ERR_IO;
          }};
  while (true) {
    zx_port_packet_t packet = {};
    port_.wait(zx::time::infinite(), &packet);
    switch (packet.key) {
      case kPortPktChannelRead: {
        if (packet.signal.observed & ZX_CHANNEL_PEER_CLOSED) {
          FX_LOGS(ERROR) << "ot-radio channel closed, terminating event thread";
          return;
        }
        ::fidl::Result result = device_client_ptr_->HandleEvents(event_handlers);
        if (!result.ok()) {
          FX_PLOGS(ERROR, result.status())
              << "error calling fidl_spinel::Device::SyncClient::HandleEvents(), terminating event "
                 "thread";
          DisconnectDevice();
          loop_.Shutdown();
          return;
        }
        zx_status_t status =
            zx_object_wait_async(device_channel_->get(), port_.get(), kPortPktChannelRead,
                                 ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, 0);
        if (status != ZX_OK) {
          FX_PLOGS(ERROR, status) << "failed to wait for events, terminating event thread";
          return;
        }
      } break;
      case kPortPktTerminate:
        FX_LOGS(INFO) << "terminating event thread";
        return;
    }
  }
}

void OtStackApp::TerminateEventThread() {
  zx_port_packet packet = {kPortPktTerminate, ZX_PKT_TYPE_USER, ZX_OK, {}};
  port_.queue(&packet);
  event_thread_.join();
}

void OtStackApp::DisconnectDevice() {
  device_channel_ = zx::unowned_channel(ZX_HANDLE_INVALID);
  device_client_ptr_.release();
  device_setup_client_ptr_.release();
  connected_to_device_ = false;
}

void OtStackApp::Shutdown() {
  FX_LOGS(ERROR) << "terminating message loop in ot-stack";
  if (binding_) {
    binding_->Close(ZX_ERR_INTERNAL);
  }
  TerminateEventThread();
  DisconnectDevice();
  loop_.Quit();
}

}  // namespace otstack
