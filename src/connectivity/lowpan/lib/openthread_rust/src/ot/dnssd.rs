// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ot::WrongSize;
use crate::prelude_internal::*;

/// Functional equivalent of [`otsys::otDnsTxtEntry`](crate::otsys::otDnsTxtEntry).
#[derive(Default, Clone)]
pub struct DnsTxtEntry<'a>(pub otDnsTxtEntry, PhantomData<&'a str>);

impl_ot_castable!(lifetime DnsTxtEntry<'_>, otDnsTxtEntry, Default::default());

impl<'a> DnsTxtEntry<'a> {
    /// Tries to create a new `DnsTxtEntry` instance. Fails if value is too large.
    pub fn try_new(
        key: Option<&'a CStr>,
        value: Option<&'a [u8]>,
    ) -> Result<DnsTxtEntry<'a>, WrongSize> {
        let mut ret = DnsTxtEntry::default();

        ret.0.mKey = key.map(CStr::as_ptr).unwrap_or(std::ptr::null());

        if let Some(value) = value {
            ret.0.mValueLength = u16::try_from(value.len()).map_err(|_| WrongSize)?;
            ret.0.mValue = value.as_ptr();
        }

        Ok(ret)
    }

    /// Accessor for the `mKey` field from [`otsys::otDnsTxtEntry`](crate::otsys::otDnsTxtEntry).
    pub fn key_field(&self) -> Option<&'a CStr> {
        if self.0.mKey.is_null() {
            None
        } else {
            let cstr = unsafe { CStr::from_ptr(self.0.mKey) };
            Some(cstr)
        }
    }

    /// Accessor for the `mValue` field from [`otsys::otDnsTxtEntry`](crate::otsys::otDnsTxtEntry).
    pub fn value_field(&self) -> Option<&'a [u8]> {
        if self.0.mValue.is_null() {
            None
        } else {
            let value =
                unsafe { std::slice::from_raw_parts(self.0.mValue, self.0.mValueLength as usize) };
            Some(value)
        }
    }

    /// The key for this TXT entry.
    ///
    /// Will extract the key from the value field if `key_field()` returns `None`.
    pub fn key(&self) -> Option<&'a str> {
        if let Some(cstr) = self.key_field() {
            match cstr.to_str() {
                Ok(x) => Some(x),
                Err(x) => {
                    // We don't panic here because that would create a DoS
                    // vulnerability. Instead we log the error and return None.
                    warn!("Bad DNS TXT key {:?}: {:?}", cstr, x);
                    None
                }
            }
        } else if let Some(value) = self.value_field() {
            let key_bytes = value.splitn(2, |x| *x == b'=').next().unwrap();
            match std::str::from_utf8(key_bytes) {
                Ok(x) => Some(x),
                Err(x) => {
                    // We don't panic here because that would create a DoS
                    // vulnerability. Instead we log the error and return None.
                    warn!("Bad DNS TXT key {:?}: {:?}", key_bytes, x);
                    None
                }
            }
        } else {
            None
        }
    }

    /// The value for this TXT entry.
    ///
    /// Will only return the value part if the key is included in `value_field()`.
    pub fn value(&self) -> Option<&'a [u8]> {
        if let Some(value) = self.value_field() {
            if self.0.mKey.is_null() {
                let mut iter = value.splitn(2, |x| *x == b'=');
                let a = iter.next();
                let b = iter.next();
                match (a, b) {
                    (Some(_), Some(value)) => Some(value),
                    _ => None,
                }
            } else {
                Some(value)
            }
        } else {
            None
        }
    }

    /// Renders out this key-value pair to a `Vec<u8>`.
    pub fn to_vec(&self) -> Vec<u8> {
        let mut pair = vec![];
        match (self.key(), self.value()) {
            (Some(key), Some(value)) => {
                pair.extend_from_slice(key.as_bytes());
                pair.push(b'=');
                pair.extend_from_slice(value);
            }
            (Some(key), None) => {
                pair.extend_from_slice(key.as_bytes());
            }
            _ => {}
        }
        pair.truncate(u8::MAX as usize);
        pair
    }
}

impl<'a> std::fmt::Debug for DnsTxtEntry<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("DnsTxtEntry")
            .field("key_field", &self.key_field())
            .field("value_field", &self.value_field().map(ascii_dump))
            .finish()
    }
}

impl<'a> std::fmt::Display for DnsTxtEntry<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", ascii_dump(&self.to_vec()))
    }
}

/// Functional equivalent of [`otsys::otDnsTxtEntryIterator`](crate::otsys::otDnsTxtEntryIterator).
#[derive(Default, Debug, Clone)]
pub struct DnsTxtEntryIterator<'a>(pub otDnsTxtEntryIterator, PhantomData<&'a str>);

