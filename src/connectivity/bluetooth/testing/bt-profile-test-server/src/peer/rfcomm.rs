// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {bt_rfcomm::ServerChannel, std::collections::HashSet};

/// The `RfcommChannelSet` maintains a set of the currently allocated RFCOMM
/// channels.
pub struct RfcommChannelSet {
    allocated: HashSet<ServerChannel>,
}

impl RfcommChannelSet {
    pub fn new() -> Self {
        Self { allocated: HashSet::new() }
    }

    #[cfg(test)]
    fn contains_channel(&self, channel: &ServerChannel) -> bool {
        self.allocated.contains(channel)
    }

    /// Returns the number of available RFCOMM channels.
    pub fn available_space(&self) -> usize {
        ServerChannel::all().filter(|sc| !self.allocated.contains(&sc)).count()
    }

    /// Allocates the next available server channel. Returns None if there
    /// are no available channels.
    pub fn reserve_channel(&mut self) -> Option<ServerChannel> {
        let next = ServerChannel::all().find(|sc| !self.allocated.contains(&sc));
        next.map(|channel| {
            self.allocated.insert(channel);
            channel
        })
    }

    /// Removes the allocated server channels specified by `channels`.
    pub fn remove_channels(&mut self, channels: &HashSet<ServerChannel>) {
        self.allocated = self.allocated.difference(channels).cloned().collect();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn allocating_and_removing_channels_success() {
        let mut rfcomm = RfcommChannelSet::new();
        // Removing nothing is OK.
        rfcomm.remove_channels(&HashSet::new());

        let channel_num1 = rfcomm.reserve_channel().expect("should allocate");
        let channel_num2 = rfcomm.reserve_channel().expect("should allocate");
        assert!(rfcomm.contains_channel(&channel_num1));
        assert!(rfcomm.contains_channel(&channel_num2));

        let mut to_remove = HashSet::new();
        to_remove.insert(channel_num1);
        rfcomm.remove_channels(&to_remove);
        assert!(!rfcomm.contains_channel(&channel_num1));
        to_remove.insert(channel_num2);
        rfcomm.remove_channels(&to_remove);
        assert!(!rfcomm.contains_channel(&channel_num2));
        // Removing channels that have already been removed has no effect.
        rfcomm.remove_channels(&to_remove);
    }
}
