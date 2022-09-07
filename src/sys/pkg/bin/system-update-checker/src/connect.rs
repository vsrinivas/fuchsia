// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::DiscoverableProtocolMarker;
use fuchsia_component::client::connect_to_protocol;

pub trait ServiceConnect: Send + Sync {
    fn connect_to_service<P: DiscoverableProtocolMarker>(&self) -> Result<P::Proxy, Error>;
}

#[derive(Debug, Clone)]
pub struct ServiceConnector;

impl ServiceConnect for ServiceConnector {
    fn connect_to_service<P: DiscoverableProtocolMarker>(&self) -> Result<P::Proxy, Error> {
        connect_to_protocol::<P>()
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use anyhow::{format_err, Context};
    use fuchsia_component::client::connect_to_protocol_at;
    use fuchsia_zircon as zx;

    #[derive(Debug, Clone)]
    pub struct NamespacedServiceConnector(String);

    impl NamespacedServiceConnector {
        pub fn bind(path: impl Into<String>) -> Result<(Self, zx::Channel), Error> {
            let path = path.into();

            let ns = fdio::Namespace::installed().context("installed namespace")?;
            let (service_channel, server_end) = zx::Channel::create().context("create channel")?;
            ns.bind(path.as_str(), service_channel).context("bind svc")?;

            Ok((NamespacedServiceConnector(path), server_end))
        }
    }

    impl ServiceConnect for NamespacedServiceConnector {
        fn connect_to_service<P: DiscoverableProtocolMarker>(&self) -> Result<P::Proxy, Error> {
            connect_to_protocol_at::<P>(self.0.as_str())
        }
    }

    #[derive(Debug, Clone)]
    pub struct FailServiceConnector;

    impl ServiceConnect for FailServiceConnector {
        fn connect_to_service<P: DiscoverableProtocolMarker>(&self) -> Result<P::Proxy, Error> {
            return Err(format_err!("no services here"));
        }
    }
}

#[cfg(test)]
pub(crate) use test::*;
