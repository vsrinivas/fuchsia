// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DP_DISPLAY_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DP_DISPLAY_H_

#include <fuchsia/hardware/i2cimpl/c/banjo.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/status.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <cstdint>

#include "src/graphics/display/drivers/intel-i915-tgl/ddi-aux-channel.h"
#include "src/graphics/display/drivers/intel-i915-tgl/display-device.h"
#include "src/graphics/display/drivers/intel-i915-tgl/dpcd.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pch-engine.h"

namespace i915_tgl {

// Abstraction over the DPCD register transactions that are performed over the DisplayPort Auxiliary
// channel.
class DpcdChannel {
 public:
  virtual ~DpcdChannel() = default;

  virtual bool DpcdRead(uint32_t addr, uint8_t* buf, size_t size) = 0;
  virtual bool DpcdWrite(uint32_t addr, const uint8_t* buf, size_t size) = 0;
};

class DpAux : public DpcdChannel {
 public:
  // `mmio_buffer` must outlive this instance.
  DpAux(fdf::MmioBuffer* mmio_buffer, tgl_registers::Ddi ddi, uint16_t device_id);

  zx_status_t I2cTransact(const i2c_impl_op_t* ops, size_t count);

  // DpcdChannel overrides:
  bool DpcdRead(uint32_t addr, uint8_t* buf, size_t size) final;
  bool DpcdWrite(uint32_t addr, const uint8_t* buf, size_t size) final;

  // Exposed for configuration logging.
  DdiAuxChannel& aux_channel() { return aux_channel_; }

 private:
  DdiAuxChannel aux_channel_ __TA_GUARDED(lock_);
  mtx_t lock_;

  zx_status_t DpAuxRead(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, size_t size)
      __TA_REQUIRES(lock_);
  zx_status_t DpAuxReadChunk(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, uint32_t size_in,
                             size_t* size_out) __TA_REQUIRES(lock_);
  zx_status_t DpAuxWrite(uint32_t dp_cmd, uint32_t addr, const uint8_t* buf, size_t size)
      __TA_REQUIRES(lock_);

  zx::status<DdiAuxChannel::ReplyInfo> DoTransaction(const DdiAuxChannel::Request& request,
                                                     cpp20::span<uint8_t> reply_data_buffer)
      __TA_REQUIRES(lock_);
};

// DpCapabilities is a utility for reading and storing DisplayPort capabilities
// supported by the display based on a copy of read-only DPCD capability
// registers. Drivers can also use PublishToInspect() to publish the data to
// inspect.
class DpCapabilities final {
 public:
  // Initializes the DPCD capability array with all zeros and the EDP DPCD capabilities as
  // non-present.
  DpCapabilities();

  // Allow copy.
  DpCapabilities(const DpCapabilities&) = default;
  DpCapabilities& operator=(const DpCapabilities&) = default;

  // Allow move.
  DpCapabilities(DpCapabilities&&) = default;
  DpCapabilities& operator=(DpCapabilities&&) = default;

  // Read and parse DPCD capabilities. Clears any previously initialized content
  static fpromise::result<DpCapabilities> Read(DpcdChannel* dp_aux);

  // Publish the capabilities fields to inspect node `caps_node`.
  void PublishToInspect(inspect::Node* caps_node) const;

  // Get the cached value of a DPCD register using its DPCD address.
  uint8_t dpcd_at(dpcd::Register address) const {
    ZX_ASSERT(address < dpcd::DPCD_SUPPORTED_LINK_RATE_START);
    return dpcd_[address - dpcd::DPCD_CAP_START];
  }

  // Get the cached value of a EDP DPCD register using its address. Asserts if the eDP capabilities
  // are not available.
  uint8_t edp_dpcd_at(dpcd::EdpRegister address) const {
    ZX_ASSERT(edp_dpcd_.has_value());
    ZX_ASSERT(address < dpcd::DPCD_EDP_RESERVED);
    ZX_ASSERT(address >= dpcd::DPCD_EDP_CAP_START);
    return edp_dpcd_->bytes[address - dpcd::DPCD_EDP_CAP_START];
  }

  template <typename T, dpcd::Register A>
  T dpcd_reg() const {
    T reg;
    reg.set_reg_value(dpcd_at(A));
    return reg;
  }

  // Asserts if eDP capabilities are not available.
  template <typename T, dpcd::EdpRegister A>
  T edp_dpcd_reg() const {
    T reg;
    reg.set_reg_value(edp_dpcd_at(A));
    return reg;
  }

