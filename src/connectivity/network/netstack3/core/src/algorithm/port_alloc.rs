// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Ephemeral port allocation provider.
//!
//! Defines the [`PortAlloc`] structure and [`PortAllocImpl`] trait, used for
//! ephemeral port allocations in transport protocols.

use alloc::vec::Vec;
use core::{
    hash::Hash,
    marker::PhantomData,
    num::{NonZeroU16, NonZeroUsize},
    ops::RangeInclusive,
};

use mundane::{hash::Digest, hmac::HmacSha256};
use net_types::{ip::IpAddress, SpecifiedAddr};
use rand::RngCore;

/// A port number.
// NOTE(brunodalbo): `PortNumber` could be a trait, but given the expected use
// of the PortAlloc algorithm is to allocate `u16` ports, it's just defined as a
// type alias for simplicity.
pub(crate) type PortNumber = u16;

/// A common implementation of `HashableId` providing usual 3-tuple flow
/// identifiers.
///
/// `ProtocolFlowId` provides the most common 3-tuple needed to be used with a
/// [`PortAlloc`] structure: local IP, remote IP, and remote port number.
#[derive(Hash, Debug)]
pub(crate) struct ProtocolFlowId<I: IpAddress> {
    local_addr: SpecifiedAddr<I>,
    remote_addr: SpecifiedAddr<I>,
    remote_port: NonZeroU16,
}

impl<I: IpAddress> ProtocolFlowId<I> {
    /// Creates a new `ProtocolFlowId` with given parameters.
    pub(crate) fn new(
        local_addr: SpecifiedAddr<I>,
        remote_addr: SpecifiedAddr<I>,
        remote_port: NonZeroU16,
    ) -> Self {
        Self { local_addr, remote_addr, remote_port }
    }

    /// Gets this `ProtocolFlowId`'s local address.
    pub(crate) fn local_addr(&self) -> &SpecifiedAddr<I> {
        &self.local_addr
    }

    /// Gets this `ProtocolFlowId`'s remote address.
    pub(crate) fn remote_addr(&self) -> &SpecifiedAddr<I> {
        &self.remote_addr
    }

    /// Gets this `ProtocolFlowId`'s remote port number.
    pub(crate) fn remote_port(&self) -> NonZeroU16 {
        self.remote_port
    }
}

/// Length, in bytes, of secret numbers used for port allocations.
const SECRET_LEN: usize = 16;

/// Trait that configures the behavior of a [`PortAlloc`] structure.
///
/// `PortAllocImpl` provides the types, custom behaviors, and port availability
/// checks necessary to operate the port allocation algorithm provided by
/// [`PortAlloc`].
pub(crate) trait PortAllocImpl {
    /// The number of different incremental ranges used by [`PortAlloc`]. If
    /// `TABLE_SIZE` is 1, the algorithm used by [`PortAlloc`] is Algorithm 3,
    /// as described in [RFC 6056]. Otherwise, [`PortAlloc`] will use Algorithm
    /// 4.
    ///
    /// [RFC 6056]: https://tools.ietf.org/html/rfc6056
    const TABLE_SIZE: NonZeroUsize;
    /// The range of ports that can be allocated by [`PortAlloc`].
    ///
    /// Local ports used in transport protocols are called [Ephemeral Ports].
    /// Different transport protocols may define different ranges for the issued
    /// ports. [`PortAlloc`] is guaranteed to return a port in this range as a
    /// result of [`PortAlloc::try_alloc`].
    ///
    /// [Ephemeral Ports]: https://tools.ietf.org/html/rfc6056#section-2
    const EPHEMERAL_RANGE: RangeInclusive<PortNumber>;
    /// The "flow" identifier used to allocate port Ids.
    ///
    /// The `Id` is typically the 3 elements other other than the local port in
    /// the 4-tuple (local IP:port, remote IP:port) that is used to uniquely
    /// identify the flow information of a connection.
    type Id: Hash;

    /// Returns the number of ephemeral ports avaiable in `EPHEMERAL_RANGE`.
    fn num_ephemeral() -> usize {
        usize::from(Self::EPHEMERAL_RANGE.end() - Self::EPHEMERAL_RANGE.start()) + 1
    }

    /// Returns a random ephemeral port in `EPHEMERAL_RANGE`
    fn rand_ephemeral<R: RngCore>(rng: &mut R) -> EphemeralPort<Self> {
        EphemeralPort::new_random(rng)
    }

