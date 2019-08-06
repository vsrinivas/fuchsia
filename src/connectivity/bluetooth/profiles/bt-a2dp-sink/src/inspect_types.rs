// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! Types used to manage data that will be exposed through the Inspect API

use {
    bt_avdtp as avdtp, fuchsia_async as fasync,
    fuchsia_bluetooth::inspect::DebugExt,
    fuchsia_inspect::{self as inspect, NumericProperty, Property},
    fuchsia_inspect_contrib::nodes::NodeExt,
    fuchsia_zircon::DurationNum,
    futures::lock::Mutex,
    std::{collections::HashMap, sync::Arc},
};

pub(crate) struct StreamingInspectData {
    inspect: inspect::Node,
    rx_bytes: inspect::UintProperty,
    rx_bytes_per_second_current: inspect::UintProperty,
    /// Number of whole seconds that stream decoding has been running.
    rx_seconds: inspect::UintProperty,
    update_interval_seconds: u64,
    pub update_interval: fasync::Interval,
    stream_start: Option<fuchsia_inspect_contrib::nodes::TimeProperty>,
    pub accumulated_bytes: u64,
}

impl StreamingInspectData {
    pub fn new(inspect: inspect::Node) -> StreamingInspectData {
        let update_interval_seconds = 1;
        StreamingInspectData {
            rx_bytes: inspect.create_uint("rx_bytes", 0),
            rx_bytes_per_second_current: inspect.create_uint("rx_bytes_per_second_current", 0),
            rx_seconds: inspect.create_uint("rx_seconds", 0),
            stream_start: None,
            accumulated_bytes: 0,
            update_interval_seconds,
            update_interval: fasync::Interval::new((update_interval_seconds as i64).seconds()),
            inspect,
        }
    }

    /// Signals the start of the stream
    pub fn stream_started(&mut self) {
        if let Some(prop) = &self.stream_start {
            prop.update();
        } else {
            self.stream_start = Some(self.inspect.create_time("stream_started_at"))
        }
    }

    /// Call this method every `self.update_interval_seconds` to update inspect data.
    pub fn update_rx_statistics(&mut self) {
        self.rx_seconds.add(self.update_interval_seconds);
        self.rx_bytes.add(self.accumulated_bytes);
        self.rx_bytes_per_second_current.set(self.accumulated_bytes);
        self.accumulated_bytes = 0;
    }
}

/// Represents the inspect data for a single stream endpoint
struct StreamInspect {
    inspect: inspect::Node,
}

impl StreamInspect {
    pub fn new(inspect: inspect::Node) -> StreamInspect {
        StreamInspect { inspect }
    }

    /// Create an empty `StringProperty` that should be populated with inspect data for this
    /// stream.
    /// Each call to this method will replace the previous value in the inspect hierarchy.
    pub fn create_stream_state_inspect_data(&self) -> inspect::StringProperty {
        self.inspect.create_string("state", "")
    }

    /// Create `StreamingInspectData` that should be populated with inspect data for this stream.
    /// Each call to this method will replace the previous value in the inspect hierarchy.
    pub fn create_streaming_inspect_data(&self) -> StreamingInspectData {
        StreamingInspectData::new(self.inspect.create_child("streaming"))
    }
}

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

    /// Add a new string property to the node identied by the remote's stream endpoint id
    /// containing a list of all capabilities that the endpoint supports.
    pub async fn append<'a>(
        &'a self,
        id: &'a avdtp::StreamEndpointId,
        caps: &'a Vec<avdtp::ServiceCapability>,
    ) {
        let mut inner = self.0.lock().await;
        let prop = inner.0.create_string(id.to_string(), caps.debug());
        inner.1.push(prop);
    }
}

/// Manages the inspect subtree of data for a single remote peer.
pub(crate) struct RemotePeerInspect {
    inspect: inspect::Node,
    remote_capabilities: RemoteCapabilitiesInspect,
    streams: HashMap<avdtp::StreamEndpointId, StreamInspect>,
}

impl RemotePeerInspect {
    pub fn new(inspect: inspect::Node) -> RemotePeerInspect {
        let remote_capabilities =
            RemoteCapabilitiesInspect::new(inspect.create_child("remote stream capabilities"));
        RemotePeerInspect { inspect, remote_capabilities, streams: HashMap::new() }
    }

    /// Return a reference to the inspect data for a single stream endpoint. A new `StreamInspect`
    /// instance is created if one does not already exist for the given `seid`.
    fn stream_inspect(&mut self, seid: &avdtp::StreamEndpointId) -> &StreamInspect {
        let node = &mut self.inspect;
        self.streams.entry(seid.clone()).or_insert_with(|| {
            let inspect = node.create_child(format!("stream {}", seid));
            StreamInspect::new(inspect)
        })
    }

    /// Returns a handle to the remote capabilities inspect data for this remote peer.
    pub fn remote_capabilities_inspect(&self) -> RemoteCapabilitiesInspect {
        self.remote_capabilities.clone()
    }

    /// Create and return a StringProperty that should be populated with the inspect data
    /// for the specified stream endpoint.
    pub fn create_stream_state_inspect(
        &mut self,
        seid: &avdtp::StreamEndpointId,
    ) -> inspect::StringProperty {
        self.stream_inspect(seid).create_stream_state_inspect_data()
    }

    /// Create and return `StreamingInspectData` that should be populated with inspect data for the
    /// specified stream endpoint.
    pub fn create_streaming_inspect_data(
        &mut self,
        seid: &avdtp::StreamEndpointId,
    ) -> StreamingInspectData {
        self.stream_inspect(seid).create_streaming_inspect_data()
    }
}
