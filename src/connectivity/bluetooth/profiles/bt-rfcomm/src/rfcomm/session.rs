// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_bluetooth::types::Channel,
    futures::{select, Future, FutureExt, StreamExt},
    log::{info, trace},
};

/// An RFCOMM Session over an L2CAP channel.
pub struct Session {}

impl Session {
    /// Creates a new RFCOMM Session and returns a Future that processes data over
    /// the provided `l2cap_channel`.
    pub fn create(l2cap_channel: Channel) -> impl Future<Output = Result<(), Error>> {
        let session = Self {};
        session.processing_task(l2cap_channel)
    }

    /// Starts processing data over the `l2cap_channel`.
    async fn processing_task(self, mut l2cap_channel: Channel) -> Result<(), Error> {
        loop {
            select! {
                bytes = l2cap_channel.next().fuse() => {
                    let bytes = match bytes {
                        Some(bytes) => bytes?,
                        None => {
                            info!("PEER_CLOSED, exiting");
                            return Ok(());
                        }
                    };
                    trace!("Received packet from peer: {:?}", bytes);

                    // Send a canned DisconnectedMode response to reject any incoming
                    // RFCOMM packet. The FCS doesn't matter so we use 0x00.
                    // TODO(58613): Parse the Frame and handle accordingly.
                    let dm_response = [0x03, 0x1F, 0x01, 0x00];
                    let _ = l2cap_channel.as_ref().write(&dm_response);
                }
                complete => { return Ok(()); }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async as fasync;
    use futures::{pin_mut, Future};

    fn setup_session() -> (impl Future<Output = Result<(), Error>>, Channel) {
        let (remote, local) = Channel::create();
        let session_fut = Session::create(local);

        (session_fut, remote)
    }

    #[test]
    fn test_register_l2cap_channel() {
        let mut exec = fasync::Executor::new().unwrap();

        let (processing_fut, remote) = setup_session();
        pin_mut!(processing_fut);
        assert!(exec.run_until_stalled(&mut processing_fut).is_pending());

        drop(remote);
        assert!(exec.run_until_stalled(&mut processing_fut).is_ready());
    }

    #[test]
    fn test_receiving_data_is_ok() {
        let mut exec = fasync::Executor::new().unwrap();

        let (processing_fut, remote) = setup_session();
        pin_mut!(processing_fut);
        assert!(exec.run_until_stalled(&mut processing_fut).is_pending());

        // Remote sends us some data.
        let frame_bytes = [0x03, 0x3F, 0x01, 0x1C];
        remote.as_ref().write(&frame_bytes[..]).expect("Should send");

        assert!(exec.run_until_stalled(&mut processing_fut).is_pending());
    }
}
