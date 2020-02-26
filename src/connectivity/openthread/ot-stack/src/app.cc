// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl-async/cpp/async_bind.h>
#include <lib/svc/dir.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

namespace otstack {

OtStackApp::LowpanSpinelDeviceFidlImpl::LowpanSpinelDeviceFidlImpl(OtStackApp& app) : app_(app) {}

void OtStackApp::LowpanSpinelDeviceFidlImpl::Open(OpenCompleter::Sync completer) {
  if (app_.device_client_ptr_.get() == nullptr) {
    FX_LOGS(ERROR) << "ot-radio not connected";
    completer.ReplyError(fidl_spinel::Error::UNSPECIFIED);
    app_.Shutdown();
    return;
  }
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
  if (app_.device_client_ptr_.get() == nullptr) {
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
  if (app_.device_client_ptr_.get() == nullptr) {
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
  // TODO (jiamingw) pass through
}

void OtStackApp::LowpanSpinelDeviceFidlImpl::ReadyToReceiveFrames(
    uint32_t number_of_frames, ReadyToReceiveFramesCompleter::Sync completer) {
  // TODO (jiamingw) pass through
}

static void connect(void* untyped_context, const char* service_name, zx_handle_t service_request) {
  auto context = static_cast<ConnectRequestContext*>(untyped_context);
  fidl::AsyncBind(context->dispatcher, zx::channel(service_request), context->server.get());
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

  fidl_req_ctx_ptr_ = std::make_unique<ConnectRequestContext>(
      ConnectRequestContext{.dispatcher = loop_.dispatcher(),
                            .server = std::make_unique<LowpanSpinelDeviceFidlImpl>(*this)});

  status = svc_dir_add_service(dir, "svc", "fuchsia.lowpan.spinel.Device", fidl_req_ctx_ptr_.get(),
                               connect);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Error adding service in ot-stack";
    return status;
  }
  return ZX_OK;
}

zx_status_t OtStackApp::Init(const std::string& path, bool is_test_env) {
  zx_status_t result = ConnectToOtRadioDev(path, is_test_env);
  if (result != ZX_OK) {
    return result;
  }
  return SetupFidlService();
}

// Connect to ot-radio device driver which allows ot-stack to talk to lower layer
zx_status_t OtStackApp::ConnectToOtRadioDev(const std::string& path, bool is_test_env) {
  zx_status_t result = ZX_OK;
  if (is_test_env) {
    result = SetDeviceSetupClientInIsolatedDevmgr(path);
  } else {
    result = SetDeviceSetupClientInDevmgr(path);
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

  zx::channel::create(0, &client_end, &server_end);
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
  device_client_ptr_ = std::make_unique<fidl_spinel::Device::SyncClient>(std::move(client_end));
  return ZX_OK;
}

void OtStackApp::Shutdown() {
  FX_LOGS(ERROR) << "terminating message loop in ot-stack";
  loop_.Quit();
}
}  // namespace otstack
