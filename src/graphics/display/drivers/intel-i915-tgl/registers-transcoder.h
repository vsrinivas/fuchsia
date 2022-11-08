// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_TRANSCODER_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_TRANSCODER_H_

#include <zircon/assert.h>

#include <cstdint>
#include <optional>

#include <hwreg/bitfields.h>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"

namespace tgl_registers {

// TRANS_HTOTAL, TRANS_HBLANK,
// TRANS_VTOTAL, TRANS_VBLANK
class TransHVTotal : public hwreg::RegisterBase<TransHVTotal, uint32_t> {
 public:
  DEF_FIELD(29, 16, count_total);  // same as blank_end
  DEF_FIELD(13, 0, count_active);  // same as blank_start
};

// TRANS_HSYNC, TRANS_VSYNC
class TransHVSync : public hwreg::RegisterBase<TransHVSync, uint32_t> {
 public:
  DEF_FIELD(29, 16, sync_end);
  DEF_FIELD(13, 0, sync_start);
};

// TRANS_VSYNCSHIFT
class TransVSyncShift : public hwreg::RegisterBase<TransVSyncShift, uint32_t> {
 public:
  DEF_FIELD(12, 0, second_field_vsync_shift);
};

// TRANS_DDI_FUNC_CTL (Transcoder DDI Function Control)
//
// This register has reserved bits that are not documented as MBZ (must be
// zero), so it should be accessed using read-modify-write.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1370-1375
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 952-956
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 pages 926-930
class TranscoderDdiControl : public hwreg::RegisterBase<TranscoderDdiControl, uint32_t> {
 public:
  // Enables the transcoder's DDI functionality.
  DEF_BIT(31, enabled);

  // Selects the DDI that the transcoder will connect to.
  //
  // This field has a non-trivial value encoding. The ddi_*() and set_ddi_*()
  // helpers should be preferred to accessing the field directly.
  //
  // This field is tagged `_subtle` because the definition matches the bits used
  // on Tiger Lake, but it's used on all supported models. Kaby Lake and Skylake
  // have a very similar field, which only takes up bits 30-28. Fortunately, bit
  // 27 is reserved MBZ (must be zero). So, there's still a 1:1 mapping between
  // DDI selection and the values of bits 30-27.
  //
  // We take advantage of this to avoid forking the entire (fairly large)
  // register definition by papering over this difference in the helpers
  // `ddi_kaby_lake()` and `set_ddi_kaby_lake()`.
  DEF_FIELD(30, 27, ddi_select_subtle);

  // The DDI that the transcoder will connect to.
  //
  // This helper works for Kaby Lake and Skylake.
  //
  // This field must not be changed while `enabled` is true. Directing multiple
  // transcoders to the same DDI is only valid for DisplayPort Multi-Streaming.
  //
  // The underlying field is ignored by the EDP transcoder, which is attached to
  // DDI A.
  std::optional<i915_tgl::DdiId> ddi_kaby_lake() const {
    // The cast is lossless because `ddi_select_subtle()` is a 4-bit field.
    const int8_t ddi_select_raw_value = static_cast<int8_t>(ddi_select_subtle());
    if (ddi_select_raw_value == 0) {
      return std::nullopt;
    }

    // Convert from the Tiger Lake field representation.
    const int ddi_index = (ddi_select_raw_value >> 1);
    return static_cast<i915_tgl::DdiId>(ddi_index);
  }

  // The DDI that the transcoder will connect to.
  //
  // This helper works for Tiger Lake.
  //
  // This field must not be changed while `enabled` is true. Directing multiple
  // transcoders to the same DDI is only valid for DisplayPort Multi-Streaming.
  //
  // The underlying field is ignored by the DSI transcoders. Each DSI transcoder
  // is attached to a DDI.
  std::optional<i915_tgl::DdiId> ddi_tiger_lake() const {
    // The cast is lossless because `ddi_select_subtle()` is a 4-bit field.
    const int8_t ddi_select_raw_value = static_cast<int8_t>(ddi_select_subtle());
    if (ddi_select_raw_value == 0) {
      return std::nullopt;
    }
    const int ddi_index = ddi_select_raw_value - 1;
    return static_cast<i915_tgl::DdiId>(ddi_index);
  }

  // See `ddi_kaby_lake()` for details.
  TranscoderDdiControl& set_ddi_kaby_lake(std::optional<i915_tgl::DdiId> ddi_id) {
    if (!ddi_id.has_value()) {
      return set_ddi_select_subtle(0);
    }

    ZX_DEBUG_ASSERT_MSG(*ddi_id != i915_tgl::DdiId::DDI_A,
                        "DDI A cannot be explicitly connected to a transcoder");
    const int ddi_index = *ddi_id - i915_tgl::DdiId::DDI_A;

    // Convert to the Tiger Lake field representation.
    return set_ddi_select_subtle(ddi_index << 1);
  }

  // See `ddi_tiger_lake()` for details.
  TranscoderDdiControl& set_ddi_tiger_lake(std::optional<i915_tgl::DdiId> ddi_id) {
    if (!ddi_id.has_value()) {
      return set_ddi_select_subtle(0);
    }
    const int ddi_index = *ddi_id - i915_tgl::DdiId::DDI_A;
    return set_ddi_select_subtle(ddi_index + 1);
  }

  // The transcoder's mode of operation.
  //
  // This field must not be changed while `enabled` is true.
  //
  // This field must be changed in the same MMIO write as the
  // `display_port_transport_tiger_lake` field.
  //
  // In HDMI mode, the transcoder sends a null packet (32 zero bytes) when
  // Vsync is asserted. The transcoder also sends a preamble and guardband
  // before each null packet. These behaviors match the HDMI specification.
  //
  // In DVI mode, enabling DIP (Data Island Packets) or audio causes the
  // transcoder to adopt the HDMI behavior described above.
  //
  // DisplayPort modes SST (Single Stream) or MST (Multi-Stream) must match the
  // mode selected in the `DpTransportControl` register.
  //
  // On Tiger Lake, the DSI transcoders ignore this field.
  //
  // On Kaby Lake, transcoder EDP (and therefore DDI A) must be in the
  // DisplayPort SST (Single Stream) mode.
  DEF_FIELD(26, 24, ddi_mode);

