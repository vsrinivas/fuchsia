// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module for all things related to OpenThread messages.

use crate::prelude_internal::*;
use num_derive::FromPrimitive;
use std::convert::TryInto;
use std::marker::PhantomData;

/// Message Priority.
/// Functional equivalent of [`otsys::otMessagePriority`](crate::otsys::otMessagePriority).
#[derive(Debug, Copy, Clone, Eq, Ord, PartialOrd, PartialEq, FromPrimitive)]
pub enum Priority {
    /// Low message priority.
    /// Equivalent to [`crate::otsys::otMessagePriority_OT_MESSAGE_PRIORITY_LOW`]
    Low = otMessagePriority_OT_MESSAGE_PRIORITY_LOW as isize,

    /// Normal message priority.
    /// Equivalent to [`crate::otsys::otMessagePriority_OT_MESSAGE_PRIORITY_NORMAL`]
    ///
    /// This is the default priority.
    Normal = otMessagePriority_OT_MESSAGE_PRIORITY_NORMAL as isize,

    /// High message priority.
    /// Equivalent to [`crate::otsys::otMessagePriority_OT_MESSAGE_PRIORITY_HIGH`]
    High = otMessagePriority_OT_MESSAGE_PRIORITY_HIGH as isize,
}

impl Default for Priority {
    /// The default priority is `Priority::Normal`.
    fn default() -> Self {
        Priority::Normal
    }
}

impl Priority {
    /// Returns value as a [`otsys::otMessagePriority`](crate::otsys::otMessagePriority).
    pub fn as_ot_message_priority(self) -> otMessagePriority {
        self as otMessagePriority
    }
}

impl From<otMessagePriority> for Priority {
    fn from(x: otMessagePriority) -> Self {
        use num::FromPrimitive;
        Self::from_u32(x).expect(format!("Unknown otMessagePriority value: {}", x).as_str())
    }
}

impl From<Priority> for otMessagePriority {
    fn from(x: Priority) -> Self {
        x.as_ot_message_priority()
    }
}

impl From<u8> for Priority {
    fn from(x: u8) -> Self {
        use num::FromPrimitive;
        Self::from_u8(x).expect(format!("Unknown otMessagePriority value: {}", x).as_str())
    }
}

impl From<Priority> for u8 {
    fn from(x: Priority) -> Self {
        x.try_into().unwrap()
    }
}

/// Message Info.
/// Functional equivalent of `otsys::otMessageInfo`.
#[derive(Clone)]
#[repr(transparent)]
pub struct Info(pub otMessageInfo);

impl_ot_castable!(Info, otMessageInfo);

impl AsRef<Info> for Info {
    fn as_ref(&self) -> &Info {
        self
    }
}

impl Info {
    /// Creates a new message info instance.
    pub fn new(local: SockAddr, remote: SockAddr) -> Info {
        Info(otMessageInfo {
            mSockAddr: local.addr().into_ot(),
            mPeerAddr: remote.addr().into_ot(),
            mSockPort: local.port(),
            mPeerPort: remote.port(),
            ..otMessageInfo::default()
        })
    }

    /// The ECN status of the packet, represented as in the IPv6 header.
    pub fn ecn(&self) -> u8 {
        self.0.mEcn()
    }

    /// Sets the the ECN status of the packet.
    pub fn set_ecn(&mut self, ecn: u8) {
        self.0.set_mEcn(ecn)
    }

    /// Gets the IPv6 Hop Limit value.
    pub fn hop_limit(&self) -> u8 {
        self.0.mHopLimit
    }

    /// Sets the IPv6 Hop Limit value.
    pub fn set_hop_limit(&mut self, hop_limit: u8) {
        self.0.mHopLimit = hop_limit
    }

    /// TRUE if packets sent/received via host interface, FALSE otherwise.
    pub fn is_host_interface(&self) -> bool {
        self.0.mIsHostInterface()
    }

    /// Set to TRUE if packets sent/received via host interface, FALSE otherwise.
    pub fn set_host_interface(&mut self, host_interface: bool) {
        self.0.set_mIsHostInterface(host_interface)
    }

