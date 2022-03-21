// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_AML_SDMMC_AML_SDMMC_H_
#define SRC_DEVICES_BLOCK_DRIVERS_AML_SDMMC_AML_SDMMC_H_

#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/sdmmc/cpp/banjo.h>
#include <lib/ddk/phys-iter.h>
#include <lib/fit/function.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/mmio/mmio.h>
#include <lib/stdcompat/span.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/status.h>
#include <threads.h>

#include <limits>
#include <optional>
#include <vector>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <soc/aml-common/aml-sdmmc.h>

#include "src/lib/vmo_store/vmo_store.h"

namespace sdmmc {

class AmlSdmmc;
using AmlSdmmcType = ddk::Device<AmlSdmmc, ddk::Suspendable>;

class AmlSdmmc : public AmlSdmmcType, public ddk::SdmmcProtocol<AmlSdmmc, ddk::base_protocol> {
 public:
  AmlSdmmc(zx_device_t* parent, zx::bti bti, fdf::MmioBuffer mmio,
           fdf::MmioPinnedBuffer pinned_mmio, aml_sdmmc_config_t config, zx::interrupt irq,
           const ddk::GpioProtocolClient& gpio);

  virtual ~AmlSdmmc() = default;
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation
  void DdkRelease();
  void DdkSuspend(ddk::SuspendTxn txn);

  // Sdmmc Protocol implementation
  zx_status_t SdmmcHostInfo(sdmmc_host_info_t* out_info);
  zx_status_t SdmmcSetSignalVoltage(sdmmc_voltage_t voltage);
  zx_status_t SdmmcSetBusWidth(sdmmc_bus_width_t bus_width);
  zx_status_t SdmmcSetBusFreq(uint32_t bus_freq);
  zx_status_t SdmmcSetTiming(sdmmc_timing_t timing);
  void SdmmcHwReset();
  zx_status_t SdmmcPerformTuning(uint32_t cmd_idx);
  zx_status_t SdmmcRequest(sdmmc_req_t* req);
  zx_status_t SdmmcRegisterInBandInterrupt(const in_band_interrupt_protocol_t* interrupt_cb);
  zx_status_t SdmmcRegisterVmo(uint32_t vmo_id, uint8_t client_id, zx::vmo vmo, uint64_t offset,
                               uint64_t size, uint32_t vmo_rights);
  zx_status_t SdmmcUnregisterVmo(uint32_t vmo_id, uint8_t client_id, zx::vmo* out_vmo);
  zx_status_t SdmmcRequestNew(const sdmmc_req_new_t* req, uint32_t out_response[4]);

  // Visible for tests
  zx_status_t Init(const pdev_device_info_t& device_info);
  void set_board_config(const aml_sdmmc_config_t& board_config) { board_config_ = board_config; }

 protected:
  // Visible for tests
  zx_status_t Bind();

  virtual zx_status_t WaitForInterruptImpl();
  virtual void WaitForBus() const;

  aml_sdmmc_desc_t* descs() { return static_cast<aml_sdmmc_desc_t*>(descs_buffer_.virt()); }

  zx::vmo GetInspectVmo() const { return inspect_.inspector.DuplicateVmo(); }

  fdf::MmioBuffer mmio_;

 private:
  constexpr static size_t kResponseCount = 4;

  struct TuneResults {
    uint64_t results = 0;
    uint32_t param_max = 0;

    bool all_passed() const {
      if (results == std::numeric_limits<decltype(results)>::max()) {
        return true;
      }

      return results == ((1ULL << param_max) - 1);
    }

    std::string ToString() const {
      char string[param_max + 2];
      for (uint32_t i = 0; i <= param_max; i++) {
        string[i] = (results & (1ULL << i)) ? '|' : '-';
      }
      string[param_max + 1] = '\0';
      return string;
    }
  };

  struct TuneWindow {
    uint32_t start = 0;
    uint32_t size = 0;
    TuneResults results;

    uint32_t middle() const { return start + (size / 2); }
  };

  struct TuneSettings {
    uint32_t adj_delay = 0;
    uint32_t delay = 0;
  };

  // VMO metadata that needs to be stored in accordance with the SDMMC protocol.
  struct OwnedVmoInfo {
    uint64_t offset;
    uint64_t size;
    uint32_t rights;
  };

  struct Inspect {
    inspect::Inspector inspector;
    inspect::Node root;
    inspect::UintProperty bus_clock_frequency;
    inspect::UintProperty adj_delay;
    inspect::UintProperty delay_lines;
    std::vector<inspect::StringProperty> tuning_results;
    inspect::UintProperty max_delay;
    inspect::UintProperty longest_window_start;
    inspect::UintProperty longest_window_size;
    inspect::UintProperty longest_window_adj_delay;
    inspect::UintProperty distance_to_failing_point;
    inspect::StringProperty tuning_method;

    void Init(const pdev_device_info_t& device_info);
  };

  using SdmmcVmoStore = vmo_store::VmoStore<vmo_store::HashTableStorage<uint32_t, OwnedVmoInfo>>;