  // TODO(fxbug.dev/110690): Move the constants below into an enum class once we
  // figure out how to handle invalid field values.
  static constexpr uint32_t kModeHdmi = 0;
  static constexpr uint32_t kModeDvi = 1;
  static constexpr uint32_t kModeDisplayPortSingleStream = 2;
  static constexpr uint32_t kModeDisplayPortMultiStream = 3;

  // Selects the bpc (number of bits per color) output on the connected DDI.
  //
  // This field must not be changed while `enabled` is true.
  //
  // HDMI and DSC (Display Stream Compression) don't support 6bpc.
  //
  // On Tiger Lake, the DSI transcoder ignores this field, and uses the pixel
  // format in the TRANS_DSI_FUNC_CONF register.
  DEF_FIELD(22, 20, bits_per_color);

  // TODO(fxbug.dev/110690): Move the constants below into an enum class once we
  // figure out how to handle invalid field values.
  static constexpr uint32_t k8bpc = 0;
  static constexpr uint32_t k10bpc = 1;
  static constexpr uint32_t k6bpc = 2;
  static constexpr uint32_t k12bpc = 3;

  // When operating as a port sync secondary, selects the primary transcoder.
  //
  // This field has a non-trivial value encoding. The
  // `port_sync_primary_transcoder_kaby_lake()` and
  // `set_port_sync_primary_transcoder_kaby_lake()` helpers should be preferred
  // to accessing the field directly.
  DEF_FIELD(19, 18, port_sync_primary_transcoder_select_kaby_lake);

  // When operating as a port sync secondary, selects the primary transcoder.
  //
  // This field is ignored by the EDP transcoder, because it cannot function as
  // a port sync secondary.
  //
  // This field's bits are reserved MBZ (must be zero) on Tiger Lake. The field
  // was moved to the TRANS_DDI_FUNC_CTL2 register and widened.
  i915_tgl::TranscoderId port_sync_primary_transcoder_kaby_lake() const {
    // The cast is lossless because `port_sync_primary_select_kaby_lake()` is a
    // 2-bit field.
    const int8_t raw_port_sync_primary_select =
        static_cast<int8_t>(port_sync_primary_transcoder_select_kaby_lake());
    if (raw_port_sync_primary_select == 0) {
      return i915_tgl::TranscoderId::TRANSCODER_EDP;
    }

    // The subtraction result is non-negative, because we checked for zero
    // above. The addition will not overflow because
    // `port_sync_primary_select_kaby_lake()` is a 2-bit field.
    return static_cast<i915_tgl::TranscoderId>(i915_tgl::TranscoderId::TRANSCODER_A +
                                               (raw_port_sync_primary_select - 1));
  }

  // See `port_sync_primary_kaby_lake()`.
  TranscoderDdiControl& set_port_sync_primary_kaby_lake(i915_tgl::TranscoderId transcoder_id) {
    if (transcoder_id == i915_tgl::TranscoderId::TRANSCODER_EDP) {
      return set_port_sync_primary_transcoder_select_kaby_lake(0);
    }

    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);
    return set_port_sync_primary_transcoder_select_kaby_lake(
        (transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A) + 1);
  }

  // If true, VSync is active high. If false, VSync is active low.
  //
  // On Tiger Lake, the DSI transcoders ignore this field.
  //
  // Active high is the default, and considered the standard polarity. Active
  // low is considered an inverted polarity.
  DEF_BIT(17, vsync_polarity_not_inverted);

  // If true, HSync is active high. If false, HSync is active low.
  //
  // On Tiger Lake, the DSI transcoders ignore this field.
  //
  // Active high is the default, and considered the standard polarity. Active
  // low is considered an inverted polarity.
  DEF_BIT(16, hsync_polarity_not_inverted);

  // If true, this transcoder operates as a port sync secondary transcoder.
  //
  // Only the secondary transcoders must be explicitly configured for port sync.
  // This is set to false for the port sync primary transcoder.
  //
  // This field is ignored by the EDP transcoder, because it cannot function as
  // a port sync secondary.
  //
  // This field's bits are reserved MBZ (must be zero) on Tiger Lake. The field
  // was moved to the TRANS_DDI_FUNC_CTL2 register.
  DEF_BIT(15, is_port_sync_secondary_kaby_lake);

  // Selects the input pipe, for transcoders that are not attached to pipes.
  //
  // This field has a non-trivial value encoding. The input_pipe_*() and
  // set_input_pipe_*() helpers should be preferred to accessing the field
  // directly.
  DEF_FIELD(14, 12, input_pipe_select);

  // Selects the input pipe, for transcoders that are not attached to pipes.
  //
  // On Tiger Lake, this field is only used by the DSI transcoders. On Kaby
  // Lake, the field is only used by the EDP transcoder. These are the
  // transcoders that are not attached to pipes.
  //
  // This field is not documented on Skylake, and its bits are documented as
  // reserved. However, several PRM locations (IHD-OS-SKL-Vol 12-05.16 section
  // "Display Connections" pages 103, section "Pipe to Transcoder to DDI
  // Mappings" page 107) mention that the EDP transcoder can connect to pipes
  // A-C. So, the field likely works the same way as on Kaby Lake.
  Pipe input_pipe() const {
    switch (input_pipe_select()) {
      case kInputSelectPipeA:
        return Pipe::PIPE_A;
      case kInputSelectPipeB:
        return Pipe::PIPE_B;
      case kInputSelectPipeC:
        return Pipe::PIPE_C;

        // TODO(fxbug.dev/109278): Add pipe D, once we support it.
    };

    return Pipe::PIPE_INVALID;
  }

