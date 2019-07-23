// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A portable representation of handle-like objects for fidl.

/// Fuchsia implementation of handles just aliases the zircon library
#[cfg(target_os = "fuchsia")]
pub mod fuchsia_handles {

    use fuchsia_zircon as zx;

    pub use zx::MessageBuf;
    pub use zx::Handle;
    pub use zx::HandleBased;
    pub use zx::AsHandleRef;

    pub use zx::Channel;
    pub use zx::Event;
    pub use zx::EventPair;
    pub use zx::Fifo;
    pub use zx::Interrupt;
    pub use zx::Job;
    pub use zx::Log;
    pub use zx::Port;
    pub use zx::Process;
    pub use zx::Resource;
    pub use zx::Socket;
    pub use zx::Thread;
    pub use zx::Timer;
    pub use zx::Vmo;
    pub use zx::Vmar;

}

/// Non-Fuchsia implementation of handles
#[cfg(not(target_os = "fuchsia"))]
pub mod non_fuchsia_handles {

    use std::mem;

    /// A trait to get a reference to the underlying handle of an object.
    pub trait AsHandleRef {}

    /// A trait implemented by all handle-based types.
    pub trait HandleBased : AsHandleRef + From<Handle> + Into<Handle> {
        /// Creates an instance of this type from a handle.
        ///
        /// This is a convenience function which simply forwards to the `From` trait.
        fn from_handle(handle: Handle) -> Self {
            Self::from(handle)
        }

        /// Converts the value into its inner handle.
        ///
        /// This is a convenience function which simply forwards to the `Into` trait.
        fn into_handle(self) -> Handle {
            self.into()
        }
    }

    /// Representation of a handle-like object
    #[derive(PartialEq, Eq, Debug)]
    pub struct Handle {}

    impl AsHandleRef for Handle {}

    impl HandleBased for Handle {}

    impl Handle {
        /// Return an invalid handle
        pub fn invalid() -> Handle {
            Handle {}
        }

        /// Return true if this handle is invalid
        pub fn is_invalid(&self) -> bool {
            true
        }
    }

    /// A buffer for _receiving_ messages from a channel.
    ///
    /// A `MessageBuf` is essentially a byte buffer and a vector of
    /// handles, but move semantics for "taking" handles requires special handling.
    ///
    /// Note that for sending messages to a channel, the caller manages the buffers,
    /// using a plain byte slice and `Vec<Handle>`.
    #[derive(Debug, Default)]
    pub struct MessageBuf {
        bytes: Vec<u8>,
        handles: Vec<Handle>,
    }

    impl MessageBuf {
        /// Create a new, empty, message buffer.
        pub fn new() -> Self {
            Default::default()
        }

        /// Create a new non-empty message buffer.
        pub fn new_with(v: Vec<u8>, h: Vec<Handle>) -> Self {
            Self { bytes: v, handles: h }
        }

        /// Splits apart the message buf into a vector of bytes and a vector of handles.
        pub fn split_mut(&mut self) -> (&mut Vec<u8>, &mut Vec<Handle>) {
            (&mut self.bytes, &mut self.handles)
        }

        /// Splits apart the message buf into a vector of bytes and a vector of handles.
        pub fn split(self) -> (Vec<u8>, Vec<Handle>) {
            (self.bytes, self.handles)
        }

        /// Ensure that the buffer has the capacity to hold at least `n_bytes` bytes.
        pub fn ensure_capacity_bytes(&mut self, n_bytes: usize) {
            ensure_capacity(&mut self.bytes, n_bytes);
        }

        /// Ensure that the buffer has the capacity to hold at least `n_handles` handles.
        pub fn ensure_capacity_handles(&mut self, n_handles: usize) {
            ensure_capacity(&mut self.handles, n_handles);
        }

        /// Ensure that at least n_bytes bytes are initialized (0 fill).
        pub fn ensure_initialized_bytes(&mut self, n_bytes: usize) {
            if n_bytes <= self.bytes.len() {
                return;
            }
            self.bytes.resize(n_bytes, 0);
        }

        /// Get a reference to the bytes of the message buffer, as a `&[u8]` slice.
        pub fn bytes(&self) -> &[u8] {
            self.bytes.as_slice()
        }

        /// The number of handles in the message buffer. Note this counts the number
        /// available when the message was received; `take_handle` does not affect
        /// the count.
        pub fn n_handles(&self) -> usize {
            self.handles.len()
        }

        /// Take the handle at the specified index from the message buffer. If the
        /// method is called again with the same index, it will return `None`, as
        /// will happen if the index exceeds the number of handles available.
        pub fn take_handle(&mut self, index: usize) -> Option<Handle> {
            self.handles.get_mut(index).and_then(|handle| {
                if handle.is_invalid() {
                    None
                } else {
                    Some(mem::replace(handle, Handle::invalid()))
                }
            })
        }

        /// Clear the bytes and handles contained in the buf. This will drop any
        /// contained handles, resulting in their resources being freed.
        pub fn clear(&mut self) {
            self.bytes.clear();
            self.handles.clear();
        }
    }

    fn ensure_capacity<T>(vec: &mut Vec<T>, size: usize) {
        let len = vec.len();
        if size > len {
            vec.reserve(size - len);
        }
    }

}

#[cfg(target_os = "fuchsia")]
pub use fuchsia_handles::*;

#[cfg(not(target_os = "fuchsia"))]
pub use non_fuchsia_handles::*;
