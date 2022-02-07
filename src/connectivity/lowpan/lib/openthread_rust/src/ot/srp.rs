// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// for now, we allow this for this module because we can't apply it
// specifically to the type `ChangedFlags`, due to a bug in `bitflags!`.
#![allow(missing_docs)]

use crate::prelude_internal::*;
use std::borrow::Borrow;
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
        A: Borrow<CStr>,
        B: Borrow<CStr>,
    {
        let prev = prev.map(|x| x.as_ot_ptr()).unwrap_or(null());
        unsafe {
            SrpServerService::ref_from_ot_ptr(otSrpServerHostFindNextService(
                self.as_ot_ptr(),
                prev,
                flags.bits(),
                service_name.map(|x| x.borrow().as_ptr()).unwrap_or(null()),
                instance_name.map(|x| x.borrow().as_ptr()).unwrap_or(null()),
            ))
        }
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
/// Functional equivalent of
/// [`otsys::otSrpServerServiceUpdateId`](crate::otsys::otSrpServerServiceUpdateId).
pub type SrpServerServiceUpdateId = otSrpServerServiceUpdateId;

/// Helper type that holds onto a `SrpServerServiceUpdateId` and automatically closes
/// it out if it goes out of scope.
#[derive(Debug)]
pub struct SrpServerServiceUpdateResponder<'a, OT: SrpServer + ?Sized> {
    id: SrpServerServiceUpdateId,
    instance: &'a OT,
}

impl<'a, OT: SrpServer + ?Sized> SrpServerServiceUpdateResponder<'a, OT> {
    /// Takes ownership of a `SrpServerServiceUpdateId`.
    pub fn new(instance: &'a OT, id: SrpServerServiceUpdateId) -> Self {
        Self { id, instance }
    }

    /// Converts this instance into the underlying ID value.
    pub fn into_inner(self) -> SrpServerServiceUpdateId {
        let ret = self.id;
        core::mem::forget(self);
        ret
    }

    /// Responds to the service update.
    pub fn respond(self, result: Result) {
        self.instance.srp_server_handle_service_update_result(self.id, result);
        core::mem::forget(self)
    }
}

impl<'a, OT: SrpServer + ?Sized> Drop for SrpServerServiceUpdateResponder<'a, OT> {
    fn drop(&mut self) {
        self.instance.srp_server_handle_service_update_result(self.id, Ok(()));
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

    /// Functional equivalent of
    /// [`otsys::otSrpServerHandleServiceUpdateResult`](crate::otsys::otSrpServerHandleServiceUpdateResult).
    fn srp_server_handle_service_update_result(&self, id: SrpServerServiceUpdateId, result: Result);

    /// Functional equivalent of
    /// [`otsys::otSrpServerSetServiceUpdateHandler`](crate::otsys::otSrpServerSetServiceUpdateHandler).
    fn srp_server_set_service_update_fn<'a, F>(&'a self, f: Option<F>)
    where
        F: FnMut(SrpServerServiceUpdateId, &'a SrpServerHost, u32) + 'a;
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
        F: FnMut(SrpServerServiceUpdateId, &'a SrpServerHost, u32) + 'a,
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
            otSrpServerHandleServiceUpdateResult(self.as_ot_ptr(), id, Error::from(result).into())
        }
    }

    fn srp_server_set_service_update_fn<'a, F>(&'a self, f: Option<F>)
    where
        F: FnMut(SrpServerServiceUpdateId, &'a SrpServerHost, u32) + 'a,
    {
        unsafe extern "C" fn _ot_srp_server_service_update_handler<'a, F>(
            id: otSrpServerServiceUpdateId,
            host: *const otSrpServerHost,
            timeout: u32,
            context: *mut ::std::os::raw::c_void,
        ) where
            F: FnMut(otSrpServerServiceUpdateId, &'a SrpServerHost, u32) + 'a,
        {
            // Reconstitute a reference to our closure.
            let sender = &mut *(context as *mut F);

            sender(id, SrpServerHost::ref_from_ot_ptr(host).unwrap(), timeout)
        }

        let (fn_ptr, fn_box, cb): (_, _, otSrpServerServiceUpdateHandler) = if let Some(f) = f {
            let mut x = Box::new(f);

            (
                x.as_mut() as *mut _ as *mut ::std::os::raw::c_void,
                Some(
                    x as Box<
                        dyn FnMut(ot::SrpServerServiceUpdateId, &'a ot::SrpServerHost, u32) + 'a,
                    >,
                ),
                Some(_ot_srp_server_service_update_handler::<F>),
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
