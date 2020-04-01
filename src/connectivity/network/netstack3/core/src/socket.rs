// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! General-purpose socket utilities common to device layer and IP layer
//! sockets.

/// A socket providing the ability to communicate with a remote or local host.
///
/// A `Socket` is a long-lived object which provides the ability to either send
/// outbound traffic to or receive inbound traffic from a particular remote or
/// local host or set of hosts.
///
/// `Socket`s may cache certain routing information that is used to speed up the
/// operation of sending outbound packets. However, this means that updates to
/// global state (for example, updates to the forwarding table, to the neighbor
/// cache, etc) may invalidate that cached information. Thus, certain updates
/// may require updating all stored sockets as well. See the `Update` and
/// `UpdateMeta` associated types and the `apply_update` method for more
/// details.
pub(crate) trait Socket {
    /// The type of updates to the socket.
    ///
    /// Updates are emitted whenever information changes that might require
    /// information cached in sockets to be updated. For example, for IP
    /// sockets, changes to the forwarding table might require that an IP
    /// socket's outbound device be updated.
    type Update;

    /// Metadata required to perform an update.
    ///
    /// Extra metadata may be required in order to apply an update to a socket.
    type UpdateMeta;

    /// The type of errors that can occur while performing an update.
    type UpdateError;

    /// Apply an update to the socket.
    ///
    /// `apply_update` applies the given update, possibly changing cached
    /// information. If it returns `Err`, then the socket MUST be closed. This
    /// is a MUST, not a SHOULD, as the cached information may now be invalid,
    /// and the behavior of any further use of the socket may now be
    /// unspecified. It is the caller's responsibility to ensure that the socket
    /// is no longer used, including instructing the bindings not to use it
    /// again (if applicable).
    fn apply_update(
        &mut self,
        update: &Self::Update,
        meta: &Self::UpdateMeta,
    ) -> Result<(), Self::UpdateError>;
}