    /// TRUE if allowing looping back multicast, FALSE otherwise.
    pub fn multicast_loop(&self) -> bool {
        self.0.mMulticastLoop()
    }

    /// Set to TRUE to allow looping back multicast, FALSE otherwise.
    pub fn set_multicast_loop(&mut self, multicast_loop: bool) {
        self.0.set_mMulticastLoop(multicast_loop)
    }

    /// TRUE if allowing IPv6 Hop Limit 0 in mHopLimit, FALSE otherwise.
    pub fn allow_zero_hop_limit(&self) -> bool {
        self.0.mAllowZeroHopLimit()
    }

    /// Set to TRUE to allow IPv6 Hop Limit 0 in mHopLimit, FALSE otherwise.
    pub fn set_allow_zero_hop_limit(&mut self, allow_zero_hop_limit: bool) {
        self.0.set_mAllowZeroHopLimit(allow_zero_hop_limit)
    }

    /// Local address and port.
    pub fn sock_name(&self) -> SockAddr {
        SockAddr::new(Ip6Address::from_ot(self.0.mSockAddr), self.0.mSockPort)
    }

    /// Remote address and port.
    pub fn peer_name(&self) -> SockAddr {
        SockAddr::new(Ip6Address::from_ot(self.0.mPeerAddr), self.0.mPeerPort)
    }
}

impl std::fmt::Debug for Info {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("otMessageInfo")
            .field("sock_name", &self.sock_name())
            .field("peer_name", &self.peer_name())
            .field("ecn", &self.ecn())
            .field("hop_limit", &self.hop_limit())
            .field("allow_zero_hop_limit", &self.allow_zero_hop_limit())
            .field("multicast_loop", &self.multicast_loop())
            .field("is_host_interface", &self.is_host_interface())
            .finish()
    }
}

/// Message Settings.
/// Functional equivalent of `otsys::otMessageSettings`.
#[derive(Debug, Clone)]
#[repr(transparent)]
pub struct Settings(pub otMessageSettings);

impl_ot_castable!(Settings, otMessageSettings);

impl AsRef<Settings> for Settings {
    fn as_ref(&self) -> &Settings {
        self
    }
}

impl Default for Settings {
    fn default() -> Self {
        Settings(otMessageSettings {
            mLinkSecurityEnabled: true,
            mPriority: otMessagePriority_OT_MESSAGE_PRIORITY_NORMAL.try_into().unwrap(),
        })
    }
}

impl Settings {
    /// Returns settings with the priority set as indicated.
    pub fn set_priority(mut self, priority: Priority) -> Self {
        self.0.mPriority = priority.into();
        self
    }

    /// Returns settings with the link_security flag set as indicated.
    pub fn set_link_security(mut self, enabled: bool) -> Self {
        self.0.mLinkSecurityEnabled = enabled;
        self
    }
}

/// OpenThread Message. Functional equivalent of [`otsys::otMessage`](crate::otsys::otMessage).
///
/// This type cannot be instantiated directly: owned instances are
/// passed around in an [`ot::Box<ot::Message>`], or [`OtMessageBox`](crate::OtMessageBox)
/// for short.
///
/// `otMessage` instances keep a reference to their `otInstance`, so this type does not
/// implement `Send` nor `Sync`.
#[repr(transparent)]
pub struct Message<'a>(otMessage, PhantomData<*mut otMessage>, PhantomData<&'a ()>);

impl std::fmt::Debug for Message<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("otMessage")
            .field("otPtr", &self.as_ot_ptr())
            .field("len", &self.len())
            .field("offset", &self.offset())
            .field("RSSI", &self.rssi())
            .finish()
    }
}

// SAFETY: `Message` transparently wraps around an opaque type and is
//         never used by value nor passed by value.
unsafe impl<'a> ot::Boxable for Message<'a> {
    type OtType = otMessage;
    unsafe fn finalize(&mut self) {
        otMessageFree(self.as_ot_ptr())
    }
}

