// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/net/routes/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/internal/DeviceNetworkInfo.h>
#include <Weave/DeviceLayer/PlatformManager.h>
#include <Weave/DeviceLayer/ThreadStackManager.h>
#include <Weave/Support/TraitEventUtils.h>
// clang-format on

#include "thread_stack_manager_delegate_impl.h"
#include "weave_inspector.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {

namespace {
using fuchsia::lowpan::ConnectivityState;
using fuchsia::lowpan::Credential;
using fuchsia::lowpan::Identity;
using fuchsia::lowpan::JoinerCommissioningParams;
using fuchsia::lowpan::JoinParams;
using fuchsia::lowpan::MAX_PROVISION_URL_LEN;
using fuchsia::lowpan::MAX_VENDOR_DATA_LEN;
using fuchsia::lowpan::MAX_VENDOR_MODEL_LEN;
using fuchsia::lowpan::MAX_VENDOR_NAME_LEN;
using fuchsia::lowpan::MAX_VENDOR_SW_VER_LEN;
using fuchsia::lowpan::ProvisioningParams;
using fuchsia::lowpan::PSKD_LEN;
using fuchsia::lowpan::Role;
using fuchsia::lowpan::device::AllCounters;
using fuchsia::lowpan::device::Counters;
using fuchsia::lowpan::device::CountersSyncPtr;
using fuchsia::lowpan::device::DeviceExtraSyncPtr;
using fuchsia::lowpan::device::DeviceState;
using fuchsia::lowpan::device::DeviceSyncPtr;
using fuchsia::lowpan::device::Lookup_LookupDevice_Result;
using fuchsia::lowpan::device::LookupSyncPtr;
using fuchsia::lowpan::device::MacCounters;
using fuchsia::lowpan::device::Protocols;
using fuchsia::lowpan::device::ProvisionError;
using fuchsia::lowpan::thread::LegacyJoiningSyncPtr;
using fuchsia::net::IpAddress;
using fuchsia::net::Ipv4Address;
using fuchsia::net::Ipv6Address;
using fuchsia::net::routes::State_Resolve_Result;

using nl::Weave::WeaveInspector;
using nl::Weave::DeviceLayer::ConfigurationManager;
using nl::Weave::DeviceLayer::ConfigurationMgr;
using nl::Weave::DeviceLayer::PlatformMgrImpl;
using nl::Weave::DeviceLayer::Internal::DeviceNetworkInfo;
using nl::Weave::Profiles::NetworkProvisioning::kNetworkType_Thread;

using ThreadDeviceType = ConnectivityManager::ThreadDeviceType;

namespace TelemetryNetworkWpanTrait = Schema::Nest::Trait::Network::TelemetryNetworkWpanTrait;

constexpr uint16_t kMinThreadChannel = 11;
constexpr uint16_t kMaxThreadChannel = 26;

// Default joinable period for Thread network setup.
constexpr zx::duration kThreadJoinableDurationDefault{zx_duration_from_sec(300)};
// A joinable duration of 0 stops any active joinable state.
constexpr zx::duration kThreadJoinableStop{zx_duration_from_sec(0)};

// The required size of a buffer supplied to GetPrimary802154MACAddress.
constexpr size_t k802154MacAddressBufSize =
    sizeof(Profiles::DeviceDescription::WeaveDeviceDescriptor::Primary802154MACAddress);
// Fake MAC address returned by GetPrimary802154MACAddress
constexpr uint8_t kFakeMacAddress[k802154MacAddressBufSize] = {0xFF};

// The maximum buffer size of all info needed for joining parameters.
constexpr size_t kJoinInfoBufferSize{
    MaxConstant(ConfigurationManager::kMaxPairingCodeLength + 1,
                ConfigurationManager::kMaxFirmwareRevisionLength + 1,
                ConfigurationManager::kMaxProductIdDescriptionLength + 1,
                ConfigurationManager::kMaxVendorIdDescriptionLength + 1)};
// The duration that Thread should spend attempting to join an existing network
// at startup.
constexpr zx::duration kJoinAtStartupTimeout{zx_duration_from_sec(120)};
// The duration of delay that should occur between join attempts.
constexpr zx::duration kJoinAtStartupRetryDelay{zx_duration_from_sec(10)};
}  // namespace

// Note: Since the functions within this class are intended to function
// synchronously within the Device Layer, these functions all use SyncPtrs for
// interfacing with the LoWPAN FIDL protocols.

