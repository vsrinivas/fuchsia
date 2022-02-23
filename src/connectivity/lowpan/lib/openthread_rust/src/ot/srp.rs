// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// for now, we allow this for this module because we can't apply it
// specifically to the type `ChangedFlags`, due to a bug in `bitflags!`.
#![allow(missing_docs)]

use crate::prelude_internal::*;
use std::ffi::CStr;
use std::ptr::null;

bitflags::bitflags! {
#[repr(C)]
#[derive(Default)]
pub struct SrpServerServiceFlags : u8 {
    const BASE_TYPE = OT_SRP_SERVER_SERVICE_FLAG_BASE_TYPE as u8;
    const SUB_TYPE = OT_SRP_SERVER_SERVICE_FLAG_SUB_TYPE as u8;
    const ACTIVE = OT_SRP_SERVER_SERVICE_FLAG_ACTIVE as u8;
    const DELETED = OT_SRP_SERVER_SERVICE_FLAG_DELETED as u8;

    const ANY_SERVICE =
        Self::BASE_TYPE.bits | Self::SUB_TYPE.bits | Self::ACTIVE.bits | Self::DELETED.bits;
    const BASE_TYPE_SERVICE_ONLY =
        Self::BASE_TYPE.bits | Self::ACTIVE.bits | Self::DELETED.bits;
    const SUB_TYPE_SERVICE_ONLY =
        Self::SUB_TYPE.bits | Self::ACTIVE.bits | Self::DELETED.bits;
    const ANY_TYPE_ACTIVE_SERVICE =
        Self::BASE_TYPE.bits | Self::SUB_TYPE.bits | Self::ACTIVE.bits;
    const ANY_TYPE_DELETED_SERVICE =
        Self::BASE_TYPE.bits | Self::SUB_TYPE.bits | Self::ACTIVE.bits;
}
}

/// Iterates over the available SRP server hosts. See [`SrpServer::srp_server_get_hosts`].
#[derive(Debug, Clone)]
pub struct SrpServerHostIterator<'a, T: SrpServer> {
    prev: Option<&'a SrpServerHost>,
    ot_instance: &'a T,
}

impl<'a, T: SrpServer> Iterator for SrpServerHostIterator<'a, T> {
    type Item = &'a SrpServerHost;

    fn next(&mut self) -> Option<Self::Item> {
        self.prev = self.ot_instance.srp_server_next_host(self.prev);
        self.prev
    }
}

/// Iterates over all the available SRP services.
#[derive(Debug, Clone)]
pub struct SrpServerServiceIterator<'a> {
    prev: Option<&'a SrpServerService>,
    host: &'a SrpServerHost,
}

impl<'a> Iterator for SrpServerServiceIterator<'a> {
    type Item = &'a SrpServerService;

    fn next(&mut self) -> Option<Self::Item> {
        self.prev = self.host.next_service(self.prev);
        self.prev
    }
}

/// Iterates over selected SRP services.
#[derive(Debug, Clone)]
pub struct SrpServerServiceFindIterator<'a, A, B> {
    prev: Option<&'a SrpServerService>,
    host: &'a SrpServerHost,
    flags: SrpServerServiceFlags,
    service_name: Option<A>,
    instance_name: Option<B>,
}

impl<'a, A, B> Iterator for SrpServerServiceFindIterator<'a, A, B>
where
    A: AsRef<CStr>,
    B: AsRef<CStr>,
{
    type Item = &'a SrpServerService;

    fn next(&mut self) -> Option<Self::Item> {
        self.prev = self.host.find_next_service(
            self.prev,
            self.flags,
            self.service_name.as_ref(),
            self.instance_name.as_ref(),
        );
        self.prev
    }
}

/// This opaque type (only used by reference) represents a SRP host.
///
/// Functional equivalent of [`otsys::otSrpServerHost`](crate::otsys::otSrpServerHost).
#[derive(Debug)]
#[repr(transparent)]
pub struct SrpServerHost(otSrpServerHost);
impl_ot_castable!(opaque SrpServerHost, otSrpServerHost);

