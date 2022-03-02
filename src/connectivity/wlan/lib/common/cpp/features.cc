// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "features.h"

#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>
#include <zircon/status.h>

#include <wlan/common/logging.h>

namespace wlan::common {

namespace fidl_common = ::fuchsia::wlan::common;

zx_status_t ConvertDiscoverySupportToFidl(const discovery_support_t& in,
                                          fidl_common::DiscoverySupport* out) {
  *out = {};
  out->scan_offload.supported = in.scan_offload.supported;
  out->probe_response_offload.supported = in.probe_response_offload.supported;
  return ZX_OK;
}

zx_status_t ConvertDiscoverySupportToDdk(const fidl_common::DiscoverySupport& in,
                                         discovery_support_t* out) {
  *out = {};
  out->scan_offload.supported = in.scan_offload.supported;
  out->probe_response_offload.supported = in.probe_response_offload.supported;
  return ZX_OK;
}

zx_status_t ConvertMacSublayerSupportToFidl(const mac_sublayer_support_t& in,
                                            fidl_common::MacSublayerSupport* out) {
  *out = {};
  out->rate_selection_offload.supported = in.rate_selection_offload.supported;
  out->device.is_synthetic = in.device.is_synthetic;
  switch (in.device.mac_implementation_type) {
    case MAC_IMPLEMENTATION_TYPE_SOFTMAC:
      out->device.mac_implementation_type = fidl_common::MacImplementationType::SOFTMAC;
      break;
    case MAC_IMPLEMENTATION_TYPE_FULLMAC:
      out->device.mac_implementation_type = fidl_common::MacImplementationType::FULLMAC;
      break;
    default:
      errorf("MAC implementation type %hhu not supported", in.device.mac_implementation_type);
      return ZX_ERR_INVALID_ARGS;
  }
  out->device.tx_status_report_supported = in.device.tx_status_report_supported;
  switch (in.data_plane.data_plane_type) {
    case DATA_PLANE_TYPE_ETHERNET_DEVICE:
      out->data_plane.data_plane_type = fidl_common::DataPlaneType::ETHERNET_DEVICE;
      break;
    case DATA_PLANE_TYPE_GENERIC_NETWORK_DEVICE:
      out->data_plane.data_plane_type = fidl_common::DataPlaneType::GENERIC_NETWORK_DEVICE;
      break;
    default:
      errorf("Data plane type %hhu not supported", in.data_plane.data_plane_type);
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t ConvertMacSublayerSupportToDdk(const fidl_common::MacSublayerSupport& in,
                                           mac_sublayer_support_t* out) {
  out->rate_selection_offload.supported = in.rate_selection_offload.supported;
  out->device.is_synthetic = in.device.is_synthetic;
  switch (in.device.mac_implementation_type) {
    case fidl_common::MacImplementationType::SOFTMAC:
      out->device.mac_implementation_type = MAC_IMPLEMENTATION_TYPE_SOFTMAC;
      break;
    case fidl_common::MacImplementationType::FULLMAC:
      out->device.mac_implementation_type = MAC_IMPLEMENTATION_TYPE_FULLMAC;
      break;
    default:
      errorf("MAC implementation type %hhu not supported", in.device.mac_implementation_type);
      return ZX_ERR_INVALID_ARGS;
  }
  out->device.tx_status_report_supported = in.device.tx_status_report_supported;
  switch (in.data_plane.data_plane_type) {
    case fidl_common::DataPlaneType::ETHERNET_DEVICE:
      out->data_plane.data_plane_type = DATA_PLANE_TYPE_ETHERNET_DEVICE;
      break;
    case fidl_common::DataPlaneType::GENERIC_NETWORK_DEVICE:
      out->data_plane.data_plane_type = DATA_PLANE_TYPE_GENERIC_NETWORK_DEVICE;
      break;
    default:
      errorf("Data plane type %hhu not supported", in.data_plane.data_plane_type);
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t ConvertSecuritySupportToFidl(const security_support_t& in,
                                         fidl_common::SecuritySupport* out) {
  *out = {};
  out->mfp.supported = in.mfp.supported;
  out->sae.supported = in.sae.supported;
  switch (in.sae.handler) {
    case SAE_HANDLER_DRIVER:
      out->sae.handler = fidl_common::SaeHandler::DRIVER;
      break;
    case SAE_HANDLER_SME:
      out->sae.handler = fidl_common::SaeHandler::SME;
      break;
    default:
      errorf("SAE handler %hhu not supported", in.sae.handler);
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t ConvertSecuritySupportToDdk(const fidl_common::SecuritySupport& in,
                                        security_support_t* out) {
  *out = {};
  out->mfp.supported = in.mfp.supported;
  out->sae.supported = in.sae.supported;
  switch (in.sae.handler) {
    case fidl_common::SaeHandler::DRIVER:
      out->sae.handler = SAE_HANDLER_DRIVER;
      break;
    case fidl_common::SaeHandler::SME:
      out->sae.handler = SAE_HANDLER_SME;
      break;
    default:
      errorf("SAE handler %hhu not supported", in.sae.handler);
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t ConvertSpectrumManagementSupportToFidl(const spectrum_management_support_t& in,
                                                   fidl_common::SpectrumManagementSupport* out) {
  *out = {};
  out->dfs.supported = in.dfs.supported;
  return ZX_OK;
}

zx_status_t ConvertSpectrumManagementSupportToDdk(const fidl_common::SpectrumManagementSupport& in,
                                                  spectrum_management_support_t* out) {
  *out = {};
  out->dfs.supported = in.dfs.supported;
  return ZX_OK;
}

}  // namespace wlan::common