WEAVE_ERROR ThreadStackManagerDelegateImpl::InitThreadStack() {
  // See note at top to explain these SyncPtrs.
  LookupSyncPtr lookup;
  Lookup_LookupDevice_Result result;
  Protocols protocols;
  std::vector<std::string> interface_names;
  zx_status_t status;

  // Check whether Thread support is enabled in the ConfigurationManager
  if (!ConfigurationMgrImpl().IsThreadEnabled()) {
    FX_LOGS(INFO) << "Thread support is disabled for this device.";
    is_thread_supported_ = false;
    return WEAVE_NO_ERROR;
  }

  // Access the LoWPAN service.
  status = PlatformMgrImpl().GetComponentContextForProcess()->svc()->Connect(lookup.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to fuchsia.lowpan.device.Lookup: "
                   << zx_status_get_string(status);
    return status;
  }

  // Retrieve LoWPAN interface names.
  status = lookup->GetDevices(&interface_names);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to retrieve LoWPAN interface names: " << zx_status_get_string(status);
    return status;
  }

  // Check returned interfaces for Thread support.
  fuchsia::lowpan::device::DeviceSyncPtr device;
  bool found_device = false;
  for (auto& name : interface_names) {
    std::vector<std::string> net_types;

    protocols.set_device(device.NewRequest());

    // Look up the device by interface name.
    status = lookup->LookupDevice(name, std::move(protocols), &result);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to lookup device: " << zx_status_get_string(status);
      return status;
    }
    if (result.is_err()) {
      FX_LOGS(WARNING) << "LoWPAN service error during lookup: "
                       << static_cast<int32_t>(result.err());
      continue;
    }

    // Check if the device supports Thread.
    status = device->GetSupportedNetworkTypes(&net_types);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to request supported network types from device \"" << name
                     << "\": " << zx_status_get_string(status);
      return status;
    }

    for (auto& net_type : net_types) {
      if (net_type == fuchsia::lowpan::NET_TYPE_THREAD_1_X) {
        // Found a Thread device.
        interface_name_ = name;
        found_device = true;
        break;
      }
    }

    if (found_device) {
      break;
    }
  }

  if (!found_device) {
    FX_LOGS(ERROR) << "Could not find a device that supports Thread networks!";
    return ZX_ERR_NOT_FOUND;
  }

  is_thread_supported_ = true;

  if (!IsThreadProvisioned()) {
    return StartThreadJoining();
  }

  return WEAVE_NO_ERROR;
}

bool ThreadStackManagerDelegateImpl::HaveRouteToAddress(const IPAddress& destAddr) {
  fuchsia::net::routes::StateSyncPtr routes;
  State_Resolve_Result result;
  IpAddress netstack_addr;
  zx_status_t status;

  status = PlatformMgrImpl().GetComponentContextForProcess()->svc()->Connect(routes.NewRequest());
  if (status != ZX_OK) {
    // Unfortunately, no way to inform of error status.
    return false;
  }

  if (destAddr.IsIPv6()) {
    Ipv6Address netstack_v6_addr;
    static_assert(sizeof(netstack_v6_addr.addr) == sizeof(destAddr.Addr));
    std::memcpy(&netstack_v6_addr.addr, destAddr.Addr, sizeof(destAddr.Addr));
    netstack_addr.set_ipv6(std::move(netstack_v6_addr));
  } else if (destAddr.IsIPv4()) {
    Ipv4Address netstack_v4_addr;
    static_assert(sizeof(netstack_v4_addr.addr) == sizeof(destAddr.Addr[3]));
    std::memcpy(&netstack_v4_addr.addr, &destAddr.Addr[3], sizeof(destAddr.Addr[3]));
    netstack_addr.set_ipv4(std::move(netstack_v4_addr));
  } else {
    // No route to the "unspecified address".
    FX_LOGS(ERROR) << "HaveRouteToAddress recieved unspecified IP address.";
    return false;
  }

  status = routes->Resolve(std::move(netstack_addr), &result);
  if (status != ZX_OK) {
    // Unfortunately, no way to inform of error status.
    return false;
  } else if (result.is_err()) {
    // Result will be ZX_ERR_ADDRESS_UNREACHABLE if unreachable.
    if (result.err() != ZX_ERR_ADDRESS_UNREACHABLE) {
      FX_LOGS(ERROR) << "Result from resolving route was error "
                     << zx_status_get_string(result.err());
    }
    return false;
  }

  // Result resolved, a route exists.
  return true;
}

void ThreadStackManagerDelegateImpl::OnPlatformEvent(const WeaveDeviceEvent* event) {}

bool ThreadStackManagerDelegateImpl::IsThreadEnabled() {
  DeviceState device_state;

  if (!IsThreadSupported()) {
    return false;
  }

  // Get the device state.
  if (GetDeviceState(device_state) != ZX_OK) {
    return false;
  }

  // Determine whether Thread is enabled.
  switch (device_state.connectivity_state()) {
    case ConnectivityState::OFFLINE:
    case ConnectivityState::ATTACHING:
    case ConnectivityState::ATTACHED:
    case ConnectivityState::ISOLATED:
    case ConnectivityState::COMMISSIONING:
      return true;
    default:
      return false;
  }
}