    /// Checks if `port` is available to be used for the flow `id`.
    ///
    /// Implementers return `true` if the provided `port` is available to be
    /// used for a given flow `id`. An available port is a port that would not
    /// conflict for the given `id` *plus* ideally the port is not in LISTEN or
    /// CLOSED states for a given protocol (see [RFC 6056]).
    ///
    /// Note: Callers must guarantee that the given port being checked is within
    /// the `EPHEMERAL_RANGE`.
    ///
    /// [RFC 6056]: https://tools.ietf.org/html/rfc6056#section-2.2
    fn is_port_available(&self, id: &Self::Id, port: PortNumber) -> bool;
}

/// Provides a port allocation algorithm, following the guidelines in [RFC
/// 6056].
///
/// `PortAlloc` provides the port allocation algorithms 3 or 4 (depending on the
/// [`PortAllocImpl`] type parameter) described in [`RFC 6056`].
///
/// The algorithm consists of 2 sources of obfuscation:
/// - For a given flow identifier, a base offset is calculated from an HMAC.
/// - For a given flow identifier, a *different* hash provides an index into an
///   internal table that keeps monotonically increasing port numbers.
///
/// The tentative port is acquired by selecting a port in the provided
/// `EPHEMERAL_RANGE` using the two obfuscated offsets described above, and then
/// a check for conflicts is performed. A simple linear scan from the initial
/// calculated port is performed from that point, until an available port is
/// found.
///
/// `PortAlloc` holds two secrets internally, that are used to generate the 2
/// sources of obfuscation above. The secrets can be updated periodically using
/// [`PortAlloc::update_secrets`].
///
/// [RFC 6056]: https://tools.ietf.org/html/rfc6056
pub(crate) struct PortAlloc<I: PortAllocImpl> {
    // TODO(brunodalbo): Table can be made an array once we can declare
    // arrays with associated consts.
    table: Vec<PortNumber>,
    secret_a: [u8; SECRET_LEN],
    secret_b: [u8; SECRET_LEN],
    _marker: core::marker::PhantomData<I>,
}

/// A witness type for a port within some ephemeral port range.
///
/// `EphemeralPort` is always guaranteed to contain a port that is within
/// `I::EPHEMERAL_RANGE`.
pub(crate) struct EphemeralPort<I: PortAllocImpl + ?Sized> {
    port: PortNumber,
    _marker: PhantomData<I>,
}

impl<I: PortAllocImpl + ?Sized> EphemeralPort<I> {
    /// Creates a new `EphemeralPort` with a port chosen randomly in `range`.
    pub(crate) fn new_random<R: RngCore>(rng: &mut R) -> Self {
        let num_ephemeral = u32::from(I::EPHEMERAL_RANGE.end() - I::EPHEMERAL_RANGE.start()) + 1;
        let port = I::EPHEMERAL_RANGE.start() + ((rng.next_u32() % num_ephemeral) as PortNumber);
        Self { port, _marker: PhantomData }
    }

    /// Increments the current [`PortNumber`] to the next value in the contained
    /// range, wrapping around to the start of the range.
    pub(crate) fn next(&mut self) {
        if self.port == *I::EPHEMERAL_RANGE.end() {
            self.port = *I::EPHEMERAL_RANGE.start();
        } else {
            self.port += 1;
        }
    }

    /// Gets the `PortNumber` value.
    pub(crate) fn get(&self) -> PortNumber {
        self.port
    }
}

impl<I: PortAllocImpl> PortAlloc<I> {
    /// Creates a new `PortAlloc` port allocation provider.
    ///
    /// `rng` is used to generate the initial secrets and random offsets in the
    /// internal table.
    // TODO(brunodalbo) make R: RngCore + CryptoRng when we tighten the security
    // around this algorithm.
    pub(crate) fn new<R: RngCore>(rng: &mut R) -> Self {
        let mut table = Vec::with_capacity(I::TABLE_SIZE.into());
        for _ in 0..I::TABLE_SIZE.into() {
            table.push(rng.next_u32() as PortNumber);
        }
        let mut secret_a = [0; SECRET_LEN];
        let mut secret_b = [0; SECRET_LEN];
        rng.fill_bytes(&mut secret_a[..]);
        rng.fill_bytes(&mut secret_b[..]);
        Self { table, secret_a, secret_b, _marker: core::marker::PhantomData }
    }