impl<'a> Message<'a> {
    /// Functional equivalent of [`otsys::otIp6NewMessage`](crate::otsys::otIp6NewMessage).
    pub fn ip6_new<T: ot::Boxable<OtType = otInstance>>(
        instance: &'a T,
        settings: Option<&Settings>,
    ) -> Result<ot::Box<Message<'a>>, ot::NoBufs> {
        unsafe {
            // This is safe because we own the `otMessage*` resulting from
            // `otIp6NewMessage`. Safety is also dependent on the correctness
            //  of the implementation of `otIp6NewMessage`.
            ot::Box::from_ot_ptr(otIp6NewMessage(
                instance.as_ot_ptr(),
                settings.map(|x| x.as_ref().as_ot_ptr()).unwrap_or(null()),
            ))
        }
        .ok_or(ot::NoBufs)
    }

    /// Functional equivalent of
    /// [`otsys::otIp6NewMessageFromBuffer`](crate::otsys::otIp6NewMessageFromBuffer).
    ///
    /// Note that, due to limitation in how the underlying method can report errors,
    /// it is impossible to differentiate the "NoBufs" error condition from the "IPv6 header
    /// malformed" error condition, hence the unusual error type.
    pub fn ip6_new_from_bytes<T: ot::Boxable<OtType = otInstance>, D: AsRef<[u8]>>(
        instance: &'a T,
        data: D,
        settings: Option<&Settings>,
    ) -> Result<ot::Box<Message<'a>>, ot::MalformedOrNoBufs> {
        let data = data.as_ref();
        unsafe {
            // SAFETY: This is safe because we own the `otMessage*` resulting from
            //         `otIp6NewMessageFromBuffer`. Safety is also dependent on the correctness
            //         of the implementation of `otIp6NewMessageFromBuffer`.
            ot::Box::from_ot_ptr(otIp6NewMessageFromBuffer(
                instance.as_ot_ptr(),
                data.as_ptr(),
                data.len().try_into().expect("ot::Message::ip6_new_from_bytes: Buffer too large"),
                settings.map(|x| x.as_ref().as_ot_ptr()).unwrap_or(null()),
            ))
        }
        .ok_or(ot::MalformedOrNoBufs)
    }
}

impl<'a> Message<'a> {
    /// Functional equivalent of [`otsys::otIp6NewMessage`](crate::otsys::otIp6NewMessage).
    pub fn udp_new<T: ot::Boxable<OtType = otInstance>>(
        instance: &'a T,
        settings: Option<&Settings>,
    ) -> Result<ot::Box<Message<'a>>, ot::NoBufs> {
        unsafe {
            // This is safe because we own the `otMessage*` resulting from
            // `otUdpNewMessage`. Safety is also dependent on the correctness
            //  of the implementation of `otUdpNewMessage`.
            ot::Box::from_ot_ptr(otUdpNewMessage(
                instance.as_ot_ptr(),
                settings.map(|x| x.as_ref().as_ot_ptr()).unwrap_or(null()),
            ))
        }
        .ok_or(ot::NoBufs)
    }
}

impl<'a> Message<'a> {
    /// Functional equivalent of [`otsys::otMessageAppend`](crate::otsys::otMessageAppend).
    ///
    /// The length of `data` must be less than 2^16, or else the method will return
    /// an `Err` with [`ot::Error::InvalidArgs`].
    pub fn append(&mut self, data: &[u8]) -> Result {
        Error::from(unsafe {
            // We verify that the length of the slice will fit into
            // the length argument type, throwing an error if this is not possible.
            otMessageAppend(
                self.as_ot_ptr(),
                data.as_ptr() as *const ::std::os::raw::c_void,
                data.len().try_into().or(Err(Error::InvalidArgs))?,
            )
        })
        .into()
    }

    /// Functional equivalent of
    /// [`otsys::otMessageIsLinkSecurityEnabled`](crate::otsys::otMessageIsLinkSecurityEnabled).
    pub fn is_link_security_enabled(&self) -> bool {
        unsafe { otMessageIsLinkSecurityEnabled(self.as_ot_ptr()) }
    }