WEAVE_ERROR ThreadStackManagerDelegateImpl::SetThreadEnabled(bool val) {
  DeviceSyncPtr device;
  zx_status_t status;

  if (!IsThreadSupported()) {
    return WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE;
  }

  status = GetDevice(device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to acquire LoWPAN device: " << zx_status_get_string(status);
    return status;
  }

  // Enable or disable the device.
  status = device->SetActive(val);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to " << (val ? "enable" : "disable")
                   << " Thread: " << zx_status_get_string(status);
    return status;
  }

  return WEAVE_NO_ERROR;
}

bool ThreadStackManagerDelegateImpl::IsThreadProvisioned() {
  DeviceState device_state;

  if (!IsThreadSupported()) {
    return false;
  }

  // Get the device state.
  if (GetDeviceState(device_state) != ZX_OK) {
    return false;
  }

  // Check for the provision.
  switch (device_state.connectivity_state()) {
    case ConnectivityState::INACTIVE:
    case ConnectivityState::OFFLINE:
      return false;
    default:
      return true;
  }
}

bool ThreadStackManagerDelegateImpl::IsThreadAttached() {
  DeviceState device_state;

  if (!IsThreadSupported()) {
    return false;
  }

  // Get the device state.
  if (GetDeviceState(device_state) != ZX_OK) {
    return false;
  }

  return device_state.connectivity_state() == ConnectivityState::ATTACHED;
}

WEAVE_ERROR ThreadStackManagerDelegateImpl::GetThreadProvision(DeviceNetworkInfo& netInfo,
                                                               bool includeCredentials) {
  DeviceExtraSyncPtr device_extra;
  Identity identity;
  zx_status_t status;

  if (!IsThreadSupported()) {
    return WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE;
  }

  if (!IsThreadProvisioned()) {
    return WEAVE_ERROR_INCORRECT_STATE;
  }

  // Get the Device pointer.
  status = GetProtocols(std::move(Protocols().set_device_extra(device_extra.NewRequest())));
  if (status != ZX_OK) {
    return status;
  }

  // Get the network identity.
  status = device_extra->WatchIdentity(&identity);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not get LoWPAN network identity: " << zx_status_get_string(status);
    return status;
  }

  // TODO(fxbug.dev/67254): Restore the following block once the LoWPAN service
  // correctly returns the net_type.

  // // Check if the provision is a Thread network.
  // if (!identity.has_net_type()) {
  //   FX_LOGS(ERROR) << "No net_type provided; cannot confirm Thread network type.";
  //   return ZX_ERR_INTERNAL;
  // }
  // if (identity.net_type() != fuchsia::lowpan::NET_TYPE_THREAD_1_X) {
  //   FX_LOGS(ERROR) << "Cannot support LoWPAN network type \"" << identity.net_type()
  //                  << "\" in ThreadStackManager.";
  //   return ZX_ERR_INTERNAL;
  // }

  // Start copying provision info.
  netInfo.Reset();
  netInfo.NetworkType = kNetworkType_Thread;
  netInfo.NetworkId = Internal::kThreadNetworkId;
  netInfo.FieldPresent.NetworkId = true;

  // Copy network name.
  if (identity.has_raw_name()) {
    std::memcpy(netInfo.ThreadNetworkName, identity.raw_name().data(),
                std::min<size_t>(DeviceNetworkInfo::kMaxThreadNetworkNameLength,
                                 identity.raw_name().size()));
  }
  // Copy extended PAN id.
  if (identity.has_xpanid()) {
    std::memcpy(
        netInfo.ThreadExtendedPANId, identity.xpanid().data(),
        std::min<size_t>(DeviceNetworkInfo::kThreadExtendedPANIdLength, identity.xpanid().size()));
    netInfo.FieldPresent.ThreadExtendedPANId = true;
  }
  // Copy PAN id.
  if (!identity.has_panid()) {
    // Warn that PAN id remains unspecified.
    FX_LOGS(WARNING) << "PAN id not supplied.";
  } else {
    netInfo.ThreadPANId = identity.panid();
  }
  // Copy channel.
  if (!identity.has_channel() || identity.channel() < kMinThreadChannel ||
      identity.channel() > kMaxThreadChannel) {
    // Warn that channel remains unspecified.
    std::string channel_info =
        identity.has_channel() ? std::to_string(identity.channel()) : "(none)";
    FX_LOGS(WARNING) << "Invalid Thread channel: " << channel_info;
  } else {
    netInfo.ThreadChannel = identity.channel();
  }

  // TODO(fxbug.dev/55638) - Implement mesh prefix and pre-shared commisioning key.

  if (!includeCredentials) {
    // No futher processing needed, credentials won't be included.
    return WEAVE_NO_ERROR;
  }

  // Get credential.
  std::unique_ptr<Credential> credential;
  status = device_extra->GetCredential(&credential);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not retrieve credential: " << zx_status_get_string(status);
    return status;
  }

  // Copy credential info.
  if (!credential) {
    // Warn that credential remains unset.
    FX_LOGS(WARNING) << "Credential requested but no credential provided from LoWPAN device";
  } else {
    std::memcpy(netInfo.ThreadNetworkKey, credential->master_key().data(),
                std::min<size_t>(DeviceNetworkInfo::kMaxThreadNetworkNameLength,
                                 credential->master_key().size()));
    netInfo.FieldPresent.ThreadNetworkKey = true;
  }

  return WEAVE_NO_ERROR;
}

