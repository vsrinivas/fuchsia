// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ge2d.h"

#include <lib/image-format/image_format.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/protocol/amlogiccanvas.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <hw/reg.h>

#include "src/lib/syslog/cpp/logger.h"

namespace ge2d {

namespace {

constexpr uint32_t kGe2d = 0;
constexpr auto TAG = "ge2d";

enum {
  COMPONENT_PDEV,
  COMPONENT_SENSOR,
  COMPONENT_CANVAS,
  COMPONENT_COUNT,
};

}  // namespace

zx_status_t Ge2dDevice::Ge2dInitTaskResize(
    const buffer_collection_info_2_t* input_buffer_collection,
    const buffer_collection_info_2_t* output_buffer_collection, const resize_info_t* info,
    const image_format_2_t* input_image_format,
    const image_format_2_t* output_image_format_table_list, size_t output_image_format_table_count,
    uint32_t output_image_format_index, const hw_accel_frame_callback_t* frame_callback,
    const hw_accel_res_change_callback_t* res_callback, uint32_t* out_task_index) {
  if (out_task_index == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  auto task = std::unique_ptr<Ge2dTask>(new (&ac) Ge2dTask());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status =
      task->InitResize(input_buffer_collection, output_buffer_collection, info, input_image_format,
                       output_image_format_table_list, output_image_format_table_count,
                       output_image_format_index, frame_callback, res_callback, bti_, canvas_);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "Task Creation Failed";
    return status;
  }

  // Put an entry in the hashmap.
  task_map_[next_task_index_] = std::move(task);
  *out_task_index = next_task_index_;
  next_task_index_++;

  return status;
}

zx_status_t Ge2dDevice::Ge2dInitTaskWaterMark(
    const buffer_collection_info_2_t* input_buffer_collection,
    const buffer_collection_info_2_t* output_buffer_collection, const water_mark_info_t* info,
    zx::vmo watermark_vmo, const image_format_2_t* image_format_table_list,
    size_t image_format_table_count, uint32_t image_format_index,
    const hw_accel_frame_callback_t* frame_callback,
    const hw_accel_res_change_callback_t* res_callback, uint32_t* out_task_index) {
  if (out_task_index == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  auto task = std::unique_ptr<Ge2dTask>(new (&ac) Ge2dTask());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status =
      task->InitWatermark(input_buffer_collection, output_buffer_collection, info, watermark_vmo,
                          image_format_table_list, image_format_table_count, image_format_index,
                          frame_callback, res_callback, bti_, canvas_);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "Task Creation Failed";
    return status;
  }

  // Put an entry in the hashmap.
  task_map_[next_task_index_] = std::move(task);
  *out_task_index = next_task_index_;
  next_task_index_++;

  return status;
}

void Ge2dDevice::Ge2dRemoveTask(uint32_t task_index) {
  // Find the entry in hashmap.
  auto task_entry = task_map_.find(task_index);
  ZX_ASSERT(task_entry != task_map_.end());

  // Remove map entry.
  task_map_.erase(task_entry);
}

void Ge2dDevice::Ge2dReleaseFrame(uint32_t task_index, uint32_t buffer_index) {
  // Find the entry in hashmap.
  auto task_entry = task_map_.find(task_index);
  ZX_ASSERT(task_entry != task_map_.end());

  auto task = task_entry->second.get();
  ZX_ASSERT(ZX_OK == task->ReleaseOutputBuffer(buffer_index));
}

zx_status_t Ge2dDevice::Ge2dSetOutputResolution(uint32_t task_index,
                                                uint32_t new_output_image_format_index) {
  // Find the entry in hashmap.
  auto task_entry = task_map_.find(task_index);
  if (task_entry == task_map_.end()) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (task_entry->second->Ge2dTaskType() != Ge2dTask::GE2D_RESIZE) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Validate new image format index|.
  if (!task_entry->second->IsOutputFormatIndexValid(new_output_image_format_index)) {
    return ZX_ERR_INVALID_ARGS;
  }

  TaskInfo info;
  info.op = GE2D_OP_SETOUTPUTRES;
  info.task = task_entry->second.get();
  info.index = new_output_image_format_index;

  // Put the task on queue.
  fbl::AutoLock lock(&lock_);
  processing_queue_.push_front(info);
  frame_processing_signal_.Signal();
  return ZX_OK;
}

zx_status_t Ge2dDevice::Ge2dSetInputAndOutputResolution(uint32_t task_index,
                                                        uint32_t new_image_format_index) {
  // Find the entry in hashmap.
  auto task_entry = task_map_.find(task_index);
  if (task_entry == task_map_.end()) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (task_entry->second->Ge2dTaskType() != Ge2dTask::GE2D_WATERMARK) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Validate new image format index|.
  if (!task_entry->second->IsInputFormatIndexValid(new_image_format_index) ||
      !task_entry->second->IsOutputFormatIndexValid(new_image_format_index)) {
    return ZX_ERR_INVALID_ARGS;
  }

  TaskInfo info;
  info.op = GE2D_OP_SETINPUTOUTPUTRES;
  info.task = task_entry->second.get();
  info.index = new_image_format_index;

  // Put the task on queue.
  fbl::AutoLock lock(&lock_);
  processing_queue_.push_front(info);
  frame_processing_signal_.Signal();
  return ZX_OK;
}

zx_status_t Ge2dDevice::Ge2dProcessFrame(uint32_t task_index, uint32_t input_buffer_index) {
  // Find the entry in hashmap.
  auto task_entry = task_map_.find(task_index);
  if (task_entry == task_map_.end()) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Validate |input_buffer_index|.
  if (!task_entry->second->IsInputBufferIndexValid(input_buffer_index)) {
    return ZX_ERR_INVALID_ARGS;
  }

  TaskInfo info;
  info.op = GE2D_OP_FRAME;
  info.task = task_entry->second.get();
  info.index = input_buffer_index;

  // Put the task on queue.
  fbl::AutoLock lock(&lock_);
  processing_queue_.push_front(info);
  frame_processing_signal_.Signal();
  return ZX_OK;
}

void Ge2dDevice::Ge2dSetCropRectangle(uint32_t task_index, const crop_rectangle_t* crop) {}

void Ge2dDevice::ProcessTask(TaskInfo& info) {
  auto task = info.task;

  if (info.op == GE2D_OP_SETOUTPUTRES || info.op == GE2D_OP_SETINPUTOUTPUTRES) {
    // This has to free and reallocate the output buffer canvas ids.
    task->Ge2dChangeOutputRes(info.index);
    if (info.op == GE2D_OP_SETINPUTOUTPUTRES) {
      // This has to free and reallocate the input buffer canvas ids.
      task->Ge2dChangeInputRes(info.index);
    }
    frame_available_info f_info;
    f_info.frame_status = FRAME_STATUS_OK;
    f_info.metadata.timestamp = static_cast<uint64_t>(zx_clock_get_monotonic());
    f_info.metadata.image_format_index = task->output_format_index();
    task->res_callback()->frame_resolution_changed(task->frame_callback()->ctx, &f_info);
    return;
  }
  auto input_buffer_index = info.index;
  zx_port_packet_t packet;
  ZX_ASSERT(ZX_OK == WaitForInterrupt(&packet));
  if (packet.key == kPortKeyIrqMsg) {
    ZX_ASSERT(ge2d_irq_.ack() == ZX_OK);
  }

  // First lets fetch an unused buffer from the VMO pool.
  auto result = task->GetOutputBufferPhysAddr();
  if (result.is_error()) {
    frame_available_info info;
    info.frame_status = FRAME_STATUS_ERROR_BUFFER_FULL;
    info.metadata.input_buffer_index = input_buffer_index;
    info.metadata.timestamp = static_cast<uint64_t>(zx_clock_get_monotonic());
    info.metadata.image_format_index = task->output_format_index();
    task->frame_callback()->frame_ready(task->frame_callback()->ctx, &info);
    return;
  }

  __UNUSED auto output_phy_addr = result.value();

  if (packet.key == kPortKeyDebugFakeInterrupt || packet.key == kPortKeyIrqMsg) {
    // Invoke the callback function and tell about the output buffer index
    // which is ready to be used.
    frame_available_info f_info;
    f_info.frame_status = FRAME_STATUS_OK;
    f_info.buffer_id = task->GetOutputBufferIndex();
    f_info.metadata.timestamp = static_cast<uint64_t>(zx_clock_get_monotonic());
    f_info.metadata.image_format_index = task->output_format_index();
    f_info.metadata.input_buffer_index = input_buffer_index;
    task->frame_callback()->frame_ready(task->res_callback()->ctx, &f_info);
  }
}

int Ge2dDevice::FrameProcessingThread() {
  FX_LOG(INFO, TAG, "start");
  for (;;) {
    fbl::AutoLock al(&lock_);
    while (processing_queue_.empty() && !shutdown_) {
      frame_processing_signal_.Wait(&lock_);
    }
    if (shutdown_) {
      break;
    }
    auto info = processing_queue_.back();
    processing_queue_.pop_back();
    al.release();
    ProcessTask(info);
  }
  return ZX_OK;
}

zx_status_t Ge2dDevice::StartThread() {
  return thrd_status_to_zx_status(thrd_create_with_name(
      &processing_thread_,
      [](void* arg) -> int { return reinterpret_cast<Ge2dDevice*>(arg)->FrameProcessingThread(); },
      this, "ge2d-processing-thread"));
}

zx_status_t Ge2dDevice::StopThread() {
  // Signal the worker thread and wait for it to terminate.
  {
    fbl::AutoLock al(&lock_);
    shutdown_ = true;
    frame_processing_signal_.Signal();
  }
  JoinThread();
  return ZX_OK;
}

zx_status_t Ge2dDevice::WaitForInterrupt(zx_port_packet_t* packet) {
  return port_.wait(zx::time::infinite(), packet);
}

// static
zx_status_t Ge2dDevice::Setup(zx_device_t* parent, std::unique_ptr<Ge2dDevice>* out) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    FX_LOG(ERROR, TAG, "could not get composite protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_device_t* components[COMPONENT_COUNT];
  size_t actual;
  composite.GetComponents(components, COMPONENT_COUNT, &actual);
  if (actual != COMPONENT_COUNT) {
    FX_LOG(ERROR, TAG, "Could not get components");
    return ZX_ERR_NOT_SUPPORTED;
  }

  ddk::PDev pdev(components[COMPONENT_PDEV]);
  if (!pdev.is_valid()) {
    FX_LOG(ERROR, TAG, "ZX_PROTOCOL_PDEV not available");
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> ge2d_mmio;
  zx_status_t status = pdev.MapMmio(kGe2d, &ge2d_mmio);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "pdev_.MapMmio failed";
    return status;
  }

  zx::interrupt ge2d_irq;
  status = pdev.GetInterrupt(0, &ge2d_irq);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "pdev_.GetInterrupt failed";
    return status;
  }