    /// Functional equivalent of [`otsys::otMessageGetLength`](crate::otsys::otMessageGetLength).
    pub fn len(&self) -> usize {
        unsafe { otMessageGetLength(self.as_ot_ptr()).into() }
    }

    /// Functional equivalent of [`otsys::otMessageSetLength`](crate::otsys::otMessageSetLength).
    ///
    /// In addition to the error codes outlined in `otsys::otMessageSetLength`, this method
    /// will return `Err(Error::InvalidArgs)` if the `len` parameter is larger than 65,535 bytes.
    pub fn set_len(&mut self, len: usize) -> Result {
        Error::from(unsafe {
            // SAFETY: We verify that the given length will fit into the
            //         the length argument type, throwing an error if this is not possible.
            otMessageSetLength(self.as_ot_ptr(), len.try_into().or(Err(Error::InvalidArgs))?)
        })
        .into()
    }

    /// Functional equivalent of [`otsys::otMessageGetOffset`](crate::otsys::otMessageGetOffset).
    pub fn offset(&self) -> usize {
        unsafe { otMessageGetOffset(self.as_ot_ptr()).into() }
    }

    /// Functional equivalent of [`otsys::otMessageSetOffset`](crate::otsys::otMessageSetOffset).
    ///
    /// Calling this method with an `offset` larger than or equal to exceed 2^16 will cause a panic.
    pub fn set_offset(&mut self, offset: usize) {
        unsafe {
            // SAFETY: We verify that the given offset will fit into the
            //         the offset argument type and panic if this is not possible.
            otMessageSetOffset(
                self.as_ot_ptr(),
                offset.try_into().expect("ot::Message::set_offset: Offset too large"),
            )
        }
    }

    /// This function sets/forces the message to be forwarded using direct transmission.
    /// Functional equivalent of
    /// [`otsys::otMessageSetDirectTransmission`](crate::otsys::otMessageSetDirectTransmission).
    ///
    /// Default setting for a new message is `false`.
    pub fn set_direct_transmission(&mut self, direct_transmission: bool) {
        unsafe {
            otMessageSetDirectTransmission(self.as_ot_ptr(), direct_transmission);
        }
    }

    /// Functional equivalent of [`otsys::otMessageGetRss`](crate::otsys::otMessageGetRss).
    pub fn rssi(&self) -> Decibels {
        unsafe { otMessageGetRss(self.as_ot_ptr()) }
    }

    /// Renders this message to a new `Vec<u8>` using
    /// [`otsys::otMessageRead`](crate::otsys::otMessageRead).
    pub fn to_vec(&self) -> Vec<u8> {
        let mut buffer = Vec::with_capacity(self.len());
        unsafe {
            buffer.set_len(self.len());
            otMessageRead(
                self.as_ot_ptr(),
                0,
                buffer.as_mut_ptr() as *mut core::ffi::c_void,
                otMessageGetLength(self.as_ot_ptr()),
            );
        }
        buffer
    }
}

/// Message buffer info.
/// Functional equivalent of [`otsys::otBufferInfo`](crate::otsys::otBufferInfo).
#[derive(Debug, Default, Copy, Clone)]
#[repr(transparent)]
pub struct BufferInfo(pub otBufferInfo);

impl_ot_castable!(BufferInfo, otBufferInfo);

/// Trait for probing message buffer internal metrics.
pub trait MessageBuffer {
    /// Functional equivalent of [`otsys::otMessageGetBufferInfo`](crate::otsys::otMessageGetBufferInfo).
    fn get_buffer_info(&self) -> BufferInfo;
}

impl<T: MessageBuffer + Boxable> MessageBuffer for ot::Box<T> {
    fn get_buffer_info(&self) -> BufferInfo {
        self.as_ref().get_buffer_info()
    }
}

impl MessageBuffer for Instance {
    fn get_buffer_info(&self) -> BufferInfo {
        let mut ret = BufferInfo::default();
        unsafe {
            otMessageGetBufferInfo(self.as_ot_ptr(), ret.as_ot_mut_ptr());
        }
        ret
    }
}