WEAVE_ERROR ThreadStackManagerDelegateImpl::SetThreadProvision(const DeviceNetworkInfo& netInfo) {
  DeviceSyncPtr device;
  std::unique_ptr<Credential> credential;
  Identity identity;

  if (!IsThreadSupported()) {
    return WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE;
  }

  // Cancel join operation, if active:
  StopThreadJoining();

  // Set up identity.
  std::vector<uint8_t> network_name{
      netInfo.ThreadNetworkName,
      netInfo.ThreadNetworkName + std::strlen(netInfo.ThreadNetworkName)};

  identity.set_raw_name(std::move(network_name));
  identity.set_net_type(fuchsia::lowpan::NET_TYPE_THREAD_1_X);

  if (netInfo.FieldPresent.ThreadExtendedPANId) {
    identity.set_xpanid(std::vector<uint8_t>{
        netInfo.ThreadExtendedPANId,
        netInfo.ThreadExtendedPANId + DeviceNetworkInfo::kThreadExtendedPANIdLength});
  } else {
    FX_LOGS(ERROR) << "No XPAN ID provided to SetThreadProvision.";
    return WEAVE_ERROR_INVALID_ARGUMENT;
  }

  if (netInfo.ThreadChannel != Profiles::NetworkProvisioning::kThreadChannel_NotSpecified) {
    identity.set_channel(netInfo.ThreadChannel);
  } else {
    FX_LOGS(ERROR) << "No channel provided to SetThreadProvision.";
    return WEAVE_ERROR_INVALID_ARGUMENT;
  }

  if (netInfo.ThreadPANId != Profiles::NetworkProvisioning::kThreadPANId_NotSpecified) {
    identity.set_panid(netInfo.ThreadPANId);
  } else {
    FX_LOGS(ERROR) << "No PAN ID provided to SetThreadProvision.";
    return WEAVE_ERROR_INVALID_ARGUMENT;
  }

  // Set up credential.
  if (netInfo.FieldPresent.ThreadNetworkKey) {
    credential = std::make_unique<Credential>();
    credential->set_master_key(std::vector<uint8_t>{
        netInfo.ThreadNetworkKey,
        netInfo.ThreadNetworkKey + DeviceNetworkInfo::kThreadNetworkKeyLength});
  } else {
    FX_LOGS(ERROR) << "No network key provided to SetThreadProvision.";
    return WEAVE_ERROR_INVALID_ARGUMENT;
  }

  // Add identity and credential to provisioning params.
  ProvisioningParams params{.identity = std::move(identity), .credential = std::move(credential)};

  // Acquire the thread device.
  zx_status_t status = GetDevice(device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to acquire LoWPAN device: " << zx_status_get_string(status);
    return status;
  }

  // Provision the thread device.
  status = device->ProvisionNetwork(std::move(params));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to provision network: " << zx_status_get_string(status);
    return status;
  }

  auto& inspector = WeaveInspector::GetWeaveInspector();
  inspector.NotifyPairingStateChange(WeaveInspector::kPairingState_ThreadNetworkCreatedOrJoined);
  return WEAVE_NO_ERROR;
}

void ThreadStackManagerDelegateImpl::ClearThreadProvision() {
  DeviceSyncPtr device;
  zx_status_t status;

  if (!IsThreadSupported()) {
    return;
  }

  // Cancel join operation, if active:
  StopThreadJoining();

  // Acquire the thread device.
  status = GetDevice(device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to acquire LoWPAN device: " << zx_status_get_string(status);
    return;
  }

  status = device->LeaveNetwork();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not clear LoWPAN provision: " << zx_status_get_string(status);
  }
}

ThreadDeviceType ThreadStackManagerDelegateImpl::GetThreadDeviceType() {
  DeviceState device_state;

  // Get the device state.
  if (GetDeviceState(device_state) != ZX_OK || !IsThreadSupported()) {
    return ThreadDeviceType::kThreadDeviceType_NotSupported;
  }

  // Determine device type by role.
  switch (device_state.role()) {
    case Role::END_DEVICE:
      return ThreadDeviceType::kThreadDeviceType_FullEndDevice;
    case Role::SLEEPY_END_DEVICE:
      return ThreadDeviceType::kThreadDeviceType_SleepyEndDevice;
    case Role::ROUTER:
    case Role::SLEEPY_ROUTER:
    case Role::LEADER:
    case Role::COORDINATOR:
      return ThreadDeviceType::kThreadDeviceType_Router;
    default:
      return ThreadDeviceType::kThreadDeviceType_NotSupported;
  };
}

bool ThreadStackManagerDelegateImpl::HaveMeshConnectivity() { return IsThreadAttached(); }