impl_ot_castable!(lifetime DnsTxtEntryIterator<'_>, otDnsTxtEntryIterator, Default::default());

impl<'a> DnsTxtEntryIterator<'a> {
    /// Tries to create a new `DnsTxtEntry` instance. Functional equivalent
    /// of [`otsys::otDnsInitTxtEntryIterator`](crate::otsys::otDnsInitTxtEntryIterator).
    ///
    /// Fails if `txt_data` is too large.
    pub fn try_new(txt_data: &'a [u8]) -> Result<DnsTxtEntryIterator<'a>, WrongSize> {
        if let Ok(len) = u16::try_from(txt_data.len()) {
            let mut ret = DnsTxtEntryIterator::default();
            // SAFETY: All values being passed into this function have been validated.
            unsafe { otDnsInitTxtEntryIterator(ret.as_ot_mut_ptr(), txt_data.as_ptr(), len) }
            Ok(ret)
        } else {
            Err(WrongSize)
        }
    }
}

impl<'a> Iterator for DnsTxtEntryIterator<'a> {
    type Item = Result<DnsTxtEntry<'a>>;

    fn next(&mut self) -> Option<Self::Item> {
        let mut ret = DnsTxtEntry::default();

        match Error::from(unsafe {
            otDnsGetNextTxtEntry(self.as_ot_mut_ptr(), ret.as_ot_mut_ptr())
        }) {
            Error::None => Some(Ok(ret)),
            Error::NotFound => None,
            err => Some(Err(err)),
        }
    }
}

/// Converts a iterator of strings into a single string separated with
/// [`DNSSD_TXT_SEPARATOR_STR`].
pub fn dns_txt_flatten<I: IntoIterator<Item = (String, Option<Vec<u8>>)>>(txt: I) -> Vec<u8> {
    let mut ret = vec![];
    for (key, value) in txt {
        let mut pair = vec![];

        if let Some(value) = value {
            pair.extend_from_slice(key.as_bytes());
            pair.push(b'=');
            pair.extend_from_slice(&value);
        } else {
            pair.extend_from_slice(key.as_bytes());
        }
        pair.truncate(u8::MAX as usize);
        ret.push(u8::try_from(pair.len()).unwrap());
        ret.extend(pair);
    }
    ret
}

/// Represents the type of a DNS query
///
/// Functional equivalent of [`otsys::otDnsQueryType`](crate::otsys::otDnsQueryType).
#[derive(Debug, Copy, Clone, Eq, Ord, PartialOrd, PartialEq, num_derive::FromPrimitive)]
#[allow(missing_docs)]
pub enum DnssdQueryType {
    /// Functional equivalent of [`otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_NONE`](crate::otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_NONE).
    None = OT_DNSSD_QUERY_TYPE_NONE as isize,

    /// Functional equivalent of [`otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_BROWSE`](crate::otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_BROWSE).
    Browse = OT_DNSSD_QUERY_TYPE_BROWSE as isize,

    /// Functional equivalent of [`otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_RESOLVE`](crate::otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_RESOLVE).
    ResolveService = OT_DNSSD_QUERY_TYPE_RESOLVE as isize,

    /// Functional equivalent of [`otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_RESOLVE_HOST`](crate::otsys::otDnsQueryType_OT_DNSSD_QUERY_TYPE_RESOLVE_HOST).
    ResolveHost = OT_DNSSD_QUERY_TYPE_RESOLVE_HOST as isize,
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
    fn test_dnstxtentry_new() {
        let cstring = CString::new("CRA").unwrap();
        assert_eq!(
            DnsTxtEntry::try_new(Some(&cstring), Some(b"300")).unwrap().to_string(),
            "CRA=300".to_string()
        );
        assert_eq!(
            DnsTxtEntry::try_new(None, Some(b"CRA=300")).unwrap().to_string(),
            "CRA=300".to_string()
        );
        assert_eq!(
            DnsTxtEntry::try_new(Some(&cstring), None).unwrap().to_string(),
            "CRA".to_string()
        );
    }

    #[test]
    fn test_split_txt() {
        assert_eq!(
            DnsTxtEntryIterator::try_new(b"")
                .unwrap()
                .map(|x| x.unwrap().to_string())
                .collect::<Vec<_>>(),
            Vec::<String>::new()
        );
        assert_eq!(
            DnsTxtEntryIterator::try_new(b"\x13xa=a7bfc4981f4e4d22\x13xp=029c6f4dbae059cb")
                .unwrap()
                .map(|x| x.unwrap().to_string())
                .collect::<Vec<_>>(),
            vec!["xa=a7bfc4981f4e4d22".to_string(), "xp=029c6f4dbae059cb".to_string()]
        );
        assert_eq!(
            DnsTxtEntryIterator::try_new(b"\x13xa=a7bfc4981f4e4d22\x11xp=029c6f4dbae059")
                .unwrap()
                .map(|x| x.unwrap().to_string())
                .collect::<Vec<_>>(),
            vec!["xa=a7bfc4981f4e4d22".to_string(), "xp=029c6f4dbae059".to_string()]
        );
        assert_eq!(
            DnsTxtEntryIterator::try_new(b"\x13xa=a7bfc4981f4e4d22\x04flag\x11xp=029c6f4dbae059")
                .unwrap()
                .map(|x| x.unwrap().to_string())
                .collect::<Vec<_>>(),
            vec![
                "xa=a7bfc4981f4e4d22".to_string(),
                "flag".to_string(),
                "xp=029c6f4dbae059".to_string()
            ]
        );
    }

    #[test]
    fn test_flatten_txt() {
        assert_eq!(ot::dns_txt_flatten(None), vec![]);
        assert_eq!(ot::dns_txt_flatten(vec![]), vec![]);
        assert_eq!(
            ot::dns_txt_flatten(vec![("xa".to_string(), Some(b"a7bfc4981f4e4d22".to_vec()))]),
            b"\x13xa=a7bfc4981f4e4d22".to_vec()
        );
        assert_eq!(
            ot::dns_txt_flatten(vec![
                ("xa".to_string(), Some(b"a7bfc4981f4e4d22".to_vec())),
                ("xp".to_string(), Some(b"029c6f4dbae059cb".to_vec()))
            ]),
            b"\x13xa=a7bfc4981f4e4d22\x13xp=029c6f4dbae059cb".to_vec()
        );
        assert_eq!(
            ot::dns_txt_flatten(vec![
                ("xa".to_string(), Some(b"a7bfc4981f4e4d22".to_vec())),
                ("flag".to_string(), None),
                ("xp".to_string(), Some(b"029c6f4dbae059cb".to_vec()))
            ]),
            b"\x13xa=a7bfc4981f4e4d22\x04flag\x13xp=029c6f4dbae059cb".to_vec()
        );
    }
}
