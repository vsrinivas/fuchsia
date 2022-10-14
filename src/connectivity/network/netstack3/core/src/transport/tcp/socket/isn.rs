// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Generate initial sequence numbers securely.

use core::hash::{Hash, Hasher};

use net_types::ip::IpAddress;
use rand::RngCore;
use siphasher::sip128::SipHasher24;

use crate::{
    transport::tcp::{seqnum::SeqNum, socket::SocketAddr},
    Instant,
};

/// A generator of TCP initial sequence numbers.
#[derive(Default)]
pub struct IsnGenerator<Instant> {
    // Secret used to choose initial sequence numbers. It will be filled by a
    // CSPRNG upon initialization. RFC suggests an implementation "could"
    // change the secret key on a regular basis, this is not something we are
    // considering as Linux doesn't seem to do that either.
    secret: [u8; 16],
    // The initial timestamp that will be used to calculate the elapsed time
    // since the beginning and that information will then be used to generate
    // ISNs being requested.
    timestamp: Instant,
}

impl<I: Instant> IsnGenerator<I> {
    pub(crate) fn new(now: I, rng: &mut impl RngCore) -> Self {
        let mut secret = [0; 16];
        rng.fill_bytes(&mut secret);
        Self { secret, timestamp: now }
    }

    pub(crate) fn generate<A: IpAddress>(
        &self,
        now: I,
        local: SocketAddr<A>,
        remote: SocketAddr<A>,
    ) -> SeqNum {
        let Self { secret, timestamp } = self;

        // Per RFC 6528 Section 3 (https://tools.ietf.org/html/rfc6528#section-3):
        //
        // TCP SHOULD generate its Initial Sequence Numbers with the expression:
        //
        //   ISN = M + F(localip, localport, remoteip, remoteport, secretkey)
        //
        // where M is the 4 microsecond timer, and F() is a pseudorandom
        // function (PRF) of the connection-id.
        //
        // Siphash is used here as it is the hash function used by Linux.
        let h = {
            let mut hasher = SipHasher24::new_with_key(secret);
            local.hash(&mut hasher);
            remote.hash(&mut hasher);
            hasher.finish()
        };

        // Reduce the hashed output (h: u64) to 32 bits using XOR, but also
        // preserve entropy.
        let elapsed = now.duration_since(*timestamp);
        SeqNum::new(((elapsed.as_micros() / 4) as u32).wrapping_add(h as u32 ^ (h >> 32) as u32))
    }
}