impl SrpServerHost {
    /// Functional equivalent of
    /// [`otsys::otSrpServerHostGetAddresses`](crate::otsys::otSrpServerHostGetAddresses).
    pub fn addresses(&self) -> &[Ip6Address] {
        let mut addresses_len = 0u8;
        unsafe {
            let addresses_ptr =
                otSrpServerHostGetAddresses(self.as_ot_ptr(), &mut addresses_len as *mut u8);

            std::slice::from_raw_parts(addresses_ptr as *const Ip6Address, addresses_len as usize)
        }
    }

    /// Functional equivalent of
    /// [`otsys::otSrpServerHostGetFullName`](crate::otsys::otSrpServerHostGetFullName).
    pub fn full_name_cstr(&self) -> &CStr {
        unsafe { CStr::from_ptr(otSrpServerHostGetFullName(self.as_ot_ptr())) }
    }

    /// Functional equivalent of
    /// [`otsys::otSrpServerHostIsDeleted`](crate::otsys::otSrpServerHostIsDeleted).
    pub fn is_deleted(&self) -> bool {
        unsafe { otSrpServerHostIsDeleted(self.as_ot_ptr()) }
    }

    /// Functional equivalent of
    /// [`otsys::otSrpServerHostGetNextService`](crate::otsys::otSrpServerHostGetNextService).
    pub fn next_service<'a>(
        &'a self,
        prev: Option<&'a SrpServerService>,
    ) -> Option<&'a SrpServerService> {
        let prev = prev.map(|x| x.as_ot_ptr()).unwrap_or(null());
        unsafe {
            SrpServerService::ref_from_ot_ptr(otSrpServerHostGetNextService(self.as_ot_ptr(), prev))
        }
    }

    /// Returns an iterator over all of the services for this host.
    pub fn services(&self) -> SrpServerServiceIterator<'_> {
        SrpServerServiceIterator { prev: None, host: self }
    }

    /// Functional equivalent of
    /// [`otsys::otSrpServerHostFindNextService`](crate::otsys::otSrpServerHostFindNextService).
    pub fn find_next_service<'a, A, B>(
        &'a self,
        prev: Option<&'a SrpServerService>,
        flags: SrpServerServiceFlags,
        service_name: Option<A>,
        instance_name: Option<B>,
    ) -> Option<&'a SrpServerService>
    where
        A: AsRef<CStr>,
        B: AsRef<CStr>,
    {
        let prev = prev.map(|x| x.as_ot_ptr()).unwrap_or(null());
        unsafe {
            SrpServerService::ref_from_ot_ptr(otSrpServerHostFindNextService(
                self.as_ot_ptr(),
                prev,
                flags.bits(),
                service_name.map(|x| x.as_ref().as_ptr()).unwrap_or(null()),
                instance_name.map(|x| x.as_ref().as_ptr()).unwrap_or(null()),
            ))
        }
    }

    /// Returns an iterator over all of the services matching the given criteria.
    pub fn find_services<A, B>(
        &self,
        flags: SrpServerServiceFlags,
        service_name: Option<A>,
        instance_name: Option<B>,
    ) -> SrpServerServiceFindIterator<'_, A, B>
    where
        A: AsRef<CStr>,
        B: AsRef<CStr>,
    {
        SrpServerServiceFindIterator { prev: None, host: self, flags, service_name, instance_name }
    }
}

/// This opaque type (only used by reference) represents a SRP service.
///
/// Functional equivalent of [`otsys::otSrpServerService`](crate::otsys::otSrpServerService).
#[derive(Debug)]
#[repr(transparent)]
pub struct SrpServerService(otSrpServerService);
impl_ot_castable!(opaque SrpServerService, otSrpServerService);

impl SrpServerService {
    /// Functional equivalent of
    /// [`otsys::otSrpServerServiceGetFullName`](crate::otsys::otSrpServerServiceGetFullName).
    pub fn full_name_cstr(&self) -> &CStr {
        unsafe { CStr::from_ptr(otSrpServerServiceGetFullName(self.as_ot_ptr())) }
    }

    /// Functional equivalent of
    /// [`otsys::otSrpServerServiceGetPort`](crate::otsys::otSrpServerServiceGetPort).
    pub fn port(&self) -> u16 {
        unsafe { otSrpServerServiceGetPort(self.as_ot_ptr()) }
    }

    /// Functional equivalent of
    /// [`otsys::otSrpServerServiceGetPriority`](crate::otsys::otSrpServerServiceGetPriority).
    pub fn priority(&self) -> u16 {
        unsafe { otSrpServerServiceGetPriority(self.as_ot_ptr()) }
    }

