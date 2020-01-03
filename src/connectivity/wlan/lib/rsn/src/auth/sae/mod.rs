// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod boringssl;

use {
    anyhow::Error,
    boringssl::Bignum,
    mundane::{hash::Digest, hmac},
    wlan_common::mac::MacAddr,
};

/// A shared key computed by an SAE handshake.
#[derive(Clone, PartialEq, Debug)]
pub struct Key {
    pub pmk: Vec<u8>,
    pub pmkid: Vec<u8>,
}

/// Types of timeout that are used by SAE handshakes. Duration and scheduling of these timeouts
/// is left to the user of this library.
#[derive(Debug)]
pub enum Timeout {
    /// Timeout before the most recent message(s) should be resent.
    Retransmission,
    /// Timeout before the PMK produced by a successful handshake is considered invalid.
    KeyExpiration,
}

#[derive(Debug)]
pub enum RejectReason {
    /// We experienced a failure that was unrelated to data received from the peer. This likely
    /// means we are not in a good state.
    InternalError(Error),
    /// Data received from the peer failed validation, and we cannot generate a PMK.
    AuthFailed,
    /// The peer has failed to respond or sent incorrect responses too many times.
    TooManyRetries,
}

impl From<Error> for RejectReason {
    fn from(e: Error) -> Self {
        Self::InternalError(e)
    }
}

/// An SAE Commit message received or sent to a peer.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CommitMsg {
    scalar: Vec<u8>,
    element: Vec<u8>,
}

/// An SAE Confirm message received or sent to a peer.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ConfirmMsg {
    confirm: Vec<u8>,
    send_confirm: u16,
}

/// An update generated to progress an SAE handshake. These updates should generally be converted
/// into a frame and sent to the SAE peer.
#[derive(Debug)]
pub enum SaeUpdate {
    /// Send a commit frame to the peer.
    Commit(CommitMsg),
    /// Send a confirm frame to the peer.
    Confirm(ConfirmMsg),
    /// Indicates the handshake is complete. The handshake should *not* be deleted at this point.
    Complete(Key),
    /// Indicates that the handshake has failed and must be aborted or restarted.
    Reject(RejectReason),
    /// Request the user of the library to set or reset a timeout. If this timeout expires, it
    /// should be passed to SaeHandshake::handle_timeout.
    ResetTimeout(Timeout),
    /// Request the user of the library to cancel a timeout that was previously set.
    CancelTimeout(Timeout),
}

pub type SaeUpdateSink = Vec<SaeUpdate>;

/// IEEE 802.11-2016 12.4: Simultaneous Authentication of Equals (SAE)
///
/// An SAE handshake with a peer is a symmetric handshake that may be used in place of open
/// authentication as the AKM. A full handshake consists of both peers sending a Commit and Confirm
/// frame, at which point they have both derived a shared key that is unique to those peers and that
/// session.
///
/// Structs implementing this trait are responsible for handling both a successful SAE handshake,
/// various failure modes, and edge cases such as retries and timeouts. The user of this trait is
/// responsible for parsing and writing SAE auth frames.
///
/// None of the functions in this trait return errors. Instead, non-fatal errors are logged, and
/// fatal errors push an SaeUpdate::Reject to the update sink. Once an SaeUpdate::Reject is pushed,
/// all further operations are no-ops.
pub trait SaeHandshake {
    /// Initiate SAE by sending the first commit message. If the peer STA sends the first commit
    /// message, handle_commit should be called first and initiate_sae should never be called.
    fn initiate_sae(&mut self, sink: &mut SaeUpdateSink);
    fn handle_commit(&mut self, sink: &mut SaeUpdateSink, commit_msg: &CommitMsg);
    fn handle_confirm(&mut self, sink: &mut SaeUpdateSink, confirm_msg: &ConfirmMsg);
    fn handle_timeout(&mut self, sink: &mut SaeUpdateSink, timeout: Timeout);
}

// Internal mod for structs with mod-public visibility.
mod internal {
    use super::*;

    /// IEEE 802.11-2016 12.4.4
    /// SAE may use many different finite cyclic groups (FCGs) to compute the various values used
    /// during the handshake. This trait allows our SAE implementation to seamlessly handle
    /// different classes of FCG. IEEE 802.11-2016 defines support for both elliptic curve groups
    /// and finite field cryptography groups.
    ///
    /// All functions provided by this trait will only return an Error when something internal has
    /// gone wrong.
    pub trait FiniteCyclicGroup {
        /// Different classes of FCG have different Element types, but scalars can always be
        /// represented by a Bignum.
        type Element;
        /// IEEE 802.11-2016 12.4.3
        /// Generates a new password element, a secret value shared by the two peers in SAE.
        fn generate_pwe(&self, params: &SaeParameters) -> Result<Self::Element, Error>;

        /// IEEE 12.4.4.1
        /// These three operators are used to manipulate FCG elements for the purposes of the
        /// Diffie-Hellman key exchange used by SAE.
        fn scalar_op(
            &self,
            scalar: &Bignum,
            element: &Self::Element,
        ) -> Result<Self::Element, Error>;
        fn elem_op(
            &self,
            element1: &Self::Element,
            element2: &Self::Element,
        ) -> Result<Self::Element, Error>;
        fn inverse_op(&self, element: Self::Element) -> Result<Self::Element, Error>;