WEAVE_ERROR ThreadStackManagerDelegateImpl::GetAndLogThreadStatsCounters() {
  nl::Weave::Profiles::DataManagement_Current::event_id_t event_id;
  TelemetryNetworkWpanTrait::NetworkWpanStatsEvent counter_event = {};
  zx_status_t status;

  // Get LoWPAN protocols.
  CountersSyncPtr counters;
  DeviceExtraSyncPtr device_extra;
  status = GetProtocols(std::move(
      Protocols().set_counters(counters.NewRequest()).set_device_extra(device_extra.NewRequest())));
  if (status != ZX_OK) {
    return status;
  }

  // Get MAC counters.
  AllCounters all_counters;
  status = counters->Get(&all_counters);
  if (status != ZX_OK) {
    return status;
  }

  // MAC TX counters.
  if (all_counters.has_mac_tx()) {
    const MacCounters& mac_tx = all_counters.mac_tx();

    counter_event.phyTx = mac_tx.total();
    counter_event.macUnicastTx = mac_tx.unicast();
    counter_event.macBroadcastTx = mac_tx.broadcast();
    counter_event.macTxAckReq = mac_tx.ack_requested();
    counter_event.macTxNoAckReq = mac_tx.no_ack_requested();
    counter_event.macTxAcked = mac_tx.acked();
    counter_event.macTxData = mac_tx.data();
    counter_event.macTxDataPoll = mac_tx.data_poll();
    counter_event.macTxBeacon = mac_tx.beacon();
    counter_event.macTxBeaconReq = mac_tx.beacon_request();
    counter_event.macTxOtherPkt = mac_tx.other();
    counter_event.macTxRetry = mac_tx.retries();

    counter_event.macTxFailCca = mac_tx.err_cca();
  }

  // MAC RX counters.
  if (all_counters.has_mac_rx()) {
    const MacCounters& mac_rx = all_counters.mac_rx();

    counter_event.phyRx = mac_rx.total();
    counter_event.macUnicastRx = mac_rx.unicast();
    counter_event.macBroadcastRx = mac_rx.broadcast();
    counter_event.macRxData = mac_rx.data();
    counter_event.macRxDataPoll = mac_rx.data_poll();
    counter_event.macRxBeacon = mac_rx.beacon();
    counter_event.macRxBeaconReq = mac_rx.beacon_request();
    counter_event.macRxOtherPkt = mac_rx.other();
    counter_event.macRxFilterWhitelist = mac_rx.address_filtered();
    counter_event.macRxFilterDestAddr = mac_rx.dest_addr_filtered();

    // Rx Error Counters
    counter_event.macRxFailDecrypt = mac_rx.err_sec();
    counter_event.macRxFailNoFrame = mac_rx.err_no_frame();
    counter_event.macRxFailUnknownNeighbor = mac_rx.err_unknown_neighbor();
    counter_event.macRxFailInvalidSrcAddr = mac_rx.err_invalid_src_addr();
    counter_event.macRxFailFcs = mac_rx.err_fcs();
    counter_event.macRxFailOther = mac_rx.err_other();
  }

  // Thread channel.
  Identity identity;
  status = device_extra->WatchIdentity(&identity);
  if (status != ZX_OK) {
    return status;
  }
  if (identity.has_channel()) {
    counter_event.channel = identity.channel();
  }

  // Node type.
  DeviceState device_state;
  status = GetDeviceState(device_state);
  if (status != ZX_OK) {
    return status;
  }

  switch (device_state.role()) {
    case Role::LEADER:
      counter_event.nodeType |= TelemetryNetworkWpanTrait::NODE_TYPE_LEADER;
      // Fallthrough intentional, leaders are also routers.
      __FALLTHROUGH;
    case Role::ROUTER:
    case Role::SLEEPY_ROUTER:
    case Role::COORDINATOR:
      counter_event.nodeType |= TelemetryNetworkWpanTrait::NODE_TYPE_ROUTER;
      break;
    default:
      counter_event.nodeType = 0;
      break;
  }

  FX_LOGS(DEBUG) << "Rx Counters:\n"
                 << "  PHY Rx Total:            " << counter_event.phyRx << "\n"
                 << "  MAC Rx Unicast:          " << counter_event.macUnicastRx << "\n"
                 << "  MAC Rx Broadcast:        " << counter_event.macBroadcastRx << "\n"
                 << "  MAC Rx Data:             " << counter_event.macRxData << "\n"
                 << "  MAC Rx Data Polls:       " << counter_event.macRxDataPoll << "\n"
                 << "  MAC Rx Beacons:          " << counter_event.macRxBeacon << "\n"
                 << "  MAC Rx Beacon Reqs:      " << counter_event.macRxBeaconReq << "\n"
                 << "  MAC Rx Other:            " << counter_event.macRxOtherPkt << "\n"
                 << "  MAC Rx Filtered List:    " << counter_event.macRxFilterWhitelist << "\n"
                 << "  MAC Rx Filtered Addr:    " << counter_event.macRxFilterDestAddr << "\n"
                 << "\n"
                 << "Tx Counters:\n"
                 << "  PHY Tx Total:            " << counter_event.phyTx << "\n"
                 << "  MAC Tx Unicast:          " << counter_event.macUnicastTx << "\n"
                 << "  MAC Tx Broadcast:        " << counter_event.macBroadcastTx << "\n"
                 << "  MAC Tx Data:             " << counter_event.macTxData << "\n"
                 << "  MAC Tx Data Polls:       " << counter_event.macTxDataPoll << "\n"
                 << "  MAC Tx Beacons:          " << counter_event.macTxBeacon << "\n"
                 << "  MAC Tx Beacon Reqs:      " << counter_event.macTxBeaconReq << "\n"
                 << "  MAC Tx Other:            " << counter_event.macTxOtherPkt << "\n"
                 << "  MAC Tx Retry:            " << counter_event.macTxRetry << "\n"
                 << "  MAC Tx CCA Fail:         " << counter_event.macTxFailCca << "\n"
                 << "\n"
                 << "Failure Counters:\n"
                 << "  MAC Rx Decrypt Fail:     " << counter_event.macRxFailDecrypt << "\n"
                 << "  MAC Rx No Frame:         " << counter_event.macRxFailNoFrame << "\n"
                 << "  MAC Rx Unkwn Neighbor:   " << counter_event.macRxFailUnknownNeighbor << "\n"
                 << "  MAC Rx Invalid Src Addr: " << counter_event.macRxFailInvalidSrcAddr << "\n"
                 << "  MAC Rx FCS Fail:         " << counter_event.macRxFailFcs << "\n"
                 << "  MAC Rx Other Fail:       " << counter_event.macRxFailOther << "\n";

  event_id = LogNetworkWpanStatsEvent(&counter_event);
  FX_LOGS(DEBUG) << "Thread telemetry stats event ID: " << event_id << ".";

  return status;
}

