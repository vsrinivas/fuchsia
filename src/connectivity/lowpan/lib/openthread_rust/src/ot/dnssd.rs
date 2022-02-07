// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Represents the type of a DNS query
///
/// Functional equivalent of [`otsys::otDnsQueryType`](crate::otsys::otDnsQueryType).
#[derive(Debug, Copy, Clone, Eq, Ord, PartialOrd, PartialEq, num_derive::FromPrimitive)]
#[allow(missing_docs)]
pub enum DnssdQueryType {
    /// Functional equivalent of [`otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_NONE`](crate::otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_NONE).
    None = otDnssdQueryType_OT_DNSSD_QUERY_TYPE_NONE as isize,

    /// Functional equivalent of [`otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_BROWSE`](crate::otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_BROWSE).
    Browse = otDnssdQueryType_OT_DNSSD_QUERY_TYPE_BROWSE as isize,

    /// Functional equivalent of [`otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_RESOLVE`](crate::otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_RESOLVE).
    Resolve = otDnssdQueryType_OT_DNSSD_QUERY_TYPE_RESOLVE as isize,

    /// Functional equivalent of [`otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_RESOLVE_HOST`](crate::otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_RESOLVE_HOST).
    Host = otDnssdQueryType_OT_DNSSD_QUERY_TYPE_RESOLVE_HOST as isize,
}

/// Functional equivalent of `otDnssdQuery`
#[repr(transparent)]
pub struct DnssdQuery(otDnssdQuery, PhantomData<*mut otDnssdQuery>);

impl_ot_castable!(opaque DnssdQuery, otDnssdQuery);

impl std::fmt::Debug for DnssdQuery {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let (query_type, name) = self.query_type_and_name();
        f.debug_struct("otDnsQuery").field("type", &query_type).field("name", &name).finish()
    }
}

impl DnssdQuery {
    /// Functional equivalent of `otDnssdGetQueryTypeAndName`.
    pub fn query_type_and_name(&self) -> (DnssdQueryType, CString) {
        let mut bytes: [::std::os::raw::c_char; OT_DNS_MAX_NAME_SIZE as usize] =
            [0; OT_DNS_MAX_NAME_SIZE as usize];
        let query_type = unsafe {
            otDnssdGetQueryTypeAndName(
                self.as_ot_ptr(),
                bytes.as_mut_ptr() as *mut [::std::os::raw::c_char; OT_DNS_MAX_NAME_SIZE as usize],
            )
        };
        let name = unsafe { CStr::from_ptr(bytes.as_ptr()) };

        (DnssdQueryType::from_u32(query_type).unwrap(), name.to_owned())
    }
}

/// Methods from the [OpenThread DNS-SD Server Module][1].
///
/// [1]: https://openthread.io/reference/group/api-dnssd-server
pub trait Dnssd {
    /// Functional equivalent to `otDnssdGetNextQuery`.
    fn dnssd_get_next_query(&self, prev: Option<&DnssdQuery>) -> Option<&DnssdQuery>;

    /// Functional equivalent to `otDnssdQueryHandleDiscoveredHost`.
    fn dnssd_query_handle_discovered_host(
        &self,
        hostname: &CStr,
        addresses: &[Ip6Address],
        ttl: u32,
    );

    /// Functional equivalent to `otDnssdQueryHandleDiscoveredServiceInstance`.
    fn dnssd_query_handle_discovered_service_instance(
        &self,
        service_full_name: &CStr,
        addresses: &[Ip6Address],
        full_name: &CStr,
        host_name: &CStr,
        port: u16,
        priority: u16,
        ttl: u32,
        txt_data: &[u8],
        weight: u16,
    );

    /// Functional equivalent of
    /// [`otsys::otDnssdQuerySetCallbacks`](crate::otsys::otDnssdQuerySetCallbacks).
    ///
    /// The callback closure takes two arguments:
    ///
    /// * bool: True if subscribing, false if unsubscribing.
    /// * &CStr: Full name.
    fn dnssd_query_set_callbacks<'a, F>(&'a self, f: Option<F>)
    where
        F: FnMut(bool, &CStr) + 'a;
}

impl<T: Dnssd + Boxable> Dnssd for ot::Box<T> {
    fn dnssd_get_next_query(&self, prev: Option<&DnssdQuery>) -> Option<&DnssdQuery> {
        self.as_ref().dnssd_get_next_query(prev)
    }

    fn dnssd_query_handle_discovered_host(
        &self,
        hostname: &CStr,
        addresses: &[Ip6Address],
        ttl: u32,
    ) {
        self.as_ref().dnssd_query_handle_discovered_host(hostname, addresses, ttl);
    }

    fn dnssd_query_handle_discovered_service_instance(
        &self,
        service_full_name: &CStr,
        addresses: &[Ip6Address],
        full_name: &CStr,
        host_name: &CStr,
        port: u16,
        priority: u16,
        ttl: u32,
        txt_data: &[u8],
        weight: u16,
    ) {
        self.as_ref().dnssd_query_handle_discovered_service_instance(
            service_full_name,
            addresses,
            full_name,
            host_name,
            port,
            priority,
            ttl,
            txt_data,
            weight,
        );
    }

    fn dnssd_query_set_callbacks<'a, F>(&'a self, f: Option<F>)
    where
        F: FnMut(bool, &CStr) + 'a,
    {
        self.as_ref().dnssd_query_set_callbacks(f)
    }
}

