// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::convert_ext::*;
use crate::prelude::*;
use fidl_fuchsia_lowpan::*;
use fidl_fuchsia_lowpan_device::{
    ExternalRoute, OnMeshPrefix, ProvisionError, ProvisioningProgress,
};

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
            subnet: Some(Ipv6Subnet {
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
            subnet: Some(Ipv6Subnet {
                addr: fidl_fuchsia_net::Ipv6Address { addr: x.prefix().addr().octets() },
                prefix_len: x.prefix().prefix_len(),
            }),
            route_preference: None,
            stable: Some(x.is_stable()),
            ..ExternalRoute::EMPTY
        }
    }
}

impl FromExt<fidl_fuchsia_lowpan_device::RoutePreference> for ot::RoutePreference {
    fn from_ext(x: fidl_fuchsia_lowpan_device::RoutePreference) -> Self {
        match x {
            fidl_fuchsia_lowpan_device::RoutePreference::Low => ot::RoutePreference::Low,
            fidl_fuchsia_lowpan_device::RoutePreference::Medium => ot::RoutePreference::Medium,
            fidl_fuchsia_lowpan_device::RoutePreference::High => ot::RoutePreference::High,
        }
    }
}

impl FromExt<ot::RoutePreference> for fidl_fuchsia_lowpan_device::RoutePreference {
    fn from_ext(x: ot::RoutePreference) -> Self {
        match x {
            ot::RoutePreference::Low => fidl_fuchsia_lowpan_device::RoutePreference::Low,
            ot::RoutePreference::Medium => fidl_fuchsia_lowpan_device::RoutePreference::Medium,
            ot::RoutePreference::High => fidl_fuchsia_lowpan_device::RoutePreference::High,
        }
    }
}

impl FromExt<ot::ActiveScanResult> for BeaconInfo {
    fn from_ext(x: ot::ActiveScanResult) -> Self {
        BeaconInfo {
            identity: Identity {
                raw_name: if x.network_name().len() != 0 {
                    Some(x.network_name().to_vec())
                } else {
                    None
                },
                channel: Some(x.channel().into()),
                panid: Some(x.pan_id()),
                xpanid: Some(x.extended_pan_id().to_vec()),
                ..Identity::EMPTY
            },
            rssi: x.rssi().into(),
            lqi: x.lqi(),
            address: x.ext_address().to_vec(),
            flags: vec![],
        }
    }
}

impl FromExt<&ot::OperationalDataset> for Identity {
    fn from_ext(operational_dataset: &ot::OperationalDataset) -> Self {
        Identity {
            raw_name: operational_dataset.get_network_name().map(ot::NetworkName::to_vec),
            xpanid: operational_dataset.get_extended_pan_id().map(ot::ExtendedPanId::to_vec),
            net_type: Some(fidl_fuchsia_lowpan::NET_TYPE_THREAD_1_X.to_string()),
            channel: operational_dataset.get_channel().map(|x| x as u16),
            panid: operational_dataset.get_pan_id(),
            mesh_local_prefix: operational_dataset
                .get_mesh_local_prefix()
                .copied()
                .map(fidl_fuchsia_net::Ipv6Address::from),
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
                    .clone()
                    .map(|x| std::net::Ipv6Addr::from(x.addr))
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
            Credential::MasterKey(key) => {
                self.set_network_key(Some(ot::NetworkKey::try_ref_from_slice(key.as_slice())?))
            }
            _ => Err(format_err!("Unknown credential type"))?,
        }
        Ok(())
    }
}
