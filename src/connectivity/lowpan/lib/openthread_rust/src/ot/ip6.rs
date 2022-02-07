// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Functional equivalent of [`otsys::otIcmp6EchoMode`](crate::otsys::otIcmp6EchoMode).
#[derive(Debug, Copy, Clone, Eq, Ord, PartialOrd, PartialEq, num_derive::FromPrimitive)]
pub enum Icmp6EchoMode {
    /// ICMPv6 Echo processing enabled for unicast and multicast requests.
    HandleAll = otIcmp6EchoMode_OT_ICMP6_ECHO_HANDLER_ALL as isize,

    /// ICMPv6 Echo processing disabled.
    HandleDisabled = otIcmp6EchoMode_OT_ICMP6_ECHO_HANDLER_DISABLED as isize,

    /// ICMPv6 Echo processing enabled only for multicast requests only.
    HandleMulticastOnly = otIcmp6EchoMode_OT_ICMP6_ECHO_HANDLER_MULTICAST_ONLY as isize,

    /// ICMPv6 Echo processing enabled only for unicast requests only.
    HandleUnicastOnly = otIcmp6EchoMode_OT_ICMP6_ECHO_HANDLER_UNICAST_ONLY as isize,
}

impl From<otIcmp6EchoMode> for Icmp6EchoMode {
    fn from(x: otIcmp6EchoMode) -> Self {
        use num::FromPrimitive;
        Self::from_u32(x).expect(format!("Unknown otIcmp6EchoMode value: {}", x).as_str())
    }
}

impl From<Icmp6EchoMode> for otIcmp6EchoMode {
    fn from(x: Icmp6EchoMode) -> Self {
        x as otIcmp6EchoMode
    }
}

/// Methods from the [OpenThread "IPv6" Module](https://openthread.io/reference/group/api-ip6).
pub trait Ip6 {
    /// Functional equivalent of [`otsys::otIp6Send`](crate::otsys::otIp6Send).
    fn ip6_send(&self, message: OtMessageBox<'_>) -> Result;

    /// Similar to [`ip6_send()`], but takes a byte slice instead
    /// of an [`OtMessageBox`](crate::OtMessageBox).
    fn ip6_send_data(&self, data: &[u8]) -> Result;

    /// Similar to [`ip6_send_data()`], but sends the packet without layer-2 security.
    fn ip6_send_data_direct(&self, data: &[u8]) -> Result;

    /// Functional equivalent of [`otsys::otIp6IsEnabled`](crate::otsys::otIp6IsEnabled).
    fn ip6_is_enabled(&self) -> bool;

    /// Functional equivalent of [`otsys::otIp6SetEnabled`](crate::otsys::otIp6SetEnabled).
    fn ip6_set_enabled(&self, enabled: bool) -> Result;

    /// Functional equivalent of
    /// [`otsys::otIp6AddUnicastAddress`](crate::otsys::otIp6AddUnicastAddress).
    fn ip6_add_unicast_address(&self, addr: &NetifAddress) -> Result;

    /// Functional equivalent of
    /// [`otsys::otIp6RemoveUnicastAddress`](crate::otsys::otIp6RemoveUnicastAddress).
    fn ip6_remove_unicast_address(&self, addr: &Ip6Address) -> Result;

    /// Functional equivalent of
    /// [`otsys::otIp6SubscribeMulticastAddress`](crate::otsys::otIp6SubscribeMulticastAddress).
    fn ip6_join_multicast_group(&self, addr: &Ip6Address) -> Result;

    /// Functional equivalent of
    /// [`otsys::otIp6UnsubscribeMulticastAddress`](crate::otsys::otIp6UnsubscribeMulticastAddress).
    fn ip6_leave_multicast_group(&self, addr: &Ip6Address) -> Result;

    /// Sets the IPv6 receive callback closure. Functional equivalent of
    /// [`otsys::otIp6SetReceiveCallback`](crate::otsys::otIp6SetReceiveCallback).
    ///
    /// The closure will ultimately be executed via
    /// [`ot::Tasklets::process`](crate::ot::Tasklets::process).
    fn ip6_set_receive_fn<'a, F>(&'a self, f: Option<F>)
    where
        F: FnMut(OtMessageBox<'_>) + 'a;

    /// Sets the IPv6 address callback closure. Functional equivalent of
    /// [`otsys::otIp6SetAddressCallback`](crate::otsys::otIp6SetAddressCallback).
    ///
    /// The closure takes two arguments:
    ///
    ///  * An instance of [`ot::Ip6AddressInfo`](crate::ot::Ip6AddressInfo).
    ///  * A `bool` indicating if the address is being added (`true`) or removed (`false`).
    ///
    /// The closure will ultimately be executed via
    /// [`ot::Tasklets::process`](crate::ot::Tasklets::process).
    fn ip6_set_address_fn<'a, F>(&'a self, f: Option<F>)
    where
        F: for<'r> FnMut(Ip6AddressInfo<'r>, bool) + 'a;

    /// Functional equivalent of [`otsys::otIp6IsSlaacEnabled`](crate::otsys::otIp6IsSlaacEnabled).
    fn ip6_is_slaac_enabled(&self) -> bool;

    /// Functional equivalent of
    /// [`otsys::otIp6SetSlaacEnabled`](crate::otsys::otIp6SetSlaacEnabled).
    fn ip6_set_slaac_enabled(&self, enabled: bool);

    /// Functional equivalent of
    /// [`otsys::otIcmp6GetEchoMode`](crate::otsys::otIcmp6GetEchoMode).
    fn icmp6_get_echo_mode(&self) -> Icmp6EchoMode;

    /// Functional equivalent of
    /// [`otsys::otIcmp6SetEchoMode`](crate::otsys::otIcmp6SetEchoMode).
    fn icmp6_set_echo_mode(&self, mode: Icmp6EchoMode);
}

