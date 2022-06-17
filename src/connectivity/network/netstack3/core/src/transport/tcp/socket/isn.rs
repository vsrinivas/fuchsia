// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Generate initial sequence numbers securely.

use core::{
    hash::{Hash, Hasher},
    time::Duration,
};

use net_types::ip::IpAddress;
use siphasher::sip128::SipHasher24;

use crate::transport::tcp::{seqnum::SeqNum, socket::SocketAddr};

pub(super) fn generate_isn<A: IpAddress>(
    elapsed: Duration,
    local: SocketAddr<A>,
    remote: SocketAddr<A>,
    secret: &[u8; 16],
) -> SeqNum {
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
    let mut hasher = SipHasher24::new_with_key(secret);

    local.hash(&mut hasher);
    remote.hash(&mut hasher);

    let h = hasher.finish();

    // Reduce the hashed output (h: u64) to 32 bits using XOR, but also
    // preserve entropy.
    SeqNum::new(((elapsed.as_micros() / 4) as u32).wrapping_add(h as u32 ^ (h >> 32) as u32))
}
