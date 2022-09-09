// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::convert_ext::*;
use crate::prelude::*;
use lowpan_driver_common::lowpan_fidl::*;
use std::num::NonZeroU8;

impl FromExt<ot::JoinerState> for ProvisioningProgress {
    fn from_ext(x: ot::JoinerState) -> Self {
        // Note that this mapping is somewhat arbitrary. The values
        // are intended to be used by a user interface to display a
        // connection progress bar.
        match x {
            ot::JoinerState::Idle => ProvisioningProgress::Progress(0.0),
            ot::JoinerState::Discover => ProvisioningProgress::Progress(0.2),
            ot::JoinerState::Connect => ProvisioningProgress::Progress(0.4),
            ot::JoinerState::Connected => ProvisioningProgress::Progress(0.6),
            ot::JoinerState::Entrust => ProvisioningProgress::Progress(0.8),
            ot::JoinerState::Joined => ProvisioningProgress::Progress(1.0),
        }
    }
}

impl FromExt<ot::Error> for ProvisionError {
    fn from_ext(x: ot::Error) -> Self {
        match x {
            ot::Error::Security => ProvisionError::CredentialRejected,
            ot::Error::NotFound => ProvisionError::NetworkNotFound,
            ot::Error::ResponseTimeout => ProvisionError::NetworkNotFound,
            ot::Error::Abort => ProvisionError::Canceled,
            x => {
                warn!("Unexpected error when joining: {:?}", x);
                ProvisionError::Canceled
            }
        }
    }
}

impl FromExt<ot::BorderRouterConfig> for OnMeshPrefix {
    fn from_ext(x: ot::BorderRouterConfig) -> Self {
        OnMeshPrefix {
            subnet: Some(fidl_fuchsia_net::Ipv6AddressWithPrefix {
                addr: fidl_fuchsia_net::Ipv6Address { addr: x.prefix().addr().octets() },
                prefix_len: x.prefix().prefix_len(),
            }),
            default_route_preference: x.default_route_preference().map(|x| x.into_ext()),
            stable: Some(x.is_stable()),
            slaac_preferred: Some(x.is_preferred()),
            slaac_valid: Some(x.is_slaac()),
            ..OnMeshPrefix::EMPTY
        }
    }
}

impl FromExt<ot::ExternalRouteConfig> for ExternalRoute {
    fn from_ext(x: ot::ExternalRouteConfig) -> Self {
        ExternalRoute {
            subnet: Some(fidl_fuchsia_net::Ipv6AddressWithPrefix {
                addr: fidl_fuchsia_net::Ipv6Address { addr: x.prefix().addr().octets() },
                prefix_len: x.prefix().prefix_len(),
            }),
            route_preference: None,
            stable: Some(x.is_stable()),
            ..ExternalRoute::EMPTY
        }
    }
}

impl FromExt<lowpan_driver_common::lowpan_fidl::RoutePreference> for ot::RoutePreference {
    fn from_ext(x: lowpan_driver_common::lowpan_fidl::RoutePreference) -> Self {
        match x {
            lowpan_driver_common::lowpan_fidl::RoutePreference::Low => ot::RoutePreference::Low,
            lowpan_driver_common::lowpan_fidl::RoutePreference::Medium => {
                ot::RoutePreference::Medium
            }
            lowpan_driver_common::lowpan_fidl::RoutePreference::High => ot::RoutePreference::High,
        }
    }
}

impl FromExt<ot::RoutePreference> for lowpan_driver_common::lowpan_fidl::RoutePreference {
    fn from_ext(x: ot::RoutePreference) -> Self {
        match x {
            ot::RoutePreference::Low => lowpan_driver_common::lowpan_fidl::RoutePreference::Low,
            ot::RoutePreference::Medium => {
                lowpan_driver_common::lowpan_fidl::RoutePreference::Medium
            }
            ot::RoutePreference::High => lowpan_driver_common::lowpan_fidl::RoutePreference::High,
        }
    }
}

