// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "input.h"

#include <limits.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/status.h>

#include <memory>
#include <utility>

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "trace.h"

#define LOCAL_TRACE 0

namespace virtio {

// DDK level ops
static zx_status_t virtio_input_OpenClient(void* ctx, uint32_t id, zx_handle_t handle,
                                           fidl_txn_t* txn) {
  return fuchsia_hardware_pty_DeviceOpenClient_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t virtio_input_ClrSetFeature(void* ctx, uint32_t clr, uint32_t set,
                                              fidl_txn_t* txn) {
  return fuchsia_hardware_pty_DeviceClrSetFeature_reply(txn, ZX_ERR_NOT_SUPPORTED, 0);
}

static zx_status_t virtio_input_GetWindowSize(void* ctx, fidl_txn_t* txn) {
  fuchsia_hardware_pty_WindowSize wsz = {.width = 0, .height = 0};
  return fuchsia_hardware_pty_DeviceGetWindowSize_reply(txn, ZX_ERR_NOT_SUPPORTED, &wsz);
}

static zx_status_t virtio_input_MakeActive(void* ctx, uint32_t client_pty_id, fidl_txn_t* txn) {
  return fuchsia_hardware_pty_DeviceMakeActive_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t virtio_input_ReadEvents(void* ctx, fidl_txn_t* txn) {
  return fuchsia_hardware_pty_DeviceReadEvents_reply(txn, ZX_ERR_NOT_SUPPORTED, 0);
}

static zx_status_t virtio_input_SetWindowSize(void* ctx,
                                              const fuchsia_hardware_pty_WindowSize* size,
                                              fidl_txn_t* txn) {
  return fuchsia_hardware_pty_DeviceSetWindowSize_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static constexpr fuchsia_hardware_pty_Device_ops_t fidl_ops = []() {
  // TODO: Why does this implement fuchsia.hardware.pty/Device?  This device
  // does not provide read/write methods, so shouldn't be usable as a terminal.
  fuchsia_hardware_pty_Device_ops_t ops = {};
  ops.OpenClient = virtio_input_OpenClient;
  ops.ClrSetFeature = virtio_input_ClrSetFeature;
  ops.GetWindowSize = virtio_input_GetWindowSize;
  ops.MakeActive = virtio_input_MakeActive;
  ops.ReadEvents = virtio_input_ReadEvents;
  ops.SetWindowSize = virtio_input_SetWindowSize;
  return ops;
}();

static bool IsQemuTouchscreen(const virtio_input_config_t& config) {
  if (config.u.ids.bustype == 0x06 && config.u.ids.vendor == 0x00 && config.u.ids.product == 0x00) {
    if (config.u.ids.version == 0x01 || config.u.ids.version == 0x00) {
      return true;
    }
  }
  return false;
}

zx_status_t InputDevice::virtio_input_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_pty_Device_dispatch(ctx, txn, msg, &fidl_ops);
}

void InputDevice::virtio_input_release(void* ctx) {
  virtio::InputDevice* inp = static_cast<virtio::InputDevice*>(ctx);
  return inp->Release();
}

zx_status_t InputDevice::virtio_input_query(void* ctx, uint32_t options, hid_info_t* info) {
  virtio::InputDevice* inp = static_cast<virtio::InputDevice*>(ctx);
  return inp->Query(options, info);
}

zx_status_t InputDevice::virtio_input_get_descriptor(void* ctx, uint8_t desc_type,
                                                     void* out_data_buffer, size_t data_size,
                                                     size_t* out_data_actual) {
  virtio::InputDevice* inp = static_cast<virtio::InputDevice*>(ctx);
  return inp->GetDescriptor(desc_type, out_data_buffer, data_size, out_data_actual);
}

zx_status_t InputDevice::virtio_input_get_report(void* ctx, uint8_t rpt_type, uint8_t rpt_id,
                                                 void* data, size_t len, size_t* out_len) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t InputDevice::virtio_input_set_report(void* ctx, uint8_t rpt_type, uint8_t rpt_id,
                                                 const void* data, size_t len) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t InputDevice::virtio_input_get_idle(void* ctx, uint8_t rpt_type, uint8_t* duration) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t InputDevice::virtio_input_set_idle(void* ctx, uint8_t rpt_type, uint8_t duration) {
  return ZX_OK;
}

zx_status_t InputDevice::virtio_input_get_protocol(void* ctx, uint8_t* protocol) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t InputDevice::virtio_input_set_protocol(void* ctx, uint8_t protocol) { return ZX_OK; }

zx_status_t InputDevice::virtio_input_start(void* ctx, const hidbus_ifc_protocol_t* ifc) {
  virtio::InputDevice* inp = static_cast<virtio::InputDevice*>(ctx);
  return inp->Start(ifc);
}

void InputDevice::virtio_input_stop(void* ctx) {
  virtio::InputDevice* inp = static_cast<virtio::InputDevice*>(ctx);
  inp->Stop();
}

InputDevice::InputDevice(zx_device_t* bus_device, zx::bti bti, std::unique_ptr<Backend> backend)
    : Device(bus_device, std::move(bti), std::move(backend)) {}

InputDevice::~InputDevice() {}

zx_status_t InputDevice::Init() {
  LTRACEF("Device %p\n", this);

  fbl::AutoLock lock(&lock_);

  // Reset the device and read configuration
  DeviceReset();

  SelectConfig(VIRTIO_INPUT_CFG_ID_NAME, 0);
  LTRACEF_LEVEL(2, "name %s\n", config_.u.string);

  SelectConfig(VIRTIO_INPUT_CFG_ID_SERIAL, 0);
  LTRACEF_LEVEL(2, "serial %s\n", config_.u.string);

  SelectConfig(VIRTIO_INPUT_CFG_ID_DEVIDS, 0);
  if (config_.size >= sizeof(virtio_input_devids_t)) {
    LTRACEF_LEVEL(2, "bustype %d\n", config_.u.ids.bustype);
    LTRACEF_LEVEL(2, "vendor %d\n", config_.u.ids.vendor);
    LTRACEF_LEVEL(2, "product %d\n", config_.u.ids.product);
    LTRACEF_LEVEL(2, "version %d\n", config_.u.ids.version);
  }

  SelectConfig(VIRTIO_INPUT_CFG_EV_BITS, VIRTIO_INPUT_EV_KEY);
  uint8_t cfg_key_size = config_.size;
  SelectConfig(VIRTIO_INPUT_CFG_EV_BITS, VIRTIO_INPUT_EV_REL);
  uint8_t cfg_rel_size = config_.size;
  SelectConfig(VIRTIO_INPUT_CFG_EV_BITS, VIRTIO_INPUT_EV_ABS);
  uint8_t cfg_abs_size = config_.size;

  // At the moment we support keyboards and a specific touchscreen.
  // Support for more devices should be added here.
  SelectConfig(VIRTIO_INPUT_CFG_ID_DEVIDS, 0);
  if (IsQemuTouchscreen(config_)) {
    // QEMU MultiTouch Touchscreen
    SelectConfig(VIRTIO_INPUT_CFG_ABS_INFO, VIRTIO_INPUT_EV_MT_POSITION_X);
    virtio_input_absinfo_t x_info = config_.u.abs;
    SelectConfig(VIRTIO_INPUT_CFG_ABS_INFO, VIRTIO_INPUT_EV_MT_POSITION_Y);
    virtio_input_absinfo_t y_info = config_.u.abs;
    hid_device_ = std::make_unique<HidTouch>(x_info, y_info);
  } else if (cfg_key_size > 0) {
    // Keyboard
    dev_class_ = HID_DEVICE_CLASS_KBD;
    hid_device_ = std::make_unique<HidKeyboard>();
  } else if (cfg_rel_size > 0 || cfg_abs_size > 0) {
    // TODO: This is where a Mouse should be implemented.
    dev_class_ = HID_DEVICE_CLASS_POINTER;
    return ZX_ERR_NOT_SUPPORTED;
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }

  DriverStatusAck();

  // Plan to clean up unless everything succeeds.
  auto cleanup = fbl::MakeAutoCall([this]() { Release(); });

  // Allocate the main vring
  zx_status_t status = vring_.Init(0, kEventCount);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to allocate vring: %s\n", zx_status_get_string(status));
    return status;
  }

  // Allocate event buffers for the ring.
  // TODO: Avoid multiple allocations, allocate enough for all buffers once.
  for (uint16_t id = 0; id < kEventCount; ++id) {
    static_assert(sizeof(virtio_input_event_t) <= PAGE_SIZE, "");
    status = io_buffer_init(&buffers_[id], bti_.get(), sizeof(virtio_input_event_t),
                            IO_BUFFER_RO | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to allocate I/O buffers: %s\n", zx_status_get_string(status));
      return status;
    }
  }

  // Expose event buffers to the host
  vring_desc* desc = nullptr;
  uint16_t id;
  for (uint16_t i = 0; i < kEventCount; ++i) {
    desc = vring_.AllocDescChain(1, &id);
    if (desc == nullptr) {
      zxlogf(ERROR, "Failed to allocate descriptor chain\n");
      return ZX_ERR_NO_RESOURCES;
    }
    ZX_ASSERT(id < kEventCount);
    desc->addr = io_buffer_phys(&buffers_[id]);
    desc->len = sizeof(virtio_input_event_t);
    desc->flags |= VRING_DESC_F_WRITE;
    LTRACE_DO(virtio_dump_desc(desc));
    vring_.SubmitChain(id);
  }

  StartIrqThread();
  DriverStatusOk();

  device_ops_.message = virtio_input_message;
  device_ops_.release = virtio_input_release;

  hidbus_ops_.query = virtio_input_query;
  hidbus_ops_.start = virtio_input_start;
  hidbus_ops_.stop = virtio_input_stop;
  hidbus_ops_.get_descriptor = virtio_input_get_descriptor;
  hidbus_ops_.get_report = virtio_input_get_report;
  hidbus_ops_.set_report = virtio_input_set_report;
  hidbus_ops_.get_idle = virtio_input_get_idle;
  hidbus_ops_.set_idle = virtio_input_set_idle;
  hidbus_ops_.get_protocol = virtio_input_get_protocol;
  hidbus_ops_.set_protocol = virtio_input_set_protocol;

  hidbus_ifc_.ops = nullptr;

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "virtio-input";
  args.ctx = this;
  args.ops = &device_ops_;
  args.proto_id = ZX_PROTOCOL_HIDBUS;
  args.proto_ops = &hidbus_ops_;

  status = device_add(bus_device_, &args, &device_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to add device: %s\n", zx_status_get_string(status));
    device_ = nullptr;
    return status;
  }

  vring_.Kick();
  cleanup.cancel();
  return ZX_OK;
}

zx_status_t InputDevice::Start(const hidbus_ifc_protocol_t* ifc) {
  fbl::AutoLock lock(&lock_);
  if (hidbus_ifc_.ops != nullptr) {
    return ZX_ERR_ALREADY_BOUND;
  }
  hidbus_ifc_ = *ifc;
  return ZX_OK;
}

void InputDevice::Stop() {
  fbl::AutoLock lock(&lock_);
  hidbus_ifc_.ops = nullptr;
}

void InputDevice::Release() {
  fbl::AutoLock lock(&lock_);
  hidbus_ifc_.ops = nullptr;
  for (size_t i = 0; i < kEventCount; ++i) {
    if (io_buffer_is_valid(&buffers_[i])) {
      io_buffer_release(&buffers_[i]);
    }
  }
}

zx_status_t InputDevice::Query(uint32_t options, hid_info_t* info) {
  info->dev_num = dev_class_;  // Use type for dev_num for now.
  info->device_class = dev_class_;
  info->boot_device = true;
  return ZX_OK;
}

zx_status_t InputDevice::GetDescriptor(uint8_t desc_type, void* out_data_buffer, size_t data_size,
                                       size_t* out_data_actual) {
  return hid_device_->GetDescriptor(desc_type, out_data_buffer, data_size, out_data_actual);
}

void InputDevice::ReceiveEvent(virtio_input_event_t* event) {
  hid_device_->ReceiveEvent(event);

  if (event->type == VIRTIO_INPUT_EV_SYN) {
    fbl::AutoLock lock(&lock_);
    if (hidbus_ifc_.ops) {
      size_t size;
      const uint8_t* report = hid_device_->GetReport(&size);
      hidbus_ifc_io_queue(&hidbus_ifc_, report, size, zx_clock_get_monotonic());
    }
  }
}

void InputDevice::IrqRingUpdate() {
  auto free_chain = [this](vring_used_elem* used_elem) {
    uint16_t id = static_cast<uint16_t>(used_elem->id & 0xffff);
    vring_desc* desc = vring_.DescFromIndex(id);
    ZX_ASSERT(id < kEventCount);
    ZX_ASSERT(desc->len == sizeof(virtio_input_event_t));

    auto evt = static_cast<virtio_input_event_t*>(io_buffer_virt(&buffers_[id]));
    ReceiveEvent(evt);

    ZX_ASSERT((desc->flags & VRING_DESC_F_NEXT) == 0);
    vring_.FreeDesc(id);
  };

  vring_.IrqRingUpdate(free_chain);

  vring_desc* desc = nullptr;
  uint16_t id;
  bool need_kick = false;
  while ((desc = vring_.AllocDescChain(1, &id))) {
    desc->len = sizeof(virtio_input_event_t);
    vring_.SubmitChain(id);
    need_kick = true;
  }

  if (need_kick) {
    vring_.Kick();
  }
}

void InputDevice::IrqConfigChange() { LTRACEF("IrqConfigChange\n"); }

void InputDevice::SelectConfig(uint8_t select, uint8_t subsel) {
  WriteDeviceConfig(offsetof(virtio_input_config_t, select), select);
  WriteDeviceConfig(offsetof(virtio_input_config_t, subsel), subsel);
  CopyDeviceConfig(&config_, sizeof(config_));
}

}  // namespace virtio
