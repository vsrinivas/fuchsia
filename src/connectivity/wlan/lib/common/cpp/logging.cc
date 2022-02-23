// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/wlan/phyinfo/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>

#include <map>
#include <string>

#include <wlan/common/logging.h>

static std::map<wlan_info_driver_feature_t, std::string> driver_feature_flags_string_map = {
    {WLAN_INFO_DRIVER_FEATURE_SCAN_OFFLOAD, "Scan Offload"},
    {WLAN_INFO_DRIVER_FEATURE_RATE_SELECTION, "Rate Selection"},
    {WLAN_INFO_DRIVER_FEATURE_SYNTH, "Synthetic Device"},
    {WLAN_INFO_DRIVER_FEATURE_TX_STATUS_REPORT, "Tx Status Report"},
    {WLAN_INFO_DRIVER_FEATURE_DFS, "Dynamic Frequency Selection (DFS)"},
    {WLAN_INFO_DRIVER_FEATURE_PROBE_RESP_OFFLOAD, "Probe Response Offload"},
};

static std::map<wlan_softmac_hardware_capability_t, std::string>
    softmac_hardware_capability_flags_string_map = {
        {WLAN_SOFTMAC_HARDWARE_CAPABILITY_BIT_SHORT_PREAMBLE, "Short Preamble"},
        {WLAN_SOFTMAC_HARDWARE_CAPABILITY_BIT_SPECTRUM_MGMT, "Spectrum Management"},
        {WLAN_SOFTMAC_HARDWARE_CAPABILITY_BIT_QOS, "QoS"},
        {WLAN_SOFTMAC_HARDWARE_CAPABILITY_BIT_SHORT_SLOT_TIME, "Short Slot Time"},
        {WLAN_SOFTMAC_HARDWARE_CAPABILITY_BIT_RADIO_MSMT, "Radio Measurement"}};

template <typename flags_t>
static void DebugFlags(flags_t flags, std::map<flags_t, std::string> flags_string_map,
                       std::string flags_display_name, std::string flag_on_display_string,
                       std::string flag_off_display_string) {
  static_assert(std::is_integral<flags_t>::value, "flags_t must be an integral type.");
  static_assert(sizeof(flags_t) <= sizeof(uint32_t), "flags_t can be at most 4 bytes.");

  // Setup output stream for formatting hexidecimal numbers
  debugflags("%s: 0x%04x\n", flags_display_name.c_str(), flags);
  for (const auto& [mask, mask_name] : flags_string_map) {
    std::string flag_status_display_string = flag_off_display_string;
    if (mask & flags) {
      flag_status_display_string = flag_on_display_string;
    }
    debugflags("  %s: %s\n", mask_name.c_str(), flag_status_display_string.c_str());
  }
}

void wlan::DebugDriverFeatureFlags(wlan_info_driver_feature_t flags) {
  DebugFlags(flags, driver_feature_flags_string_map, "Driver Features", "Available",
             "NOT Available");
}

void wlan::DebugSoftmacHardwareCapabilityFlags(wlan_softmac_hardware_capability_t flags) {
  DebugFlags(flags, softmac_hardware_capability_flags_string_map, "Hardware Capabilities",
             "Available", "NOT Available");
}
