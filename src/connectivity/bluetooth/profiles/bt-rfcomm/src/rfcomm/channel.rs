// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::Channel,
    futures::{select, FutureExt, StreamExt},
    log::{error, info, trace},
};

use crate::rfcomm::types::DLCI;

/// The RFCOMM Channel for a Session. This is a client facing channel which sends
/// & receives data from the profile-client that has registered the ServerChannel
/// associated with the `dlci`.
pub struct SessionChannel {
    /// The DLCI associated with this channel.
    dlci: DLCI,

    /// The processing task associated with the channel. This is set through
    /// `self.establish()`, and indicates whether this SessionChannel is
    /// currently active.
    processing_task: Option<fasync::Task<()>>,
}

impl SessionChannel {
    pub fn new(dlci: DLCI) -> Self {
        Self { dlci, processing_task: None }
    }

    /// Returns true if this SessionChannel has been established. Namely, `self.establish()`
    /// has been called, and a processing task started up.
    pub fn is_established(&self) -> bool {
        self.processing_task.is_some()
    }

    /// Starts the processing task over the provided `channel`.
    // TODO(59582): Update the processing task with a Sender to relay user data frames to the
    // main task in Session. Update the processing task with a way to receive user data
    // from the main task in Session to be relayed through the `channel`.
    pub fn establish(&mut self, mut channel: Channel) {
        let dlci = self.dlci.clone();
        let processing_task = fasync::Task::spawn(async move {
            loop {
                select! {
                    user_data = channel.next().fuse() => {
                        let data = match user_data {
                            Some(Ok(bytes)) => bytes,
                            Some(Err(e)) => {
                                error!("Error receiving data from client {:?}", e);
                                continue;
                            }
                            None => {
                                info!("RFCOMM profile client dropped channel endpoint");
                                break;
                            }
                        };
                        info!("Received user-data packet for DLCI {:?}: {:?}", dlci, data);
                    }
                    complete => break,
                }
            }
            info!("Processing task for DLCI {:?} finished", dlci);
        });
        self.processing_task = Some(processing_task);
        trace!("Established SessionChannel for DLCI {:?}", self.dlci);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::convert::TryFrom;

    #[test]
    fn test_establish_channel_and_send_data() {
        let _exec = fasync::Executor::new().unwrap();

        let dlci = DLCI::try_from(3).unwrap();
        let mut session_channel = SessionChannel::new(dlci);
        assert!(!session_channel.is_established());

        let (local, client) = Channel::create();
        session_channel.establish(local);
        assert!(session_channel.is_established());

        // Profile-client sends data, should be received OK.
        let client_data = vec![0xff, 0x00, 0xaa, 0x0bb];
        let res = client.as_ref().write(&client_data);
        assert_eq!(res, Ok(4));
    }
}
