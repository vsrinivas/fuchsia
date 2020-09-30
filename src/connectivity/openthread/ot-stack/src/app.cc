// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include <alarm.h>
#include <fcntl.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/svc/dir.h>
#include <radio.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <fbl/auto_lock.h>
#include <openthread/tasklet.h>

#define OT_STACK_ASSERT assert

namespace otstack {

constexpr uint8_t kSpinelResetFrame[]{0x80, 0x06, 0x0};

OtStackApp::LowpanSpinelDeviceFidlImpl::LowpanSpinelDeviceFidlImpl(OtStackApp& app) : app_(app) {}

void OtStackApp::ClientAllowanceInit() {
  client_outbound_allowance_ = kOutboundAllowanceInit;
  client_inbound_allowance_ = 0;
  (*binding_)->OnReadyForSendFrames(kOutboundAllowanceInit);
}

void OtStackApp::RadioAllowanceInit() {
  radio_inbound_allowance_ = kInboundAllowanceInit;
  radio_outbound_allowance_ = 0;

  // try to open the device
  auto fidl_result = device_client_ptr_->Open();
  if (fidl_result.status() != ZX_OK) {
    FX_LOGS(ERROR) << "FIDL error while sending open() req to ot-radio";
    Shutdown();
    return;
  }
  auto* result = fidl_result.Unwrap();
  if (result->result.is_err() == false) {
    // send inbound allowance
    device_client_ptr_->ReadyToReceiveFrames(kInboundAllowanceInit);
  } else {
    FX_LOGS(DEBUG) << "ot-stack: radio returned err in spinel Open()";
  }
}

void OtStackApp::HandleRadioOnReadyForSendFrame(uint32_t allowance) {
  radio_outbound_allowance_ += allowance;
}

void OtStackApp::HandleClientReadyToReceiveFrames(uint32_t allowance) {
  if (client_inbound_allowance_ == 0 && client_inbound_queue_.size() > 0) {
    async::PostTask(loop_.dispatcher(), [this]() { this->SendOneFrameToClient(); });
  }
  client_inbound_allowance_ += allowance;
}

void OtStackApp::UpdateRadioOutboundAllowance() {
  OT_STACK_ASSERT(radio_outbound_allowance_ > 0);
  radio_outbound_allowance_--;
  radio_outbound_cnt++;
  FX_LOGS(DEBUG) << "ot-stack: updated radio_outbound_allowance_:" << radio_outbound_allowance_;
}

void OtStackApp::UpdateRadioInboundAllowance() {
  OT_STACK_ASSERT(radio_inbound_allowance_ > 0);
  radio_inbound_allowance_--;
  radio_inbound_cnt++;
  if (((radio_inbound_allowance_ & 1) == 0) && device_client_ptr_.get()) {
    device_client_ptr_->ReadyToReceiveFrames(kInboundAllowanceInc);
    radio_inbound_allowance_ += kInboundAllowanceInc;
  }
  FX_LOGS(DEBUG) << "ot-stack: updated radio_inbound_allowance_:" << radio_inbound_allowance_;
}

void OtStackApp::UpdateClientOutboundAllowance() {
  OT_STACK_ASSERT(client_outbound_allowance_ > 0);
  client_outbound_allowance_--;
  client_outbound_cnt++;
  if (((client_outbound_allowance_ & 1) == 0) && device_client_ptr_.get()) {
    FX_LOGS(DEBUG) << "ot-stack: OnReadyForSendFrames: " << client_outbound_allowance_;
    (*binding_)->OnReadyForSendFrames(kOutboundAllowanceInc);
    client_outbound_allowance_ += kOutboundAllowanceInc;
  }
  FX_LOGS(DEBUG) << "ot-stack: updated client_outbound_allowance_:" << client_outbound_allowance_;
}

void OtStackApp::UpdateClientInboundAllowance() {
  OT_STACK_ASSERT(client_inbound_allowance_ > 0);
  client_inbound_allowance_--;
  client_inbound_cnt++;
  FX_LOGS(DEBUG) << "ot-stack: updated client_inbound_allowance_:" << client_inbound_allowance_;
}

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

void OtStackApp::LowpanSpinelDeviceFidlImpl::Open(OpenCompleter::Sync& completer) {
  if (app_.connected_to_device_ == false) {
    FX_LOGS(ERROR) << "ot-radio not connected when client called Open()";
    completer.ReplyError(fidl_spinel::Error::UNSPECIFIED);
    app_.Shutdown();
    return;
  }

  FX_LOGS(INFO) << "FIDL request Open got";

  app_.ClientAllowanceInit();
  // Send out the reset frame
  app_.client_inbound_queue_.push_back(std::vector<uint8_t>{0x80, 0x06, 0x0, 0x70});
  completer.ReplySuccess();
}

void OtStackApp::LowpanSpinelDeviceFidlImpl::Close(CloseCompleter::Sync& completer) {
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
    GetMaxFrameSizeCompleter::Sync& completer) {
  if (app_.connected_to_device_ == false) {
    FX_LOGS(ERROR) << "ot-stack: ot-radio not connected";
    app_.Shutdown();
    return;
  }
  auto fidl_result = app_.device_client_ptr_->GetMaxFrameSize();
  if (fidl_result.status() != ZX_OK) {
    FX_LOGS(ERROR) << "ot-stack: FIDL error while sending req to ot-radio";
    app_.Shutdown();
    return;
  }
  auto* result = fidl_result.Unwrap();
  completer.Reply(result->size);
}

void OtStackApp::PushFrameToOtLib() {
  FX_LOGS(INFO) << "ot-stack: entering push frame to ot-lib task";
  assert(client_outbound_queue_.size() > 0);
  ot::Ncp::otNcpGetInstance()->HandleFidlReceiveDone(client_outbound_queue_.front().data(),
                                                     client_outbound_queue_.front().size());
  client_outbound_queue_.pop_front();
  FX_LOGS(INFO) << "ot-stack: leaving push frame to ot-lib task";
}

void OtStackApp::LowpanSpinelDeviceFidlImpl::SendFrame(::fidl::VectorView<uint8_t> data,
                                                       SendFrameCompleter::Sync& completer) {
  if (app_.connected_to_device_ == false) {
    FX_LOGS(ERROR) << "ot-radio not connected";
    return;
  }
  FX_LOGS(INFO) << "ot-stack: SendFrame() received";
  app_.UpdateClientOutboundAllowance();
  // Invoke ot-lib
  app_.client_outbound_queue_.push_back(std::vector<uint8_t>(data.cbegin(), data.cend()));
  async::PostTask(app_.loop_.dispatcher(), [this]() { this->app_.PushFrameToOtLib(); });
}

void OtStackApp::LowpanSpinelDeviceFidlImpl::ReadyToReceiveFrames(
    uint32_t number_of_frames, ReadyToReceiveFramesCompleter::Sync& completer) {
  if (app_.connected_to_device_ == false) {
    FX_LOGS(ERROR) << "ot-radio not connected";
    return;
  }
  app_.HandleClientReadyToReceiveFrames(number_of_frames);
}

OtStackApp::OtStackCallBackImpl::OtStackCallBackImpl(OtStackApp& app) : app_(app) {}

// TODO (jiamingw): flow control, and timeout when it is unable to send out the packet
void OtStackApp::OtStackCallBackImpl::SendOneFrameToRadio(uint8_t* buffer, uint32_t size) {
  ::fidl::VectorView<uint8_t> data;
  data.set_count(size);
  data.set_data(fidl::unowned_ptr_t<uint8_t>(buffer));
  if (app_.radio_outbound_allowance_ == 0) {
    FX_LOGS(ERROR) << "ot-stack: radio_outbound_allowance_ is 0, cannot send packet";
    return;
  }
  app_.device_client_ptr_->SendFrame(std::move(data));
  app_.UpdateRadioOutboundAllowance();
}

std::vector<uint8_t> OtStackApp::OtStackCallBackImpl::WaitForFrameFromRadio(uint64_t timeout_us) {
  FX_LOGS(INFO) << "ot-stack-callbackform: radio-callback: waiting for frame";
  {
    fbl::AutoLock lock(&app_.radio_q_mtx_);
    if (app_.radio_inbound_queue_.size() == 0) {
      sync_completion_reset(&app_.radio_rx_complete_);
    }
  }
  zx_status_t res = sync_completion_wait(&app_.radio_rx_complete_, ZX_USEC(timeout_us));
  sync_completion_reset(&app_.radio_rx_complete_);
  FX_PLOGS(INFO, res) << "ot-stack-callbackform: radio-callback: waiting end";
  if (res == ZX_ERR_TIMED_OUT) {
    // This method will be called multiple times by ot-lib. It is okay to timeout here.
    return std::vector<uint8_t>{};
  } else if (res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "ot-stack-callbackform: radio-callback: waiting frame end with err";
    return std::vector<uint8_t>{};
  }
  fbl::AutoLock lock0(&app_.radio_q_mtx_);
  assert(app_.radio_inbound_queue_.size() > 0);
  std::vector<uint8_t> vec = std::move(app_.radio_inbound_queue_.front());
  app_.radio_inbound_queue_.pop_front();
  return vec;
}

std::vector<uint8_t> OtStackApp::OtStackCallBackImpl::Process() {
  FX_LOGS(INFO) << "ot-stack-callbackform: radio-callback: checking for frame";
  std::vector<uint8_t> vec;
  fbl::AutoLock lock(&app_.radio_q_mtx_);
  if (app_.radio_inbound_queue_.size() > 0) {
    vec = std::move(app_.radio_inbound_queue_.front());
    app_.radio_inbound_queue_.pop_front();
    FX_LOGS(INFO) << "ot-stack-callbackform: radio-callback: check for frame: new frame";
  }
  return vec;
}

void OtStackApp::OtStackCallBackImpl::SendOneFrameToClient(uint8_t* buffer, uint32_t size) {
  if (memcmp(buffer, kSpinelResetFrame, sizeof(kSpinelResetFrame)) == 0) {
    // Reset frame
    FX_LOGS(WARNING) << "ot-stack: reset frame received from ot-radio";
    return;
  }
  app_.client_inbound_queue_.push_back(std::vector<uint8_t>(buffer, buffer + size));
  app_.SendOneFrameToClient();
}

void OtStackApp::OtStackCallBackImpl::PostNcpFidlInboundTask() {
  async::PostTask(app_.loop_.dispatcher(),
                  []() { ot::Ncp::otNcpGetInstance()->HandleFrameAddedToNcpBuffer(); });
}

void OtStackApp::OtStackCallBackImpl::PostOtLibTaskletProcessTask() {
  async::PostTask(app_.loop_.dispatcher(), [this]() {
    otTaskletsProcess(static_cast<otInstance*>(this->app_.ot_instance_ptr_.value()));
  });
}

void OtStackApp::OtStackCallBackImpl::PostDelayedAlarmTask(zx::duration delay) {
  async::PostDelayedTask(
      app_.loop_.dispatcher(), [this]() { this->app_.AlarmTask(); }, delay);
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

void OtStackApp::SendOneFrameToClient() {
  if (!binding_.has_value()) {
    FX_LOGS(ERROR) << "ot-stack: Sending frame to client, but client is not connected";
    assert(0);
  }
  if (client_inbound_queue_.size() > 0 && client_inbound_allowance_ > 0) {
    ::fidl::VectorView<uint8_t> data;
    uint8_t* ptr = client_inbound_queue_.front().data();
    data.set_data(fidl::unowned_ptr_t<uint8_t>(ptr));
    data.set_count(client_inbound_queue_.front().size());
    (*binding_)->OnReceiveFrame(std::move(data));
    UpdateClientInboundAllowance();
    client_inbound_queue_.pop_front();
    if (client_inbound_queue_.size() > 0 && client_inbound_allowance_ > 0) {
      async::PostTask(loop_.dispatcher(), [this]() { this->SendOneFrameToClient(); });
    }
    FX_LOGS(DEBUG) << "ot-stack: sent one frame to the client of ot-stack";
  } else {
    FX_LOGS(WARNING) << "ot-stack: unable to sent one frame to the client of ot-stack, q size:"
                     << client_inbound_queue_.size()
                     << " client_inbound_allowance_:" << client_inbound_allowance_;
  }
}

zx_status_t OtStackApp::Init(const std::string& path, bool is_test_env) {
  is_test_env_ = is_test_env;
  device_path_ = path;

  zx_status_t result = ConnectToOtRadioDev();
  if (result != ZX_OK) {
    return result;
  }
  RadioAllowanceInit();

  lowpan_spinel_ptr_ = std::make_unique<OtStackCallBackImpl>(*this);

  otPlatformConfig config;
  config.callback_ptr = lowpan_spinel_ptr_.get();
  config.m_speed_up_factor = 1;
  ot_instance_ptr_ = static_cast<void*>(otSysInit(&config));
  ot::Ncp::otNcpInit(static_cast<otInstance*>(ot_instance_ptr_.value()));
  ot::Ncp::otNcpGetInstance()->Init(lowpan_spinel_ptr_.get());

  return SetupFidlService();
}

void OtStackApp::AlarmTask() {
  zx_time_t remaining;
  platformAlarmUpdateTimeout(&remaining);
  if (remaining == 0) {
    FX_LOGS(DEBUG) << "ot-stack: calling platformAlarmProcess()";
    platformAlarmProcess(static_cast<otInstance*>(ot_instance_ptr_.value()));
  } else {
    // If remaining is not 0, then the alarm is likely already being reset.
    // do not need to do anything here
    FX_LOGS(DEBUG) << "ot-stack: alarm process not called, remaining: " << remaining;
  }
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

  status = zx_object_wait_async(device_channel_->get(), port_.get(), kPortRadioChannelRead,
                                ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, ZX_WAIT_ASYNC_ONCE);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to wait for events";
  }
  return status;
}

void OtStackApp::EventLoopHandleInboundFrame(::fidl::VectorView<uint8_t> data) {
  fbl::AutoLock lock(&radio_q_mtx_);
  radio_inbound_queue_.push_back(std::vector<uint8_t>(data.cbegin(), data.cend()));
  sync_completion_signal(&radio_rx_complete_);
  async::PostTask(loop_.dispatcher(), [this]() {
    platformRadioProcess(static_cast<otInstance*>(this->ot_instance_ptr_.value()));
  });
  FX_LOGS(INFO) << "signaled ot-stack-callbackform";
}

void OtStackApp::EventThread() {
  fidl_spinel::Device::EventHandlers event_handlers{
      .on_ready_for_send_frames =
          [this](fidl_spinel::Device::OnReadyForSendFramesResponse* message) {
            this->HandleRadioOnReadyForSendFrame(message->number_of_frames);
            return ZX_OK;
          },
      .on_receive_frame =
          [this](fidl_spinel::Device::OnReceiveFrameResponse* message) {
            EventLoopHandleInboundFrame(std::move(message->data));
            UpdateRadioInboundAllowance();
            return ZX_OK;
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
      case kPortRadioChannelRead: {
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
            zx_object_wait_async(device_channel_->get(), port_.get(), kPortRadioChannelRead,
                                 ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, ZX_WAIT_ASYNC_ONCE);
        if (status != ZX_OK) {
          FX_PLOGS(ERROR, status) << "failed to wait for events, terminating event thread";
          return;
        }
      } break;
      case kPortTerminate:
        FX_LOGS(INFO) << "terminating event thread";
        return;
    }
  }
}

void OtStackApp::TerminateEventThread() {
  zx_port_packet packet = {kPortTerminate, ZX_PKT_TYPE_USER, ZX_OK, {}};
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