    /// Functional equivalent of
    /// [`otsys::otSrpServerServiceIsDeleted`](crate::otsys::otSrpServerServiceIsDeleted).
    pub fn is_deleted(&self) -> bool {
        unsafe { otSrpServerServiceIsDeleted(self.as_ot_ptr()) }
    }

    /// Functional equivalent of
    /// [`otsys::otSrpServerServiceGetTxtData`](crate::otsys::otSrpServerServiceGetTxtData).
    pub fn txt_data(&self) -> &[u8] {
        let mut txt_data_len = 0u16;
        unsafe {
            let txt_data_ptr =
                otSrpServerServiceGetTxtData(self.as_ot_ptr(), &mut txt_data_len as *mut u16);

            std::slice::from_raw_parts(txt_data_ptr, txt_data_len as usize)
        }
    }

    /// Functional equivalent of
    /// [`otsys::otSrpServerServiceGetWeight`](crate::otsys::otSrpServerServiceGetWeight).
    pub fn weight(&self) -> u16 {
        unsafe { otSrpServerServiceGetWeight(self.as_ot_ptr()) }
    }

    /// Functional equivalent of
    /// [`otsys::otSrpServerServiceIsSubType`](crate::otsys::otSrpServerServiceIsSubType).
    pub fn is_subtype(&self) -> bool {
        unsafe { otSrpServerServiceIsSubType(self.as_ot_ptr()) }
    }

    /// Functional equivalent of
    /// [`otsys::otSrpServerServiceGetServiceName`](crate::otsys::otSrpServerServiceGetServiceName).
    pub fn service_name_cstr(&self) -> &CStr {
        unsafe { CStr::from_ptr(otSrpServerServiceGetServiceName(self.as_ot_ptr())) }
    }

    /// Functional equivalent of
    /// [`otsys::otSrpServerServiceGetInstanceName`](crate::otsys::otSrpServerServiceGetInstanceName).
    pub fn instance_name_cstr(&self) -> &CStr {
        unsafe { CStr::from_ptr(otSrpServerServiceGetInstanceName(self.as_ot_ptr())) }
    }
}

/// The ID of a SRP service update transaction on the SRP Server.
///
/// This type will panic if dropped without being fed
/// to [`SrpServer::srp_server_handle_service_update_result`].
///
/// Functional equivalent of
/// [`otsys::otSrpServerServiceUpdateId`](crate::otsys::otSrpServerServiceUpdateId).
#[derive(Debug)]
pub struct SrpServerServiceUpdateId(otSrpServerServiceUpdateId);

impl SrpServerServiceUpdateId {
    fn new(x: otSrpServerServiceUpdateId) -> Self {
        Self(x)
    }

    fn take(self) -> otSrpServerServiceUpdateId {
        let ret = self.0;
        core::mem::forget(self);
        ret
    }
}

impl Drop for SrpServerServiceUpdateId {
    fn drop(&mut self) {
        panic!("SrpServerServiceUpdateId dropped without being passed to SrpServer::srp_server_handle_service_update_result");
    }
}

/// Server Methods from the [OpenThread SRP Module][1].
///
/// [1]: https://openthread.io/reference/group/api-srp
pub trait SrpServer {
    /// Functional equivalent of
    /// [`otsys::otSrpServerSetEnabled`](crate::otsys::otSrpServerSetEnabled).
    fn srp_server_set_enabled(&self, enabled: bool);

    /// Returns true if the SRP server is enabled.
    fn srp_server_is_enabled(&self) -> bool;

    /// Returns true if the SRP server is running, false if it is stopped or disabled.
    fn srp_server_is_running(&self) -> bool;

    /// Functional equivalent of
    /// [`otsys::otSrpServerSetDomain`](crate::otsys::otSrpServerSetDomain).
    fn srp_server_set_domain(&self, domain: &CStr) -> Result;

    /// Functional equivalent of
    /// [`otsys::otSrpServerGetDomain`](crate::otsys::otSrpServerGetDomain).
    fn srp_server_get_domain(&self) -> &CStr;

