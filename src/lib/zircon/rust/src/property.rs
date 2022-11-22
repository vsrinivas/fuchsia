// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon object properties.

use fuchsia_zircon_sys as sys;
use std::ops::Deref;

/// Object property types for use with [object_get_property()] and [object_set_property].
#[derive(Copy, Clone, Ord, PartialOrd, Eq, PartialEq, Hash)]
#[repr(transparent)]
pub struct Property(u32);

impl Deref for Property {
    type Target = u32;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

/// A definition for properties about a zircon object.
///
/// # Safety
///
/// `PROPERTY` must correspond to a valid property type and `PropTy` must be the
/// corresponding value type for that property. See
/// https://fuchsia.dev/fuchsia-src/reference/syscalls/object_get_property for a
/// list of available properties and their corresponding value types.
///
/// It must be valid to treat `PropTy` as its corresponding property type and
/// writing the property type to a `PropTy` must completely initialize it. That
/// is, `PropTy` must be "transparent" over the property type.
pub unsafe trait PropertyQuery {
    /// The raw `Property` value
    const PROPERTY: Property;
    /// The data type of this property
    type PropTy;
}

assoc_values!(Property, [
    NAME = sys::ZX_PROP_NAME;

    #[cfg(target_arch = "x86_64")]
    REGISTER_GS = sys::ZX_PROP_REGISTER_GS;
    #[cfg(target_arch = "x86_64")]
    REGISTER_FS = sys::ZX_PROP_REGISTER_FS;

    PROCESS_DEBUG_ADDR = sys::ZX_PROP_PROCESS_DEBUG_ADDR;
    PROCESS_VDSO_BASE_ADDRESS = sys::ZX_PROP_PROCESS_VDSO_BASE_ADDRESS;
    SOCKET_RX_THRESHOLD = sys::ZX_PROP_SOCKET_RX_THRESHOLD;
    SOCKET_TX_THRESHOLD = sys::ZX_PROP_SOCKET_TX_THRESHOLD;
    CHANNEL_TX_MSG_MAX = sys::ZX_PROP_CHANNEL_TX_MSG_MAX;
    JOB_KILL_ON_OOM = sys::ZX_PROP_JOB_KILL_ON_OOM;
    EXCEPTION_STATE = sys::ZX_PROP_EXCEPTION_STATE;
    VMO_CONTENT_SIZE = sys::ZX_PROP_VMO_CONTENT_SIZE;
    STREAM_MODE_APPEND = sys::ZX_PROP_STREAM_MODE_APPEND;
]);