WEAVE_ERROR ThreadStackManagerDelegateImpl::GetAndLogThreadTopologyMinimal() {
  return WEAVE_ERROR_NOT_IMPLEMENTED;  // TODO(fxbug.dev/55888)
}

WEAVE_ERROR ThreadStackManagerDelegateImpl::GetAndLogThreadTopologyFull() {
  return WEAVE_ERROR_NOT_IMPLEMENTED;  // TODO(fxbug.dev/55888)
}

WEAVE_ERROR ThreadStackManagerDelegateImpl::GetPrimary802154MACAddress(uint8_t* mac_address) {
  if (!IsThreadSupported()) {
    return WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE;
  }

  // This is setting the MAC address to FF:0:0:0:0:0:0:0; this is for a few reasons:
  //   1. The actual value of the MAC address in the descriptor is not currently used.
  //   2. The MAC address (either the factory or the current address) is PII, so it should not be
  //      transmitted unless necessary.
  //   3. Some value should still be transmitted as some tools or other devices use the presence of
  //      an 802.15.4 MAC address to determine if Thread is supported.
  // The best way to meet these requirements is to provide a faked-out MAC address instead.
  std::memcpy(mac_address, kFakeMacAddress, k802154MacAddressBufSize);
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR ThreadStackManagerDelegateImpl::SetThreadJoinable(bool enable) {
  LegacyJoiningSyncPtr thread_legacy;
  zx_status_t status;

  // Cancel join operation, if active:
  StopThreadJoining();

  // Get the legacy Thread protocol
  status =
      GetProtocols(std::move(Protocols().set_thread_legacy_joining(thread_legacy.NewRequest())));
  if (status != ZX_OK) {
    return status;
  }

  // Check for configured duration.
  uint32_t duration_sec;
  zx::duration duration;
  WEAVE_ERROR err = ConfigurationMgrImpl().GetThreadJoinableDuration(&duration_sec);
  if (err == WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND) {
    FX_LOGS(INFO) << "No joinable duration specified, using default duration.";
    duration = kThreadJoinableDurationDefault;
  } else if (err == WEAVE_NO_ERROR) {
    duration = zx::duration{zx_duration_from_sec(duration_sec)};
  } else {
    FX_LOGS(ERROR) << "Error reading Thread joinable duration config value.";
    return err;
  }

  // Set joinable or disable joinable based on the intended value.
  if (enable) {
    FX_LOGS(INFO) << "Requesting Thread radio joinable state for " << duration.to_secs()
                  << " seconds.";
  } else {
    FX_LOGS(INFO) << "Requesting to disable Thread radio joinable state.";
  }
  status = thread_legacy->MakeJoinable(enable ? duration.get() : kThreadJoinableStop.get(),
                                       WEAVE_UNSECURED_PORT);
  if (status != ZX_OK) {
    return status;
  }

  // Confirm joinable state has been updated successfully.
  return WEAVE_NO_ERROR;
}

zx_status_t ThreadStackManagerDelegateImpl::GetDevice(DeviceSyncPtr& device) {
  return GetProtocols(std::move(Protocols().set_device(device.NewRequest())));
}

zx_status_t ThreadStackManagerDelegateImpl::GetDeviceState(DeviceState& device_state) {
  DeviceSyncPtr device;
  zx_status_t status;

  status = GetDevice(device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to acquire LoWPAN device: " << zx_status_get_string(status);
    return status;
  }

  status = device->WatchDeviceState(&device_state);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not get LoWPAN device state: " << zx_status_get_string(status);
    return status;
  }

  return ZX_OK;
}