impl Dnssd for Instance {
    fn dnssd_get_next_query(&self, prev: Option<&DnssdQuery>) -> Option<&DnssdQuery> {
        use std::ptr::null_mut;
        unsafe {
            DnssdQuery::ref_from_ot_ptr(otDnssdGetNextQuery(
                self.as_ot_ptr(),
                prev.map(DnssdQuery::as_ot_ptr).unwrap_or(null_mut()),
            ) as *mut otDnssdQuery)
        }
    }

    fn dnssd_query_handle_discovered_host(
        &self,
        hostname: &CStr,
        addresses: &[Ip6Address],
        ttl: u32,
    ) {
        unsafe {
            otDnssdQueryHandleDiscoveredHost(
                self.as_ot_ptr(),
                hostname.as_ptr(),
                &mut otDnssdHostInfo {
                    mAddressNum: addresses.len().try_into().unwrap(),
                    mAddresses: addresses.as_ptr() as *const otIp6Address,
                    mTtl: ttl,
                } as *mut otDnssdHostInfo,
            )
        }
    }

    fn dnssd_query_handle_discovered_service_instance(
        &self,
        service_full_name: &CStr,
        addresses: &[Ip6Address],
        full_name: &CStr,
        host_name: &CStr,
        port: u16,
        priority: u16,
        ttl: u32,
        txt_data: &[u8],
        weight: u16,
    ) {
        unsafe {
            otDnssdQueryHandleDiscoveredServiceInstance(
                self.as_ot_ptr(),
                service_full_name.as_ptr(),
                &mut otDnssdServiceInstanceInfo {
                    mFullName: full_name.as_ptr(),
                    mHostName: host_name.as_ptr(),
                    mAddressNum: addresses.len().try_into().unwrap(),
                    mAddresses: addresses.as_ptr() as *const otIp6Address,
                    mPort: port,
                    mPriority: priority,
                    mWeight: weight,
                    mTxtLength: txt_data.len().try_into().unwrap(),
                    mTxtData: txt_data.as_ptr(),
                    mTtl: ttl,
                } as *mut otDnssdServiceInstanceInfo,
            )
        }
    }

    fn dnssd_query_set_callbacks<'a, F>(&'a self, f: Option<F>)
    where
        F: FnMut(bool, &CStr) + 'a,
    {
        unsafe extern "C" fn _ot_dnssd_query_subscribe_callback<'a, F: FnMut(bool, &CStr) + 'a>(
            context: *mut ::std::os::raw::c_void,
            full_name_ptr: *const c_char,
        ) {
            trace!("_ot_dnssd_query_subscribe_callback");

            let full_name_cstr = CStr::from_ptr(full_name_ptr);

            // Reconstitute a reference to our closure.
            let sender = &mut *(context as *mut F);

            sender(true, full_name_cstr)
        }

        unsafe extern "C" fn _ot_dnssd_query_unsubscribe_callback<
            'a,
            F: FnMut(bool, &CStr) + 'a,
        >(
            context: *mut ::std::os::raw::c_void,
            full_name_ptr: *const c_char,
        ) {
            trace!("_ot_dnssd_query_unsubscribe_callback");

            let full_name_cstr = CStr::from_ptr(full_name_ptr);

            // Reconstitute a reference to our closure.
            let sender = &mut *(context as *mut F);

            sender(false, full_name_cstr)
        }

        let (fn_ptr, fn_box, cb_sub, cb_unsub): (
            _,
            _,
            otDnssdQuerySubscribeCallback,
            otDnssdQueryUnsubscribeCallback,
        ) = if let Some(f) = f {
            let mut x = Box::new(f);

            (
                x.as_mut() as *mut F as *mut ::std::os::raw::c_void,
                Some(x as Box<dyn FnMut(bool, &CStr) + 'a>),
                Some(_ot_dnssd_query_subscribe_callback::<F>),
                Some(_ot_dnssd_query_unsubscribe_callback::<F>),
            )
        } else {
            (std::ptr::null_mut() as *mut ::std::os::raw::c_void, None, None, None)
        };

        unsafe {
            otDnssdQuerySetCallbacks(self.as_ot_ptr(), cb_sub, cb_unsub, fn_ptr);

            // Make sure our object eventually gets cleaned up.
            // Here we must also transmute our closure to have a 'static lifetime.
            // We need to do this because the borrow checker cannot infer the
            // proper lifetime for the singleton instance backing, but
            // this is guaranteed by the API.
            self.borrow_backing().dnssd_query_sub_unsub_fn.set(std::mem::transmute::<
                Option<Box<dyn FnMut(bool, &CStr) + 'a>>,
                Option<Box<dyn FnMut(bool, &CStr) + 'static>>,
            >(fn_box));
        }
    }
}

/// Iterator type for DNS-SD Queries
#[allow(missing_debug_implementations)]
pub struct DnssdQueryIterator<'a, T: ?Sized> {
    ot_instance: &'a T,
    current: Option<&'a DnssdQuery>,
}

impl<'a, T: ?Sized + Dnssd> Iterator for DnssdQueryIterator<'a, T> {
    type Item = (DnssdQueryType, CString);
    fn next(&mut self) -> Option<Self::Item> {
        self.current = self.ot_instance.dnssd_get_next_query(self.current);
        self.current.map(|x| x.query_type_and_name())
    }
}

/// Extension trait for the trait [`Dnssd`].
pub trait DnssdExt {
    /// Iterator for easily iterating over all of the DNS-SD queries.
    fn dnssd_query_iter(&self) -> DnssdQueryIterator<'_, Self> {
        DnssdQueryIterator { ot_instance: self, current: None }
    }
}

impl<T: Dnssd> DnssdExt for T {}