  // See `input_pipe()` for details.
  TranscoderDdiControl& set_input_pipe(Pipe input_pipe) {
    switch (input_pipe) {
      case Pipe::PIPE_A:
        return set_input_pipe_select(kInputSelectPipeA);
      case Pipe::PIPE_B:
        return set_input_pipe_select(kInputSelectPipeB);
      case Pipe::PIPE_C:
        return set_input_pipe_select(kInputSelectPipeC);

        // TODO(fxbug.dev/109278): Add pipe D, once we support it.

      case Pipe::PIPE_INVALID:
          // The code handling the explicit invalid pipe value is outside the
          // switch() so it also applies to values that aren't Pipe enum members,
          // which are also invalid.
          ;
    };

    ZX_DEBUG_ASSERT_MSG(false, "Invalid pipe: %d", input_pipe);
    return *this;
  }

  // Values for `display_port_transport_tiger_lake`.
  enum class DisplayPortTransportTigerLake {
    kA = 0,
    kB = 1,
    kC = 2,
    kD = 3,
  };

  // Selects the DisplayPort transport that receives this transcoder's data.
  //
  // This field is only used when DisplayPort MST (multi-streaming) is enabled.
  //
  // This must be changed in the same MMIO operation as `ddi_mode`.
  DEF_ENUM_FIELD(DisplayPortTransportTigerLake, 11, 10, display_port_transport_tiger_lake);

  // If true, VC (Virtual Channel) payload allocation is enabled.
  //
  // This field is ignored by the transcoders attached to DDIs that don't
  // support multi-streaming. These are the DSI transcoders On Tiger Lake, and
  // the EDP transcoder on Kaby Lake and Skylake.
  DEF_BIT(8, allocate_display_port_virtual_circuit_payload);

  // If true, the HDMI scrambler is in CTS (Compliance Test Specification) mode.
  //
  // This field must not be changed while `hdmi_scrambler_enabled` is true.
  //
  // This field is not documented on Kaby Lake and Skylake. The bit is reserved
  // MBZ (must be zero). This extends the good read semantics of `hdmi_enabled_`
  // -- reading zero means that the CTS mode is disabled, which makes perfect
  // sense while the HDMI scrambler is disabled.
  DEF_BIT(7, hdmi_scrambler_cts_mode);

  // If false, the HDMI scrambler is reset on every line.
  //
  // This field is only used when the HDMI scrambler is in CTS mode. In that
  // case, it determines whether the transcoder sends a SSCP (Scrambler
  // Synchronization Control Period) during HSync for every line, or for every
  // other line.
  //
  // This field must be not be set while `hdmi_scrambler_cts_mode` is true.
  //
  // This field is not documented on Kaby Lake and Skylake. The bit is reserved
  // MBZ (must be zero). This extends the good read semantics of
  // `hdmi_scrambler_cts_mode` -- the CTS mode is never enabled, and this field
  // can always be ignored.
  DEF_BIT(6, hdmi_scrambler_resets_every_other_line);

  // If true, the high TMDS character rate is enabled over the HDMI link.
  //
  // This field must be set to true if and only if the HDMI link symbol rate is
  // greater than 340 MHz.
  //
  // This field is not documented on Kaby Lake and Skylake. The bits are
  // reserved MBZ (must be zero), which makes for good read semantics -- reading
  // zero means that the high TMDS character rate is not enabled.
  DEF_BIT(4, high_tmds_character_rate_tiger_lake);

  // Selects the number of DisplayPort or DSI lanes enabled.
  //
  // This field has a non-trivial value encoding. The
  // `display_port_lane_count()` and `set_display_port_lane_count()` helpers
  // should be preferred to accessing the field directly.
  DEF_FIELD(3, 1, display_port_lane_count_selection);

  // The number of DisplayPort lanes enabled.
  //
  // This field is ignored for HDMI or DVI, as these modes always use 4 lanes.
  // Only the DSI transcoders support using 3 lanes.
  //
  // When the transcoder mode is a DisplayPort mode, the field must match the
  // `display_port_lane_count` in the attached DDI's DdiBufferControl register.
  uint8_t display_port_lane_count() const {
    // The addition will not overflow and the cast is lossless because
    // display_port_lane_count_selection() is a 3-bit field.
    return static_cast<int8_t>(display_port_lane_count_selection() + 1);
  }

  // See `display_port_lane_count()` for details.
  TranscoderDdiControl set_display_port_lane_count(uint8_t lane_count) {
    ZX_DEBUG_ASSERT(lane_count >= 1);
    ZX_DEBUG_ASSERT(lane_count <= 4);
    return set_display_port_lane_count_selection(lane_count - 1);
  }

  // If true, scrambling is enabled over the HDMI link.
  //
  // Scrambling must be enabled for HDMI link symbol rates above 340 MHz.
  // Scrambling should also be enabled at lower speeds, when the receiver
  // supports scrambling at those speeds.
  //
  // This field is not documented on Kaby Lake and Skylake. The bits are
  // reserved MBZ (must be zero), which makes for good read semantics -- reading
  // zero means that no HDMI scrambler is enabled.
  DEF_BIT(0, hdmi_scrambler_enabled_tiger_lake);

  static auto GetForKabyLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    if (transcoder_id == i915_tgl::TranscoderId::TRANSCODER_EDP) {
      return hwreg::RegisterAddr<TranscoderDdiControl>(0x6f400);
    }

    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);
    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderDdiControl>(0x60400 + 0x1000 * transcoder_index);
  }

  static auto GetForTigerLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);

    // TODO(fxbug.dev/109278): Allow transcoder D, once we support it.
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);

    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderDdiControl>(0x60400 + 0x1000 * transcoder_index);
  }

 private:
  static constexpr uint32_t kInputSelectPipeA = 0;
  static constexpr uint32_t kInputSelectPipeB = 5;
  static constexpr uint32_t kInputSelectPipeC = 6;
  // TODO(fxbug.dev/109278): Add pipe D, once we support it. The value is 7.
};

// TRANS_CONF (Transcoder Configuration)
//
// This register has reserved bits that are not documented as MBZ (must be
// zero), so it should be accessed using read-modify-write.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1365-1366
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 949-951
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 pages 924-925
class TranscoderConfig : public hwreg::RegisterBase<TranscoderConfig, uint32_t> {
 public:
  // Set to true/false to eventually enable/disable the transcoder.
  //
  // Turning off the transcoder disables the timing generator and the
  // synchronization pulses to the display.
  //
  // Timing registers must be set to valid values before this field is enabled.
  DEF_BIT(31, enabled_target);

