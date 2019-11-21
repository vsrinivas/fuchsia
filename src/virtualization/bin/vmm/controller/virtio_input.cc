// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/virtio_input.h"

#include <lib/sys/cpp/service_directory.h>

#include "src/lib/fxl/logging.h"
#include "src/virtualization/bin/vmm/device/input.h"

static constexpr char kVirtioInputUrl[] =
    "fuchsia-pkg://fuchsia.com/virtio_input#meta/virtio_input.cmx";

static constexpr char kDeviceName[] = "machina-input";
static_assert(sizeof(kDeviceName) - 1 < sizeof(virtio_input_config_t::u),
              "Device name is too long");

static constexpr char kDeviceSerial[] = "serial-number";
static_assert(sizeof(kDeviceSerial) - 1 < sizeof(virtio_input_config_t::u),
              "Device serial is too long");

// Make sure to report only these event codes from keyboard.
// Reporting other keycodes may cause guest OS to recognize keyboard as
// touchpad, stylus or joystick.
static constexpr uint32_t kATKeyboardFirstCode = 0;
static constexpr uint32_t kATKeyboardLastCode = 255;
static constexpr uint32_t kMediaKeyboardFirstCode = 0x160;
static constexpr uint32_t kMediaKeyboardLastCode = 0x2bf;
static_assert(kATKeyboardFirstCode % 8 == 0, "First scan code must be byte aligned.");
static_assert((kATKeyboardLastCode + 1 - kATKeyboardFirstCode) % 8 == 0,
              "Scan code range must be byte aligned.");
static_assert(kMediaKeyboardFirstCode % 8 == 0, "First scan code must be byte aligned.");
static_assert((kMediaKeyboardLastCode + 1 - kMediaKeyboardFirstCode) % 8 == 0,
              "Scan code range must be byte aligned.");
static_assert((kATKeyboardLastCode + 7) / 8 < sizeof(virtio_input_config_t().u.bitmap),
              "Last scan code cannot exceed allowed range.");
static_assert((kMediaKeyboardLastCode + 7) / 8 < sizeof(virtio_input_config_t().u.bitmap),
              "Last scan code cannot exceed allowed range.");

VirtioInput::VirtioInput(const PhysMem& phys_mem)
    : VirtioComponentDevice(phys_mem, 0 /* device_features */,
                            fit::bind_member(this, &VirtioInput::ConfigureQueue),
                            fit::bind_member(this, &VirtioInput::ConfigureDevice),
                            fit::bind_member(this, &VirtioInput::Ready)) {}

zx_status_t VirtioInput::Start(
    const zx::guest& guest,
    fidl::InterfaceRequest<fuchsia::virtualization::hardware::ViewListener> view_listener_request,
    fuchsia::sys::Launcher* launcher, async_dispatcher_t* dispatcher) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kVirtioInputUrl;
  auto services = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
  services->Connect(input_.NewRequest());
  services->Connect(std::move(view_listener_request));

  fuchsia::virtualization::hardware::StartInfo start_info;
  zx_status_t status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }
  return input_->Start(std::move(start_info));
}

zx_status_t VirtioInput::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                                        zx_gpaddr_t avail, zx_gpaddr_t used) {
  return input_->ConfigureQueue(queue, size, desc, avail, used);
}

zx_status_t VirtioInput::Ready(uint32_t negotiated_features) {
  return input_->Ready(negotiated_features);
}

static void set_config_bit(virtio_input_config_t* config, uint32_t event_code) {
  config->u.bitmap[event_code / 8] |= 1u << (event_code % 8);
}

static void configure_ev_bits(virtio_input_config_t* config) {
  // VIRTIO_INPUT_CFG_EV_BITS: subsel specifies the event type (EV_*).
  // If size is non-zero the event type is supported and a bitmap the of
  // supported event codes is returned in u.bitmap.
  memset(&config->u, 0, sizeof(config->u));
  switch (config->subsel) {
    case VIRTIO_INPUT_EV_KEY:
      memset(&config->u.bitmap[kATKeyboardFirstCode / 8], 0xff,
             (kATKeyboardLastCode + 1 - kATKeyboardFirstCode) / 8);
      memset(&config->u.bitmap[kMediaKeyboardFirstCode / 8], 0xff,
             (kMediaKeyboardLastCode + 1 - kMediaKeyboardFirstCode) / 8);
      set_config_bit(config, kButtonTouchCode);
      config->size = sizeof(config->u);
      break;
    case VIRTIO_INPUT_EV_ABS:
      set_config_bit(config, VIRTIO_INPUT_EV_ABS_X);
      set_config_bit(config, VIRTIO_INPUT_EV_ABS_Y);
      config->size = 1;
      break;
    default:
      config->size = 0;
      break;
  }
}

static void configure_abs_info(virtio_input_config_t* config) {
  memset(&config->u, 0, sizeof(config->u));
  switch (config->subsel) {
    case VIRTIO_INPUT_EV_ABS_X:
      config->u.abs.min = 0;
      config->u.abs.max = kInputAbsMaxX;
      config->size = sizeof(config->u.abs);
      break;
    case VIRTIO_INPUT_EV_ABS_Y:
      config->u.abs.min = 0;
      config->u.abs.max = kInputAbsMaxY;
      config->size = sizeof(config->u.abs);
      break;
    default:
      config->size = 0;
      break;
  }
}

zx_status_t VirtioInput::ConfigureDevice(uint64_t addr, const IoValue& value) {
  if (addr >= 2) {
    return ZX_OK;
  }

  // A write to select or subselect modifies the contents of the config.u field.
  std::lock_guard<std::mutex> lock(device_config_.mutex);
  switch (config_.select) {
    case VIRTIO_INPUT_CFG_EV_BITS:
      configure_ev_bits(&config_);
      return ZX_OK;
    case VIRTIO_INPUT_CFG_ABS_INFO:
      configure_abs_info(&config_);
      return ZX_OK;
    case VIRTIO_INPUT_CFG_ID_NAME:
      // From virtio-input spec, Section 5.7.4, Device configuration layout:
      // Strings do not include a terminating NUL byte.
      config_.size = sizeof(kDeviceName) - 1;
      memcpy(config_.u.string, kDeviceName, config_.size);
      return ZX_OK;
    case VIRTIO_INPUT_CFG_ID_SERIAL:
      config_.size = sizeof(kDeviceSerial) - 1;
      memcpy(config_.u.string, kDeviceSerial, config_.size);
      return ZX_OK;
    case VIRTIO_INPUT_CFG_UNSET:
    case VIRTIO_INPUT_CFG_ID_DEVIDS:
    case VIRTIO_INPUT_CFG_PROP_BITS:
      memset(&config_.u, 0, sizeof(config_.u));
      config_.size = 0;
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unsupported select value " << config_.select;
      return ZX_ERR_NOT_SUPPORTED;
  }
}
