// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! Types used to manage data that will be exposed through the Inspect API

use {
    bt_a2dp::stream::Streams, bt_avdtp as avdtp, fuchsia_bluetooth::inspect::DebugExt,
    fuchsia_inspect as inspect, fuchsia_inspect_derive::Inspect, futures::lock::Mutex,
    std::sync::Arc,
};

/// A threadsafe handle to an inspect node representing the capabilities of a peer.
/// The capabilities for each stream endpoint on the remote end are added as a seperate string
/// property on the node with the `append` function.
#[derive(Clone)]
pub(crate) struct RemoteCapabilitiesInspect(
    Arc<Mutex<(inspect::Node, Vec<inspect::StringProperty>)>>,
);

impl RemoteCapabilitiesInspect {
    pub fn new(inspect: inspect::Node) -> RemoteCapabilitiesInspect {
        RemoteCapabilitiesInspect(Arc::new(Mutex::new((inspect, vec![]))))
    }

    /// Add a string properties to the node identied by the remote's stream endpoint ids
    /// and containing a list of all capabilities that the endpoint supports.
    pub async fn append<'a>(&'a self, endpoints: &'a [avdtp::StreamEndpoint]) {
        let mut inner = self.0.lock().await;
        for endpoint in endpoints {
            let id_str = endpoint.local_id().to_string();
            let caps_str = endpoint.capabilities().debug();
            let prop = inner.0.create_string(id_str, caps_str);
            inner.1.push(prop);
        }
    }
}

/// Manages the inspect subtree of data for a single remote peer.
pub(crate) struct RemotePeerInspect {
    _inspect: inspect::Node,
    remote_capabilities: RemoteCapabilitiesInspect,
}

impl RemotePeerInspect {
    pub fn new(inspect: inspect::Node, streams: &mut Streams) -> RemotePeerInspect {
        let remote_capabilities =
            RemoteCapabilitiesInspect::new(inspect.create_child("remote_stream_capabilities"));
        let _ = streams.iattach(&inspect, "local_streams");
        RemotePeerInspect { _inspect: inspect, remote_capabilities }
    }

    /// Returns a handle to the remote capabilities inspect data for this remote peer.
    pub fn remote_capabilities_inspect(&self) -> RemoteCapabilitiesInspect {
        self.remote_capabilities.clone()
    }
}
