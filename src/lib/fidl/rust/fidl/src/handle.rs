// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A portable representation of handle-like objects for fidl.

#[cfg(target_os = "fuchsia")]
pub use fuchsia_handles::*;

#[cfg(not(target_os = "fuchsia"))]
pub use non_fuchsia_handles::*;

pub use fuchsia_async::Channel as AsyncChannel;
pub use fuchsia_async::Socket as AsyncSocket;

/// Fuchsia implementation of handles just aliases the zircon library
#[cfg(target_os = "fuchsia")]
pub mod fuchsia_handles {
    use crate::invoke_for_handle_types;

    use fuchsia_zircon as zx;

    pub use zx::AsHandleRef;
    pub use zx::Handle;
    pub use zx::HandleBased;
    pub use zx::HandleRef;
    pub use zx::MessageBuf;

    macro_rules! fuchsia_handle {
        ($x:tt, Stub) => {
            /// Stub implementation of Zircon handle type $x.
            #[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
            #[repr(transparent)]
            pub struct $x(zx::Handle);

            impl zx::AsHandleRef for $x {
                fn as_handle_ref(&self) -> HandleRef<'_> {
                    self.0.as_handle_ref()
                }
            }
            impl From<Handle> for $x {
                fn from(handle: Handle) -> Self {
                    $x(handle)
                }
            }
            impl From<$x> for Handle {
                fn from(x: $x) -> Handle {
                    x.0
                }
            }
            impl zx::HandleBased for $x {}
        };
        ($x:tt, $availability:tt) => {
            pub use zx::$x;
        };
    }

    invoke_for_handle_types!(fuchsia_handle);

    pub use zx::SocketOpts;
}

/// Non-Fuchsia implementation of handles
#[cfg(not(target_os = "fuchsia"))]
pub mod non_fuchsia_handles {
    use crate::invoke_for_handle_types;

    pub use fuchsia_async::emulated_handle::{
        AsHandleRef, EmulatedHandleRef, Handle, HandleBased, HandleRef, HandleType, MessageBuf,
        SocketOpts,
    };

    macro_rules! declare_unsupported_fidl_handle {
        ($name:ident) => {
            /// An unimplemented Zircon-like $name
            #[derive(PartialEq, Eq, Debug, PartialOrd, Ord, Hash)]
            pub struct $name;

            impl From<$crate::handle::Handle> for $name {
                fn from(_: $crate::handle::Handle) -> $name {
                    $name
                }
            }
            impl From<$name> for Handle {
                fn from(_: $name) -> $crate::handle::Handle {
                    $crate::handle::Handle::invalid()
                }
            }
            impl HandleBased for $name {}
            impl AsHandleRef for $name {
                fn as_handle_ref<'a>(&'a self) -> HandleRef<'a> {
                    HandleRef::invalid()
                }
            }
        };
    }

    macro_rules! declare_fidl_handle {
        ($name:ident) => {
            pub use fuchsia_async::emulated_handle::$name;
        };
    }

    macro_rules! host_handle {
        ($x:tt, Everywhere) => {
            declare_fidl_handle! {$x}
        };
        ($x:tt, $availability:ident) => {
            declare_unsupported_fidl_handle! {$x}
        };
    }

    invoke_for_handle_types!(host_handle);
}