  dpcd::Revision dpcd_revision() const {
    return static_cast<dpcd::Revision>(dpcd_[dpcd::DPCD_REV]);
  }

  std::optional<dpcd::EdpRevision> edp_revision() const {
    if (edp_dpcd_) {
      return edp_dpcd_->revision;
    }
    return std::nullopt;
  }

  // Total number of stream sinks within this Sink device.
  size_t sink_count() const { return sink_count_.count(); }

  // Maximum number of DisplayPort lanes.
  uint8_t max_lane_count() const { return max_lane_count_.lane_count_set(); }

  // True for SST mode displays that support the Enhanced Framing symbol sequence (see DP v1.4a
  // Section 2.2.1.2).
  bool enhanced_frame_capability() const { return max_lane_count_.enhanced_frame_enabled(); }

  // True for eDP displays that support the `backlight_enable` bit in the
  // dpcd::DPCD_EDP_DISPLAY_CTRL register (see dpcd.h).
  bool backlight_aux_power() const { return edp_dpcd_ && edp_dpcd_->backlight_aux_power; }

  // True for eDP displays that support backlight adjustment through the
  // dpcd::DPCD_EDP_BACKLIGHT_BRIGHTNESS_[MSB|LSB] registers.
  bool backlight_aux_brightness() const { return edp_dpcd_ && edp_dpcd_->backlight_aux_brightness; }

  // The list of supported link rates in ascending order, measured in units of Mbps/lane.
  const std::vector<uint32_t>& supported_link_rates_mbps() const {
    return supported_link_rates_mbps_;
  }

  // True if the contents of vector returned by `supported_link_rates_mbps()` was populated using
  // the  "Link Rate Table" method. If true, the link rate must be selected by writing the vector
  // index to the DPCD LINK_RATE_SET register. Otherwise, the selected link rate must be programmed
  // using the DPCD LINK_BW_SET register.
  bool use_link_rate_table() const { return use_link_rate_table_; }

 private:
  // DpCapabilities that are only present in eDP displays.
  struct Edp {
    Edp();

    std::array<uint8_t, dpcd::DPCD_EDP_RESERVED - dpcd::DPCD_EDP_CAP_START> bytes;
    dpcd::EdpRevision revision;
    bool backlight_aux_power = false;
    bool backlight_aux_brightness = false;
  };

  bool ProcessEdp(DpcdChannel* dp_aux);
  bool ProcessSupportedLinkRates(DpcdChannel* dp_aux);

  std::array<uint8_t, dpcd::DPCD_SUPPORTED_LINK_RATE_START - dpcd::DPCD_CAP_START> dpcd_;
  dpcd::SinkCount sink_count_;
  dpcd::LaneCount max_lane_count_;
  std::vector<uint32_t> supported_link_rates_mbps_;
  bool use_link_rate_table_ = false;

  std::optional<Edp> edp_dpcd_;
};

class DpDisplay : public DisplayDevice {
 public:
  DpDisplay(Controller* controller, uint64_t id, tgl_registers::Ddi ddi, DpcdChannel* dp_aux,
            PchEngine* pch_engine, inspect::Node* parent_node);

  DpDisplay(const DpDisplay&) = delete;
  DpDisplay(DpDisplay&&) = delete;
  DpDisplay& operator=(const DpDisplay&) = delete;
  DpDisplay& operator=(DpDisplay&&) = delete;

  ~DpDisplay() override;

  // Gets the backlight brightness as a coefficient on the maximum brightness,
  // between the minimum brightness and 1.
  double GetBacklightBrightness();

  // DisplayDevice overrides:
  bool Query() final;
  bool InitWithDpllState(const DpllState* dpll_state) final;

  uint8_t lane_count() const { return dp_lane_count_; }
  uint32_t link_rate_mhz() const { return dp_link_rate_mhz_; }

 private:
  // DisplayDevice overrides:
  bool InitDdi() final;
  bool DdiModeset(const display_mode_t& mode) final;
  bool PipeConfigPreamble(const display_mode_t& mode, tgl_registers::Pipe pipe,
                          tgl_registers::Trans transcoder) final;
  bool PipeConfigEpilogue(const display_mode_t& mode, tgl_registers::Pipe pipe,
                          tgl_registers::Trans transcoder) final;
  bool ComputeDpllState(uint32_t pixel_clock_10khz, DpllState* config) final;
  uint32_t LoadClockRateForTranscoder(tgl_registers::Trans transcoder) final;