    /// Attempts to allocate a new port for a flow with the given `id`.
    ///
    /// `try_alloc` performs the algorithm 3 or 4, as described in [RFC 6056]
    /// and returns `Some(PortNumber)` if an available port could be selected
    /// (`state` provides the availability checks) or `None` otherwise.
    ///
    /// [RFC 6056]: https://tools.ietf.org/html/rfc6056
    pub(crate) fn try_alloc(&mut self, id: &I::Id, state: &I) -> Option<PortNumber> {
        let num_ephemeral = I::num_ephemeral();
        let offset = hmac_with_secret(id, &self.secret_a[..]);
        let table_index = hmac_with_secret(id, &self.secret_b[..]) % self.table.len();
        let table_val = &mut self.table[table_index];
        for _ in 0..num_ephemeral {
            let local_off = offset.wrapping_add(*table_val as usize) % num_ephemeral;
            // We can safely cast `local_off` to `PortNumber` because of the %
            // num_ephemeral operation above.
            let port = I::EPHEMERAL_RANGE.start() + (local_off as PortNumber);
            *table_val = table_val.wrapping_add(1);
            if state.is_port_available(id, port) {
                return Some(port);
            }
        }
        None
    }

    /// Updates the internal secrets used in the Hash-Based port selection.
    ///
    /// It is interesting to update the internal secrets periodically, making a
    /// remote attack involving port number predictions harder. [RFC 6056
    /// section 3.4] suggest this can be done periodically. Care must be taken
    /// in doing so, because the new offsets calculated from refreshed secrets
    /// can cause collisions of instance-ids.
    ///
    /// [RFC 6056 section 3.4]: https://tools.ietf.org/html/rfc6056#section-3.4
    // TODO(brunodalbo) make R: RngCore + CryptoRng when we tighten the security
    // around this algorithm.
    // TODO(rheacock): Remove _ prefix when this function is used.
    pub(crate) fn _update_secrets<R: RngCore>(&mut self, rng: &mut R) {
        rng.fill_bytes(&mut self.secret_a[..]);
        rng.fill_bytes(&mut self.secret_b[..]);
    }
}

/// Helper function to hash an `id` with a given `secret` into a `usize`.
fn hmac_with_secret<I: Hash>(id: &I, secret: &[u8]) -> usize {
    use core::convert::TryInto as _;

    let mut hmac = HmacSha256::new(secret);
    let () = id.hash(&mut hmac);

    usize::from_ne_bytes(hmac.finish().bytes()[..core::mem::size_of::<usize>()].try_into().unwrap())
}

#[cfg(test)]
mod tests {
    use nonzero_ext::nonzero;

    use super::*;
    use crate::testutil::{with_fake_rngs, FakeCryptoRng};

    /// A fake flow identifier.
    #[derive(Hash)]
    struct FakeId(usize);

    /// Number of different RNG seeds used in tests in this mod.
    const RNG_ROUNDS: u128 = 128;

    /// Hard-coded fake of available port filter.
    enum FakeAvailable {
        /// Only a single port is available.
        AllowSingle(PortNumber),
        /// No ports are available.
        DenyAll,
        /// Only even-numbered ports are available.
        AllowEvens,
        /// All ports are available.
        AllowAll,
    }

    /// Fake implementation of [`PortAllocImpl`].
    ///
    /// The `available` field will dictate the return of
    /// [`PortAllocImpl::is_port_available`] and can be set to get the expected
    /// testing behavior.
    struct FakeImpl {
        available: FakeAvailable,
    }

    impl PortAllocImpl for FakeImpl {
        const TABLE_SIZE: NonZeroUsize = nonzero!(2usize);
        const EPHEMERAL_RANGE: RangeInclusive<u16> = 100..=200;
        type Id = FakeId;

        fn is_port_available(&self, _id: &Self::Id, port: u16) -> bool {
            match self.available {
                FakeAvailable::AllowEvens => (port & 1) == 0,
                FakeAvailable::DenyAll => false,
                FakeAvailable::AllowSingle(p) => port == p,
                FakeAvailable::AllowAll => true,
            }
        }
    }

    /// Helper fn to test that if only a single port is available, we will
    /// eventually get that
    fn test_allow_single(single: u16) {
        with_fake_rngs(RNG_ROUNDS, |mut rng| {
            let fake = FakeImpl { available: FakeAvailable::AllowSingle(single) };
            let mut alloc = PortAlloc::<FakeImpl>::new(&mut rng);
            let port = alloc.try_alloc(&FakeId(0), &fake);
            assert_eq!(port.unwrap(), single);
        });
    }

    #[test]
    fn test_single_range_start() {
        // Test boundary condition for first ephemeral port.
        test_allow_single(FakeImpl::EPHEMERAL_RANGE.start().clone())
    }