  zx::port port;
  status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "port create failed";
    return status;
  }

  status = ge2d_irq.bind(port, kPortKeyIrqMsg, 0 /*options*/);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "interrupt bind failed";
    return status;
  }

  zx::bti bti;
  status = pdev.GetBti(0, &bti);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "could not obtain bti";
    return status;
  }

  ddk::AmlogicCanvasProtocolClient canvas(components[COMPONENT_CANVAS]);
  if (!canvas.is_valid()) {
    FX_LOG(ERROR, TAG, "Could not get Amlogic Canvas protocol");
    return ZX_ERR_NO_RESOURCES;
  }
  amlogic_canvas_protocol_t c;
  canvas.GetProto(&c);

  fbl::AllocChecker ac;
  auto ge2d_device = std::unique_ptr<Ge2dDevice>(new (&ac) Ge2dDevice(
      parent, std::move(*ge2d_mmio), std::move(ge2d_irq), std::move(bti), std::move(port), c));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = ge2d_device->StartThread();
  *out = std::move(ge2d_device);
  return status;
}

void Ge2dDevice::DdkUnbindNew(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void Ge2dDevice::DdkRelease() {
  StopThread();
  delete this;
}

void Ge2dDevice::ShutDown() {}

zx_status_t Ge2dBind(void* ctx, zx_device_t* device) {
  std::unique_ptr<Ge2dDevice> ge2d_device;
  zx_status_t status = ge2d::Ge2dDevice::Setup(device, &ge2d_device);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "Could not setup ge2d device";
    return status;
  }
  zx_device_prop_t props[] = {
      {BIND_PLATFORM_PROTO, 0, ZX_PROTOCOL_GE2D},
  };

// Run the unit tests for this device
// TODO(braval): CAM-44 (Run only when build flag enabled)
// This needs to be replaced with run unittests hooks when
// the framework is available.
#if 0
    status = ge2d::Ge2dDeviceTester::RunTests(ge2d_device.get());
    if (status != ZX_OK) {
        FX_LOG(ERROR, TAG, "Device Unit Tests Failed");
        return status;
    }
#endif

  status = ge2d_device->DdkAdd("ge2d", 0, props, countof(props));
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "Could not add ge2d device";
    return status;
  }

  FX_LOG(INFO, TAG, "ge2d driver added");

  // ge2d device intentionally leaked as it is now held by DevMgr.
  __UNUSED auto* dev = ge2d_device.release();
  return status;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Ge2dBind;
  return ops;
}();

}  // namespace ge2d

// clang-format off
ZIRCON_DRIVER_BEGIN(ge2d, ge2d::driver_ops, "ge2d", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_T931),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_GE2D),
ZIRCON_DRIVER_END(ge2d)
