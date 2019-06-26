// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gdc.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <stdint.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <memory>

#include "gdc-regs.h"

namespace gdc {

namespace {

constexpr uint32_t kHiu = 0;
constexpr uint32_t kGdc = 1;

}  // namespace

void GdcDevice::InitClocks() {
  // First reset the clocks.
  GdcClkCntl::Get().ReadFrom(&clock_mmio_).reset_axi().reset_core().WriteTo(&clock_mmio_);

  // Set the clocks to 8Mhz
  // Source XTAL
  // Clock divisor = 3
  GdcClkCntl::Get()
      .ReadFrom(&clock_mmio_)
      .set_axi_clk_div(3)
      .set_axi_clk_en(1)
      .set_axi_clk_sel(0)
      .set_core_clk_div(3)
      .set_core_clk_en(1)
      .set_core_clk_sel(0)
      .WriteTo(&clock_mmio_);

  // Enable GDC Power domain.
  GdcMemPowerDomain::Get().ReadFrom(&clock_mmio_).set_gdc_pd(0).WriteTo(&clock_mmio_);
}

zx_status_t GdcDevice::GdcInitTask(const buffer_collection_info_t* input_buffer_collection,
                                   const buffer_collection_info_t* output_buffer_collection,
                                   zx::vmo config_vmo, const gdc_callback_t* callback,
                                   uint32_t* out_task_index) {
  if (out_task_index == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::unique_ptr<Task> task;
  zx_status_t status = gdc::Task::Create(input_buffer_collection, output_buffer_collection,
                                         config_vmo, callback, bti_, &task);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "%s: Task Creation Failed %d\n", __func__, status);
    return status;
  }

  // Put an entry in the hashmap.
  task_map_[next_task_index_] = std::move(task);
  *out_task_index = next_task_index_;
  next_task_index_++;
  return ZX_OK;
}

void GdcDevice::ProcessTask(TaskInfo& info) {
  auto task = info.task;
  auto input_buffer_index = info.input_buffer_index;

  // TODO(CAM-33): Add Processing of the frame implementation here.

  zx_port_packet_t packet;
  ZX_ASSERT(ZX_OK == WaitForInterrupt(&packet));
  gdc_irq_.ack();

  // Currently there is only type of event coming in at this port.
  // We could possibly add more, like one to terminate the process.
  if (packet.key == kPortKeyIrqMsg) {
    // Invoke the callback function and tell about the output buffer index
    // which is ready to be used.
    // TODO(CAM-33): pass actual output buffer index instead of
    // input_buffer_index.
    task->callback()->frame_ready(task->callback()->ctx, input_buffer_index);
  }
}

int GdcDevice::FrameProcessingThread() {
  FX_LOGF(INFO, "", "%s: start \n", __func__);

  while (running_.load()) {
    // Waiting for the event when a task is queued.
    sync_completion_wait(&frame_processing_signal_, ZX_TIME_INFINITE);
    bool pending_task = false;
    // Dequeing the entire deque till it drains.
    do {
      TaskInfo info;
      {
        fbl::AutoLock lock(&deque_lock_);
        if (!processing_queue_.empty()) {
          info = processing_queue_.back();
          processing_queue_.pop_back();
          pending_task = true;
        } else {
          pending_task = false;
        }
      }
      if (pending_task) {
        ProcessTask(info);
      }
    } while (pending_task);
    // Now that the deque is drained we reset the signal.
    sync_completion_reset(&frame_processing_signal_);
  }
  return 0;
}

zx_status_t GdcDevice::GdcProcessFrame(uint32_t task_index, uint32_t input_buffer_index) {
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
  info.task = task_entry->second.get();
  info.input_buffer_index = input_buffer_index;

  // Put the task on queue.
  fbl::AutoLock lock(&deque_lock_);
  processing_queue_.push_front(std::move(info));
  sync_completion_signal(&frame_processing_signal_);
  return ZX_OK;
}

zx_status_t GdcDevice::StartThread() {
  running_.store(true);
  return thrd_status_to_zx_status(thrd_create_with_name(
      &processing_thread_,
      [](void* arg) -> int { return reinterpret_cast<GdcDevice*>(arg)->FrameProcessingThread(); },
      this, "gdc-processing-thread"));
}