  // Read-only, reflects the current state.
  DEF_BIT(30, enabled);

  // If false, the transcoder operates in Progressive Fetch mode.
  //
  // The following features are not supported with Interlaced Fetch mode:
  // * Y tiling
  // * 90 or 270 rotation
  // * scaling
  // * YUV 4:2:0 hybrid planar source pixel formats
  DEF_BIT(22, interlaced_fetch);

  // If false, the transcoder operates in Progressive Display mode.
  //
  // Must be true if `interlaced_fetch` is true.
  //
  // When `interlaced_fetch` is false and `interlaced_display` is true:
  // * Pipe scaling is required
  // * The vertical resolution doubles
  // * The maximum supported pixel rate is cut down in half
  DEF_BIT(21, interlaced_display);

  // The number of symbols that must be in the DisplayPort audio symbol RAM
  // before it starts to drain during horizontal blank.
  //
  // The value must be between 2 and 64.
  //
  // This field does not exist (must be zero) on Kaby Lake or Skylake.
  DEF_FIELD(6, 0, display_port_audio_symbol_watermark_tiger_lake);

  static auto GetForKabyLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    if (transcoder_id == i915_tgl::TranscoderId::TRANSCODER_EDP) {
      return hwreg::RegisterAddr<TranscoderConfig>(0x7f008);
    }

    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);
    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderConfig>(0x70008 + 0x1000 * transcoder_index);
  }

  static auto GetForTigerLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);

    // TODO(fxbug.dev/109278): Allow transcoder D, once we support it.
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);

    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderConfig>(0x70008 + 0x1000 * transcoder_index);
  }
};

// TRANS_CLK_SEL (Transcoder Clock Select).
//
// On Kaby Lake and Skylake, the EDP transcoder always uses the DDI A clock, so
// it doesn't have a Clock Select register.
//
// On Tiger Lake, all reserved bits are MBZ (must be zero), so this register can
// be safely written without reading it first. On Kaby Lake and Skylake, the
// reserved bits are not documented as MBZ, so this register should be accessed
// using read-modify-write.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1365-1366
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 947-948
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 pages 922-923
class TranscoderClockSelect : public hwreg::RegisterBase<TranscoderClockSelect, uint32_t> {
 public:
  // Selects the DDI whose port clock is used by this transcoder.
  //
  // This field has a non-trivial value encoding. The ddi_*() and set_ddi_*()
  // helpers should be preferred to accessing the field directly.
  //
  // This field is tagged `_subtle` because the definition matches the bits used
  // on Tiger Lake, but it's used on all supported models. Kaby Lake and Skylake
  // have a very similar field, which only takes up bits 30-28. Fortunately,
  // bit 27 is reserved, and we can still paper over the field width difference
  // in the helpers `ddi_clock_kaby_lake()` and `set_ddi_clock_kaby_lake()`.
  DEF_FIELD(31, 28, ddi_clock_select_subtle);

  // The DDI whose port clock is used by the transcoder.
  //
  // This helper works for Kaby Lake and Skylake.
  //
  // This field must not be changed while the transcoder is enabled.
  std::optional<i915_tgl::DdiId> ddi_clock_kaby_lake() const {
    // Shifting converts from the Tiger Lake field width. The cast is lossless
    // because `ddi_clock_select_subtle()` is a 4-bit field.
    const int8_t ddi_clock_select_raw_value = static_cast<int8_t>(ddi_clock_select_subtle() >> 1);
    if (ddi_clock_select_raw_value == 0) {
      return std::nullopt;
    }
    const int ddi_index = ddi_clock_select_raw_value - 1;
    return static_cast<i915_tgl::DdiId>(ddi_index);
  }

  // The DDI whose port clock is used by the transcoder.
  //
  // This helper works for Tiger Lake.
  //
  // This field must not be changed while the transcoder is enabled.
  std::optional<i915_tgl::DdiId> ddi_clock_tiger_lake() const {
    // The cast is lossless because `ddi_clock_select_subtle()` is a 4-bit field.
    const int8_t ddi_select_raw_value = static_cast<int8_t>(ddi_clock_select_subtle());
    if (ddi_select_raw_value == 0) {
      return std::nullopt;
    }
    const int ddi_index = ddi_select_raw_value - 1;
    return static_cast<i915_tgl::DdiId>(ddi_index);
  }

  // See `ddi_clock_kaby_lake()` for details.
  TranscoderClockSelect& set_ddi_clock_kaby_lake(std::optional<i915_tgl::DdiId> ddi_id) {
    ZX_DEBUG_ASSERT_MSG(!ddi_id.has_value() || ddi_id != i915_tgl::DdiId::DDI_A,
                        "DDI A cannot be explicitly connected to a transcoder");

    const int8_t ddi_select_raw = RawDdiClockSelect(ddi_id);

    // Convert to the Tiger Lake field representation.
    const uint32_t reserved_bit = (ddi_clock_select_subtle() & 1);
    return set_ddi_clock_select_subtle((ddi_select_raw << 1) | reserved_bit);
  }

  // See `ddi_clock_tiger_lake()` for details.
  TranscoderClockSelect& set_ddi_clock_tiger_lake(std::optional<i915_tgl::DdiId> ddi_id) {
    return set_ddi_clock_select_subtle(RawDdiClockSelect(ddi_id));
  }

  static auto GetForTranscoder(i915_tgl::TranscoderId transcoder_id) {
    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);

    // TODO(fxbug.dev/109278): Allow transcoder D, once we support it.
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);

    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderClockSelect>(0x46140 + 4 * transcoder_index);
  }

 private:
  static int8_t RawDdiClockSelect(std::optional<i915_tgl::DdiId> ddi) {
    if (!ddi.has_value()) {
      return 0;
    }
    // The cast is lossless because DDI indices fit in 4 bits.
    const int8_t ddi_index = static_cast<int8_t>(*ddi - i915_tgl::DdiId::DDI_A);
    // The addition doesn't overflow and the cast is lossless because DDI
    // indices fit in 4 bits.
    return static_cast<int8_t>(ddi_index + 1);
  }
};