impl<T: Ip6 + ot::Boxable> Ip6 for ot::Box<T> {
    fn ip6_send(&self, message: OtMessageBox<'_>) -> Result<()> {
        self.as_ref().ip6_send(message)
    }

    fn ip6_send_data(&self, data: &[u8]) -> Result {
        self.as_ref().ip6_send_data(data)
    }

    fn ip6_send_data_direct(&self, data: &[u8]) -> Result {
        self.as_ref().ip6_send_data_direct(data)
    }

    fn ip6_is_enabled(&self) -> bool {
        self.as_ref().ip6_is_enabled()
    }

    fn ip6_set_enabled(&self, enabled: bool) -> Result {
        self.as_ref().ip6_set_enabled(enabled)
    }

    fn ip6_add_unicast_address(&self, addr: &NetifAddress) -> Result {
        self.as_ref().ip6_add_unicast_address(addr)
    }

    fn ip6_remove_unicast_address(&self, addr: &std::net::Ipv6Addr) -> Result {
        self.as_ref().ip6_remove_unicast_address(addr)
    }

    fn ip6_join_multicast_group(&self, addr: &std::net::Ipv6Addr) -> Result {
        self.as_ref().ip6_join_multicast_group(addr)
    }

    fn ip6_leave_multicast_group(&self, addr: &std::net::Ipv6Addr) -> Result {
        self.as_ref().ip6_join_multicast_group(addr)
    }

