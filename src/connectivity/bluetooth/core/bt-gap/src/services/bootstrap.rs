// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_sys::{self as sys, BootstrapRequest, BootstrapRequestStream},
    fuchsia_bluetooth::types::{BondingData, Identity, PeerId},
    futures::prelude::*,
    std::{convert::TryFrom, iter, mem},
};

use crate::host_dispatcher::*;

/// A session for a particular Boostrap protocol client we are curently connected to
#[derive(Default)]
struct BootstrapSession {
    identities: Vec<Identity>,
}

impl BootstrapSession {
    fn take_identities(&mut self) -> Vec<Identity> {
        mem::replace(&mut self.identities, vec![])
    }

    /// Handle an incoming request from a client of the Bootstrap Protocol
    async fn handle_request(
        &mut self,
        hd: HostDispatcher,
        event: BootstrapRequest,
    ) -> Result<(), Error> {
        match event {
            BootstrapRequest::Commit { responder } => {
                let identities = self.take_identities();
                let mut result = hd
                    .commit_bootstrap(identities)
                    .await
                    .map_err(|_| sys::BootstrapError::WriteFailure);
                responder.send(&mut result).map_err(Into::into)
            }
            BootstrapRequest::AddIdentities { identities, control_handle: _ } => {
                // Accumulate identities locally; Only push to HostDispatcher once `commit()` is
                // received
                let identities =
                    identities.into_iter().map(validate).collect::<Result<Vec<_>, _>>()?;
                for ident in identities {
                    self.identities.push(ident);
                }
                Ok(())
            }
        }
    }
}

/// Run a server for a bootstrap protocol service request
pub async fn run(hd: HostDispatcher, mut stream: BootstrapRequestStream) -> Result<(), Error> {
    let mut session = BootstrapSession::default();

    while let Some(event) = stream.try_next().await? {
        session.handle_request(hd.clone(), event).await?;
    }
    Ok(())
}

/// Parse and validate the table, which might have missing fields. Will only succeed if all bonds
/// are valid. This ensures operations are atomic. Since peers being bootstrapped may not have
/// Fuchsia Bluetooth assigned unique PeerIds, we provide a generator of random ids here as a
/// backup source. This is because internally all Peers must have a unique fuchsia-assigned PeerId.
fn validate(src: sys::Identity) -> Result<Identity, Error> {
    let host = src.host.ok_or(format_err!("Identity is missing the .host field"))?;
    // A generated sequence of PeerIds to be used if any bonding data are missing Fuchsia
    // Identifiers
    let generate_random_ids = iter::repeat_with(PeerId::random);
    let bonds = src.bonds.unwrap_or(vec![]);
    // We'll fail if any BondingData is missing required fields - I think this is better than
    // silently dropping individual bonding datas
    let bonds = bonds
        .into_iter()
        .zip(generate_random_ids)
        .map(BondingData::try_from)
        .collect::<Result<Vec<_>, _>>()?;
    Ok(Identity { host: host.into(), bonds })
}