impl FromExt<ot::ActiveScanResult> for BeaconInfo {
    fn from_ext(x: ot::ActiveScanResult) -> Self {
        BeaconInfo {
            identity: Some(Identity {
                raw_name: if x.network_name().len() != 0 {
                    Some(x.network_name().to_vec())
                } else {
                    None
                },
                channel: Some(x.channel().into()),
                panid: Some(x.pan_id()),
                xpanid: Some(x.extended_pan_id().to_vec()),
                ..Identity::EMPTY
            }),
            rssi: Some(x.rssi()),
            lqi: NonZeroU8::new(x.lqi()).map(NonZeroU8::get),
            address: Some(MacAddress { octets: x.ext_address().into_array() }),
            ..BeaconInfo::EMPTY
        }
    }
}

impl FromExt<&ot::OperationalDataset> for Identity {
    fn from_ext(operational_dataset: &ot::OperationalDataset) -> Self {
        Identity {
            raw_name: operational_dataset.get_network_name().map(ot::NetworkName::to_vec),
            xpanid: operational_dataset.get_extended_pan_id().map(ot::ExtendedPanId::to_vec),
            net_type: Some(NET_TYPE_THREAD_1_X.to_string()),
            channel: operational_dataset.get_channel().map(|x| x as u16),
            panid: operational_dataset.get_pan_id(),
            mesh_local_prefix: operational_dataset.get_mesh_local_prefix().copied().map(|x| {
                fidl_fuchsia_net::Ipv6AddressWithPrefix {
                    addr: x.into(),
                    prefix_len: ot::IP6_PREFIX_BITSIZE,
                }
            }),
            ..Identity::EMPTY
        }
    }
}

impl FromExt<ot::OperationalDataset> for Identity {
    fn from_ext(f: ot::OperationalDataset) -> Self {
        FromExt::<&ot::OperationalDataset>::from_ext(&f)
    }
}

pub trait UpdateOperationalDataset<T> {
    fn update_from(&mut self, data: &T) -> Result<(), anyhow::Error>;
}

impl UpdateOperationalDataset<ProvisioningParams> for ot::OperationalDataset {
    fn update_from(&mut self, params: &ProvisioningParams) -> Result<(), anyhow::Error> {
        self.update_from(&params.identity)?;
        if let Some(cred) = params.credential.as_ref() {
            self.update_from(cred.as_ref())?
        }
        Ok(())
    }
}

impl UpdateOperationalDataset<Identity> for ot::OperationalDataset {
    fn update_from(&mut self, ident: &Identity) -> Result<(), anyhow::Error> {
        if ident.channel.is_some() {
            self.set_channel(ident.channel.map(|x| x.try_into().unwrap()));
        }
        if ident.panid.is_some() {
            self.set_pan_id(ident.panid)
        }
        if ident.xpanid.is_some() {
            self.set_extended_pan_id(
                ident
                    .xpanid
                    .as_ref()
                    .map(|v| ot::ExtendedPanId::try_ref_from_slice(v.as_slice()))
                    .transpose()?,
            );
        }
        if ident.raw_name.is_some() {
            self.set_network_name(
                ident
                    .raw_name
                    .as_ref()
                    .map(|n| ot::NetworkName::try_from_slice(n.as_slice()))
                    .transpose()?
                    .as_ref(),
            )
        }
        if ident.mesh_local_prefix.is_some() {
            self.set_mesh_local_prefix(
                ident
                    .mesh_local_prefix
                    .map(|x| std::net::Ipv6Addr::from(x.addr.addr))
                    .map(ot::MeshLocalPrefix::from)
                    .as_ref(),
            )
        }
        Ok(())
    }
}

impl UpdateOperationalDataset<Credential> for ot::OperationalDataset {
    fn update_from(&mut self, cred: &Credential) -> Result<(), anyhow::Error> {
        match cred {
            Credential::NetworkKey(key) => {
                self.set_network_key(Some(ot::NetworkKey::try_ref_from_slice(key.as_slice())?))
            }
            _ => Err(format_err!("Unknown credential type"))?,
        }
        Ok(())
    }
}

pub trait AllCountersUpdate<T> {
    fn update_from(&mut self, data: &T);
}