zx_status_t GdcDevice::StopThread() {
  running_.store(false);
  // Signal the thread waiting on this signal
  sync_completion_signal(&frame_processing_signal_);
  gdc_irq_.destroy();
  JoinThread();
  return ZX_OK;
}

zx_status_t GdcDevice::WaitForInterrupt(zx_port_packet_t* packet) {
  return port_.wait(zx::time::infinite(), packet);
}

void GdcDevice::GdcRemoveTask(uint32_t task_index) {
  // Find the entry in hashmap.
  auto task_entry = task_map_.find(task_index);
  ZX_ASSERT(task_entry != task_map_.end());

  // Remove map entry.
  task_map_.erase(task_entry);
}

void GdcDevice::GdcReleaseFrame(uint32_t task_index, uint32_t buffer_index) {
  // TODO(CAM-33): Implement this.
}

// static
zx_status_t GdcDevice::Setup(void* /*ctx*/, zx_device_t* parent, std::unique_ptr<GdcDevice>* out) {
  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    FX_LOGF(ERROR, "", "%s: ZX_PROTOCOL_PDEV not available\n", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> clk_mmio;
  zx_status_t status = pdev.MapMmio(kHiu, &clk_mmio);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> gdc_mmio;
  status = pdev.MapMmio(kGdc, &gdc_mmio);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
    return status;
  }

  zx::interrupt gdc_irq;
  status = pdev.GetInterrupt(0, &gdc_irq);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "%s: pdev_.GetInterrupt failed %d\n", __func__, status);
    return status;
  }

  zx::port port;
  status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "%s: port create failed %d\n", __func__, status);
    return status;
  }

  status = gdc_irq.bind(port, kPortKeyIrqMsg, 0 /*options*/);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "%s: interrupt bind failed %d\n", __func__, status);
    return status;
  }

  zx::bti bti;
  status = pdev.GetBti(0, &bti);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "%s: could not obtain bti: %d\n", __func__, status);
    return status;
  }

  fbl::AllocChecker ac;
  auto gdc_device = std::unique_ptr<GdcDevice>(
      new (&ac) GdcDevice(parent, std::move(*clk_mmio), std::move(*gdc_mmio), std::move(gdc_irq),
                          std::move(bti), std::move(port)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  gdc_device->InitClocks();

  status = gdc_device->StartThread();
  *out = std::move(gdc_device);
  return status;
}

void GdcDevice::DdkUnbind() {
  ShutDown();
  DdkRemove();
}

void GdcDevice::DdkRelease() {
  StopThread();
  delete this;
}

void GdcDevice::ShutDown() {}

zx_status_t GdcBind(void* ctx, zx_device_t* device) {
  std::unique_ptr<GdcDevice> gdc_device;
  zx_status_t status = gdc::GdcDevice::Setup(ctx, device, &gdc_device);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "%s: Could not setup gdc device: %d\n", __func__, status);
    return status;
  }
  zx_device_prop_t props[] = {
      {BIND_PLATFORM_PROTO, 0, ZX_PROTOCOL_GDC},
  };

// Run the unit tests for this device
// TODO(braval): CAM-44 (Run only when build flag enabled)
// This needs to be replaced with run unittests hooks when
// the framework is available.
#if 0
    status = gdc::GdcDeviceTester::RunTests(gdc_device.get());
    if (status != ZX_OK) {
        FX_LOGF(ERROR, "%s: Device Unit Tests Failed \n", __func__);
        return status;
    }
#endif

  status = gdc_device->DdkAdd("gdc", 0, props, countof(props));
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "%s: Could not add gdc device: %d\n", __func__, status);
    return status;
  }

  FX_LOGF(INFO, "", "%s: gdc driver added\n", __func__);

  // gdc device intentionally leaked as it is now held by DevMgr.
  __UNUSED auto* dev = gdc_device.release();
  return status;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = GdcBind;
  return ops;
}();

}  // namespace gdc

// clang-format off
ZIRCON_DRIVER_BEGIN(gdc, gdc::driver_ops, "gdc", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_ARM),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GDC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_ARM_MALI_IV010),
ZIRCON_DRIVER_END(gdc)
