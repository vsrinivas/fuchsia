// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_ISP_MALI_009_ARM_ISP_H_
#define SRC_CAMERA_DRIVERS_ISP_MALI_009_ARM_ISP_H_

#include <fuchsia/hardware/camera/c/fidl.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fit/function.h>
#include <lib/sync/completion.h>
#include <lib/zx/interrupt.h>

#include <atomic>
#include <memory>

#include <ddk/metadata/camera.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/camerahwaccel.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/isp.h>
#include <fbl/mutex.h>
#include <hw/reg.h>

#include "../modules/dma-mgr.h"
#include "../modules/gamma-rgb-registers.h"
#include "../modules/sensor.h"
#include "arm-isp-test.h"
#include "global_regs.h"
#include "pingpong_regs.h"
#ifndef _ALL_SOURCE
#define _ALL_SOURCE  // Enables thrd_create_with_name in <threads.h>.
#endif
#include <threads.h>
#include <zircon/fidl.h>

namespace camera {

namespace {

// TODO(CAM-87): Formalize isp sub-block start address style.
constexpr uint32_t kGammaRgbPingFrAddr = 0x1c064;
constexpr uint32_t kGammaRgbPingDsAddr = 0x1c1d8;

}  // namespace

// |ArmIspDevice| is spawned by the driver in |arm-isp.cc|
// This provides the interface provided in camera.fidl in Zircon.
class ArmIspDevice;
using IspDeviceType = ddk::Device<ArmIspDevice, ddk::UnbindableNew>;

class ArmIspDevice : public IspDeviceType,
                     public ddk::IspProtocol<ArmIspDevice, ddk::base_protocol> {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ArmIspDevice);

  explicit ArmIspDevice(zx_device_t* parent, ddk::MmioBuffer hiu_mmio, ddk::MmioBuffer power_mmio,
                        ddk::MmioBuffer memory_pd_mmio, ddk::MmioBuffer reset_mmio,
                        ddk::MmioBuffer isp_mmio, mmio_buffer_t local_mmio, zx::interrupt isp_irq,
                        zx::bti bti, zx_device_t* camera_sensor)
      : IspDeviceType(parent),
        pdev_(parent),
        hiu_mmio_(std::move(hiu_mmio)),
        power_mmio_(std::move(power_mmio)),
        memory_pd_mmio_(std::move(memory_pd_mmio)),
        reset_mmio_(std::move(reset_mmio)),
        isp_mmio_(std::move(isp_mmio)),
        isp_mmio_local_(local_mmio, 0),
        isp_irq_(std::move(isp_irq)),
        bti_(std::move(bti)),
        camera_sensor_(camera_sensor),
        gamma_rgb_fr_regs_(ddk::MmioView(local_mmio, kGammaRgbPingFrAddr)),
        gamma_rgb_ds_regs_(ddk::MmioView(local_mmio, kGammaRgbPingDsAddr)) {}

  ~ArmIspDevice();

  static zx_status_t Init(void** ctx_out);
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Methods required by the ddk.
  void DdkRelease();
  void DdkUnbindNew(ddk::UnbindTxn txn);

  // +++++++++   ZX_PROTOCOL_ISP +++++++++++++++++++++++
  // This is the interface that is used by the Camera Controller
  // to set the format for the ISP output streams, provide buffers
  // for the frames that the ISP writes to, and establishes a control and
  // response interface between the camera controller and the ISP.
  // |buffer_collection| : Hold the format and pool of VMOs that the ISP will
  //                       produce
  // |image_format| : The format of images in the stream
  // |rate|  : The frame rate of the output
  // |type|  : The stream type (full resolution or downscaled)
  // |frame_callback| : The protocol which calls a function when the ISP is done
  //            writing to a buffer.
  // |out_s| : (output) Protocol over which the flow of frames is controlled.
  // @Return : indicates if the stream was created.
  zx_status_t IspCreateOutputStream(const buffer_collection_info_2_t* buffer_collection,
                                    const image_format_2_t* image_format, const frame_rate_t* rate,
                                    stream_type_t type,
                                    const hw_accel_frame_callback_t* frame_callback,
                                    output_stream_protocol_t* out_s);

  // Functions to service the output_stream_protocol interface:

