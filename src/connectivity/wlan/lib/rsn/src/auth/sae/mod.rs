// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod boringssl;
mod ecc;
mod state;

use {
    anyhow::{bail, Error},
    boringssl::{Bignum, EcGroupId},
    mundane::{hash::Digest, hmac},
    wlan_common::ie::rsn::akm::{self, Akm},
    wlan_common::mac::MacAddr,
};

/// Maximum number of incorrect frames sent before SAE fails.
const DOT11_RSNA_SAE_SYNC: u16 = 30;

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

/// Creates a new SAE handshake for the given group ID and authentication parameters.
pub fn new_sae_handshake(
    group_id: u16,
    akm: Akm,
    password: Vec<u8>,
    mac: MacAddr,
    peer_mac: MacAddr,
) -> Result<Box<dyn SaeHandshake>, Error> {
    let (h, cn) = match akm.suite_type {
        akm::SAE | akm::FT_SAE => (h, cn),
        _ => bail!("Cannot construct SAE handshake with AKM {:?}", akm),
    };
    let params = internal::SaeParameters { h, cn, password, sta_a_mac: mac, sta_b_mac: peer_mac };
    match group_id {
        19 => {
            // Elliptic curve group 19 is the default supported group.
            let ec_group = Box::new(ecc::Group::new(EcGroupId::P256)?);
            Ok(Box::new(state::SaeHandshakeImpl::new(ec_group, params)?))
        }
        _ => bail!("Unsupported SAE group id: {}", group_id),
    }
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
    use {
        super::{internal::*, *},
        hex::FromHex,
        wlan_common::assert_variant,
    };

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

    #[test]
    fn bad_akm() {
        let akm = Akm::new_dot11(akm::PSK);
        let res = new_sae_handshake(19, akm, Vec::from(TEST_PWD), TEST_STA_A, TEST_STA_B);
        assert!(res.is_err());
        assert!(format!("{}", res.err().unwrap())
            .contains("Cannot construct SAE handshake with AKM 00-0F-AC:2"));
    }

    #[test]
    fn bad_fcg() {
        let akm = Akm::new_dot11(akm::SAE);
        let res = new_sae_handshake(200, akm, Vec::from(TEST_PWD), TEST_STA_A, TEST_STA_B);
        assert!(res.is_err());
        assert!(format!("{}", res.err().unwrap()).contains("Unsupported SAE group id: 200"));
    }

    struct TestHandshake {
        sta1: Box<dyn SaeHandshake>,
        sta2: Box<dyn SaeHandshake>,
    }

    // Test helper to advance through successful steps of an SAE handshake.
    impl TestHandshake {
        fn new() -> Self {
            let akm = Akm::new_dot11(akm::SAE);
            let mut sta1 =
                new_sae_handshake(19, akm.clone(), Vec::from(TEST_PWD), TEST_STA_A, TEST_STA_B)
                    .unwrap();
            let mut sta2 =
                new_sae_handshake(19, akm, Vec::from(TEST_PWD), TEST_STA_B, TEST_STA_A).unwrap();
            Self { sta1, sta2 }
        }

        fn sta1_init(&mut self) -> CommitMsg {
            let mut sink = vec![];
            self.sta1.initiate_sae(&mut sink);
            assert_eq!(sink.len(), 2);
            let commit = assert_variant!(sink.remove(0), SaeUpdate::Commit(commit) => commit);
            assert_variant!(sink.remove(0), SaeUpdate::ResetTimeout(Timeout::Retransmission));
            commit
        }

        fn sta2_handle_commit(&mut self, commit1: CommitMsg) -> (CommitMsg, ConfirmMsg) {
            let mut sink = vec![];
            self.sta2.handle_commit(&mut sink, &commit1);
            assert_eq!(sink.len(), 3);
            let commit2 = assert_variant!(sink.remove(0), SaeUpdate::Commit(commit) => commit);
            let confirm2 = assert_variant!(sink.remove(0), SaeUpdate::Confirm(confirm) => confirm);
            assert_variant!(sink.remove(0), SaeUpdate::ResetTimeout(Timeout::Retransmission));
            (commit2, confirm2)
        }

        fn sta1_handle_commit(&mut self, commit2: CommitMsg) -> ConfirmMsg {
            let mut sink = vec![];
            self.sta1.handle_commit(&mut sink, &commit2);
            assert_eq!(sink.len(), 2);
            let confirm1 = assert_variant!(sink.remove(0), SaeUpdate::Confirm(confirm) => confirm);
            assert_variant!(sink.remove(0), SaeUpdate::ResetTimeout(Timeout::Retransmission));
            confirm1
        }

        fn sta1_handle_confirm(&mut self, confirm2: ConfirmMsg) -> Key {
            Self::__internal_handle_confirm(&mut self.sta1, confirm2)
        }

        fn sta2_handle_confirm(&mut self, confirm1: ConfirmMsg) -> Key {
            Self::__internal_handle_confirm(&mut self.sta2, confirm1)
        }

        fn __internal_handle_confirm(sta: &mut Box<dyn SaeHandshake>, confirm: ConfirmMsg) -> Key {
            let mut sink = vec![];
            sta.handle_confirm(&mut sink, &confirm);
            assert_eq!(sink.len(), 3);
            assert_variant!(sink.remove(0), SaeUpdate::CancelTimeout(Timeout::Retransmission));
            assert_variant!(sink.remove(0), SaeUpdate::ResetTimeout(Timeout::KeyExpiration));
            assert_variant!(sink.remove(0), SaeUpdate::Complete(key) => key)
        }
    }

    #[test]
    fn sae_handshake_success() {
        let mut handshake = TestHandshake::new();
        let commit1 = handshake.sta1_init();
        let (commit2, confirm2) = handshake.sta2_handle_commit(commit1);
        let confirm1 = handshake.sta1_handle_commit(commit2);
        let key1 = handshake.sta1_handle_confirm(confirm2);
        let key2 = handshake.sta2_handle_confirm(confirm1);
        assert_eq!(key1, key2);
    }

    #[test]
    fn password_mismatch() {
        let akm = Akm::new_dot11(akm::SAE);
        let mut sta1 =
            new_sae_handshake(19, akm.clone(), Vec::from(TEST_PWD), TEST_STA_A, TEST_STA_B)
                .unwrap();
        let mut sta2 =
            new_sae_handshake(19, akm, Vec::from("other_pwd"), TEST_STA_B, TEST_STA_A).unwrap();
        let mut handshake = TestHandshake { sta1, sta2 };

        let commit1 = handshake.sta1_init();
        let (commit2, confirm2) = handshake.sta2_handle_commit(commit1);
        let confirm1 = handshake.sta1_handle_commit(commit2);

        let mut sink1 = vec![];
        handshake.sta1.handle_confirm(&mut sink1, &confirm2);
        let mut sink2 = vec![];
        handshake.sta2.handle_confirm(&mut sink2, &confirm1);
        // The confirm is dropped both ways.
        assert_eq!(sink1.len(), 0);
        assert_eq!(sink2.len(), 0);
    }

    #[test]
    fn retry_commit_on_unexpected_confirm() {
        let mut handshake = TestHandshake::new();

        let commit1 = handshake.sta1_init();
        let (commit2, confirm2) = handshake.sta2_handle_commit(commit1.clone());
        let mut sink = vec![];
        handshake.sta1.handle_confirm(&mut sink, &confirm2);
        assert_eq!(sink.len(), 2);
        let commit1_retry = assert_variant!(sink.remove(0), SaeUpdate::Commit(commit) => commit);
        assert_variant!(sink.remove(0), SaeUpdate::ResetTimeout(Timeout::Retransmission));

        // We retransmit the same commit in response to a faulty confirm.
        assert_eq!(commit1, commit1_retry);
    }

    #[test]
    fn ignore_wrong_confirm() {
        let mut handshake = TestHandshake::new();

        let commit1 = handshake.sta1_init();
        let (commit2, confirm2) = handshake.sta2_handle_commit(commit1);
        let confirm1 = handshake.sta1_handle_commit(commit2);

        let mut sink = vec![];
        let mut confirm2_wrong = confirm2.clone();
        confirm2_wrong.confirm[0] += 1;
        confirm2_wrong.send_confirm += 1;

        handshake.sta1.handle_confirm(&mut sink, &confirm2_wrong);
        assert_eq!(sink.len(), 0); // Ignored.

        // STA1 should still be able to handle a subsequent correct confirm.
        handshake.sta1_handle_confirm(confirm2);
    }

    #[test]
    fn handle_resent_commit() {
        let mut handshake = TestHandshake::new();
        let commit1 = handshake.sta1_init();
        let (commit2, confirm2) = handshake.sta2_handle_commit(commit1.clone());
        let (commit2_retry, confirm2_retry) = handshake.sta2_handle_commit(commit1);

        // The resent commit message should be unchanged, but the resent confirm should increment
        // sc and produce a different value.
        assert_eq!(commit2, commit2_retry);
        assert_eq!(confirm2.send_confirm, 1);
        assert_eq!(confirm2_retry.send_confirm, 2);
        assert!(confirm2.confirm != confirm2_retry.confirm);

        // Now complete the handshake.
        let confirm1 = handshake.sta1_handle_commit(commit2_retry);
        let key1 = handshake.sta1_handle_confirm(confirm2_retry);
        let key2 = handshake.sta2_handle_confirm(confirm1);
        assert_eq!(key1, key2);
    }

    #[test]
    fn completed_handshake_handles_resent_confirm() {
        let mut handshake = TestHandshake::new();
        let commit1 = handshake.sta1_init();
        let (commit2, confirm2) = handshake.sta2_handle_commit(commit1.clone());
        let (commit2_retry, confirm2_retry) = handshake.sta2_handle_commit(commit1);
        // Send STA1 the second confirm message first.
        let confirm1 = handshake.sta1_handle_commit(commit2);
        let key1 = handshake.sta1_handle_confirm(confirm2.clone());

        // STA1 should respond to the second confirm with its own confirm.
        let mut sink = vec![];
        handshake.sta1.handle_confirm(&mut sink, &confirm2_retry);
        assert_eq!(sink.len(), 1);
        let confirm1_retry = assert_variant!(
            sink.remove(0), SaeUpdate::Confirm(confirm) => confirm);
        assert!(confirm1.confirm != confirm1_retry.confirm);
        assert_eq!(confirm1_retry.send_confirm, u16::max_value());

        // STA2 should complete the handshake with the resent confirm.
        let key2 = handshake.sta2_handle_confirm(confirm1_retry);
        assert_eq!(key1, key2);

        // STA1 should silently drop either of our confirm frames now.
        handshake.sta1.handle_confirm(&mut sink, &confirm2_retry);
        assert!(sink.is_empty());
        handshake.sta1.handle_confirm(&mut sink, &confirm2);
        assert!(sink.is_empty());

        // STA1 should also silently drop an incorrect confirm, even if send_confirm is incremented.
        let mut confirm2_wrong = confirm2;
        confirm2_wrong.send_confirm = 10;
        handshake.sta1.handle_confirm(&mut sink, &confirm2_wrong);
        assert!(sink.is_empty());
    }

    #[test]
    fn completed_handshake_ignores_commit() {
        let mut handshake = TestHandshake::new();
        let commit1 = handshake.sta1_init();
        let (commit2, confirm2) = handshake.sta2_handle_commit(commit1);
        handshake.sta1_handle_commit(commit2);
        handshake.sta1_handle_confirm(confirm2.clone());

        // STA1 has completed it's side of the handshake.
        let mut sink = vec![];
        handshake.sta1.handle_confirm(&mut sink, &confirm2);
        assert!(sink.is_empty());
    }

    #[test]
    fn bad_first_commit_rejects_auth() {
        let mut handshake = TestHandshake::new();
        let mut commit1_wrong = handshake.sta1_init();
        commit1_wrong.element[0] += 1;

        let mut sink = vec![];
        handshake.sta2.handle_commit(&mut sink, &commit1_wrong);
        assert_eq!(sink.len(), 1);
        assert_variant!(sink.remove(0), SaeUpdate::Reject(RejectReason::AuthFailed));
    }

    #[test]
    fn bad_second_commit_ignored() {
        let mut handshake = TestHandshake::new();
        let mut commit1 = handshake.sta1_init();
        let (mut commit2_wrong, _confirm2) = handshake.sta2_handle_commit(commit1.clone());
        commit2_wrong.element[0] += 1;

        let mut sink = vec![];
        handshake.sta1.handle_commit(&mut sink, &commit2_wrong);
        assert_eq!(sink.len(), 0);
    }

    #[test]
    fn reflected_commit_discarded() {
        let mut handshake = TestHandshake::new();
        let mut commit1 = handshake.sta1_init();

        let mut sink = vec![];
        handshake.sta1.handle_commit(&mut sink, &commit1);
        assert_eq!(sink.len(), 1);
        assert_variant!(sink.remove(0), SaeUpdate::ResetTimeout(Timeout::Retransmission));
    }

    #[test]
    fn maximum_commit_retries() {
        let mut handshake = TestHandshake::new();
        let mut commit1 = handshake.sta1_init();
        let (commit2, confirm2) = handshake.sta2_handle_commit(commit1.clone());

        // STA2 should allow DOT11_RSNA_SAE_SYNC retry operations before giving up.
        for i in 0..DOT11_RSNA_SAE_SYNC {
            let (commit2_retry, confirm2_retry) = handshake.sta2_handle_commit(commit1.clone());
            assert_eq!(commit2, commit2_retry);
            assert_eq!(confirm2_retry.send_confirm, i + 2);
        }

        // The last straw!
        let mut sink = vec![];
        handshake.sta2.handle_commit(&mut sink, &commit1);
        assert_eq!(sink.len(), 1);
        assert_variant!(sink.remove(0), SaeUpdate::Reject(RejectReason::TooManyRetries));
    }

    #[test]
    fn completed_exchange_fails_after_retries() {
        let mut handshake = TestHandshake::new();
        let mut commit1 = handshake.sta1_init();
        let (commit2, confirm2) = handshake.sta2_handle_commit(commit1.clone());

        // STA2 should allow DOT11_RSNA_SAE_SYNC retry operations before giving up. We subtract 1
        // here for the reason explained in the note below.
        for i in 0..(DOT11_RSNA_SAE_SYNC - 1) {
            let (commit2_retry, confirm2_retry) = handshake.sta2_handle_commit(commit1.clone());
            assert_eq!(commit2, commit2_retry);
            assert_eq!(confirm2_retry.send_confirm, i + 2);
        }

        let mut sink = vec![];

        // Generate 3 different confirm messages for our testing...
        let confirm1_sc1 = handshake.sta1_handle_commit(commit2.clone());
        handshake.sta1.handle_commit(&mut sink, &commit2);
        assert_eq!(sink.len(), 3);
        let confirm1_sc2 = assert_variant!(sink.remove(1), SaeUpdate::Confirm(confirm) => confirm);
        let confirm1_invalid = ConfirmMsg { send_confirm: 3, ..confirm1_sc2.clone() };
        sink.clear();

        // STA2 completes the handshake. However, one more indication that STA1 is misbehaving will
        // immediately kill the authentication.
        handshake.sta2_handle_confirm(confirm1_sc1.clone());

        // NOTE: We run all of the operations here two times. This is because of a quirk in the SAE
        // state machine: while only certain operations *increment* sync, all invalid operations
        // will *check* sync. We can test whether sync is being incremented by running twice to see
        // if this pushes us over the DOT11_RSNA_SAE_SYNC threshold.

        // STA2 ignores commits.
        handshake.sta2.handle_commit(&mut sink, &commit1);
        handshake.sta2.handle_commit(&mut sink, &commit1);
        assert_eq!(sink.len(), 0);

        // STA2 ignores invalid confirm.
        handshake.sta2.handle_confirm(&mut sink, &confirm1_invalid);
        handshake.sta2.handle_confirm(&mut sink, &confirm1_invalid);
        assert_eq!(sink.len(), 0);

        // STA2 ignores old confirm.
        handshake.sta2.handle_confirm(&mut sink, &confirm1_sc1);
        handshake.sta2.handle_confirm(&mut sink, &confirm1_sc1);
        assert_eq!(sink.len(), 0);

        // But another valid confirm increments sync!
        handshake.sta2.handle_confirm(&mut sink, &confirm1_sc2);
        assert_eq!(sink.len(), 1);
        assert_variant!(sink.remove(0), SaeUpdate::Confirm(_));
        handshake.sta2.handle_confirm(&mut sink, &confirm1_sc2);
        assert_eq!(sink.len(), 1);
        assert_variant!(sink.remove(0), SaeUpdate::Reject(RejectReason::TooManyRetries));
    }
}
