// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_avdtp::{self as avdtp, ServiceCapability, StreamEndpoint, StreamEndpointId},
    std::{collections::HashMap, fmt},
};

/// Representation of a Stream Endpoint and associated data.
/// Currently only the endpoint.
pub struct Stream {
    /// Endpoint associated with this Stream.
    endpoint: avdtp::StreamEndpoint,
}

impl fmt::Debug for Stream {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Stream").field("endpoint", &self.endpoint).finish()
    }
}

impl Stream {
    pub fn build(endpoint: avdtp::StreamEndpoint) -> Self {
        Self { endpoint }
    }

    fn as_new(&self) -> Self {
        Self { endpoint: self.endpoint.as_new() }
    }

    pub fn endpoint(&self) -> &StreamEndpoint {
        &self.endpoint
    }

    pub fn endpoint_mut(&mut self) -> &mut StreamEndpoint {
        &mut self.endpoint
    }

    pub fn configure(
        &mut self,
        remote_id: &StreamEndpointId,
        capabilities: Vec<ServiceCapability>,
    ) -> avdtp::Result<()> {
        self.endpoint.configure(remote_id, capabilities)
    }

    pub fn reconfigure(&mut self, capabilities: Vec<ServiceCapability>) -> avdtp::Result<()> {
        self.endpoint.reconfigure(capabilities)
    }

    /// Attempt to start the stream.
    pub fn start(&mut self) -> avdtp::Result<()> {
        self.endpoint.start()
    }

    /// Releases the endpoint and stops the processing of audio.
    pub async fn release(
        &mut self,
        responder: avdtp::SimpleResponder,
        peer: &avdtp::Peer,
    ) -> avdtp::Result<()> {
        self.endpoint.release(responder, peer).await
    }

    pub async fn abort(&mut self, peer: Option<&avdtp::Peer>) -> avdtp::Result<()> {
        self.endpoint.abort(peer).await
    }
}

/// A Collection of Streeams which are indexed by their endpoint id.
pub struct Streams(HashMap<StreamEndpointId, Stream>);

impl Streams {
    /// A new empty set of streams.
    pub fn new() -> Self {
        Self(HashMap::new())
    }

    /// Makes a copy of this set of streams, but with all streams reset to their iniital idle states.
    pub fn as_new(&self) -> Self {
        let mut new_map = HashMap::new();
        for (id, stream) in self.0.iter() {
            new_map.insert(id.clone(), stream.as_new());
        }
        Self(new_map)
    }

    /// Inserts a stream, indexing it by the local endpoint id.
    /// It replaces any other stream with the same endpoint id.
    pub fn insert(&mut self, stream: Stream) {
        self.0.insert(stream.endpoint().local_id().clone(), stream);
    }

    /// Retrieves a reference to the Stream referenced by `id`, if the stream exists,
    pub fn get(&mut self, id: &StreamEndpointId) -> Option<&Stream> {
        self.0.get(id)
    }

    /// Retrieves a mutable reference to the Stream referenced by `id`, if the stream exists,
    pub fn get_mut(&mut self, id: &StreamEndpointId) -> Option<&mut Stream> {
        self.0.get_mut(id)
    }

    /// Returns a vector of information on all the contained streams.
    pub fn information(&self) -> Vec<avdtp::StreamInformation> {
        self.0.values().map(|x| x.endpoint().information()).collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::convert::TryInto;

    fn make_endpoint(seid: u8) -> StreamEndpoint {
        StreamEndpoint::new(seid, avdtp::MediaType::Audio, avdtp::EndpointType::Source, vec![])
            .expect("endpoint creation should succeed")
    }

    #[test]
    fn test_streams() {
        let mut streams = Streams::new();

        streams.insert(Stream::build(make_endpoint(1)));
        streams.insert(Stream::build(make_endpoint(6)));

        let first_id = 1_u8.try_into().expect("good id");
        let missing_id = 5_u8.try_into().expect("good id");

        assert!(streams.get(&first_id).is_some());
        assert!(streams.get(&missing_id).is_none());

        assert!(streams.get_mut(&first_id).is_some());
        assert!(streams.get_mut(&missing_id).is_none());

        let expected_info = vec![make_endpoint(1).information(), make_endpoint(6).information()];

        let infos = streams.information();

        assert_eq!(expected_info.len(), infos.len());

        if infos[0].id() == &first_id {
            assert_eq!(expected_info[0], infos[0]);
            assert_eq!(expected_info[1], infos[1]);
        } else {
            assert_eq!(expected_info[0], infos[1]);
            assert_eq!(expected_info[1], infos[0]);
        }
    }
}
