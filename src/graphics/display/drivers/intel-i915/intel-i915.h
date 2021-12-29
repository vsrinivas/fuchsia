// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_INTEL_I915_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_INTEL_I915_H_

#if __cplusplus

#include <fuchsia/hardware/display/controller/cpp/banjo.h>
#include <fuchsia/hardware/i2cimpl/c/banjo.h>
#include <fuchsia/hardware/intelgpucore/cpp/banjo.h>
#include <fuchsia/hardware/pci/c/banjo.h>
#include <fuchsia/hardware/sysmem/c/banjo.h>
#include <lib/ddk/mmio-buffer.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/mmio/mmio.h>
#include <lib/pci/hw.h>
#include <lib/zx/channel.h>
#include <threads.h>

#include <memory>
#include <optional>

#include <fbl/vector.h>

#include "display-device.h"
#include "dp-display.h"
#include "gtt.h"
#include "hdmi-display.h"
#include "igd.h"
#include "interrupts.h"
#include "pipe.h"
#include "power.h"
#include "registers-ddi.h"
#include "registers-dpll.h"
#include "registers-pipe.h"
#include "registers-transcoder.h"
#include "registers.h"

namespace i915 {

typedef struct buffer_allocation {
  uint16_t start;
  uint16_t end;
} buffer_allocation_t;

// TODO(armansito): Turn this into a C++-style data structure and document internals.
typedef struct dpll_state {
  bool is_hdmi;
  union {
    registers::DpllControl1::LinkRate dp_rate;
    struct {
      uint16_t dco_int;
      uint16_t dco_frac;
      uint8_t q;
      uint8_t q_mode;
      uint8_t k;
      uint8_t p;
      uint8_t cf;
    } hdmi;
  };
} dpll_state_t;

class Controller;
using DeviceType = ddk::Device<Controller, ddk::Initializable, ddk::Unbindable, ddk::Suspendable,
                               ddk::Resumable, ddk::GetProtocolable, ddk::ChildPreReleaseable>;

class Controller : public DeviceType,
                   public ddk::DisplayControllerImplProtocol<Controller, ddk::base_protocol>,
                   public ddk::IntelGpuCoreProtocol<Controller> {
 public:
  explicit Controller(zx_device_t* parent);
  ~Controller();

  // Perform short-running initialization of all subcomponents and instruct the DDK to publish the
  // device. On success, returns ZX_OK and the owernship of the Controller instance is claimed by
  // the DDK.
  //
  // Long-running initialization is performed in the DdkInit hook.
  static zx_status_t Create(zx_device_t* parent);

  static bool CompareDpllStates(const dpll_state_t& a, const dpll_state_t& b);

  // DDK ops
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  void DdkSuspend(ddk::SuspendTxn txn);
  void DdkResume(ddk::ResumeTxn txn);
  void DdkChildPreRelease(void* child_ctx) {
    fbl::AutoLock lock(&display_lock_);
    if (dc_intf_.is_valid()) {
      display_controller_interface_protocol_t proto;
      dc_intf_.GetProto(&proto);
      if (proto.ctx == child_ctx) {
        dc_intf_ = ddk::DisplayControllerInterfaceProtocolClient();
      }
    }
  }

  // display controller protocol ops
  void DisplayControllerImplSetDisplayControllerInterface(
      const display_controller_interface_protocol* intf);
  zx_status_t DisplayControllerImplImportImage(image_t* image, zx_unowned_handle_t handle,
                                               uint32_t index);
  void DisplayControllerImplReleaseImage(image_t* image);
  uint32_t DisplayControllerImplCheckConfiguration(const display_config_t** display_config,
                                                   size_t display_count,
                                                   uint32_t** layer_cfg_result,
                                                   size_t* layer_cfg_result_count);
  void DisplayControllerImplApplyConfiguration(const display_config_t** display_config,
                                               size_t display_count,
                                               const config_stamp_t* config_stamp);
  void DisplayControllerImplSetEld(uint64_t display_id, const uint8_t* raw_eld_list,
                                   size_t raw_eld_count);
  zx_status_t DisplayControllerImplGetSysmemConnection(zx::channel connection);
  zx_status_t DisplayControllerImplSetBufferCollectionConstraints(const image_t* config,
                                                                  uint32_t collection);

  // gpu core ops
  zx_status_t IntelGpuCoreReadPciConfig16(uint16_t addr, uint16_t* value_out);
  zx_status_t IntelGpuCoreMapPciMmio(uint32_t pci_bar, uint8_t** addr_out, uint64_t* size_out);
  zx_status_t IntelGpuCoreUnmapPciMmio(uint32_t pci_bar);
  zx_status_t IntelGpuCoreGetPciBti(uint32_t index, zx::bti* bti_out);
  zx_status_t IntelGpuCoreRegisterInterruptCallback(const intel_gpu_core_interrupt_t* callback,
                                                    uint32_t interrupt_mask);
  zx_status_t IntelGpuCoreUnregisterInterruptCallback();
  uint64_t IntelGpuCoreGttGetSize();
  zx_status_t IntelGpuCoreGttAlloc(uint64_t page_count, uint64_t* addr_out);
  zx_status_t IntelGpuCoreGttFree(uint64_t addr);
  zx_status_t IntelGpuCoreGttClear(uint64_t addr);
  zx_status_t IntelGpuCoreGttInsert(uint64_t addr, zx::vmo buffer, uint64_t page_offset,
                                    uint64_t page_count);
  void GpuRelease();

  // i2c ops
  uint32_t GetBusCount();
  zx_status_t GetMaxTransferSize(uint32_t bus_id, size_t* out_size);
  zx_status_t SetBitrate(uint32_t bus_id, uint32_t bitrate);
  zx_status_t Transact(uint32_t bus_id, const i2c_impl_op_t* ops, size_t count);

  ddk::MmioBuffer* mmio_space() { return mmio_space_.has_value() ? &*mmio_space_ : nullptr; }
  Interrupts* interrupts() { return &interrupts_; }
  uint16_t device_id() const { return device_id_; }
  const IgdOpRegion& igd_opregion() const { return igd_opregion_; }
  Power* power() { return &power_; }

  // Non-const getter to allow unit tests to modify the IGD.
  // TODO(fxbug.dev/83998): Consider making a fake IGD object injectable as allowing mutable access
  // to internal state that is intended to be externally immutable can be source of bugs if used
  // incorrectly. The various "ForTesting" methods are a typical anti-pattern that exposes internal
  // state and makes the class state machine harder to reason about.
  IgdOpRegion* igd_opregion_for_testing() { return &igd_opregion_; }

  void HandleHotplug(registers::Ddi ddi, bool long_pulse);
  void HandlePipeVsync(registers::Pipe pipe, zx_time_t timestamp);

  void ResetPipe(registers::Pipe pipe) __TA_NO_THREAD_SAFETY_ANALYSIS;
  bool ResetTrans(registers::Trans trans);
  bool ResetDdi(registers::Ddi ddi);

  registers::Dpll SelectDpll(bool is_edp, const dpll_state_t& state);
  const dpll_state_t* GetDpllState(registers::Dpll dpll);

  void SetMmioForTesting(ddk::MmioBuffer mmio_space) { mmio_space_ = std::move(mmio_space); }

  void ResetMmioSpaceForTesting() { mmio_space_.reset(); }

  // For every frame, in order to use the imported image, it is required to set
  // up the image based on given rotation in GTT and use the handle offset in
  // GTT. Returns the image base address used for display registers.
  uint64_t SetupGttImage(const image_t* image, uint32_t rotation);

 private:
  // Perform short-running initialization of all subcomponents and instruct the DDK to publish the
  // device. On success, returns ZX_OK and the ownership of the Controller instance is claimed by
  // the DDK.
  //
  // Long-running initialization is performed in the DdkInit hook.
  zx_status_t Init();

  const std::unique_ptr<GttRegion>& GetGttRegion(uint64_t handle);
  void EnableBacklight(bool enable);
  void InitDisplays();
  std::unique_ptr<DisplayDevice> QueryDisplay(registers::Ddi ddi) __TA_REQUIRES(display_lock_);
  bool LoadHardwareState(registers::Ddi ddi, DisplayDevice* device) __TA_REQUIRES(display_lock_);
  zx_status_t AddDisplay(std::unique_ptr<DisplayDevice> display) __TA_REQUIRES(display_lock_);
  void RemoveDisplay(std::unique_ptr<DisplayDevice> display) __TA_REQUIRES(display_lock_);
  bool BringUpDisplayEngine(bool resume) __TA_REQUIRES(display_lock_);
  void InitDisplayBuffers();
  DisplayDevice* FindDevice(uint64_t display_id) __TA_REQUIRES(display_lock_);

  void CallOnDisplaysChanged(DisplayDevice** added, size_t added_count, uint64_t* removed,
                             size_t removed_count) __TA_REQUIRES(display_lock_);

  // Gets the layer_t* config for the given pipe/plane. Return false if there is no layer.
  bool GetPlaneLayer(registers::Pipe pipe, uint32_t plane, const display_config_t** configs,
                     size_t display_count, const layer_t** layer_out) __TA_REQUIRES(display_lock_);
  uint16_t CalculateBuffersPerPipe(size_t display_count);
  // Returns false if no allocation is possible. When that happens,
  // plane 0 of the failing displays will be set to UINT16_MAX.
  bool CalculateMinimumAllocations(
      const display_config_t** display_configs, size_t display_count,
      uint16_t min_allocs[registers::kPipeCount][registers::kImagePlaneCount])
      __TA_REQUIRES(display_lock_);
  // Updates plane_buffers_ based pipe_buffers_ and the given parameters
  void UpdateAllocations(
      const uint16_t min_allocs[registers::kPipeCount][registers::kImagePlaneCount],
      const uint64_t display_rate[registers::kPipeCount][registers::kImagePlaneCount])
      __TA_REQUIRES(display_lock_);
  // Reallocates the pipe buffers when a pipe comes online/goes offline. This is a
  // long-running operation, as shifting allocations between pipes requires waiting
  // for vsync.
  void DoPipeBufferReallocation(buffer_allocation_t active_allocation[registers::kPipeCount])
      __TA_REQUIRES(display_lock_);
  // Reallocates plane buffers based on the given layer config.
  void ReallocatePlaneBuffers(const display_config_t** display_configs, size_t display_count,
                              bool reallocate_pipes) __TA_REQUIRES(display_lock_);

  // Validates that a basic layer configuration can be supported for the
  // given modes of the displays.
  bool CheckDisplayLimits(const display_config_t** display_configs, size_t display_count,
                          uint32_t** layer_cfg_results) __TA_REQUIRES(display_lock_);

  bool CalculatePipeAllocation(const display_config_t** display_config, size_t display_count,
                               uint64_t alloc[registers::kPipeCount]) __TA_REQUIRES(display_lock_);
  bool ReallocatePipes(const display_config_t** display_config, size_t display_count)
      __TA_REQUIRES(display_lock_);

  zx_device_t* zx_gpu_dev_ = nullptr;
  bool gpu_released_ = false;
  bool display_released_ = false;

  sysmem_protocol_t sysmem_;

  ddk::DisplayControllerInterfaceProtocolClient dc_intf_ __TA_GUARDED(display_lock_);
  bool ready_for_callback_ __TA_GUARDED(display_lock_) = false;

  Gtt gtt_ __TA_GUARDED(gtt_lock_);
  mtx_t gtt_lock_;
  // These regions' VMOs are not owned
  fbl::Vector<std::unique_ptr<GttRegion>> imported_images_ __TA_GUARDED(gtt_lock_);
  // These regions' VMOs are owned
  fbl::Vector<std::unique_ptr<GttRegion>> imported_gtt_regions_ __TA_GUARDED(gtt_lock_);

  IgdOpRegion igd_opregion_;  // Read only, no locking
  Interrupts interrupts_;     // Internal locking

  pci_protocol_t pci_;
  struct {
    mmio_buffer_t mmio;
    int32_t count = 0;
  } mapped_bars_[PCI_MAX_BAR_COUNT] __TA_GUARDED(bar_lock_);
  mtx_t bar_lock_;
  // The mmio_space_ is read only. The internal registers are guarded by various locks where
  // appropriate.
  std::optional<ddk::MmioBuffer> mmio_space_;

  // References to displays. References are owned by devmgr, but will always
  // be valid while they are in this vector.
  fbl::Vector<std::unique_ptr<DisplayDevice>> display_devices_ __TA_GUARDED(display_lock_);
  uint64_t next_id_ __TA_GUARDED(display_lock_) = 1;  // id can't be INVALID_DISPLAY_ID == 0
  mtx_t display_lock_;

  fbl::Vector<Pipe> pipes_ __TA_GUARDED(display_lock_);

  Power power_;
  PowerWellRef cd_clk_power_well_;
  struct {
    uint8_t use_count = 0;
    dpll_state_t state;
  } dplls_[registers::kDpllCount] = {};

  GMBusI2c gmbus_i2cs_[registers::kDdiCount] = {
      GMBusI2c(registers::DDI_A), GMBusI2c(registers::DDI_B), GMBusI2c(registers::DDI_C),
      GMBusI2c(registers::DDI_D), GMBusI2c(registers::DDI_E),
  };

  DpAux dp_auxs_[registers::kDdiCount] = {
      DpAux(registers::DDI_A), DpAux(registers::DDI_B), DpAux(registers::DDI_C),
      DpAux(registers::DDI_D), DpAux(registers::DDI_E),
  };

  // Plane buffer allocation. If no alloc, start == end == registers::PlaneBufCfg::kBufferCount.
  buffer_allocation_t plane_buffers_[registers::kPipeCount]
                                    [registers::kImagePlaneCount] __TA_GUARDED(display_lock_) = {};
  // Buffer allocations for pipes
  buffer_allocation_t pipe_buffers_[registers::kPipeCount] __TA_GUARDED(display_lock_) = {};
  bool initial_alloc_ = true;

  uint16_t device_id_;
  uint32_t flags_;

  // Various configuration values set by the BIOS which need to be carried across suspend.
  uint32_t pp_divisor_val_;
  uint32_t pp_off_delay_val_;
  uint32_t pp_on_delay_val_;
  uint32_t sblc_ctrl2_val_;
  uint32_t schicken1_val_;
  bool ddi_a_lane_capability_control_;
  bool sblc_polarity_;

  std::optional<uint64_t> eld_display_id_;

  // Debug
  inspect::Inspector inspector_;
  inspect::Node root_node_;
};

}  // namespace i915

#endif  // __cplusplus

__BEGIN_CDECLS
zx_status_t intel_i915_bind(void* ctx, zx_device_t* parent);
__END_CDECLS

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_INTEL_I915_H_
