// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    fidl_fuchsia_bluetooth_bredr::{ChannelMode, ChannelParameters},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::channel::{mpsc, oneshot},
    std::{
        cmp::PartialEq,
        collections::{hash_map::Iter, HashMap},
        convert::TryFrom,
        fmt::Debug,
        iter::IntoIterator,
    },
};

/// The default buffer size for the mpsc channels used to relay user data
/// packets to be sent to the remote peer.
/// This value is arbitrarily chosen and should be enough to send buffers
/// from multiple `Cmd::Send` commands.
const USER_DATA_BUFFER_SIZE: usize = 50;

/// The ServerChannel number of the service advertised by the remote peer. This
/// is used as an identifier for the RFCOMM service.
#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub struct ServerChannelNumber(pub u8);

impl TryFrom<u8> for ServerChannelNumber {
    type Error = Error;
    fn try_from(src: u8) -> Result<Self, Self::Error> {
        if src < 1 || src > 30 {
            return Err(anyhow!("Server channel must be [1, 30]"));
        }
        Ok(ServerChannelNumber(src))
    }
}

#[derive(Debug, PartialEq)]
pub struct IncrementedIdMap<T> {
    next_id: u32,
    map: HashMap<u32, T>,
}

impl<T: Debug> IncrementedIdMap<T> {
    pub fn new() -> IncrementedIdMap<T> {
        IncrementedIdMap { next_id: 0, map: HashMap::new() }
    }

    pub fn map(&self) -> &HashMap<u32, T> {
        &self.map
    }

    /// Returns id assigned.
    pub fn insert(&mut self, value: T) -> u32 {
        let id = self.next_id;
        self.next_id += 1;
        assert!(self.map.insert(id, value).is_none());
        id
    }

    pub fn remove(&mut self, id: &u32) -> Option<T> {
        self.map.remove(id)
    }
}

impl<'a, T> IntoIterator for &'a IncrementedIdMap<T> {
    type Item = (&'a u32, &'a T);
    type IntoIter = Iter<'a, u32, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.map.iter()
    }
}

#[derive(Debug)]
pub struct L2capChannel {
    pub socket: zx::Socket,
    pub mode: ChannelMode,
    pub max_tx_sdu_size: u16,
}

#[derive(Debug)]
pub struct SdpService {
    pub advertisement_stopper: oneshot::Sender<()>,
    pub params: ChannelParameters,
}

/// Tracks all state local to the command line tool.
pub struct ProfileState {
    /// Currently connected L2CAP channels.
    pub l2cap_channels: IncrementedIdMap<L2capChannel>,
    /// Currently active service advertisements.
    pub services: IncrementedIdMap<SdpService>,
    /// The current RFCOMM state.
    pub rfcomm: RfcommState,
}

impl ProfileState {
    pub fn new() -> ProfileState {
        ProfileState {
            l2cap_channels: IncrementedIdMap::new(),
            services: IncrementedIdMap::new(),
            rfcomm: RfcommState::new(),
        }
    }

    pub fn reset(&mut self) {
        // Dropping the services will stop the advertisements.
        self.services = IncrementedIdMap::new();

        // Dropping the L2CAP sockets will disconnect channels.
        self.l2cap_channels = IncrementedIdMap::new();

        // Resetting the RFCOMM state will cancel any active service advertisement &
        // search and disconnect the channels.
        self.rfcomm = RfcommState::new();
    }
}

/// A payload containing user data to be sent over an RFCOMM channel.
type UserData = Vec<u8>;
/// Tracks the state local to the command line tool.
pub struct RfcommState {
    /// The task representing the RFCOMM service advertisement and search.
    pub service: Option<fasync::Task<()>>,
    /// The currently connected and active RFCOMM channels. Each channel
    /// is represented by a sender that is used to send user data to the
    /// remote peer.
    active_channels: HashMap<ServerChannelNumber, mpsc::Sender<UserData>>,
}

impl RfcommState {
    pub fn new() -> Self {
        Self { active_channels: HashMap::new(), service: None }
    }

    /// Creates a new RFCOMM channel for the provided `server_channel`. Returns a
    /// receiver for user data to be sent to the remote peer.
    pub fn create_channel(
        &mut self,
        server_channel: ServerChannelNumber,
    ) -> mpsc::Receiver<UserData> {
        let (sender, receiver) = mpsc::channel(USER_DATA_BUFFER_SIZE);
        self.active_channels.insert(server_channel, sender);
        receiver
    }

    /// Removes the RFCOMM channel for the provided `server_channel`. Returns true if
    /// the channel was removed.
    pub fn remove_channel(&mut self, server_channel: ServerChannelNumber) -> bool {
        self.active_channels.remove(&server_channel).is_some()
    }

    /// Sends the `user_data` buf to the peer that provides the service identified
    /// by the `server_channel`. Returns the result of the send operation.
    pub fn send_user_data(
        &mut self,
        server_channel: ServerChannelNumber,
        user_data: UserData,
    ) -> Result<(), Error> {
        self.active_channels
            .get_mut(&server_channel)
            .ok_or(anyhow!("No registered server channel"))
            .and_then(|sender| sender.try_send(user_data).map_err(|e| anyhow!("{:?}", e)))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use futures::{task::Poll, StreamExt};
    use matches::assert_matches;

    #[test]
    fn incremented_id_map() {
        let mut numbers = IncrementedIdMap::<i32>::new();
        assert_eq!(0, numbers.insert(0));
        assert_eq!(1, numbers.insert(1));

        assert_eq!(2, numbers.map().len());
        assert_eq!(Some(&0i32), numbers.map().get(&0u32));
        assert_eq!(Some(&1i32), numbers.map().get(&1u32));
    }

    #[test]
    fn send_rfcomm_data_is_received_by_peer() {
        let mut exec = fasync::Executor::new().unwrap();

        let mut state = ProfileState::new();
        assert!(state.rfcomm.active_channels.is_empty());

        // Registering channel is OK.
        let server_channel = ServerChannelNumber(5);
        let mut receiver = state.rfcomm.create_channel(server_channel);
        assert!(state.rfcomm.active_channels.contains_key(&server_channel));

        let mut received_bytes = Box::pin(receiver.next());
        assert!(exec.run_until_stalled(&mut received_bytes).is_pending());

        // Sending user data to the peer should be successful.
        let random_user_data = vec![0x00, 0x02, 0x04, 0x06, 0x08];
        assert_matches!(
            state.rfcomm.send_user_data(server_channel, random_user_data.clone()),
            Ok(_)
        );
        match exec.run_until_stalled(&mut received_bytes) {
            Poll::Ready(Some(buf)) => {
                assert_eq!(buf, random_user_data);
            }
            x => panic!("Expected ready with data but got: {:?}", x),
        }

        // Removing channel is OK.
        assert!(state.rfcomm.remove_channel(server_channel));
        // Trying to send more data on the closed channel should fail immediately.
        assert_matches!(state.rfcomm.send_user_data(server_channel, vec![0x09]), Err(_));
    }
}
