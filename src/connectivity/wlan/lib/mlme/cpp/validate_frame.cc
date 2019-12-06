// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>

#include <fbl/span.h>
#include <wlan/common/buffer_reader.h>
#include <wlan/common/mac_frame.h>
#include <wlan/mlme/debug.h>

namespace wlan {

using namespace element_id;

class ErrorAccumulator {
 public:
  __attribute__((__format__(__printf__, 3, 4))) void Add(size_t offset, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    char buf[200];
    snprintf(buf, sizeof(buf), "(at 0x%04zx) ", offset);
    message_ += buf;

    vsnprintf(buf, sizeof(buf), fmt, ap);
    message_ += buf;
    message_ += '\n';
  }

  bool HaveErrors() { return !message_.empty(); }

  const char* GetMessage() { return message_.c_str(); }

 private:
  std::string message_;
};

struct AllowedElement {
  ElementId id;
  bool required;

  constexpr AllowedElement(ElementId id, bool required = false) : id(id), required(required) {}
};

constexpr AllowedElement Required(ElementId id) { return AllowedElement(id, true); }

// IEEE Std 802.11-2016, 9.3.3.3
static constexpr AllowedElement kBeaconElements[] = {
    Required(kSsid),
    Required(kSuppRates),
    kDsssParamSet,
    kCfParamSet,
    kIbssParamSet,
    kTim,
    kCountry,
    kPowerConstraint,
    kChannelSwitchAnn,
    kQuiet,
    kIbssDfs,
    kTpcReport,
    kErp,
    kExtSuppRates,
    kRsn,
    kBssLoad,
    kEdcaParamSet,
    kQosCapability,
    kApChannelReport,
    kBssAvgAccessDelay,
    kAntenna,
    kBssAvailAdmissionCapacity,
    kBssAcAccessDelay,
    kMeasurementPilotTrans,
    kMultipleBssid,
    kRmEnabledCapabilities,
    kMobilityDomain,
    kDseRegisteredLocation,
    kExtChannelSwitchAnn,
    kSuppOperatingClasses,
    kHtCapabilities,
    kHtOperation,
    k2040BssCoex,
    kOverlappingBssScanParams,
    kExtCapabilities,
    kFmsDescriptor,
    kQosTrafficCapability,
    kTimeAdvertisement,
    kInterworking,
    kAdvertisementProtocol,
    kRoamingConsortium,
    kEmergencyAlertId,
    kMeshId,
    kMeshConfiguration,
    kMeshAwakeWindow,
    kBeaconTiming,
    kMccaopAdvertisementOverview,
    kMccaopAdvertisement,
    kMeshChannelSwitchParams,
    kQmfPolicy,
    kQloadReport,
    kHccaTxopUpdateCount,
    kMultiband,
    kVhtCapabilities,
    kVhtOperation,
    kTransmitPowerEnvelope,
    kChannelSwitchWrapper,
    kExtBssLoad,
    kQuietChannel,
    kOperatingModeNotification,
    kReducedNeighborReport,
    kTvhtOperation,
    // TODO: Estimated Service Parameters (2-byte ID)
    // TODO: Future Channel Guidance (2-byte ID)
};

// IEEE Std 802.11-2016, 9.3.3.5
static constexpr AllowedElement kDisassocElements[] = {kManagementMic};

// IEEE Std 802.11-2016, 9.3.3.6
static constexpr AllowedElement kAssocReqElements[] = {
    Required(kSsid),
    Required(kSuppRates),
    kExtSuppRates,
    kPowerCapability,
    kSupportedChannels,
    kRsn,
    kQosCapability,
    kRmEnabledCapabilities,
    kMobilityDomain,
    kSuppOperatingClasses,
    kHtCapabilities,
    k2040BssCoex,
    kExtCapabilities,
    kQosTrafficCapability,
    kTimBroadcastRequest,
    kInterworking,
    kMultiband,
    kDmgCapabilities,
    kMultipleMacSublayers,
    kVhtCapabilities,
    kOperatingModeNotification,
};

// IEEE Std 802.11-2016, 9.3.3.7
static constexpr AllowedElement kAssocRespElements[] = {
    kSuppRates,
    kExtSuppRates,
    kEdcaParamSet,
    kRcpi,
    kRsni,
    kRmEnabledCapabilities,
    kMobilityDomain,
    kFastBssTransition,
    kDseRegisteredLocation,
    kTimeoutInterval,
    kHtCapabilities,
    kHtOperation,
    k2040BssCoex,
    kOverlappingBssScanParams,
    kExtCapabilities,
    kBssMaxIdlePeriod,
    kTimBroadcastResponse,
    kQosMap,
    kQmfPolicy,
    kMultiband,
    kDmgCapabilities,
    kDmgOperation,
    kMultipleMacSublayers,
    kNeighborReport,
    kVhtCapabilities,
    kVhtOperation,
    kOperatingModeNotification,
    // TODO: Future Channel Guidance (2-byte ID)
};

// IEEE Std 802.11-2016, 9.3.3.8
static constexpr AllowedElement kReassocReqElements[] = {
    // clang-format off
    Required(kSsid),
    Required(kSuppRates),
    kExtSuppRates,
    kPowerCapability,
    kSupportedChannels,
    kRsn,
    kQosCapability,
    kRmEnabledCapabilities,
    kMobilityDomain,
    kFastBssTransition,
    // TODO: RIC container? (can be several elements)
    kSuppOperatingClasses,
    kHtCapabilities,
    k2040BssCoex,
    kExtCapabilities,
    kQosTrafficCapability,
    kTimBroadcastRequest,
    kFmsRequest,
    kDmsRequest,
    kInterworking,
    kMultiband,
    kDmgCapabilities,
    kMultipleMacSublayers,
    kVhtCapabilities,
    kOperatingModeNotification
    // clang-format on
};

// IEEE Std 802.11-2016, 9.3.3.9
static constexpr AllowedElement kReassocRespElements[] = {
    // clang-format off
    Required(kSuppRates),
    kExtSuppRates,
    kEdcaParamSet,
    kRcpi,
    kRsni,
    kRmEnabledCapabilities,
    kRsn,
    kMobilityDomain,
    kFastBssTransition,
    // TODO: RIC container? (can be several elements)
    kDseRegisteredLocation,
    kTimeoutInterval,
    kHtCapabilities,
    kHtOperation,
    k2040BssCoex,
    kOverlappingBssScanParams,
    kExtCapabilities,
    kBssMaxIdlePeriod,
    kTimBroadcastResponse,
    kFmsResponse,
    kDmsResponse,
    kQosMap,
    kQmfPolicy,
    kMultiband,
    kDmgCapabilities,
    kDmgOperation,
    kMultipleMacSublayers,
    kNeighborReport,
    kVhtCapabilities,
    kVhtOperation,
    kOperatingModeNotification
    // TODO: Future Channel Guidance (2-byte ID)
    // clang-format on
};

// IEEE Std 802.11-2016, 9.3.3.10
static constexpr AllowedElement kProbeReqElements[] = {
    // clang-format off
    Required(kSsid),
    Required(kSuppRates),
    kRequest,
    kExtSuppRates,
    kDsssParamSet,
    kSuppOperatingClasses,
    kHtCapabilities,
    k2040BssCoex,
    kExtCapabilities,
    kSsidList,
    kChannelUsage,
    kInterworking,
    kMeshId,
    kMultiband,
    kDmgCapabilities,
    kMultipleMacSublayers,
    kVhtCapabilities,
    // TODO: Estimated Service Parameters (2-byte ID)
    // TODO: Extended Request (2-byte ID)
    // clang-format on
};

// IEEE Std 802.11-2016, 9.3.3.11
static constexpr AllowedElement kProbeRespElements[] = {
    // clang-format off
    Required(kSsid),
    Required(kSuppRates),
    kDsssParamSet,
    kCfParamSet,
    kIbssParamSet,
    kCountry,
    kPowerConstraint,
    kChannelSwitchAnn,
    kQuiet,
    kIbssDfs,
    kTpcReport,
    kErp,
    kExtSuppRates,
    kRsn,
    kBssLoad,
    kEdcaParamSet,
    kMeasurementPilotTrans,
    kMultipleBssid,
    kRmEnabledCapabilities,
    kApChannelReport,
    kBssAvgAccessDelay,
    kAntenna,
    kBssAvailAdmissionCapacity,
    kBssAcAccessDelay,
    kMobilityDomain,
    kDseRegisteredLocation,
    kExtChannelSwitchAnn,
    kSuppOperatingClasses,
    kHtCapabilities,
    kHtOperation,
    k2040BssCoex,
    kOverlappingBssScanParams,
    kExtCapabilities,
    kQosTrafficCapability,
    kChannelUsage,
    kTimeAdvertisement,
    kTimeZone,
    kInterworking,
    kAdvertisementProtocol,
    kRoamingConsortium,
    kEmergencyAlertId,
    kMeshId,
    kMeshConfiguration,
    kMeshAwakeWindow,
    kBeaconTiming,
    kMccaopAdvertisementOverview,
    kMccaopAdvertisement,
    kMeshChannelSwitchParams,
    kQmfPolicy,
    kQloadReport,
    kMultiband,
    kDmgCapabilities,
    kDmgOperation,
    kMultipleMacSublayers,
    kAntennaSectorIdPattern,
    kVhtCapabilities,
    kVhtOperation,
    kTransmitPowerEnvelope,
    kChannelSwitchWrapper,
    kExtBssLoad,
    kQuietChannel,
    kOperatingModeNotification,
    kReducedNeighborReport,
    kTvhtOperation,
    // TODO: Estimated Service Parameters (2-byte ID)
    kRelayCapabilities
    // clang-format on
};

// IEEE Std 802.11-2016, 9.3.3.12
static constexpr AllowedElement kAuthElements[] = {
    // clang-format off
    kChallengeText,
    kRsn,
    kMobilityDomain,
    kFastBssTransition,
    kTimeoutInterval
    // TODO: RIC (can be several elements)
    // clang-format on
};

// IEEE Std 802.11-2016, 9.3.3.13
static constexpr AllowedElement kDeauthElements[] = {kManagementMic};

// IEEE Std 802.11-2016, 9.3.3.16
static constexpr AllowedElement kTimingAdElements[] = {
    // clang-format off
    kCountry,
    kPowerConstraint,
    kTimeAdvertisement,
    kExtCapabilities
    // clang-format on
};

static void ValidateFixedSizeElement(size_t offset, fbl::Span<const uint8_t> body,
                                     size_t expected_size, const char* element_name,
                                     ErrorAccumulator* errors) {
  if (body.size() != expected_size) {
    errors->Add(offset, "%s element has invalid length (%zu bytes vs %zu expected)", element_name,
                body.size(), expected_size);
  }
}

static void ValidateElement(size_t offset, ElementId id, fbl::Span<const uint8_t> body,
                            ErrorAccumulator* errors) {
  switch (id) {
    case kSsid:
      if (body.size() > 32) {
        errors->Add(offset, "SSID element is too long (%zu bytes)", body.size());
      }
      break;
    case kSuppRates:
      if (body.empty() || body.size() > 8) {
        errors->Add(offset, "Supported Rates element has invalid length (%zu bytes)", body.size());
      }
      break;
    case kDsssParamSet:
      ValidateFixedSizeElement(offset, body, 1, "DSSS Parameter Set", errors);
      break;
    case kCfParamSet:
      ValidateFixedSizeElement(offset, body, 6, "CF Parameter Set", errors);
      break;
    case kTim:
      if (body.size() < 4) {
        errors->Add(offset, "TIM element is too short (%zu bytes)", body.size());
      }
      break;
    case kCountry:
      if (body.size() < 3) {
        errors->Add(offset, "Country element is too short (%zu bytes)", body.size());
      } else if (body.size() % 2 != 0) {
        errors->Add(offset, "Country element is not padded to even length");
      } else if (body.size() % 3 == 2) {
        errors->Add(offset, "Country element includes an extra padding byte");
      }
      break;
    case kExtSuppRates:
      if (body.empty()) {
        errors->Add(offset, "Extended Supported Rates element is empty");
      }
      break;
    case kMeshId:
      if (body.size() > 32) {
        errors->Add(offset, "Mesh ID element is too long (%zu bytes)", body.size());
      }
      break;
    case kMeshConfiguration:
      ValidateFixedSizeElement(offset, body, 7, "Mesh Configuration", errors);
      break;
    case kMeshPeeringManagement:
      if (body.size() < 4) {
        errors->Add(offset, "Mesh Peering Management element is too short (%zu bytes)",
                    body.size());
      } else if (body.size() > 24) {
        errors->Add(offset, "Mesh Peering Management element is too long (%zu bytes)", body.size());
      } else {
        size_t opt = (body.size() - 4) % 16;
        if (opt != 0 && opt != 2 && opt != 4) {
          errors->Add(offset, "Mesh Peering Management element has invalid length (%zu bytes)",
                      body.size());
        }
      }
      break;
    case kQosCapability:
      ValidateFixedSizeElement(offset, body, 1, "QoS Capability", errors);
      break;
    case kGcrGroupAddress:
      ValidateFixedSizeElement(offset, body, 6, "GCR Group Address", errors);
      break;
    case kHtCapabilities:
      ValidateFixedSizeElement(offset, body, 26, "HT Capabilities", errors);
      break;
    case kHtOperation:
      ValidateFixedSizeElement(offset, body, 22, "HT Operation", errors);
      break;
    case kVhtCapabilities:
      ValidateFixedSizeElement(offset, body, 12, "VHT Capabilities", errors);
      break;
    case kVhtOperation:
      ValidateFixedSizeElement(offset, body, 5, "VHT Operation", errors);
      break;
    default:
      break;
  }
}

static void ValidateElements(BufferReader* r, fbl::Span<const AllowedElement> allowed,
                             ErrorAccumulator* errors) {
  bool seen[allowed.size()];
  std::fill_n(seen, allowed.size(), false);

  size_t prev_order = 0;
  uint8_t prev_id = 0;

  while (r->RemainingBytes() > 0) {
    size_t hdr_offset = r->ReadBytes();

    auto hdr = r->ReadValue<ElementHeader>();
    if (!hdr) {
      errors->Add(hdr_offset, "Incomplete element header at end of frame");
      break;
    }
    auto id = static_cast<element_id::ElementId>(hdr->id);
    auto len = hdr->len;

    auto it = std::find_if(allowed.begin(), allowed.end(),
                           [id](const AllowedElement& ae) { return ae.id == id; });
    if (it == allowed.end()) {
      errors->Add(hdr_offset, "Unexpected element ID %u", id);
    } else {
      size_t order = it - allowed.begin();
      if (order < prev_order) {
        errors->Add(hdr_offset, "Wrong element order: %u is expected to appear before %u", id,
                    prev_id);
      }
      prev_order = order;
      prev_id = id;

      if (seen[order]) {
        errors->Add(hdr_offset, "Duplicate element %u", id);
      }
      seen[order] = true;
    }

    auto ie_body = r->Read(len);
    if (len > 0 && ie_body.empty()) {
      errors->Add(hdr_offset + 1, "Element length %u exceeds the number of remaining bytes %zu",
                  len, r->RemainingBytes());
      break;
    }

    ValidateElement(hdr_offset, id, ie_body, errors);
  }

  for (size_t i = 0; i < allowed.size(); ++i) {
    if (allowed[i].required && !seen[i]) {
      errors->Add(r->ReadBytes() + r->RemainingBytes(), "Required element %u is not present",
                  allowed[i].id);
    }
  }
}

static void ValidateFrameWithElements(BufferReader* r, size_t fixed_header_len,
                                      const char* frame_name,
                                      fbl::Span<const AllowedElement> allowed_elements,
                                      ErrorAccumulator* errors) {
  if (fixed_header_len != 0 && r->Read(fixed_header_len).empty()) {
    errors->Add(r->ReadBytes(), "Expected a %s header but the frame is too short", frame_name);
    return;
  }

  ValidateElements(r, allowed_elements, errors);
}

static void ValidateMgmtFrame(BufferReader* r, ErrorAccumulator* errors) {
  auto mgmt_header = r->Read<MgmtFrameHeader>();
  if (mgmt_header == nullptr) {
    errors->Add(r->ReadBytes(), "Frame is shorter than minimum mgmt header length");
    return;
  }

  if (mgmt_header->fc.HasHtCtrl()) {
    auto ht_ctrl = r->Read<HtControl>();
    if (ht_ctrl == nullptr) {
      errors->Add(r->ReadBytes(), "FC indicates that HTC is present but the frame is too short");
      return;
    }
  }

  switch (mgmt_header->fc.subtype()) {
    case kAssociationRequest:
      ValidateFrameWithElements(r, sizeof(AssociationRequest), "Association Request",
                                kAssocReqElements, errors);
      break;
    case kAssociationResponse:
      ValidateFrameWithElements(r, sizeof(AssociationResponse), "Association Response",
                                kAssocRespElements, errors);
      break;
    case kReassociationRequest:
      ValidateFrameWithElements(r, sizeof(ReassociationRequest), "Reassociation Request",
                                kReassocReqElements, errors);
      break;
    case kReassociationResponse:
      ValidateFrameWithElements(r, sizeof(ReassociationResponse), "Reassociation Response",
                                kReassocRespElements, errors);
      break;
    case kProbeRequest:
      ValidateFrameWithElements(r, ProbeRequest::max_len(), "Probe Request", kProbeReqElements,
                                errors);
      break;
    case kProbeResponse:
      ValidateFrameWithElements(r, sizeof(ProbeResponse), "Probe Response", kProbeRespElements,
                                errors);
      break;
    case kTimingAdvertisement:
      ValidateFrameWithElements(r, sizeof(TimingAdvertisement), "Timing Advertisement",
                                kTimingAdElements, errors);
      break;
    case kBeacon:
      ValidateFrameWithElements(r, sizeof(Beacon), "Beacon", kBeaconElements, errors);
      break;
    case kAtim:
      if (r->RemainingBytes() > 0) {
        errors->Add(r->ReadBytes(), "ATIM frame has a non-null body");
      }
      break;
    case kDisassociation:
      ValidateFrameWithElements(r, sizeof(Disassociation), "Disassociation", kDisassocElements,
                                errors);
      break;
    case kAuthentication:
      // This will report a false positive if we attempt to write an auth frame
      // with trailing non-element fields, e.g. "Finite Cyclic Group".
      // If we get there one day, we can delete this check (or write a proper
      // validator, which is probably not worth the effort, given how
      // complicated the encoding is).
      ValidateFrameWithElements(r, sizeof(Authentication), "Authentication", kAuthElements, errors);
      break;
    case kDeauthentication:
      ValidateFrameWithElements(r, sizeof(Deauthentication), "Deauthentication", kDeauthElements,
                                errors);
      break;
    case kAction:
    case kActionNoAck:
      break;
  }
}

static void DoValidateFrame(fbl::Span<const uint8_t> data, ErrorAccumulator* errors) {
  BufferReader r{data};
  auto fc = r.Peek<FrameControl>();

  if (fc == nullptr) {
    errors->Add(0, "Frame is too short to contain a Frame Control field");
    return;
  }

  switch (fc->type()) {
    case kManagement:
      ValidateMgmtFrame(&r, errors);
      break;
    case kControl:
      break;
    case kData:
      break;
    case kExtension:
      break;
  }
}

bool ValidateFrame(const char* context_msg, fbl::Span<const uint8_t> data) {
  ErrorAccumulator errors;
  DoValidateFrame(data, &errors);
  if (errors.HaveErrors()) {
    errorf("%s:\n%sFrame contents: %s\n", context_msg, errors.GetMessage(),
           debug::HexDump(data).c_str());
  }
  return !errors.HaveErrors();
}

}  // namespace wlan
