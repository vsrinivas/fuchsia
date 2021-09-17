// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{DiscoverableProtocolMarker, Proxy, ServiceMarker},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    std::fmt,
};

enum ExposedCapabilities {
    /// v1 component App
    App(fuchsia_component::client::App),
    /// v2 component exposed capabilities directory
    Directory(zx::Channel),
}

impl fmt::Debug for ExposedCapabilities {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ExposedCapabilities::App(_) => write!(f, "CFv1 App"),
            ExposedCapabilities::Directory(_) => write!(f, "CFv2 exposed capabilities Directory"),
        }
    }
}

/// Represents a component launched by an Element Manager.
///
/// The component can be either a v1 component launched by the fuchsia.sys.Launcher, or a v2
/// component launched as a child of the Element Manager's realm.
///
/// The Element can be used to connect to services exposed by the underlying v1 or v2 component.
#[derive(Debug)]
pub struct Element {
    /// CF v1 or v2 object that manages a `Directory` request channel for requesting services
    /// exposed by the component.
    exposed_capabilities: ExposedCapabilities,

    /// The component URL used to launch the component.
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    url: String,

    /// v2 component child name, or empty string if not a child of the realm (such as a CFv1
    /// component).
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    name: String,

    /// v2 component child collection name or empty string if not a child of the realm (such as a
    /// CFv1 component).
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    collection: String,
}

/// A component launched in response to `ElementManager::ProposeElement()`.
///
/// A session uses `ElementManager` to launch and return the Element, and can then use the Element
/// to connect to exposed capabilities.
///
/// An Element composes either a CFv2 component (launched as a child of the `ElementManager`'s
/// realm) or a CFv1 component (launched via a fuchsia::sys::Launcher).
impl Element {
    /// Creates an Element from a `fuchsia_component::client::App`.
    ///
    /// # Parameters
    /// - `url`: The launched component URL.
    /// - `app`: The v1 component wrapped in an App, returned by the launch function.
    pub fn from_app(app: fuchsia_component::client::App, url: &str) -> Element {
        Element {
            exposed_capabilities: ExposedCapabilities::App(app),
            url: url.to_string(),
            name: "".to_string(),
            collection: "".to_string(),
        }
    }

    /// Creates an Element from a component's exposed capabilities directory.
    ///
    /// # Parameters
    /// - `directory_channel`: A channel to the component's `Directory` of exposed capabilities.
    /// - `name`: The launched component's name.
    /// - `url`: The launched component URL.
    /// - `collection`: The launched component's collection name.
    pub fn from_directory_channel(
        directory_channel: zx::Channel,
        name: &str,
        url: &str,
        collection: &str,
    ) -> Element {
        Element {
            exposed_capabilities: ExposedCapabilities::Directory(directory_channel),
            url: url.to_string(),
            name: name.to_string(),
            collection: collection.to_string(),
        }
    }

    // # Note
    //
    // The methods below are copied from fuchsia_component::client::App in order to offer
    // services in the same way, but from any `Element`, wrapping either a v1 `App` or a v2
    // component's `Directory` of exposed services.

    /// Returns a reference to the component's `Directory` of exposed capabilities. A session can
    /// request services, and/or other capabilities, from the Element, using this channel.
    ///
    /// # Returns
    /// A `channel` to the component's `Directory` of exposed capabilities.
    #[inline]
    pub fn directory_channel(&self) -> &zx::Channel {
        match &self.exposed_capabilities {
            ExposedCapabilities::App(app) => &app.directory_channel(),
            ExposedCapabilities::Directory(directory_channel) => &directory_channel,
        }
    }

    /// Connect to a protocol provided by the `Element`.
    ///
    /// # Type Parameters
    /// - P: A FIDL service `Marker` type.
    ///
    /// # Returns
    /// - A service `Proxy` matching the `Marker`, or an error if the service is not available from
    /// the `Element`.
    #[inline]
    pub fn connect_to_protocol<P: DiscoverableProtocolMarker>(&self) -> Result<P::Proxy, Error> {
        let (client_channel, server_channel) = zx::Channel::create()?;
        self.connect_to_protocol_with_channel::<P>(server_channel)?;
        Ok(P::Proxy::from_channel(fasync::Channel::from_channel(client_channel)?))
    }

    /// Connect to the "default" instance of a FIDL service provided by the `Element`.
    ///
    /// # Type Parameters
    /// - US: A FIDL service `Marker` type.
    ///
    /// # Returns
    /// - A service `Proxy` matching the `Marker`, or an error if the service is not available from
    /// the `Element`.
    #[inline]
    pub fn connect_to_service<US: ServiceMarker>(&self) -> Result<US::Proxy, Error> {
        fuchsia_component::client::connect_to_service_at_dir::<US>(&self.directory_channel())
    }

    /// Connect to a protocol by passing a channel for the server.
    ///
    /// # Type Parameters
    /// - P: A FIDL service `Marker` type.
    ///
    /// # Parameters
    /// - server_channel: The server-side endpoint of a channel pair, to bind to the requested
    /// service. The caller will interact with the service via the client-side endpoint.
    ///
    /// # Returns
    /// - Result::Ok or an error if the service is not available from the `Element`.
    #[inline]
    pub fn connect_to_protocol_with_channel<P: DiscoverableProtocolMarker>(
        &self,
        server_channel: zx::Channel,
    ) -> Result<(), Error> {
        self.connect_to_named_protocol_with_channel(P::PROTOCOL_NAME, server_channel)
    }

    /// Connect to a protocol by name.
    ///
    /// # Parameters
    /// - service_name: A FIDL service by name.
    /// - server_channel: The server-side endpoint of a channel pair, to bind to the requested
    /// service. The caller will interact with the service via the client-side endpoint.
    ///
    /// # Returns
    /// - Result::Ok or an error if the service is not available from the `Element`.
    #[inline]
    pub fn connect_to_named_protocol_with_channel(
        &self,
        protocol_name: &str,
        server_channel: zx::Channel,
    ) -> Result<(), Error> {
        fdio::service_connect_at(&self.directory_channel(), protocol_name, server_channel)?;
        Ok(())
    }
}
