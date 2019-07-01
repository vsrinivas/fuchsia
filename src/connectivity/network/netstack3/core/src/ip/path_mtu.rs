// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module for IP level paths' maximum transmission unit (PMTU) size
//! cache support.

use std::collections::HashMap;

use log::trace;
use specialize_ip_macro::{specialize_ip, specialize_ip_address};

use crate::ip::{Ip, IpAddress};
use crate::{Context, EventDispatcher};

/// [RFC 791 section 3.2] requires that an IPv4 node be able to forward
/// datagrams of up to 68 octets without furthur fragmentation. That is,
/// the minimum MTU of an IPv4 path must be 68 bytes. This is because
/// an IPv4 header may be up to 60 octets, and the minimum fragment is
/// 8 octets.
///
/// [RFC 791 section 3.2]: https://tools.ietf.org/html/rfc791#section-3.2
pub(crate) const IPV4_MIN_MTU: u32 = 68;

/// [RFC 8200 section 5] requires that every link in the Internet have an
/// MTU of 1280 octets or greater. Any link that cannot convey a 1280-
/// octet packet in one piece must provide link-specific fragmentation
/// and reassembly at a layer below IPv6.
///
/// [RFC 8200 section 5]: https://tools.ietf.org/html/rfc8200#section-5
pub(crate) const IPV6_MIN_MTU: u32 = 1280;

/// Get the minimum MTU size for a specific IP version, identified by `I`.
#[specialize_ip]
pub(crate) fn min_mtu<I: Ip>() -> u32 {
    #[ipv4]
    let ret = IPV4_MIN_MTU;

    #[ipv6]
    let ret = IPV6_MIN_MTU;

    ret
}

/// Get the PMTU between `src_ip` and `dst_ip`.
///
/// See [`IpLayerPathMtuCache::get`].
#[specialize_ip_address]
pub(crate) fn get_pmtu<A: IpAddress, D: EventDispatcher>(
    ctx: &mut Context<D>,
    src_ip: A,
    dst_ip: A,
) -> Option<u32> {
    #[ipv4addr]
    let ret = ctx.state.ip.v4.path_mtu.get(src_ip, dst_ip);

    #[ipv6addr]
    let ret = ctx.state.ip.v6.path_mtu.get(src_ip, dst_ip);

    ret
}

/// Update the PMTU between `src_ip` and `dst_ip`.
///
/// See [`IpLayerPathMtuCache::update`].
#[specialize_ip_address]
pub(crate) fn update_pmtu<A: IpAddress, D: EventDispatcher>(
    ctx: &mut Context<D>,
    src_ip: A,
    dst_ip: A,
    new_mtu: u32,
) -> Result<Option<u32>, Option<u32>> {
    #[ipv4addr]
    let ret = ctx.state.ip.v4.path_mtu.update(src_ip, dst_ip, new_mtu);

    #[ipv6addr]
    let ret = ctx.state.ip.v6.path_mtu.update(src_ip, dst_ip, new_mtu);

    trace!(
        "update_pmtu: Updating the PMTU between src {} and dest {} to {}; was {:?}",
        src_ip,
        dst_ip,
        new_mtu,
        ret
    );

    ret
}

/// Update the PMTU between `src_ip` and `dst_ip` if `new_mtu` is less than
/// the current PMTU and does not violate the minimum MTU size requirements
/// for an IP.
///
/// See [`IpLayerPathMtuCache::update`].
pub(crate) fn update_pmtu_if_less<A: IpAddress, D: EventDispatcher>(
    ctx: &mut Context<D>,
    src_ip: A,
    dst_ip: A,
    new_mtu: u32,
) -> Result<Option<u32>, Option<u32>> {
    let prev_mtu = get_pmtu(ctx, src_ip, dst_ip);

    match prev_mtu {
        // No PMTU exists so update.
        None => update_pmtu(ctx, src_ip, dst_ip, new_mtu),
        // A PMTU exists but it is greater than `new_mtu` so update.
        Some(mtu) if new_mtu < mtu => update_pmtu(ctx, src_ip, dst_ip, new_mtu),
        // A PMTU exists but it is less than or equal to `new_mtu` so no need to update.
        _ => {
            trace!("update_pmtu_if_less: Not updating the PMTU  between src {} and dest {} to {}; is {}"
, src_ip, dst_ip, new_mtu, prev_mtu.unwrap());
            Ok(prev_mtu)
        }
    }
}

/// The key used to identify a path.
///
/// This is a tuple of (src_ip, dst_ip) as a path is only identified
/// by the source and destination addresses.
// TODO(ghanan): Should device play a part in the key-ing of a path?
#[derive(Copy, Clone, Debug, Hash, PartialEq, Eq)]
pub(crate) struct PathMtuCacheKey<A: IpAddress>(A, A);

impl<A: IpAddress> PathMtuCacheKey<A> {
    fn new(src_ip: A, dst_ip: A) -> Self {
        Self(src_ip, dst_ip)
    }
}

/// Structure to keep track of the PMTU from a (local) source address to
/// some destination address.
type PathMtuCache<A> = HashMap<PathMtuCacheKey<A>, u32>;

/// Ip Layer PMTU cache.
#[derive(Debug)]
pub(crate) struct IpLayerPathMtuCache<I: Ip> {
    cache: PathMtuCache<I::Addr>,
}

impl<I: Ip> IpLayerPathMtuCache<I> {
    /// Create a new `IpLayerPathMtuCache`.
    pub(crate) fn new() -> Self {
        Self { cache: PathMtuCache::new() }
    }