    fn ip6_set_receive_fn<'a, F>(&'a self, f: Option<F>)
    where
        F: FnMut(OtMessageBox<'_>) + 'a,
    {
        self.as_ref().ip6_set_receive_fn(f)
    }

    fn ip6_set_address_fn<'a, F>(&'a self, f: Option<F>)
    where
        F: for<'r> FnMut(Ip6AddressInfo<'r>, bool) + 'a,
    {
        self.as_ref().ip6_set_address_fn(f)
    }

    fn ip6_is_slaac_enabled(&self) -> bool {
        self.as_ref().ip6_is_slaac_enabled()
    }

    fn ip6_set_slaac_enabled(&self, enabled: bool) {
        self.as_ref().ip6_set_slaac_enabled(enabled);
    }

    fn icmp6_get_echo_mode(&self) -> Icmp6EchoMode {
        self.as_ref().icmp6_get_echo_mode()
    }

    fn icmp6_set_echo_mode(&self, mode: Icmp6EchoMode) {
        self.as_ref().icmp6_set_echo_mode(mode)
    }
}

impl Ip6 for Instance {
    fn ip6_send(&self, message: OtMessageBox<'_>) -> Result {
        Error::from(unsafe { otIp6Send(self.as_ot_ptr(), message.take_ot_ptr()) }).into()
    }

    fn ip6_send_data(&self, data: &[u8]) -> Result {
        if let Ok(msg) = Message::ip6_new_from_bytes(self, data, None) {
            self.ip6_send(msg)
        } else {
            if self.get_buffer_info().0.mFreeBuffers == 0 {
                Err(ot::Error::NoBufs)
            } else {
                Err(ot::Error::Failed)
            }
        }
    }

    fn ip6_send_data_direct(&self, data: &[u8]) -> Result {
        if let Ok(mut msg) = Message::ip6_new_from_bytes(self, data, None) {
            msg.set_direct_transmission(true);
            self.ip6_send(msg)
        } else {
            if self.get_buffer_info().0.mFreeBuffers == 0 {
                Err(ot::Error::NoBufs)
            } else {
                Err(ot::Error::Failed)
            }
        }
    }

    fn ip6_is_enabled(&self) -> bool {
        unsafe { otIp6IsEnabled(self.as_ot_ptr()) }
    }

    fn ip6_set_enabled(&self, enabled: bool) -> Result {
        Error::from(unsafe { otIp6SetEnabled(self.as_ot_ptr(), enabled) }).into()
    }

    fn ip6_add_unicast_address(&self, addr: &NetifAddress) -> Result {
        Error::from(unsafe { otIp6AddUnicastAddress(self.as_ot_ptr(), addr.as_ot_ptr()) }).into()
    }

    fn ip6_remove_unicast_address(&self, addr: &std::net::Ipv6Addr) -> Result {
        Error::from(unsafe { otIp6RemoveUnicastAddress(self.as_ot_ptr(), addr.as_ot_ptr()) }).into()
    }

    fn ip6_join_multicast_group(&self, addr: &std::net::Ipv6Addr) -> Result {
        Error::from(unsafe { otIp6SubscribeMulticastAddress(self.as_ot_ptr(), addr.as_ot_ptr()) })
            .into()
    }

    fn ip6_leave_multicast_group(&self, addr: &std::net::Ipv6Addr) -> Result {
        Error::from(unsafe { otIp6UnsubscribeMulticastAddress(self.as_ot_ptr(), addr.as_ot_ptr()) })
            .into()
    }