impl AllCountersUpdate<ot::MacCounters> for AllCounters {
    fn update_from(&mut self, data: &ot::MacCounters) {
        self.mac_tx = Some(MacCounters {
            total: Some(data.tx_total()),
            unicast: Some(data.tx_unicast()),
            broadcast: Some(data.tx_broadcast()),
            ack_requested: Some(data.tx_ack_requested()),
            acked: Some(data.tx_acked()),
            no_ack_requested: Some(data.tx_no_ack_requested()),
            data: Some(data.tx_data()),
            data_poll: Some(data.tx_data_poll()),
            beacon: Some(data.tx_beacon()),
            beacon_request: Some(data.tx_beacon_request()),
            other: Some(data.tx_other()),
            retries: Some(data.tx_retry()),
            direct_max_retry_expiry: Some(data.tx_direct_max_retry_expiry()),
            indirect_max_retry_expiry: Some(data.tx_indirect_max_retry_expiry()),
            err_cca: Some(data.tx_err_cca()),
            err_abort: Some(data.tx_err_abort()),
            err_busy_channel: Some(data.tx_err_busy_channel()),
            ..MacCounters::EMPTY
        });
        self.mac_rx = Some(MacCounters {
            total: Some(data.rx_total()),
            unicast: Some(data.rx_unicast()),
            broadcast: Some(data.rx_broadcast()),
            data: Some(data.rx_data()),
            data_poll: Some(data.rx_data_poll()),
            beacon: Some(data.rx_beacon()),
            beacon_request: Some(data.rx_beacon_request()),
            other: Some(data.rx_other()),
            address_filtered: Some(data.rx_address_filtered()),
            dest_addr_filtered: Some(data.rx_dest_addr_filtered()),
            duplicated: Some(data.rx_duplicated()),
            err_no_frame: Some(data.rx_err_no_frame()),
            err_unknown_neighbor: Some(data.rx_err_unknown_neighbor()),
            err_invalid_src_addr: Some(data.rx_err_invalid_src_addr()),
            err_sec: Some(data.rx_err_sec()),
            err_fcs: Some(data.rx_err_fcs()),
            err_other: Some(data.rx_err_other()),
            ..MacCounters::EMPTY
        });
    }
}

impl AllCountersUpdate<ot::RadioCoexMetrics> for AllCounters {
    fn update_from(&mut self, data: &ot::RadioCoexMetrics) {
        self.coex_tx = Some(CoexCounters {
            requests: Some(data.num_tx_request().into()),
            grant_immediate: Some(data.num_tx_grant_immediate().into()),
            grant_wait: Some(data.num_tx_grant_wait().into()),
            grant_wait_activated: Some(data.num_tx_grant_wait_activated().into()),
            grant_wait_timeout: Some(data.num_tx_grant_wait_timeout().into()),
            grant_deactivated_during_request: Some(
                data.num_tx_grant_deactivated_during_request().into(),
            ),
            delayed_grant: Some(data.num_tx_delayed_grant().into()),
            avg_delay_request_to_grant_usec: Some(data.avg_tx_request_to_grant_time()),
            ..CoexCounters::EMPTY
        });
        self.coex_rx = Some(CoexCounters {
            requests: Some(data.num_tx_request().into()),
            grant_immediate: Some(data.num_rx_grant_immediate().into()),
            grant_wait: Some(data.num_rx_grant_wait().into()),
            grant_wait_activated: Some(data.num_rx_grant_wait_activated().into()),
            grant_wait_timeout: Some(data.num_rx_grant_wait_timeout().into()),
            grant_deactivated_during_request: Some(
                data.num_rx_grant_deactivated_during_request().into(),
            ),
            delayed_grant: Some(data.num_rx_delayed_grant().into()),
            avg_delay_request_to_grant_usec: Some(data.avg_rx_request_to_grant_time()),
            grant_none: Some(data.num_rx_grant_none().into()),
            ..CoexCounters::EMPTY
        });
        if self.coex_saturated != Some(true) {
            self.coex_saturated = Some(data.stopped());
        }
    }
}

impl AllCountersUpdate<ot::IpCounters> for AllCounters {
    fn update_from(&mut self, data: &ot::IpCounters) {
        self.ip_tx = Some(IpCounters {
            success: Some(data.tx_success()),
            failure: Some(data.tx_failure()),
            ..IpCounters::EMPTY
        });
        self.ip_rx = Some(IpCounters {
            success: Some(data.rx_success()),
            failure: Some(data.rx_failure()),
            ..IpCounters::EMPTY
        });
    }
}
