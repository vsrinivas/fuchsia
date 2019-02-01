// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! WatcherConnection handles interaction with directory watchers as described in io.fidl.

use {
    fidl_fuchsia_io::{
        WatchedEvent, MAX_FILENAME, WATCH_EVENT_EXISTING, WATCH_EVENT_IDLE, WATCH_MASK_EXISTING,
        WATCH_MASK_IDLE,
    },
    fuchsia_async::Channel,
    fuchsia_zircon::MessageBuf,
    futures::{task::LocalWaker, Poll},
    std::iter,
};

pub struct WatcherConnection {
    mask: u32,
    channel: Channel,
}

impl WatcherConnection {
    pub fn new(mask: u32, channel: Channel) -> Self {
        WatcherConnection { mask, channel }
    }

    /// A helper used by other send_event*() methods.  Sends a collection of
    /// fidl_fuchsia_io::WatchEvent instances over this watcher connection.
    fn send_event_structs(
        &self, events: &mut Iterator<Item = WatchedEvent>,
    ) -> Result<(), fidl::Error> {
        // Unfortunately, io.fidl currently does not provide encoding for the watcher events.
        // Seems to be due to
        //
        //     https://fuchsia.atlassian.net/browse/ZX-2645
        //
        // As soon as that is fixed we should switch to the generated binding.
        //
        // For now this code duplicates what the C++ version is doing:
        //
        //     https://fuchsia.googlesource.com/zircon/+/1dcb46aa1c4001e9d1d68b8ff5d8fae0c00fbb49/system/ulib/fs/watcher.cpp
        //
        // There is no Transaction wrapping the messages, as for the full blown FIDL events.

        let buffer = &mut vec![];
        let (bytes, handles) = (&mut vec![], &mut vec![]);
        for mut event in events {
            // Keep bytes and handles across loop iterations, to reduce reallocations.
            bytes.clear();
            handles.clear();
            fidl::encoding::Encoder::encode(bytes, handles, &mut event)?;
            if handles.len() > 0 {
                panic!("WatchedEvent struct is not expected to contain any handles")
            }

            if buffer.len() + bytes.len() >= fidl_fuchsia_io::MAX_BUF as usize {
                self.channel
                    .write(&*buffer, &mut vec![])
                    .map_err(fidl::Error::ServerResponseWrite)?;

                buffer.clear();
            }

            buffer.append(bytes);
        }

        if buffer.len() > 0 {
            self.channel
                .write(&*buffer, &mut vec![])
                .map_err(fidl::Error::ServerResponseWrite)?;
        }

        Ok(())
    }

    /// Constructs and sends a fidl_fuchsia_io::WatchEvent instance over the watcher connection.
    ///
    /// `event` is one of the WATCH_EVENT_* constants, with the values used to populate the `event`
    /// field.
    pub fn send_event(&self, event: u8, name: &str) -> Result<(), fidl::Error> {
        // This assertion is never expected to trigger as the only caller of this interface is the
        // [`PseudoDirectory`] instance that is expected to only pass entry names in here.  And
        // [`add_entry`] will not allow entries longer than [`MAX_FILENAME`].
        assert!(
            name.len() < MAX_FILENAME as usize,
            "name argument should not contain more than {} bytes.\n\
             Got: {}\n\
             Content: '{}'",
            MAX_FILENAME,
            name.len(),
            name
        );
        self.send_event_structs(&mut iter::once(WatchedEvent {
            event,
            len: name.len() as u8,
            name: name.as_bytes().to_vec(),
        }))
    }

    /// Constructs and sends a fidl_fuchsia_io::WatchEvent instance over the watcher connection,
    /// skipping the operation if the watcher did not request this kind of events to be delivered -
    /// filtered by the mask value.
    pub fn send_event_check_mask(
        &self, mask: u32, event: u8, name: &str,
    ) -> Result<(), fidl::Error> {
        if self.mask & mask == 0 {
            return Ok(());
        }

        self.send_event(event, name)
    }

    /// Sends one fidl_fuchsia_io::WatchEvent instance of type WATCH_EVENT_EXISTING, for every name
    /// in the list.  If the watcher has requested this kind of events - similar to to
    /// [`send_event_check_mask`] above, but with a predefined mask and event type.
    pub fn send_events_existing(
        &self, names: &mut Iterator<Item = &str>,
    ) -> Result<(), fidl::Error> {
        if self.mask & WATCH_MASK_EXISTING == 0 {
            return Ok(());
        }

        self.send_event_structs(&mut names.map(|name| {
            // This assertion is never expected to trigger as the only caller of this interface is
            // the [`PseudoDirectory`] instance that is expected to only pass entry names in here.
            // And [`add_entry`] will not allow entries longer than [`MAX_FILENAME`].
            assert!(
                name.len() < MAX_FILENAME as usize,
                "name argument should not contain more than {} bytes.\n\
                 Got: {}\n\
                 Content: '{}'",
                MAX_FILENAME,
                name.len(),
                name
            );
            WatchedEvent {
                event: WATCH_EVENT_EXISTING,
                len: name.len() as u8,
                name: name.as_bytes().to_vec(),
            }
        }))
    }

    /// Sends one instance of fidl_fuchsia_io::WatchEvent of type WATCH_MASK_IDLE.  If the watcher
    /// has requested this kind of events - similar to to [`send_event_check_mask`] above, but with
    /// the predefined mask and event type.
    pub fn send_event_idle(&self) -> Result<(), fidl::Error> {
        if self.mask & WATCH_MASK_IDLE == 0 {
            return Ok(());
        }

        self.send_event(WATCH_EVENT_IDLE, "")
    }

    /// Checks if the watcher has closed the connection.  And sets the waker to trigger when the
    /// connection is closed if it was still opened during the call.
    pub fn is_dead(&self, lw: &LocalWaker) -> bool {
        let channel = &self.channel;

        if channel.is_closed() {
            return true;
        }

        // Make sure we will be notified when the watcher has closed its connected or when any
        // message is send.
        //
        // We are going to close the connection when we receive any message as this is currently an
        // error.  When we fix ZX-2645 and wrap the watcher connection with FIDL, it would be up to
        // the binding code to fail on any unexpected messages.  At that point we can switch to
        // fuchsia_async::OnSignals and only monitor for the close event.
        //
        // We rely on [`Channel::recv_from()`] to invoke [`Channel::poll_read()`], which would call
        // [`RWHandle::poll_read()`] that would set the signal mask to `READABLE | CLOSE`.
        let mut msg = MessageBuf::new();
        match channel.recv_from(&mut msg, lw) {
            // We are not expecting any messages.  Returning true would cause this watcher
            // connection to be dropped and closed as a result.
            Poll::Ready(_) => true,
            // Poll::Pending is actually the only value we are expecting to see from a watcher that
            // did not close it's side of the connection.  And when the connection is closed, we
            // are expecting Poll::Ready(Err(Status::PEER_CLOSED.into_raw())), but that is covered
            // by the case above.
            Poll::Pending => false,
        }
    }
}