    fn ip6_set_receive_fn<'a, F>(&'a self, f: Option<F>)
    where
        F: FnMut(OtMessageBox<'_>) + 'a,
    {
        unsafe extern "C" fn _ot_ip6_receive_callback<'a, F: FnMut(OtMessageBox<'_>) + 'a>(
            message: *mut otMessage,
            context: *mut ::std::os::raw::c_void,
        ) {
            trace!("_ot_ip6_receive_callback");

            // Convert the `*otMessage` into an `ot::Box<ot::Message>`.
            let message = OtMessageBox::from_ot_ptr(message)
                .expect("_ot_ip6_receive_callback: Got NULL otMessage");

            // Reconstitute a reference to our closure.
            let sender = &mut *(context as *mut F);

            sender(message)
        }

        let (fn_ptr, fn_box, cb): (_, _, otIp6ReceiveCallback) = if let Some(f) = f {
            let mut x = Box::new(f);

            (
                x.as_mut() as *mut F as *mut ::std::os::raw::c_void,
                Some(x as Box<dyn FnMut(OtMessageBox<'_>) + 'a>),
                Some(_ot_ip6_receive_callback::<F>),
            )
        } else {
            (std::ptr::null_mut() as *mut ::std::os::raw::c_void, None, None)
        };

        unsafe {
            otIp6SetReceiveCallback(self.as_ot_ptr(), cb, fn_ptr);

            // Make sure our object eventually gets cleaned up.
            // Here we must also transmute our closure to have a 'static lifetime.
            // We need to do this because the borrow checker cannot infer the
            // proper lifetime for the singleton instance backing, but
            // this is guaranteed by the API.
            self.borrow_backing().ip6_receive_fn.set(std::mem::transmute::<
                Option<Box<dyn FnMut(OtMessageBox<'_>) + 'a>>,
                Option<Box<dyn FnMut(OtMessageBox<'_>) + 'static>>,
            >(fn_box));
        }
    }

    fn ip6_set_address_fn<'a, F>(&'a self, f: Option<F>)
    where
        F: for<'r> FnMut(Ip6AddressInfo<'r>, bool) + 'a,
    {
        unsafe extern "C" fn _ot_ip6_address_callback<
            'a,
            F: FnMut(Ip6AddressInfo<'_>, bool) + 'a,
        >(
            info: *const otIp6AddressInfo,
            is_added: bool,
            context: *mut ::std::os::raw::c_void,
        ) {
            trace!("_ot_ip6_address_callback");

            // Convert the `*otIp6AddressInfo` into an `&ot::Ip6AddressInfo`.
            let info = Ip6AddressInfo::ref_from_ot_ptr(info).unwrap().clone();

            // Reconstitute a reference to our closure.
            let sender = &mut *(context as *mut F);

            sender(info, is_added)
        }

        let (fn_ptr, fn_box, cb): (_, _, otIp6AddressCallback) = if let Some(f) = f {
            let mut x = Box::new(f);

            (
                x.as_mut() as *mut F as *mut ::std::os::raw::c_void,
                Some(x as Box<dyn FnMut(Ip6AddressInfo<'_>, bool) + 'a>),
                Some(_ot_ip6_address_callback::<F>),
            )
        } else {
            (std::ptr::null_mut() as *mut ::std::os::raw::c_void, None, None)
        };

        unsafe {
            otIp6SetAddressCallback(self.as_ot_ptr(), cb, fn_ptr);

            // Make sure our object eventually gets cleaned up.
            // Here we must also transmute our closure to have a 'static lifetime.
            // We need to do this because the borrow checker cannot infer the
            // proper lifetime for the singleton instance backing, but
            // this is guaranteed by the API.
            self.borrow_backing().ip6_address_fn.set(std::mem::transmute::<
                Option<Box<dyn FnMut(Ip6AddressInfo<'_>, bool) + 'a>>,
                Option<Box<dyn FnMut(Ip6AddressInfo<'_>, bool) + 'static>>,
            >(fn_box));
        }
    }

    fn ip6_is_slaac_enabled(&self) -> bool {
        unsafe { otIp6IsSlaacEnabled(self.as_ot_ptr()) }
    }

    fn ip6_set_slaac_enabled(&self, enabled: bool) {
        unsafe { otIp6SetSlaacEnabled(self.as_ot_ptr(), enabled) }
    }

    fn icmp6_get_echo_mode(&self) -> Icmp6EchoMode {
        unsafe { otIcmp6GetEchoMode(self.as_ot_ptr()) }.into()
    }

    fn icmp6_set_echo_mode(&self, mode: Icmp6EchoMode) {
        unsafe { otIcmp6SetEchoMode(self.as_ot_ptr(), mode.into()) }
    }
}