  bool CheckPixelRate(uint64_t pixel_rate) final;

  uint32_t i2c_bus_id() const final { return 2 * ddi(); }

  // Returns true if the eDP panel is powered on.
  //
  // This method performs any configuration and power sequencing needed to get
  // the eDP panel powered on, which may include waiting for a significant
  // amount of time.
  //
  // This method returns fairly quickly if the panel is already configured and
  // powered on. It is almost idempotent, modulo the panel changing power
  // states independently.
  bool EnsureEdpPanelIsPoweredOn();

  bool DpcdWrite(uint32_t addr, const uint8_t* buf, size_t size);
  bool DpcdRead(uint32_t addr, uint8_t* buf, size_t size);
  bool DpcdRequestLinkTraining(const dpcd::TrainingPatternSet& tp_set,
                               const dpcd::TrainingLaneSet lanes[]);
  bool DpcdUpdateLinkTraining(const dpcd::TrainingLaneSet lanes[]);
  template <uint32_t addr, typename T>
  bool DpcdReadPairedRegs(hwreg::RegisterBase<T, typename T::ValueType>* status);
  bool DpcdHandleAdjustRequest(dpcd::TrainingLaneSet* training, dpcd::AdjustRequestLane* adjust);

  bool TypeCConnectTigerLake();
  bool TypeCDisconnectTigerLake();
  bool ProgramDpModeTigerLake();

  bool DoLinkTraining();

  void ConfigureVoltageSwingKabyLake(size_t phy_config_index);
  void ConfigureVoltageSwingTigerLake(size_t phy_config_index);
  void ConfigureVoltageSwingTypeCTigerLake(size_t phy_config_index);

  bool LinkTrainingSetupKabyLake();
  bool LinkTrainingSetupTigerLake();

  // For locking Clock Recovery Circuit of the DisplayPort receiver
  bool LinkTrainingStage1(dpcd::TrainingPatternSet* tp_set, dpcd::TrainingLaneSet* lanes);
  // For optimizing equalization, determining symbol  boundary, and achieving inter-lane alignment
  bool LinkTrainingStage2(dpcd::TrainingPatternSet* tp_set, dpcd::TrainingLaneSet* lanes);

  bool SetBacklightOn(bool on);
  bool InitBacklightHw() override;

  bool IsBacklightOn();
  // Sets the backlight brightness with |val| as a coefficient on the maximum
  // brightness. |val| must be in [0, 1]. If the panel has a minimum fractional
  // brightness, then |val| will be clamped to [min, 1].
  bool SetBacklightBrightness(double val);

  bool HandleHotplug(bool long_pulse) override;
  bool HasBacklight() override;
  zx::status<> SetBacklightState(bool power, double brightness) override;
  zx::status<FidlBacklight::wire::State> GetBacklightState() override;

  void SetLinkRate(uint32_t value);

  // The object referenced by this pointer must outlive the DpDisplay.
  DpcdChannel* dp_aux_;  // weak

  // Used by eDP displays.
  PchEngine* pch_engine_;

  // Contains a value only if successfully initialized via Query().
  std::optional<DpCapabilities> capabilities_;

  // The current lane count and link rate. 0 if invalid/uninitialized.
  uint8_t dp_lane_count_ = 0;

  // The current per-lane link rate configuration. Use SetLinkRate to mutate the value which also
  // updates the related inspect properties.
  //
  // These values can be initialized by:
  //   1. InitWithDpllState based on an the current DPLL state
  //   2. Init, which selects the highest supported link rate
  //
  // The lane count is always initialized to the maximum value that the device can support in
  // Query().
  uint32_t dp_link_rate_mhz_ = 0;
  std::optional<uint8_t> dp_link_rate_table_idx_;

  // The backlight brightness coefficient, in the range [min brightness, 1].
  double backlight_brightness_ = 1.0f;

  enum class DisplayType {
    Unknown = 0,
    Legacy,
    DpAlternate,
    Thunderbolt,
  } display_type_ = DisplayType::Unknown;

  // Debug
  inspect::Node inspect_node_;
  inspect::Node dp_capabilities_node_;
  inspect::UintProperty dp_lane_count_inspect_;
  inspect::UintProperty dp_link_rate_mhz_inspect_;
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DP_DISPLAY_H_