zx_status_t ThreadStackManagerDelegateImpl::GetProtocols(Protocols protocols) {
  // See note at top to explain these SyncPtrs.
  LookupSyncPtr lookup;
  Lookup_LookupDevice_Result result;
  zx_status_t status;

  // Access the LoWPAN service.
  status = PlatformMgrImpl().GetComponentContextForProcess()->svc()->Connect(lookup.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to fuchsia.lowpan.device.Lookup: "
                   << zx_status_get_string(status);
    return status;
  }

  // Look up the device by interface name.
  status = lookup->LookupDevice(interface_name_, std::move(protocols), &result);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to lookup device: " << zx_status_get_string(status);
    return status;
  }
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LoWPAN service error during lookup: " << static_cast<int32_t>(result.err());
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

std::string ThreadStackManagerDelegateImpl::GetInterfaceName() const { return interface_name_; }

bool ThreadStackManagerDelegateImpl::IsThreadSupported() const { return is_thread_supported_; }

nl::Weave::Profiles::DataManagement::event_id_t
ThreadStackManagerDelegateImpl::LogNetworkWpanStatsEvent(
    Schema::Nest::Trait::Network::TelemetryNetworkWpanTrait::NetworkWpanStatsEvent* event) {
  return nl::LogEvent(event);
}

WEAVE_ERROR ThreadStackManagerDelegateImpl::StartThreadJoining() {
  WEAVE_ERROR err =
      StartJoiningTimeout(kJoinAtStartupTimeout.to_msecs(), [this] { HandleJoiningTimeout(); });
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << "Could not post timeout, cancelling join.";
    StopThreadJoining();
    return err;
  }

  return StartThreadJoiningIteration();
}

WEAVE_ERROR ThreadStackManagerDelegateImpl::StartThreadJoiningIteration() {
  JoinParams join_params;
  zx_status_t status = ZX_OK;
  DeviceExtraSyncPtr device;

  if (provisioning_monitor_) {
    FX_LOGS(ERROR) << "Already attempting to join.";
    return WEAVE_ERROR_INCORRECT_STATE;
  }

  status = GetProtocols(std::move(Protocols().set_device_extra(device.NewRequest())));
  if (status != ZX_OK || !device) {
    FX_LOGS(ERROR) << "Could not start Thread joining: Could not get DeviceExtra protocol.";
    return status;
  }

  WEAVE_ERROR err = GetJoinParams(join_params);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  FX_LOGS(INFO) << "Requesting Thread network join.";
  status = device->JoinNetwork(std::move(join_params), provisioning_monitor_.NewRequest());
  if (status != ZX_OK || !provisioning_monitor_.is_bound()) {
    FX_LOGS(ERROR) << "Could not begin join network attempt.";
    return status;
  }

  provisioning_monitor_->WatchProgress(
      fit::bind_member(this, &ThreadStackManagerDelegateImpl::HandleProvisioningProgress));

  return WEAVE_NO_ERROR;
}

void ThreadStackManagerDelegateImpl::StopThreadJoining() {
  FX_LOGS(INFO) << "Stopping Thread network joining.";
  provisioning_monitor_.Unbind();
  CancelJoiningTimeout();
  CancelJoiningRetry();
}

void ThreadStackManagerDelegateImpl::CancelJoiningTimeout() { joining_timeout_.Cancel(); }

void ThreadStackManagerDelegateImpl::CancelJoiningRetry() { joining_retry_delay_.Cancel(); }

WEAVE_ERROR ThreadStackManagerDelegateImpl::StartJoiningTimeout(uint32_t delay_milliseconds,
                                                                fit::closure callback) {
  joining_timeout_.set_handler(std::move(callback));
  return joining_timeout_.PostDelayed(PlatformMgrImpl().GetDispatcher(),
                                      zx::msec(delay_milliseconds));
}

WEAVE_ERROR ThreadStackManagerDelegateImpl::StartJoiningRetry(uint32_t delay_milliseconds,
                                                              fit::closure callback) {
  joining_retry_delay_.set_handler(std::move(callback));
  return joining_retry_delay_.PostDelayed(PlatformMgrImpl().GetDispatcher(),
                                          zx::msec(delay_milliseconds));
}

