// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Value of the separator byte between TXT entries.
pub const DNSSD_TXT_SEPARATOR_BYTE: u8 = 0x13;

/// Value of the separator character between TXT entries.
pub const DNSSD_TXT_SEPARATOR_CHAR: char = '\x13';

/// String containing the separator character used between TXT entries.
pub const DNSSD_TXT_SEPARATOR_STR: &str = "\x13";

/// Converts a iterator of strings into a single string separated with
/// [`DNSSD_TXT_SEPARATOR_STR`].
pub fn dnssd_flatten_txt<I: IntoIterator<Item = String>>(txt: I) -> String {
    txt.into_iter().collect::<Vec<_>>().join(ot::DNSSD_TXT_SEPARATOR_STR)
}

/// Splits a TXT record from OpenThread into individual values.
pub fn dnssd_split_txt(txt: &str) -> impl Iterator<Item = &'_ str> {
    txt.split(|x| x == ot::DNSSD_TXT_SEPARATOR_CHAR).filter(|x| !x.is_empty())
}

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
    ResolveService = otDnssdQueryType_OT_DNSSD_QUERY_TYPE_RESOLVE as isize,

    /// Functional equivalent of [`otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_RESOLVE_HOST`](crate::otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_RESOLVE_HOST).
    ResolveHost = otDnssdQueryType_OT_DNSSD_QUERY_TYPE_RESOLVE_HOST as isize,
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
    ///
    /// Arguments:
    ///
    /// * `prev`: Reference to the previous `DnssdQuery`, or `None` to get the first DnssdQuery.
    fn dnssd_get_next_query(&self, prev: Option<&DnssdQuery>) -> Option<&DnssdQuery>;

    /// Functional equivalent to `otDnssdQueryHandleDiscoveredHost`.
    fn dnssd_query_handle_discovered_host(
        &self,
        hostname: &CStr,
        addresses: &[Ip6Address],
        ttl: u32,
    );

    /// Functional equivalent to `otDnssdQueryHandleDiscoveredServiceInstance`.
    ///
    /// Arguments:
    ///
    /// * `service_full_name`: Full service name (e.g.`_ipps._tcp.default.service.arpa.`)
    ///                        Must end with `.`.
    /// * `addresses`: Reference to array of addresses for service.
    /// * `full_name`: Full instance name (e.g.`OpenThread._ipps._tcp.default.service.arpa.`).
    ///                Must end with `.`.
    /// * `host_name`: Host name (e.g. `ot-host.default.service.arpa.`). Must end with `.`.
    /// * `port`: Service port.
    /// * `priority`: Service priority.
    /// * `ttl`: Service TTL (in seconds).
    /// * `txt_data`: Array of bytes representing the TXT record.
    /// * `weight`: Service weight.
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
#[derive(Debug, Clone)]
pub struct DnssdQueryIterator<'a, T: Dnssd> {
    prev: Option<&'a DnssdQuery>,
    ot_instance: &'a T,
}

impl<'a, T: Dnssd> Iterator for DnssdQueryIterator<'a, T> {
    type Item = &'a DnssdQuery;

    fn next(&mut self) -> Option<Self::Item> {
        self.prev = self.ot_instance.dnssd_get_next_query(self.prev);
        self.prev
    }
}

/// Extension trait for the trait [`Dnssd`].
pub trait DnssdExt: Dnssd {
    /// Iterator for easily iterating over all of the DNS-SD queries.
    fn dnssd_queries(&self) -> DnssdQueryIterator<'_, Self>
    where
        Self: Sized,
    {
        DnssdQueryIterator { prev: None, ot_instance: self }
    }
}

impl<T: Dnssd> DnssdExt for T {}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_split_txt() {
        assert_eq!(
            ot::dnssd_split_txt("").map(ToString::to_string).collect::<Vec<_>>(),
            Vec::<String>::new()
        );
        assert_eq!(
            ot::dnssd_split_txt("\x13\x13\x13\x13").map(ToString::to_string).collect::<Vec<_>>(),
            Vec::<String>::new()
        );
        assert_eq!(
            ot::dnssd_split_txt("\x13xa=a7bfc4981f4e4d22\x13xp=029c6f4dbae059cb")
                .map(ToString::to_string)
                .collect::<Vec<_>>(),
            vec!["xa=a7bfc4981f4e4d22".to_string(), "xp=029c6f4dbae059cb".to_string()]
        );
        assert_eq!(
            ot::dnssd_split_txt("xa=a7bfc4981f4e4d22\x13xp=029c6f4dbae059cb")
                .map(ToString::to_string)
                .collect::<Vec<_>>(),
            vec!["xa=a7bfc4981f4e4d22".to_string(), "xp=029c6f4dbae059cb".to_string()]
        );
    }

    #[test]
    fn test_flatten_txt() {
        assert_eq!(ot::dnssd_flatten_txt(None), String::default());
        assert_eq!(ot::dnssd_flatten_txt(vec![]), String::default());
        assert_eq!(
            ot::dnssd_flatten_txt(vec!["xa=a7bfc4981f4e4d22".to_string()]),
            "xa=a7bfc4981f4e4d22".to_string()
        );
        assert_eq!(
            ot::dnssd_flatten_txt(
                Some(vec!["xa=a7bfc4981f4e4d22".to_string()]).into_iter().flatten()
            ),
            "xa=a7bfc4981f4e4d22".to_string()
        );
        assert_eq!(
            ot::dnssd_flatten_txt(
                Some(vec!["xa=a7bfc4981f4e4d22".to_string(), "xp=029c6f4dbae059cb".to_string()])
                    .into_iter()
                    .flatten()
            ),
            "xa=a7bfc4981f4e4d22\x13xp=029c6f4dbae059cb".to_string()
        );
    }
}