  uint32_t DistanceToFailingPoint(TuneSettings point,
                                  cpp20::span<const TuneResults> adj_delay_results);
  zx::status<TuneSettings> PerformNewTuning(cpp20::span<const TuneResults> adj_delay_results);
  zx::status<TuneSettings> PerformOldTuning(cpp20::span<const TuneResults> adj_delay_results);
  zx_status_t TuningDoTransfer(uint8_t* tuning_res, size_t blk_pattern_size,
                               uint32_t tuning_cmd_idx);
  bool TuningTestSettings(cpp20::span<const uint8_t> tuning_blk, uint32_t tuning_cmd_idx);
  // Sweeps from zero to param_max and creates a TuneWindow representing the largest span of values
  // for which check_param returned true.
  static TuneWindow ProcessTuningResults(uint32_t param_max,
                                         fit::function<bool(uint32_t)> check_param);
  // Like above, but considers param_max to be adjacent to zero when creating the TuneWindow.
  static TuneWindow ProcessTuningResultsWithWrapping(uint32_t param_max,
                                                     fit::function<bool(uint32_t)> check_param);
  static TuneWindow ProcessTuningResultsInternal(uint32_t param_max,
                                                 fit::function<bool(uint32_t)> check_param,
                                                 bool wrap);
  TuneResults TuneDelayLines(cpp20::span<const uint8_t> tuning_blk, uint32_t tuning_cmd_idx);

  void SetAdjDelay(uint32_t adj_delay);
  void SetDelayLines(uint32_t delay);
  uint32_t max_delay() const;

  void ConfigureDefaultRegs();
  void SetupCmdDesc(sdmmc_req_t* req, aml_sdmmc_desc_t** out_desc);
  aml_sdmmc_desc_t* SetupCmdDescNew(const sdmmc_req_new_t& req);
  // Prepares the VMO and sets up the data descriptors
  zx_status_t SetupDataDescsDma(sdmmc_req_t* req, aml_sdmmc_desc_t* cur_desc,
                                aml_sdmmc_desc_t** last_desc);
  // Sets up the data descriptors using the ping/pong buffers
  zx_status_t SetupDataDescsPio(sdmmc_req_t* req, aml_sdmmc_desc_t* desc,
                                aml_sdmmc_desc_t** last_desc);
  zx_status_t SetupDataDescs(sdmmc_req_t* req, aml_sdmmc_desc_t* desc,
                             aml_sdmmc_desc_t** last_desc);
  // Returns a pointer to the LAST descriptor used.
  zx::status<std::pair<aml_sdmmc_desc_t*, std::vector<fzl::PinnedVmo>>> SetupDataDescsNew(
      const sdmmc_req_new_t& req, aml_sdmmc_desc_t* cur_desc);
  // These return pointers to the NEXT descriptor to use.
  zx::status<aml_sdmmc_desc_t*> SetupOwnedVmoDescs(const sdmmc_req_new_t& req,
                                                   const sdmmc_buffer_region_t& buffer,
                                                   vmo_store::StoredVmo<OwnedVmoInfo>& vmo,
                                                   aml_sdmmc_desc_t* cur_desc);
  zx::status<std::pair<aml_sdmmc_desc_t*, fzl::PinnedVmo>> SetupUnownedVmoDescs(
      const sdmmc_req_new_t& req, const sdmmc_buffer_region_t& buffer, aml_sdmmc_desc_t* cur_desc);
  zx::status<aml_sdmmc_desc_t*> PopulateDescriptors(const sdmmc_req_new_t& req,
                                                    aml_sdmmc_desc_t* cur_desc,
                                                    fzl::PinnedVmo::Region region);
  static zx_status_t FinishReq(sdmmc_req_t* req);
  void ClearStatus();
  zx_status_t WaitForInterrupt(sdmmc_req_t* req);
  zx::status<std::array<uint32_t, kResponseCount>> WaitForInterruptNew(const sdmmc_req_new_t& req);

  void ShutDown();

  zx::bti bti_;

  fdf::MmioPinnedBuffer pinned_mmio_;
  const ddk::GpioProtocolClient reset_gpio_;
  zx::interrupt irq_;
  aml_sdmmc_config_t board_config_;

  sdmmc_host_info_t dev_info_;
  ddk::IoBuffer descs_buffer_;
  uint32_t max_freq_, min_freq_;

  fbl::Mutex mtx_;
  fbl::ConditionVariable txn_finished_ TA_GUARDED(mtx_);
  std::atomic<bool> dead_ TA_GUARDED(mtx_);
  std::atomic<bool> pending_txn_ TA_GUARDED(mtx_);
  std::optional<SdmmcVmoStore> registered_vmos_[SDMMC_MAX_CLIENT_ID + 1];

  uint64_t consecutive_cmd_errors_ = 0;
  uint64_t consecutive_data_errors_ = 0;

  Inspect inspect_;
};

}  // namespace sdmmc

#endif  // SRC_DEVICES_BLOCK_DRIVERS_AML_SDMMC_AML_SDMMC_H_