WEAVE_ERROR ThreadStackManagerDelegateImpl::GetJoinParams(JoinParams& join_params) {
  WEAVE_ERROR err = WEAVE_NO_ERROR;
  JoinerCommissioningParams commissioning_params;
  size_t config_length_out;
  char config_buffer[kJoinInfoBufferSize];

  // Copy pairing code as PSKd.
  err = ConfigurationMgr().GetPairingCode(config_buffer, kJoinInfoBufferSize, config_length_out);
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << " Failed to get pairing code: " << nl::ErrorStr(err) << ".";
    return err;
  }
  if (config_length_out > PSKD_LEN) {
    FX_LOGS(WARNING) << "PSKd was too long, truncating to " << PSKD_LEN << " bytes.";
    config_length_out = PSKD_LEN;
  }
  commissioning_params.set_pskd(std::string(config_buffer, config_length_out));

  // Copy vendor name.
  err = ConfigurationMgr().GetVendorIdDescription(config_buffer, kJoinInfoBufferSize,
                                                  config_length_out);
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << " Failed to get vendor description: " << nl::ErrorStr(err) << ".";
    return err;
  }
  if (config_length_out > MAX_VENDOR_NAME_LEN) {
    FX_LOGS(WARNING) << "Vendor name was too long, truncating to " << MAX_VENDOR_NAME_LEN << " bytes.";
    config_length_out = MAX_VENDOR_NAME_LEN;
  }
  commissioning_params.set_vendor_name(std::string(config_buffer, config_length_out));

  // Copy vendor model.
  err = ConfigurationMgr().GetProductIdDescription(config_buffer, kJoinInfoBufferSize,
                                                   config_length_out);
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << " Failed to get product description: " << nl::ErrorStr(err) << ".";
    return err;
  }
  if (config_length_out > MAX_VENDOR_MODEL_LEN) {
    FX_LOGS(WARNING) << "Vendor model was too long, truncating to " << MAX_VENDOR_MODEL_LEN << " bytes.";
    config_length_out = MAX_VENDOR_MODEL_LEN;
  }
  commissioning_params.set_vendor_model(std::string(config_buffer, config_length_out));

  // Copy vendor software version.
  err =
      ConfigurationMgr().GetFirmwareRevision(config_buffer, kJoinInfoBufferSize, config_length_out);
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << " Failed to get software version: " << nl::ErrorStr(err) << ".";
    return err;
  }
  if (config_length_out > MAX_VENDOR_SW_VER_LEN) {
    FX_LOGS(WARNING) << "Vendor SW version was too long, truncating to " << MAX_VENDOR_SW_VER_LEN << " bytes.";
    config_length_out = MAX_VENDOR_SW_VER_LEN;
  }
  commissioning_params.set_vendor_sw_version(std::string(config_buffer, config_length_out));

  join_params.set_joiner_parameter(std::move(commissioning_params));
  return WEAVE_NO_ERROR;
}

void ThreadStackManagerDelegateImpl::HandleJoiningTimeout() {
  joining_timeout_expired_ = true;
  if (!joining_in_progress_) {
    FX_LOGS(INFO) << "Thread joining attempt timed out, stopping join.";
    StopThreadJoining();
  } else {
    FX_LOGS(INFO) << "Thread joining timeout occurred, but not stoping in-progress join.";
  }
}

void ThreadStackManagerDelegateImpl::HandleJoiningRetryDelay() {
  if (!joining_timeout_expired_) {
    FX_LOGS(INFO) << "Retrying Thread network join.";
    StartThreadJoiningIteration();
  } else {
    FX_LOGS(INFO) << "Thread joining timeout occured before retry.";
  }
}

void ThreadStackManagerDelegateImpl::HandleProvisioningProgress(
    fuchsia::lowpan::device::ProvisioningMonitor_WatchProgress_Result result) {
  if (result.is_err() && result.err() != ProvisionError::CANCELED) {
    joining_in_progress_ = false;
    provisioning_monitor_.Unbind();
    // Join failed but not cancelled, delay and retry.
    if (!joining_timeout_expired_) {
      FX_LOGS(INFO) << "Did not join network, waiting before retry.";
      StartJoiningRetry(kJoinAtStartupRetryDelay.to_msecs(), [this] { HandleJoiningRetryDelay(); });
    }
    return;
  }

  if (result.is_err() && result.err() == ProvisionError::CANCELED) {
    FX_LOGS(INFO) << "Join operation canceled.";
    StopThreadJoining();
    return;
  }

  if (result.response().progress.is_progress()) {
    if (!joining_in_progress_) {
      joining_in_progress_ = true;
      FX_LOGS(INFO) << "Thread joining in progress.";
    }
    provisioning_monitor_->WatchProgress(
        fit::bind_member(this, &ThreadStackManagerDelegateImpl::HandleProvisioningProgress));
    return;
  }

  if (result.response().progress.is_identity()) {
    FX_LOGS(INFO) << "Thread joining attempt completed.";
    StopThreadJoining();
  }
}

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