  // Releases a frame that was being used by a consumer
  // |buffer_id| : the buffer_id that was sent with FrameReady
  // |type| : Either STREAM_TYPE_FULL_RESOLUTION or STREAM_TYPE_DOWNSCALED
  // @Return : indicates if the frame was released.
  zx_status_t ReleaseFrame(uint32_t buffer_id, stream_type_t type);

  // Starts streaming one of the stream types.
  // |type| : Either STREAM_TYPE_FULL_RESOLUTION or STREAM_TYPE_DOWNSCALED
  // @Return : indicates if the stream was started.
  zx_status_t StartStream(stream_type_t type);

  // Stops streaming one of the stream types.
  // |type| : Either STREAM_TYPE_FULL_RESOLUTION or STREAM_TYPE_DOWNSCALED
  // @Return : indicates if the stream was stopped.
  zx_status_t StopStream(stream_type_t type);
  // ---------------  End ZX_PROTOCOL_ISP ---------------

  // ISP Init Sequences (init_sequences.cc)
  void IspLoadSeq_linear();
  void IspLoadSeq_settings();
  void IspLoadSeq_fs_lin_2exp();
  void IspLoadSeq_fs_lin_3exp();
  void IspLoadSeq_fs_lin_4exp();
  void IspLoadSeq_settings_context();
  void IspLoadCustomSequence();

  bool IsIspConfigInitialized() { return initialized_; }

 private:
  zx_status_t InitIsp();
  zx_status_t IspContextInit();

  // A skeleton function for testing the ISP with the ISPDeviceTester:
  zx_status_t RunTests() { return ZX_OK; }

  void ShutDown();
  void PowerUpIsp();
  void IspHWReset(bool reset);
  int IspIrqHandler();
  void HandleDmaError();
  zx_status_t ErrorRoutine();
  void CopyContextInfo(uint8_t config_space, uint8_t direction);
  void CopyMeteringInfo(uint8_t config_space);
  zx_status_t SetPort(uint8_t kMode);
  bool IsFrameProcessingInProgress();
  zx_status_t SetupIspConfig();

  zx_status_t StartStreaming();
  zx_status_t StopStreaming();

  // Get the DMA Manager associated with stream type |type|.
  DmaManager* GetStream(stream_type_t type);

  // Functions used by the debugging / testing interface:
  // Returns all the current registers written into a struct for analysis.
  ArmIspRegisterDump DumpRegisters();

  ddk::PDev pdev_;

  ddk::MmioBuffer hiu_mmio_;
  ddk::MmioBuffer power_mmio_;
  ddk::MmioBuffer memory_pd_mmio_;
  ddk::MmioBuffer reset_mmio_;
  ddk::MmioBuffer isp_mmio_;
  // MmioView is currently used and created using a custom mmio_buffer_t
  // populated with malloced memory.
  // We can switch to using the actual mmio_buffer_t
  // when we plan to use SW-HW context, in order to make a easy switch.
  ddk::MmioView isp_mmio_local_;

  zx::interrupt isp_irq_;
  thrd_t irq_thread_;
  zx::bti bti_;
  std::atomic<bool> running_;
  bool initialized_ = false;

  // Thread for processing work for each frame.
  int FrameProcessingThread();
  thrd_t frame_processing_thread_;
  std::atomic<bool> running_frame_processing_;

  ddk::CameraSensorProtocolClient camera_sensor_;

  std::unique_ptr<camera::Sensor> sensor_module_;
  std::unique_ptr<camera::DmaManager> full_resolution_dma_;
  std::unique_ptr<camera::DmaManager> downscaled_dma_;
  bool streaming_ = false;

  // TODO(CAM-88): Formalize isp sub-block ownership.
  GammaRgbRegisters gamma_rgb_fr_regs_;
  GammaRgbRegisters gamma_rgb_ds_regs_;

  sync_completion_t frame_processing_signal_;

  // Callback to call when calling DdkUnbind,
  // so the ArmIspDeviceTester (if it exists) stops interfacing
  // with this class.
  fit::callback<void()> on_isp_unbind_;
  // This lock prevents this class from being unbound while it's child is being
  // set up:
  fbl::Mutex unbind_lock_;
  friend class ArmIspDeviceTester;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_ISP_MALI_009_ARM_ISP_H_