    #[test]
    fn test_single_range_end() {
        // Test boundary condition for last ephemeral port.
        test_allow_single(FakeImpl::EPHEMERAL_RANGE.end().clone())
    }

    #[test]
    fn test_single_range_mid() {
        // Test some other ephemeral port.
        test_allow_single((FakeImpl::EPHEMERAL_RANGE.end() + FakeImpl::EPHEMERAL_RANGE.start()) / 2)
    }

    #[test]
    fn test_allow_none() {
        // Test that if no ports are available, try_alloc must return none.
        with_fake_rngs(RNG_ROUNDS, |mut rng| {
            let fake = FakeImpl { available: FakeAvailable::DenyAll };
            let mut alloc = PortAlloc::<FakeImpl>::new(&mut rng);
            let port = alloc.try_alloc(&FakeId(0), &fake);
            assert_eq!(port, None);
        });
    }

    #[test]
    fn test_allow_evens() {
        // Test that if we only allow even ports, we will always get ports in
        // the specified range, and they'll always be even.
        with_fake_rngs(RNG_ROUNDS, |mut rng| {
            let fake = FakeImpl { available: FakeAvailable::AllowEvens };
            let mut alloc = PortAlloc::<FakeImpl>::new(&mut rng);
            let port = alloc.try_alloc(&FakeId(0), &fake).unwrap();
            assert!(FakeImpl::EPHEMERAL_RANGE.contains(&port));
            assert_eq!(port & 1, 0);
        });
    }

    #[test]
    fn test_sequential_allocs() {
        // Test that for a single flow ID, we get sequential ports that can span
        // the entire ephemeral range.
        with_fake_rngs(RNG_ROUNDS, |mut rng| {
            let fake = FakeImpl { available: FakeAvailable::AllowAll };
            let mut alloc = PortAlloc::<FakeImpl>::new(&mut rng);
            let mut port = alloc.try_alloc(&FakeId(0), &fake).unwrap();
            assert!(FakeImpl::EPHEMERAL_RANGE.contains(&port));
            for _ in FakeImpl::EPHEMERAL_RANGE {
                let next = alloc.try_alloc(&FakeId(0), &fake).unwrap();
                let expect = if port == *FakeImpl::EPHEMERAL_RANGE.end() {
                    *FakeImpl::EPHEMERAL_RANGE.start()
                } else {
                    port + 1
                };
                assert_eq!(next, expect);
                port = next;
            }
        });
    }

    #[test]
    fn test_different_tables() {
        // Test that different IDs can hash to different offsets in internal
        // tables, which increase independently.
        let fake = FakeImpl { available: FakeAvailable::AllowAll };
        let mut alloc = PortAlloc::<FakeImpl>::new(&mut FakeCryptoRng::new_xorshift(2));
        let table_a = alloc.table[0];
        let table_b = alloc.table[1];
        let id_a = FakeId(0);
        let id_b = FakeId(1);
        assert_eq!(hmac_with_secret(&id_a, &alloc.secret_b[..]) % 2, 0);
        assert_eq!(hmac_with_secret(&id_b, &alloc.secret_b[..]) % 2, 1);
        let _ = alloc.try_alloc(&id_a, &fake).unwrap();
        // A single allocation should've moved offset a but not b.
        assert_eq!(table_a.wrapping_add(1), alloc.table[0]);
        assert_eq!(table_b, alloc.table[1]);
        let _ = alloc.try_alloc(&id_b, &fake).unwrap();
        // Now offset b should've moved, and a remained just one forward.
        assert_eq!(table_a.wrapping_add(1), alloc.table[0]);
        assert_eq!(table_b.wrapping_add(1), alloc.table[1]);
    }

    #[test]
    fn test_ephemeral_port_random() {
        // Test that random ephemeral ports are always in range.
        let mut rng = FakeCryptoRng::new_xorshift(0);
        for _ in 0..1000 {
            let rnd_port = EphemeralPort::<FakeImpl>::new_random(&mut rng);
            assert!(FakeImpl::EPHEMERAL_RANGE.contains(&rnd_port.port));
        }
    }

    #[test]
    fn test_ephemeral_port_next() {
        let mut port = EphemeralPort::<FakeImpl> {
            port: *FakeImpl::EPHEMERAL_RANGE.start(),
            _marker: PhantomData,
        };
        // Loop over all the range twice so we see the wrap-around.
        for _ in 0..=1 {
            for x in FakeImpl::EPHEMERAL_RANGE {
                assert_eq!(port.port, x);
                port.next();
            }
        }
    }
}