    /// Get the PMTU between `src_ip` and `dst_ip`.
    ///
    /// Returns `None` if no PMTU is known by this `IpLayerPathMtuCache`, else
    /// `Some(x)` where `x` is the current estimate of the PMTU.
    pub(crate) fn get(&self, src_ip: I::Addr, dst_ip: I::Addr) -> Option<u32> {
        self.cache.get(&PathMtuCacheKey::new(src_ip, dst_ip)).map(|x| *x)
    }

    /// Update the PMTU between `src_ip` and `dst_ip` if `new_mtu` does not violate
    /// IP specific minimum MTU requirements.
    ///
    /// Returns `Err(x)` if the `new_mtu` is less than the minimum MTU for an IP
    /// where the same `x` is returned in the success case (`Ok(x)`). `x` is the
    /// PMTU known by this `IpLayerPathMtuCache` before being updated. `x` will be
    /// `None` if no PMTU is known, else `Some(y)` where `y` is the last estimate
    /// of the PMTU.
    pub(crate) fn update(
        &mut self,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        new_mtu: u32,
    ) -> Result<Option<u32>, Option<u32>> {
        // New MTU must not be smaller than the minimum MTU for an IP.
        if new_mtu < min_mtu::<I>() {
            return Err(self.get(src_ip, dst_ip));
        }

        Ok(self.cache.insert(PathMtuCacheKey::new(src_ip, dst_ip), new_mtu))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::ip::{Ipv4, Ipv6};
    use crate::testutil::{get_dummy_config, DummyEventDispatcher, DummyEventDispatcherBuilder};

    fn test_ip_path_mtu_cache<I: Ip>() {
        let dummy_config = get_dummy_config::<I::Addr>();
        let mut cache = IpLayerPathMtuCache::<I>::new();

        // Nothing in the cache yet
        assert_eq!(cache.get(dummy_config.local_ip, dummy_config.remote_ip), None);

        let new_mtu1 = min_mtu::<I>() + 100;

        // Update pmtu from local to remote
        assert_eq!(
            cache.update(dummy_config.local_ip, dummy_config.remote_ip, new_mtu1).unwrap(),
            None
        );

        // Make sure the update worked
        assert_eq!(cache.get(dummy_config.local_ip, dummy_config.remote_ip).unwrap(), new_mtu1);

        let new_mtu2 = min_mtu::<I>() + 101;

        // Updating again should return the last pmtu
        assert_eq!(
            cache.update(dummy_config.local_ip, dummy_config.remote_ip, new_mtu2).unwrap().unwrap(),
            new_mtu1
        );

        // Make sure the update worked
        assert_eq!(cache.get(dummy_config.local_ip, dummy_config.remote_ip).unwrap(), new_mtu2);

        let low_mtu = min_mtu::<I>() - 1;

        // Updating with mtu value less than the minimum MTU should fail.
        assert_eq!(
            cache
                .update(dummy_config.local_ip, dummy_config.remote_ip, low_mtu)
                .unwrap_err()
                .unwrap(),
            new_mtu2
        );

        // Make sure the update didn't work.
        assert_eq!(cache.get(dummy_config.local_ip, dummy_config.remote_ip).unwrap(), new_mtu2);
    }

    #[test]
    fn test_ipv4_path_mtu_cache() {
        test_ip_path_mtu_cache::<Ipv4>();
    }

    #[test]
    fn test_ipv6_path_mtu_cache() {
        test_ip_path_mtu_cache::<Ipv6>();
    }

    fn test_ip_path_mtu_cache_ctx<I: Ip>() {
        let dummy_config = get_dummy_config::<I::Addr>();
        let mut ctx = DummyEventDispatcherBuilder::from_config(dummy_config.clone())
            .build::<DummyEventDispatcher>();

        // Nothing in the cache yet
        assert_eq!(get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip), None);

        let new_mtu1 = min_mtu::<I>() + 50;

        // Update pmtu from local to remote
        assert_eq!(
            update_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip, new_mtu1).unwrap(),
            None
        );

        // Make sure the update worked
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu1
        );

        let new_mtu2 = min_mtu::<I>() + 100;

        // Updating again should return the last pmtu
        assert_eq!(
            update_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip, new_mtu2)
                .unwrap()
                .unwrap(),
            new_mtu1
        );

        // Make sure the update worked
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu2
        );

        let new_mtu3 = new_mtu2 - 10;

        // Make sure update only if new PMTU is less than current (it is)
        assert_eq!(
            update_pmtu_if_less(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip, new_mtu3)
                .unwrap()
                .unwrap(),
            new_mtu2
        );

        // Make sure the update worked
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu3
        );

        let new_mtu4 = new_mtu3 + 50;

        // Make sure update only if new PMTU is less than current (it isn't)
        assert_eq!(
            update_pmtu_if_less(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip, new_mtu4)
                .unwrap()
                .unwrap(),
            new_mtu3
        );

        // Make sure the update didnt work
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu3
        );

        let low_mtu = min_mtu::<I>() - 1;

        // Updating with mtu value less than the minimum MTU should fail.
        assert_eq!(
            update_pmtu_if_less(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip, low_mtu)
                .unwrap_err()
                .unwrap(),
            new_mtu3
        );

        // Make sure the update didn't work.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu3
        );
    }

    #[test]
    fn test_ipv4_path_mtu_cache_ctx() {
        test_ip_path_mtu_cache_ctx::<Ipv4>();
    }

    #[test]
    fn test_ipv6_path_mtu_cache_ctx() {
        test_ip_path_mtu_cache_ctx::<Ipv6>();
    }
}