// DATAM / TRANS_DATAM1 (Transcoder Data M Value 1)
//
// This register is double-buffered and the update triggers when the first
// MSA (Main Stream Attributes packet) that is sent after LINKN is modified.
//
// All unassigned bits in this register are MBZ (must be zero), so it's safe to
// assign this register without reading its old value.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 pages 328-329
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 427-428
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 422-423
class TranscoderDataM : public hwreg::RegisterBase<TranscoderDataM, uint32_t> {
 public:
  DEF_RSVDZ_BIT(31);

  // Selects the TU (transfer unit) or VC (Virtual Channel) payload size.
  //
  // This field has a non-trivial value encoding. The `payload_size()` and
  // `set_payload_size()` helpers should be preferred to accessing the field
  // directly.
  DEF_FIELD(30, 25, payload_size_select);

  // Selects the TU (transfer unit) or VC (Virtual Channel) payload size.
  //
  // In DisplayPort SST (Single Stream) mode, this field represents the TU
  // (transfer unit size), which is typically set to 64.
  //
  // In DisplayPort MST (Multi-Stream) mode, this field represents the Virtual
  // Channel payload size, which must be at most 63. This field must not be
  // changed while the transcoder is in MST mode, even if the transcoder is
  // disabled.
  int32_t payload_size() const {
    // The cast is lossless and the addition does not overflow (which would be
    // UB) because `payload_size_select()` is a 24-bit field.
    return static_cast<int32_t>(static_cast<int32_t>(payload_size_select()) + 1);
  }

  // See `payload_size()`.
  TranscoderDataM& set_payload_size(int payload_size) {
    ZX_DEBUG_ASSERT(payload_size > 0);
    return set_payload_size_select(payload_size - 1);
  }

  DEF_RSVDZ_BIT(24);

  // The M value in the data M/N ratio, which is used by the transcoder.
  DEF_FIELD(23, 0, m);

  static auto GetForKabyLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    if (transcoder_id == i915_tgl::TranscoderId::TRANSCODER_EDP) {
      return hwreg::RegisterAddr<TranscoderDataM>(0x6f030);
    }

    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);
    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderDataM>(0x60030 + 0x1000 * transcoder_index);
  }

  static auto GetForTigerLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);

    // TODO(fxbug.dev/109278): Allow transcoder D, once we support it.
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);

    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderDataM>(0x60030 + 0x1000 * transcoder_index);
  }
};

// DATAN / TRANS_DATAN1 (Transcoder Data N Value 1)
//
// This register is double-buffered and the update triggers when the first
// MSA (Main Stream Attributes packet) that is sent after LINKN is modified.
//
// All unassigned bits in this register are MBZ (must be zero), so it's safe to
// assign this register without reading its old value.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 330
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 429
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 424-425
class TranscoderDataN : public hwreg::RegisterBase<TranscoderDataN, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 24);

  // The N value in the data M/N ratio, which is used by the transcoder.
  DEF_FIELD(23, 0, n);

  static auto GetForKabyLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    if (transcoder_id == i915_tgl::TranscoderId::TRANSCODER_EDP) {
      return hwreg::RegisterAddr<TranscoderDataN>(0x6f034);
    }

    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);
    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderDataN>(0x60034 + 0x1000 * transcoder_index);
  }

  static auto GetForTigerLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);

    // TODO(fxbug.dev/109278): Allow transcoder D, once we support it.
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);

    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderDataN>(0x60034 + 0x1000 * transcoder_index);
  }
};

// LINKM / TRANS_LINKM1 (Transcoder Link M Value 1)
//
// This register is double-buffered and the update triggers when the first
// MSA (Main Stream Attributes packet) that is sent after LINKN is modified.
//
// All unassigned bits in this register are MBZ (must be zero), so it's safe to
// assign this register without reading its old value.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 1300
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 1123
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 1112-1113
class TranscoderLinkM : public hwreg::RegisterBase<TranscoderLinkM, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 24);

  // The M value in the link M/N ratio transmitted in the MSA packet.
  DEF_FIELD(23, 0, m);

  static auto GetForKabyLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    if (transcoder_id == i915_tgl::TranscoderId::TRANSCODER_EDP) {
      return hwreg::RegisterAddr<TranscoderLinkM>(0x6f040);
    }

    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);
    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderLinkM>(0x60040 + 0x1000 * transcoder_index);
  }

  static auto GetForTigerLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);

    // TODO(fxbug.dev/109278): Allow transcoder D, once we support it.
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);

    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderLinkM>(0x60040 + 0x1000 * transcoder_index);
  }
};

// LINKN / TRANS_LINKN1 (Transcoder Link N Value 1)
//
// Updating this register triggers an update of all double-buffered M/N
// registers (DATAM, DATAN, LINKM, LINKN) for the transcoder.
//
// All unassigned bits in this register are MBZ (must be zero), so it's safe to
// assign this register without reading its old value.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 1301
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 1124
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 1114-1115
class TranscoderLinkN : public hwreg::RegisterBase<TranscoderLinkN, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 24);

  // The N value in the link M/N ratio transmitted in the MSA packet. This is
  // also transmitted in the VB-ID (Vertical Blanking ID).
  DEF_FIELD(23, 0, n);

  static auto GetForKabyLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    if (transcoder_id == i915_tgl::TranscoderId::TRANSCODER_EDP) {
      return hwreg::RegisterAddr<TranscoderLinkN>(0x6f044);
    }

    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);
    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderLinkN>(0x60044 + 0x1000 * transcoder_index);
  }

  static auto GetForTigerLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);

    // TODO(fxbug.dev/109278): Allow transcoder D, once we support it.
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);

    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderLinkN>(0x60044 + 0x1000 * transcoder_index);
  }
};