        /// Returns the prime order of the FCG.
        fn order(&self) -> Result<Bignum, Error>;
        /// IEEE 802.11-2016 12.4.5.4
        /// Maps the given secret element to the shared secret value. Returns None if this is the
        /// identity element for this FCG, indicating that we have in invalid secret element.
        fn map_to_secret_value(&self, element: &Self::Element) -> Result<Option<Bignum>, Error>;
        /// IEEE 802.11-2016 12.4.2: The FCG Element must convert into an octet string such
        /// that it may be included in the confirmation hash when completing SAE.
        fn element_to_octets(&self, element: &Self::Element) -> Result<Vec<u8>, Error>;
        /// Convert octets into an element. Returns None if the given octet string does not
        /// contain a valid element for this group.
        fn element_from_octets(&self, octets: &[u8]) -> Result<Option<Self::Element>, Error>;

        /// Return the expected size of scalar and element values when serialized into a frame.
        fn scalar_size(&self) -> Result<usize, Error> {
            self.order().map(|order| order.len())
        }
        fn element_size(&self) -> Result<usize, Error>;
    }

    #[derive(Clone)]
    pub struct SaeParameters {
        // IEEE 802.11-2016 12.4.2: SAE theoretically supports arbitrary H and CN functions,
        // although the standard only uses HMAC-SHA-256.
        pub h: fn(salt: &[u8], ikm: &[u8]) -> Vec<u8>,
        #[allow(unused)]
        pub cn: fn(key: &[u8], counter: u16, data: Vec<&[u8]>) -> Vec<u8>,
        // IEEE 802.11-2016 12.4.3
        pub password: Vec<u8>,
        // IEEE 802.11-2016 12.4.4.2.2: The two MacAddrs are needed for generating a password seed.
        pub sta_a_mac: MacAddr,
        pub sta_b_mac: MacAddr,
    }

    impl SaeParameters {
        pub fn pwd_seed(&self, counter: u8) -> Vec<u8> {
            let (big_mac, little_mac) = match self.sta_a_mac.cmp(&self.sta_b_mac) {
                std::cmp::Ordering::Less => (self.sta_b_mac, self.sta_a_mac),
                _ => (self.sta_a_mac, self.sta_b_mac),
            };
            let mut salt = vec![];
            salt.extend_from_slice(&big_mac[..]);
            salt.extend_from_slice(&little_mac[..]);
            let mut ikm = self.password.clone();
            ikm.push(counter);
            (self.h)(&salt[..], &ikm[..])
        }
    }
}

fn h(salt: &[u8], ikm: &[u8]) -> Vec<u8> {
    let mut hasher = hmac::HmacSha256::new(salt);
    hasher.update(ikm);
    hasher.finish().bytes().to_vec()
}

fn cn(key: &[u8], counter: u16, data: Vec<&[u8]>) -> Vec<u8> {
    let mut hasher = hmac::HmacSha256::new(key);
    hasher.update(&counter.to_le_bytes()[..]);
    for data_part in data {
        hasher.update(data_part);
    }
    hasher.finish().bytes().to_vec()
}

#[cfg(test)]
mod tests {
    use super::{internal::*, *};

    // IEEE 802.11-2016 Annex J.10 SAE test vector
    const TEST_PWD: &'static str = "thisisreallysecret";
    const TEST_STA_A: MacAddr = [0x7b, 0x88, 0x56, 0x20, 0x2d, 0x8d];
    const TEST_STA_B: MacAddr = [0xe2, 0x47, 0x1c, 0x0a, 0x5a, 0xcb];
    const TEST_PWD_SEED: [u8; 32] = [
        0x69, 0xf6, 0x90, 0x99, 0x83, 0x67, 0x53, 0x92, 0xd0, 0xa3, 0xa8, 0x82, 0x47, 0xff, 0xef,
        0x20, 0x41, 0x3e, 0xe9, 0x72, 0x15, 0x87, 0x29, 0x42, 0x44, 0x15, 0xe1, 0x39, 0x46, 0xec,
        0xc2, 0x06,
    ];

    #[test]
    fn pwd_seed() {
        let params = SaeParameters {
            h,
            cn,
            password: Vec::from(TEST_PWD),
            sta_a_mac: TEST_STA_A,
            sta_b_mac: TEST_STA_B,
        };
        let seed = params.pwd_seed(1);
        assert_eq!(&seed[..], &TEST_PWD_SEED[..]);
    }

    #[test]
    fn symmetric_pwd_seed() {
        let params = SaeParameters {
            h,
            cn,
            password: Vec::from(TEST_PWD),
            // The password seed should not change depending on the order of mac addresses.
            sta_a_mac: TEST_STA_B,
            sta_b_mac: TEST_STA_A,
        };
        let seed = params.pwd_seed(1);
        assert_eq!(&seed[..], &TEST_PWD_SEED[..]);
    }
}
