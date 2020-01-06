#include "fidl_client.h"

#include <ddk/debug.h>

namespace fhd = ::llcpp::fuchsia::hardware::display;

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
}

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
  dc_ = std::make_unique<fhd::Controller::SyncClient>(std::move(dc_client));
  device_handle_.reset(device_client.release());
  return true;
}

bool TestFidlClient::Bind() {
  zxlogf(INFO, "TestFidlClient::Bind waiting for displays\n");
  while (displays_.is_empty()) {
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

  zxlogf(INFO, "Turning on vsync\n");
  return dc_->EnableVsync(true).ok();
}

}  // namespace display