// Documented values for the DisplayPort MSA MISC0 field's bits 7:5.
//
// The values come from the VESA DisplayPort Standard Version 2.0, Table 2-96
// "MSA MISC1 and MISC0 Fields for Pixel Encoding/Colorimetry Format Indication"
// at page 158. The table belongs to Section 2.2.4 "MSA Data Transport".
//
// The encoding here is correct for all modes except for RAW, which uses a
// different encoding.
//
// TODO(fxbug.dev/105221): This covers a general DisplayPort concept, so it
// belongs in a general-purpose DisplayPort support library.
enum class DisplayPortMsaBitsPerComponent {
  k6Bpc = 0,
  k8Bpc = 1,
  k10Bpc = 2,
  k12Bpc = 3,
  k16Bpc = 4,
};

// Documented values for the DisplayPort MSA MISC0 field's bits 4:1.
//
// The values come from the VESA DisplayPort Standard Version 2.0, Table 2-96
// "MSA MISC1 and MISC0 Fields for Pixel Encoding/Colorimetry Format Indication"
// at page 158. The table belongs to Section 2.2.4 "MSA Data Transport".
//
// TODO(fxbug.dev/105221): This covers a general DisplayPort concept, so it
// belongs in a general-purpose DisplayPort support library.
enum class DisplayPortMsaColorimetry {
  kRgbUnspecifiedLegacy = 0b0'0'00,
  kCtaSrgb = 0b0'1'00,
  kRgbWideGamutFixed = 0b0'0'11,
  kRgbWideGamutFloating = 0b1'0'00,
  kYCbCr422Bt601 = 0b0'1'01,
  kYCbCr422Bt709 = 0b1'1'01,
  kYCbCr444Bt601 = 0b0'1'10,
  kYCbCr444Bt709 = 0b1'1'10,
  kAdobeRgb = 0b1'1'00,
  kDciP3 = 0b0'1'11,

  // The color profile will be sent as a MCCS (VESA Monitor Control Command)
  // VCP (Virtual Control Panel).
  kVcpColorProfile = 0b0'1'11,
};

// TRANS_MSA_MISC (Transcoder Main Stream Attribute Miscellaneous)
//
// All reserved fields in this register are MBZ (must be zero), so it can be
// safely written without a prior read.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1394-1395
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 947-948
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 pages 922-923
//
// MISC fields: VESA DisplayPort Standard Version 2.0, Section 2.2.4
// "MSA Data Transport", pages 149-151 and 157-158.
class TranscoderMainStreamAttributeMisc
    : public hwreg::RegisterBase<TranscoderMainStreamAttributeMisc, uint32_t> {
 public:
  // TODO(fxbug.dev/105221): The MSA field definitions are a general DisplayPort
  // concept, and belong in a general-purpose DisplayPort support library. Once
  // we have that, this register's definition should only map MSA fields to
  // register bytes, matching the PRM.

  // Bits 31:16 are document as the value transmitted in the MSA unused fields.
  //
  // The VESA DisplayPort Standard Version 2.0, Figure 2-18 "DP MSA Packet
  // Transport Mapping over Main-Link", page 152 states this field must be zero.
  DEF_RSVDZ_FIELD(31, 16);

  // Bits 15:8 are the MISC1 MSA field from the DisplayPort standard.

  // True for Y (luminance)-only and RAW formats.
  //
  // We don't currently support these color formats.
  DEF_BIT(15, colorimetry_top_bit);

  // If true, the colorimetry information is sent separately, in a VSC SDP.
  //
  // This must only be used if the sink's DPRX_FEATURE_ENUMERATION_LIST register
  // has VSC_SDP_EXTENSION_FOR_COLORIMETRY_SUPPORTED set.
  //
  // Including colorimetry information in the VSC (Video Stream Configuration)
  // SDP (Secondary Data Packet) is described in the VESA DisplayPort Standard
  // Version 2.0, Section 2.2.5.6.5 "VSC SDP Payload for Pixel
  // Encoding/Colorimetry Format", pages 203-205
  //
  // This field was introduced in DisplayPort 1.3. Prior to that, the underlying
  // bit was MBZ (must be zero).
  //
  // We don't currently support this feature.
  DEF_BIT(14, colorimetry_in_vsc_sdp);

  // Reserved in the DisplayPort 2.0 standard, must be zero.
  DEF_RSVDZ_FIELD(13, 11);

  // If the "FS MSA MISC1 Drive Enable" field in the TRANS_STEREO3D_CTL register
  // is true, this field is ignored, and the display hardware drives the
  // corresponding MSA bits.
  DEF_FIELD(10, 9, stereo_video);

  // True iff the number of lines per interlaced frame (two fields) is even.
  DEF_BIT(8, interlaced_vertical_total_even);

  // Bits 7:0 are the MSA MISC0 field from the DisplayPort standard.

  // The bpc (number of bits per color component) for the selected format.
  //
  // Some bpc values are not supported by some colorimetry modes. For example,
  // the RGB wide gamut fixed point only supports 8, 10, and 12bpc.
  DEF_ENUM_FIELD(DisplayPortMsaBitsPerComponent, 7, 5, bits_per_component_select);

  // Selects the pixel encoding and colorimetry format.
  //
  // See the `DisplayPortMainStreamAttributeColorimetry` comments for details.
  DEF_ENUM_FIELD(DisplayPortMsaColorimetry, 4, 1, colorimetry_select);

  // If true, the main link clock and video stream clock are synchronous.
  //
  // Before DisplayPort is enabled, this field must be set to true.
  DEF_BIT(0, video_stream_clock_sync_with_link_clock);

  static auto GetForKabyLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    if (transcoder_id == i915_tgl::TranscoderId::TRANSCODER_EDP) {
      return hwreg::RegisterAddr<TranscoderMainStreamAttributeMisc>(0x6f410);
    }

    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);
    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderMainStreamAttributeMisc>(0x60410 +
                                                                  0x1000 * transcoder_index);
  }

  static auto GetForTigerLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);

    // TODO(fxbug.dev/109278): Allow transcoder D, once we support it.
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);

    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderClockSelect>(0x60410 + 0x1000 * transcoder_index);
  }
};