    /// Functional equivalent of
    /// [`otsys::otSrpServerGetNextHost`](crate::otsys::otSrpServerGetNextHost).
    fn srp_server_next_host<'a>(
        &'a self,
        prev: Option<&'a SrpServerHost>,
    ) -> Option<&'a SrpServerHost>;

    /// Returns an iterator over the SRP hosts.
    fn srp_server_hosts(&self) -> SrpServerHostIterator<'_, Self>
    where
        Self: Sized,
    {
        SrpServerHostIterator { prev: None, ot_instance: self }
    }

    /// Functional equivalent of
    /// [`otsys::otSrpServerHandleServiceUpdateResult`](crate::otsys::otSrpServerHandleServiceUpdateResult).
    fn srp_server_handle_service_update_result(&self, id: SrpServerServiceUpdateId, result: Result);

    /// Functional equivalent of
    /// [`otsys::otSrpServerSetServiceUpdateHandler`](crate::otsys::otSrpServerSetServiceUpdateHandler).
    fn srp_server_set_service_update_fn<'a, F>(&'a self, f: Option<F>)
    where
        F: FnMut(&'a ot::Instance, SrpServerServiceUpdateId, &'a SrpServerHost, u32) + 'a;
}

impl<T: SrpServer + Boxable> SrpServer for ot::Box<T> {
    fn srp_server_set_enabled(&self, enabled: bool) {
        self.as_ref().srp_server_set_enabled(enabled)
    }

    fn srp_server_is_enabled(&self) -> bool {
        self.as_ref().srp_server_is_enabled()
    }

    fn srp_server_is_running(&self) -> bool {
        self.as_ref().srp_server_is_running()
    }

    fn srp_server_set_domain(&self, domain: &CStr) -> Result {
        self.as_ref().srp_server_set_domain(domain)
    }

    fn srp_server_get_domain(&self) -> &CStr {
        self.as_ref().srp_server_get_domain()
    }

    fn srp_server_next_host<'a>(
        &'a self,
        prev: Option<&'a SrpServerHost>,
    ) -> Option<&'a SrpServerHost> {
        self.as_ref().srp_server_next_host(prev)
    }

    fn srp_server_handle_service_update_result(
        &self,
        id: SrpServerServiceUpdateId,
        result: Result,
    ) {
        self.as_ref().srp_server_handle_service_update_result(id, result)
    }

    fn srp_server_set_service_update_fn<'a, F>(&'a self, f: Option<F>)
    where
        F: FnMut(&'a ot::Instance, SrpServerServiceUpdateId, &'a SrpServerHost, u32) + 'a,
    {
        self.as_ref().srp_server_set_service_update_fn(f)
    }
}

impl SrpServer for Instance {
    fn srp_server_set_enabled(&self, enabled: bool) {
        unsafe { otSrpServerSetEnabled(self.as_ot_ptr(), enabled) }
    }

    fn srp_server_is_enabled(&self) -> bool {
        #[allow(non_upper_case_globals)]
        match unsafe { otSrpServerGetState(self.as_ot_ptr()) } {
            otSrpServerState_OT_SRP_SERVER_STATE_DISABLED => false,
            otSrpServerState_OT_SRP_SERVER_STATE_RUNNING => true,
            otSrpServerState_OT_SRP_SERVER_STATE_STOPPED => true,
            _ => panic!("Unexpected value from otSrpServerGetState"),
        }
    }

    fn srp_server_is_running(&self) -> bool {
        #[allow(non_upper_case_globals)]
        match unsafe { otSrpServerGetState(self.as_ot_ptr()) } {
            otSrpServerState_OT_SRP_SERVER_STATE_DISABLED => false,
            otSrpServerState_OT_SRP_SERVER_STATE_RUNNING => true,
            otSrpServerState_OT_SRP_SERVER_STATE_STOPPED => false,
            _ => panic!("Unexpected value from otSrpServerGetState"),
        }
    }

    fn srp_server_set_domain(&self, domain: &CStr) -> Result {
        Error::from(unsafe { otSrpServerSetDomain(self.as_ot_ptr(), domain.as_ptr()) }).into()
    }

    fn srp_server_get_domain(&self) -> &CStr {
        unsafe { CStr::from_ptr(otSrpServerGetDomain(self.as_ot_ptr())) }
    }

