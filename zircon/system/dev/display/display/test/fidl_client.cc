#include "fidl_client.h"

#include <lib/async/cpp/task.h>

#include <ddk/debug.h>
#include <fbl/auto_lock.h>

namespace fhd = ::llcpp::fuchsia::hardware::display;
namespace sysmem = ::llcpp::fuchsia::sysmem;

namespace display {

TestFidlClient::Display::Display(const fhd::Info& info) {
  id_ = info.id;

  for (size_t i = 0; i < info.pixel_format.count(); i++) {
    pixel_formats_.push_back(info.pixel_format[i]);
  }
  for (size_t i = 0; i < info.modes.count(); i++) {
    modes_.push_back(info.modes[i]);
  }
  for (size_t i = 0; i < info.cursor_configs.count(); i++) {
    cursors_.push_back(info.cursor_configs[i]);
  }
  manufacturer_name_ = fbl::String(info.manufacturer_name.data());
  monitor_name_ = fbl::String(info.monitor_name.data());
  monitor_serial_ = fbl::String(info.monitor_serial.data());
  image_config_.height = modes_[0].vertical_resolution;
  image_config_.width = modes_[0].horizontal_resolution;
  image_config_.pixel_format = pixel_formats_[0];
  image_config_.type = fhd::typeSimple;
}

uint64_t TestFidlClient::display_id() const { return displays_[0].id_; }

bool TestFidlClient::CreateChannel(zx_handle_t provider, bool is_vc) {
  zx::channel device_server, device_client;
  zx::channel dc_server, dc_client;
  zx_status_t status = zx::channel::create(0, &device_server, &device_client);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not create device channels\n");
    return false;
  }
  status = zx::channel::create(0, &dc_server, &dc_client);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not create controller channels\n");
    return false;
  }
  zxlogf(INFO, "Opening controller\n");
  if (is_vc) {
    auto response = fhd::Provider::Call::OpenVirtconController(
        zx::unowned_channel(provider), std::move(device_server), std::move(dc_server));
    if (!response.ok()) {
      zxlogf(ERROR, "Could not open VC controller, error=%s\n", response.error());
      return false;
    }
  } else {
    auto response = fhd::Provider::Call::OpenController(
        zx::unowned_channel(provider), std::move(device_server), std::move(dc_server));
    if (!response.ok()) {
      zxlogf(ERROR, "Could not open controller, error=%s\n", response.error());
      return false;
    }
  }

  fbl::AutoLock lock(mtx());
  dc_ = std::make_unique<fhd::Controller::SyncClient>(std::move(dc_client));
  device_handle_.reset(device_client.release());
  return true;
}

bool TestFidlClient::Bind(async_dispatcher_t* dispatcher) {
  dispatcher_ = dispatcher;
  while (displays_.is_empty() || !has_ownership_) {
    fbl::AutoLock lock(mtx());
    auto result = dc_->HandleEvents({
        .displays_changed =
            [this](::fidl::VectorView<fhd::Info> added, ::fidl::VectorView<uint64_t> removed) {
              for (size_t i = 0; i < added.count(); i++) {
                displays_.push_back(Display(added[i]));
              }
              return ZX_OK;
            },
        .vsync = [](uint64_t display_id, uint64_t timestamp,
                    ::fidl::VectorView<uint64_t> images) { return ZX_ERR_INVALID_ARGS; },
        .client_ownership_change =
            [this](bool owns) {
              has_ownership_ = owns;
              return ZX_OK;
            },
        .unknown = []() { return ZX_ERR_STOP; },
    });
    if (result != ZX_OK) {
      zxlogf(ERROR, "Got unexpected message\n");
      return false;
    }
  }

  fbl::AutoLock lock(mtx());

  wait_events_.set_object(dc_->channel().get());
  wait_events_.set_trigger(ZX_CHANNEL_READABLE);
  EXPECT_OK(wait_events_.Begin(dispatcher));
  return dc_->EnableVsync(true).ok();
}

void TestFidlClient::OnEventMsgAsync(async_dispatcher_t* dispatcher, async::WaitBase* self,
                                     zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    return;
  }

  if (!(signal->observed & ZX_CHANNEL_READABLE)) {
    return;
  }

  fbl::AutoLock lock(mtx());
  auto result = dc_->HandleEvents({
      .displays_changed = [](::fidl::VectorView<fhd::Info>,
                             ::fidl::VectorView<uint64_t>) { return ZX_OK; },
      // The FIDL bindings do not know that the caller holds mtx(), so we can't TA_REQ(mtx()) here.
      .vsync =
          [this](uint64_t, uint64_t, ::fidl::VectorView<uint64_t>) TA_NO_THREAD_SAFETY_ANALYSIS {
            vsync_count_++;
            return ZX_OK;
          },
      .client_ownership_change = [](bool) { return ZX_OK; },
      .unknown = []() { return ZX_ERR_STOP; },
  });

  if (result != ZX_OK) {
    zxlogf(ERROR, "Failed to handle events: %d\n", result);
    return;
  }

  if (wait_events_.object() == ZX_HANDLE_INVALID) {
    return;
  }
  // Re-arm the wait.
  self->Begin(dispatcher);
}

TestFidlClient::~TestFidlClient() {
  if (dispatcher_) {
    // Cancel must be issued from the dispatcher thread.
    sync_completion_t done;
    auto task = new async::Task();
    task->set_handler(
        [this, task, done_ptr = &done](async_dispatcher_t*, async::Task*, zx_status_t) {
          wait_events_.Cancel();
          wait_events_.set_object(ZX_HANDLE_INVALID);

          sync_completion_signal(done_ptr);
          delete task;
        });
    if (task->Post(dispatcher_) != ZX_OK) {
      delete task;
      wait_events_.Cancel();
      wait_events_.set_object(ZX_HANDLE_INVALID);
    } else {
      while (true) {
        if (sync_completion_wait(&done, ZX_MSEC(10)) == ZX_OK) {
          break;
        }
      }
    }
  }
}

}  // namespace display