// TRANS_VRR_CTL (VRR Control Register Transcoder)
//
// This register is not documented for Kaby Lake or Skylake. These display
// engines do not support the VRR (Variable Refresh Rate) feature.
//
// Tiger Lake:  IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1406-1407
class TranscoderVariableRateRefreshControl
    : public hwreg::RegisterBase<TranscoderVariableRateRefreshControl, uint32_t> {
 public:
  // If true, VRR (Variable Rate Refresh) is enabled.
  DEF_BIT(31, enabled);

  // If true, hardware varies Vblank.
  //
  // If this field is true, Vblank (the Vertical Blank period) varies between
  // the minimum set in the TRANS_VRR_VMIN register and the maximum set in the
  // TRANS_VRR_VMAX register.
  //
  // If this field is false, the Vblank (Vertical Blank period) in the
  // TRANS_VBLANK register is used.
  DEF_BIT(30, vblank_max_shift_ignored);

  // If true, the Flip Line feature is enabled.
  //
  // Changes to this field take effect at the next vertical blank.
  //
  // This field must be set to true before `enabled` is true. If this field is
  // true, `vblank_max_shift_ignored` and `use_pipeline_full_line_count_delay`
  // must also be true.
  DEF_BIT(29, flip_line_enabled);

  DEF_RSVDZ_FIELD(28, 11);

  // Delay from frame start to Pipeline Full Line Count signal generation.
  //
  // When `use_pipeline_full_line_count_delay` is true, this field indicates the
  // delay (in number of scanlines) from the start of Vblank (Vertical Blank)
  // start until the Pipeline Full Line Count signal is triggered. This signal
  // causes the start of Vactive (Vertical Active).
  //
  // This field must be set to VRR Vmin - Vblank start - 4.
  DEF_FIELD(10, 3, pipeline_full_line_count_delay_from_frame_start);

  DEF_RSVDZ_FIELD(2, 1);

  // If true, Vertical Active starts at a programmed delay from frame start.
  //
  // If this field is false, Vactive (Vertical Active) starts when a
  // hardware-generated Pipeline Full Line Count signal is triggered.
  //
  // If this field is true, `use_pipeline_full_line_count_delay` must be
  // programmed correctly.
  DEF_BIT(0, use_pipeline_full_line_count_delay);

  static auto GetForTigerLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);

    // TODO(fxbug.dev/109278): Allow transcoder D, once we support it.
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);

    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderVariableRateRefreshControl>(0x60420 +
                                                                     0x1000 * transcoder_index);
  }
};

// Per-transcoder chicken register.
//
// This register is not officially documented in any register listing. It is
// implicitly documented in display engine PRMs and workaround PRMs, via
// instructions to flip specific bits at transcoder-dependent MMIO addresses.
//
// On Kaby Lake, the transcoder chicken registers also store some DDI-specific
// chicken bits. The GetForKabyLakeDdi() helper will retrieve the register that
// contains the chicken bits for a specific DDI.
class TranscoderChicken : public hwreg::RegisterBase<TranscoderChicken, uint32_t> {
 public:
  // FEC (Forward Error Correction) workaround.
  //
  // This field must be set for correct functioning in DisplayPort 1.4 MST
  // (Multi-Stream) mode with FEC (Forward Error Correction), before the
  // `enabled_target` field in the TranscoderConfig register is set to true.
  //
  // This field must be set to false before  the `enabled_target` field in the
  // TranscoderConfig register is set back to false.
  //
  // This bit is only indirectly docuumented in IHD-OS-TGL-Vol 12-1.22-Rev2.0
  // sections "Sequences for DisplayPort" > "Enable Sequence" (page 144) and
  // "Disable Sequence" (page 147). The register is mentioned by its MMIO
  // address.
  DEF_BIT(23, override_forward_error_correction_tiger_lake);

  // HDMI port voltage swing programming workaround.
  //
  // By default, each HDMI port in Kaby Lake display engines uses the voltage
  // swing setting specified when the port is first enabled. A new voltage swing
  // setting can be programmed by setting this field to 3 (0b11) right before
  // setting the `enabled` field in the DdiBufferControl register, waiting for
  // 1us, and then setting this field to zero (0b00).
  //
  // This field is scoped to DDIs, not to transcoders. GetForKabyLakeDdi() will
  // return the correct register for applying this workaround to a DDI.
  //
  // IHD-OS-KBL-Vol 16-1.17 workaround BSpec ID 1143, pages 29-30
  DEF_FIELD(19, 18, override_ddi_hdmi_voltage_swing_kaby_lake);

  // DisplayPort audio corruption or video underflow workaround.
  //
  // This field must be set to true for DisplayPort x4 (4 main link lanes) ports
  // that use the HBR2 rate, when the CDCLK (core display clock) frequency is
  // below 432 MHz. This workaround is only valid for audio clock frequencies <=
  // 96 KHz, and fewer than 8 audio channels -- audio must not be used over
  // DisplayPort otherwise.
  //
  // IHD-OS-KBL-Vol 16-1.17 workaround BSpec ID 1144, pages 30-31
  DEF_BIT(13, override_display_port_audio_island_kaby_lake);

  // Returns the register that holds DDI-scoped chicken bits for `ddi`.
  //
  // On Kaby Lake, the transcoder chicken registers are also used for DDI-scoped
  // chicken bits. The mapping of DDIs to registers is not straightforward. This
  // method returns the correct register for accessing a DDI's chicken bits.
  static auto GetForKabyLakeDdi(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_D);