    fn srp_server_next_host<'a>(
        &'a self,
        prev: Option<&'a SrpServerHost>,
    ) -> Option<&'a SrpServerHost> {
        let prev = prev.map(|x| x.as_ot_ptr()).unwrap_or(null());
        unsafe { SrpServerHost::ref_from_ot_ptr(otSrpServerGetNextHost(self.as_ot_ptr(), prev)) }
    }

    fn srp_server_handle_service_update_result(
        &self,
        id: SrpServerServiceUpdateId,
        result: Result,
    ) {
        unsafe {
            otSrpServerHandleServiceUpdateResult(
                self.as_ot_ptr(),
                id.take(),
                Error::from(result).into(),
            )
        }
    }

    fn srp_server_set_service_update_fn<'a, F>(&'a self, f: Option<F>)
    where
        F: FnMut(&'a ot::Instance, SrpServerServiceUpdateId, &'a SrpServerHost, u32) + 'a,
    {
        unsafe extern "C" fn _ot_srp_server_service_update_handler<'a, F>(
            id: otSrpServerServiceUpdateId,
            host: *const otSrpServerHost,
            timeout: u32,
            context: *mut ::std::os::raw::c_void,
        ) where
            F: FnMut(SrpServerServiceUpdateId, &'a SrpServerHost, u32) + 'a,
        {
            // Reconstitute a reference to our closure.
            let sender = &mut *(context as *mut F);

            sender(
                SrpServerServiceUpdateId::new(id),
                SrpServerHost::ref_from_ot_ptr(host).unwrap(),
                timeout,
            )
        }

        // This helper func is just to allow us to get the type of the
        // wrapper closure (`f_wrapped`) so we can pass that type
        // as a type argument to `_ot_srp_server_service_update_handler`.
        fn get_service_update_handler<'a, X: 'a>(_: &X) -> otSrpServerServiceUpdateHandler
        where
            X: FnMut(SrpServerServiceUpdateId, &'a SrpServerHost, u32) + 'a,
        {
            Some(_ot_srp_server_service_update_handler::<X>)
        }

        let (fn_ptr, fn_box, cb): (_, _, otSrpServerServiceUpdateHandler) = if let Some(mut f) = f {
            // Grab a pointer to our ot::Instance for use below in the wrapper closure.
            let ot_instance_ptr = self.as_ot_ptr();

            // Wrap around `f` with a closure that fills in the `ot_instance` field, which we
            // won't have access to inside of `_ot_srp_server_service_update_handler::<_>`.
            let f_wrapped =
                move |id: SrpServerServiceUpdateId, host: &'a SrpServerHost, timeout: u32| {
                    // SAFETY: This ot::Instance will be passed to the original closure as
                    //         an argument. We know that it is valid because:
                    //         1. It is being called by the instance, so it must still be around.
                    //         2. By design there are no mutable references to the `ot::Instance`
                    //            in existence.
                    let ot_instance =
                        unsafe { ot::Instance::ref_from_ot_ptr(ot_instance_ptr) }.unwrap();
                    f(ot_instance, id, host, timeout)
                };

            // Since we don't have a way to directly refer to the type of the closure `f_wrapped`,
            // we need to use a helper function that can pass the type as a generic argument to
            // `_ot_srp_server_service_update_handler::<X>` as `X`.
            let service_update_handler = get_service_update_handler(&f_wrapped);

            let mut x = Box::new(f_wrapped);

            (
                x.as_mut() as *mut _ as *mut ::std::os::raw::c_void,
                Some(
                    x as Box<
                        dyn FnMut(ot::SrpServerServiceUpdateId, &'a ot::SrpServerHost, u32) + 'a,
                    >,
                ),
                service_update_handler,
            )
        } else {
            (std::ptr::null_mut() as *mut ::std::os::raw::c_void, None, None)
        };

        unsafe {
            otSrpServerSetServiceUpdateHandler(self.as_ot_ptr(), cb, fn_ptr);

            // Make sure our object eventually gets cleaned up.
            // Here we must also transmute our closure to have a 'static lifetime.
            // We need to do this because the borrow checker cannot infer the
            // proper lifetime for the singleton instance backing, but
            // this is guaranteed by the API.
            self.borrow_backing().srp_server_service_update_fn.set(std::mem::transmute::<
                Option<Box<dyn FnMut(SrpServerServiceUpdateId, &'a ot::SrpServerHost, u32) + 'a>>,
                Option<Box<dyn FnMut(SrpServerServiceUpdateId, &ot::SrpServerHost, u32) + 'static>>,
            >(fn_box));
        }
    }
}
