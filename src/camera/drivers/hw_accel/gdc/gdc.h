// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_GDC_H_
#define SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_GDC_H_

#include <ddk/platform-defs.h>
#ifndef _ALL_SOURCE
#define _ALL_SOURCE  // Enables thrd_create_with_name in <threads.h>.
#include <threads.h>
#endif
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>
#include <zircon/fidl.h>

#include <deque>
#include <unordered_map>

#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/gdc.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>

#include "gdc-task.h"

namespace gdc {
// |GdcDevice| is spawned by the driver in |gdc.cc|
namespace {

constexpr uint64_t kPortKeyIrqMsg = 0x00;
constexpr uint64_t kPortKeyDebugFakeInterrupt = 0x01;

}  // namespace

// This provides ZX_PROTOCOL_GDC.
class GdcDevice;
using GdcDeviceType = ddk::Device<GdcDevice, ddk::Unbindable>;

class GdcDevice : public GdcDeviceType, public ddk::GdcProtocol<GdcDevice, ddk::base_protocol> {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(GdcDevice);
  explicit GdcDevice(zx_device_t* parent, ddk::MmioBuffer clk_mmio, ddk::MmioBuffer gdc_mmio,
                     zx::interrupt gdc_irq, zx::bti bti, zx::port port)
      : GdcDeviceType(parent),
        port_(std::move(port)),
        clock_mmio_(std::move(clk_mmio)),
        gdc_mmio_(std::move(gdc_mmio)),
        gdc_irq_(std::move(gdc_irq)),
        bti_(std::move(bti)) {}

  ~GdcDevice() { StopThread(); }

  // Setup() is used to create an instance of GdcDevice.
  // It sets up the pdev & brings the GDC out of reset.
  static zx_status_t Setup(void* ctx, zx_device_t* parent, std::unique_ptr<GdcDevice>* out);

  // Methods required by the ddk.
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  // ZX_PROTOCOL_GDC (Refer to gdc.banjo for documentation).
  zx_status_t GdcInitTask(const buffer_collection_info_2_t* input_buffer_collection,
                          const buffer_collection_info_2_t* output_buffer_collection,
                          const image_format_2_t* input_image_format,
                          const image_format_2_t* output_image_format_table_list,
                          size_t output_image_format_table_count,
                          uint32_t output_image_format_index,
                          const gdc_config_info* config_vmo_list, size_t config_vmos_count,
                          const hw_accel_frame_callback_t* frame_callback,
                          const hw_accel_res_change_callback* res_callback,
                          const hw_accel_remove_task_callback_t* task_remove_callback,
                          uint32_t* out_task_index);

  zx_status_t GdcProcessFrame(uint32_t task_index, uint32_t input_buffer_index);
  void GdcRemoveTask(uint32_t task_index);
  void GdcReleaseFrame(uint32_t task_index, uint32_t buffer_index);
  zx_status_t GdcSetOutputResolution(uint32_t task_index, uint32_t new_output_image_format_index);

  // Used for unit tests.
  const ddk::MmioBuffer* gdc_mmio() const { return &gdc_mmio_; }
  zx_status_t StartThread();
  zx_status_t StopThread();

 protected:
  enum GdcOp {
    GDC_OP_FRAME,
    GDC_OP_SETOUTPUTRES,
    GDC_OP_REMOVE_TASK,
  };

  struct TaskInfo {
    GdcOp op;
    GdcTask* task;
    // case: GDC_OP_SETOUTPUTRES |index| = output format index
    // case: GDC_OP_FRAME        |index| = input buffer index
    uint32_t index;
    // Index of the task in the hashmap
    uint32_t task_index;
  };

  zx::port port_;

 private:
  friend class GdcDeviceTester;

  // All necessary clean up is done here in ShutDown().
  void ShutDown();
  void InitClocks();
  int FrameProcessingThread();
  int JoinThread() { return thrd_join(processing_thread_, nullptr); }
  void Start() const;
  void Stop() const;

  void ProcessTask(TaskInfo& info);
  void ChangeOutputResoultion(TaskInfo& info);
  void ProcessFrame(TaskInfo& info);
  void RemoveTask(TaskInfo& info);
  zx_status_t WaitForInterrupt(zx_port_packet_t* packet);

  // Used to access the processing queue.
  fbl::Mutex processing_queue_lock_;
  // Used to access the GDC's banjo interface.
  fbl::Mutex interface_lock_;

  // HHI register block has the clock registers
  ddk::MmioBuffer clock_mmio_;
  ddk::MmioBuffer gdc_mmio_;
  zx::interrupt gdc_irq_;
  zx::bti bti_;
  uint32_t next_task_index_ __TA_GUARDED(interface_lock_) = 0;
  std::unordered_map<uint32_t, std::unique_ptr<GdcTask>> task_map_ __TA_GUARDED(interface_lock_);
  std::deque<TaskInfo> processing_queue_ __TA_GUARDED(processing_queue_lock_);
  thrd_t processing_thread_;
  fbl::ConditionVariable frame_processing_signal_ __TA_GUARDED(processing_queue_lock_);
  bool shutdown_ __TA_GUARDED(processing_queue_lock_) = false;
};

}  // namespace gdc

#endif  // SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_GDC_H_