    // The DDI-to-MMIO address mapping is specified implicitly in
    // IHD-OS-KBL-Vol 16-1.17 BSpec (workaround) ID 1143, page 30.
    static constexpr uint32_t kMmioAddress[] = {0x420cc, 0x420c0, 0x420c4, 0x420c8};
    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    return hwreg::RegisterAddr<TranscoderChicken>(kMmioAddress[ddi_index]);
  }

  static auto GetForKabyLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);

    // The transcoder-to-MMIO address mapping is specified implicitly in
    // IHD-OS-KBL-Vol 16-1.17 BSpec (workaround) ID 1144, page 31.
    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderChicken>(0x420c0 + transcoder_index * 4);
  }

  static auto GetForTigerLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);

    // TODO(fxbug.dev/109278): Allow transcoder D, once we support it.
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);

    // The transcoder-to-MMIO address mapping is presented in section "Variable
    // Refresh Rate" in the display engine PRMs.
    // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 page 240
    // DG1: IHD-OS-DG1-Vol 12-2.21 page 192
    static constexpr uint32_t kMmioAddress[] = {0x420c0, 0x420c4, 0x420c8, 0x420d8};
    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<TranscoderChicken>(kMmioAddress[transcoder_index]);
  }
};

class TranscoderRegs {
 public:
  explicit TranscoderRegs(i915_tgl::TranscoderId transcoder_id)
      : transcoder_id_(transcoder_id),
        offset_(transcoder_id_ == i915_tgl::TranscoderId::TRANSCODER_EDP
                    ? 0xf000
                    : (transcoder_id_ * 0x1000)) {}

  hwreg::RegisterAddr<TransHVTotal> HTotal() { return GetReg<TransHVTotal>(0x60000); }
  hwreg::RegisterAddr<TransHVTotal> HBlank() { return GetReg<TransHVTotal>(0x60004); }
  hwreg::RegisterAddr<TransHVSync> HSync() { return GetReg<TransHVSync>(0x60008); }
  hwreg::RegisterAddr<TransHVTotal> VTotal() { return GetReg<TransHVTotal>(0x6000c); }
  hwreg::RegisterAddr<TransHVTotal> VBlank() { return GetReg<TransHVTotal>(0x60010); }
  hwreg::RegisterAddr<TransHVSync> VSync() { return GetReg<TransHVSync>(0x60014); }
  hwreg::RegisterAddr<TransVSyncShift> VSyncShift() { return GetReg<TransVSyncShift>(0x60028); }

  hwreg::RegisterAddr<TranscoderDdiControl> DdiControl() {
    // This works for Tiger Lake too, because the supported transcoders are a
    // subset of the Kaby Lake transcoders, and the MMIO addresses for these
    // transcoders are the same.
    // TODO(fxbug.dev/109278): This won't be true once we support transcoder D.
    return TranscoderDdiControl::GetForKabyLakeTranscoder(transcoder_id_);
  }
  hwreg::RegisterAddr<TranscoderConfig> Config() {
    // This works for Tiger Lake too, because the supported transcoders are a
    // subset of the Kaby Lake transcoders, and the MMIO addresses for these
    // transcoders are the same.
    // TODO(fxbug.dev/109278): This won't be true once we support transcoder D.
    return TranscoderConfig::GetForKabyLakeTranscoder(transcoder_id_);
  }
  hwreg::RegisterAddr<TranscoderClockSelect> ClockSelect() {
    return TranscoderClockSelect::GetForTranscoder(transcoder_id_);
  }
  hwreg::RegisterAddr<TranscoderMainStreamAttributeMisc> MainStreamAttributeMisc() {
    // This works for Tiger Lake too, because the supported transcoders are a
    // subset of the Kaby Lake transcoders, and the MMIO addresses for these
    // transcoders are the same.
    return TranscoderMainStreamAttributeMisc::GetForKabyLakeTranscoder(transcoder_id_);
  }
  hwreg::RegisterAddr<TranscoderChicken> Chicken() {
    // This works for Tiger Lake too, because the supported transcoders are a
    // subset of the Kaby Lake transcoders, and the MMIO addresses for these
    // transcoders are the same.
    // TODO(fxbug.dev/109278): This won't be true once we support transcoder D.
    return TranscoderChicken::GetForKabyLakeTranscoder(transcoder_id_);
  }

  hwreg::RegisterAddr<TranscoderVariableRateRefreshControl> VariableRateRefreshControl() {
    // We should only be using this code on Tiger Lake.
    return TranscoderVariableRateRefreshControl::GetForTigerLakeTranscoder(transcoder_id_);
  }

  hwreg::RegisterAddr<TranscoderDataM> DataM() {
    // This works for Tiger Lake too, because the supported transcoders are a
    // subset of the Kaby Lake transcoders, and the MMIO addresses for these
    // transcoders are the same.
    // TODO(fxbug.dev/109278): This won't be true once we support transcoder D.
    return TranscoderDataM::GetForKabyLakeTranscoder(transcoder_id_);
  }
  hwreg::RegisterAddr<TranscoderDataN> DataN() {
    // This works for Tiger Lake too, because the supported transcoders are a
    // subset of the Kaby Lake transcoders, and the MMIO addresses for these
    // transcoders are the same.
    // TODO(fxbug.dev/109278): This won't be true once we support transcoder D.
    return TranscoderDataN::GetForKabyLakeTranscoder(transcoder_id_);
  }
  hwreg::RegisterAddr<TranscoderLinkM> LinkM() {
    // This works for Tiger Lake too, because the supported transcoders are a
    // subset of the Kaby Lake transcoders, and the MMIO addresses for these
    // transcoders are the same.
    // TODO(fxbug.dev/109278): This won't be true once we support transcoder D.
    return TranscoderLinkM::GetForKabyLakeTranscoder(transcoder_id_);
  }
  hwreg::RegisterAddr<TranscoderLinkN> LinkN() {
    // This works for Tiger Lake too, because the supported transcoders are a
    // subset of the Kaby Lake transcoders, and the MMIO addresses for these
    // transcoders are the same.
    // TODO(fxbug.dev/109278): This won't be true once we support transcoder D.
    return TranscoderLinkN::GetForKabyLakeTranscoder(transcoder_id_);
  }

 private:
  template <class RegType>
  hwreg::RegisterAddr<RegType> GetReg(uint32_t base_addr) {
    return hwreg::RegisterAddr<RegType>(base_addr + offset_);
  }

  i915_tgl::TranscoderId transcoder_id_;
  uint32_t offset_;
};
}  // namespace tgl_registers

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_TRANSCODER_H_
