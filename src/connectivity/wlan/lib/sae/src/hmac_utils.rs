// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use mundane::{
    hash::{Digest, Hasher},
    hmac::Hmac,
};

// IEEE Std. 802.11-2020 12.4: Simultaneous Authentication of Equals (SAE)
// SAE uses a few constructions using HMACs, where the underlying cryptographic hash algorithm is
// parameterized based on properties of the connection.  This file contains implementations of
// these constructions, as well as a generic implementing a trait that allows passing around a set
// of parameterized function calls as a trait object.

fn div_ceil(x: usize, y: usize) -> usize {
    (x + (y - 1)) / y
}

/// IEEE Std. 802.11-2020 12.7.1.6.2
/// The KDF function (named "KDF-Hash-Length" in the specification).
pub fn kdf_hash_length<H>(key: &[u8], label: &str, context: &[u8], bits: usize) -> Vec<u8>
where
    H: Hasher,
    <<H as Hasher>::Digest as Digest>::Bytes: AsRef<[u8]> + AsMut<[u8]>,
{
    let byte_length = div_ceil(bits, 8);
    let iterations = div_ceil(bits, H::Digest::DIGEST_LEN * 8);
    let mut result = Vec::with_capacity(byte_length);
    let mut copied: usize = 0;
    for i in 1..=iterations {
        let to_copy = std::cmp::min(H::Digest::DIGEST_LEN, byte_length - copied);
        {
            let mut hmac: Hmac<H> = Hmac::new(key);
            hmac.update(&(i as u16).to_le_bytes());
            hmac.update(label.as_bytes());
            hmac.update(context);
            hmac.update(&(bits as u16).to_le_bytes());
            let mut digest = hmac.finish().bytes();
            // IEEE Std. 802.11-2020 12.7.1.6.2: unused bits should be "irretrievably deleted".
            digest.as_mut()[to_copy..H::Digest::DIGEST_LEN].fill(0);
            result.extend_from_slice(&digest.as_ref()[..to_copy]);
        }
        copied += to_copy;
    }
    result
}

/// IEEE Std. 802.11-2020 12.4.4.2.3/12.4.4.3.3, IETF RFC 5869
/// A KDF to "extract" entropy from a short key (named "H" or "HKDF-Extract" in the specification).
pub fn hkdf_extract<H>(salt: &[u8], ikm: &[u8]) -> Vec<u8>
where
    H: Hasher,
    <<H as Hasher>::Digest as Digest>::Bytes: AsRef<[u8]>,
{
    // The HMAC is used to extract entropy from `ikm`, the input keying material; thus it is keyed
    // with `salt` and `ikm` is used as the input message.
    let mut hmac: Hmac<H> = Hmac::new(salt);
    hmac.update(ikm);
    hmac.finish().bytes().as_ref().to_vec()
}

/// IEEE Std. 802.11-2020 12.4.4.2.3/12.4.4.3.3, IETF RFC 5869
/// A KDF to "expand" entropy into a long key (named "H" and "HKDF-Expand" in the specification).
pub fn hkdf_expand<H>(prk: &[u8], info: &str, length: usize) -> Vec<u8>
where
    H: Hasher,
    <<H as Hasher>::Digest as Digest>::Bytes: AsRef<[u8]>,
{
    let mut result: Vec<u8> = Vec::with_capacity(length);
    let mut copied: usize = 0;
    let mut prev_digest: Option<H::Digest> = None;
    let digest_count = div_ceil(length, H::Digest::DIGEST_LEN);
    for counter in 1..digest_count + 1 {
        let to_copy = std::cmp::min(H::Digest::DIGEST_LEN, length - copied);
        let digest = {
            let mut hmac: Hmac<H> = Hmac::new(prk);
            if let Some(prev_digest) = &prev_digest {
                hmac.update(prev_digest.bytes().as_ref());
            }
            hmac.update(info.as_bytes());
            hmac.update(&(counter as u8).to_le_bytes());
            hmac.finish()
        };
        result.extend_from_slice(&digest.bytes().as_ref()[..to_copy]);
        copied += to_copy;
        prev_digest.insert(digest);
    }
    result
}

/// IEEE Std. 802.11-2020 12.4.2
/// The "confirm function" (named "CN" in the specification).
pub fn confirm<H>(key: &[u8], counter: u16, data: &[&[u8]]) -> Vec<u8>
where
    H: Hasher,
    <<H as Hasher>::Digest as Digest>::Bytes: AsRef<[u8]>,
{
    let mut hmac: Hmac<H> = Hmac::new(key);
    hmac.update(&counter.to_le_bytes());
    for data_part in data {
        hmac.update(data_part);
    }
    hmac.finish().bytes().as_ref().to_vec()
}

/// Trait encapsulating implementations of the HMAC constructions for a given hasher type.
pub trait HmacUtils {
    /// The number of bits in the digest length of the HMAC type.
    fn bits(&self) -> usize;
    /// See: [kdf_hash_length()]
    fn kdf_hash_length(&self, key: &[u8], label: &str, context: &[u8], bits: usize) -> Vec<u8>;
    /// See: [hkdf_extract()]
    fn hkdf_extract(&self, salt: &[u8], ikm: &[u8]) -> Vec<u8>;
    /// See: [hkdf_expand()]
    fn hkdf_expand(&self, prk: &[u8], info: &str, length: usize) -> Vec<u8>;
    /// See: [confirm()]
    fn confirm(&self, key: &[u8], counter: u16, data: &[&[u8]]) -> Vec<u8>;
}

/// Generic implementation of [HmacUtils] parameterized on a [Hasher] type.
#[derive(Debug, Clone)]
pub struct HmacUtilsImpl<H>
where
    H: Hasher,
    <<H as Hasher>::Digest as Digest>::Bytes: AsRef<[u8]>,
{
    // Declared as PhantoimData<fn(H)> so that HmacUtilsImpl can implement Send.
    hasher_type: std::marker::PhantomData<fn(H)>,
}

impl<H> HmacUtilsImpl<H>
where
    H: Hasher,
    <<H as Hasher>::Digest as Digest>::Bytes: AsRef<[u8]>,
{
    pub fn new() -> Self {
        Self { hasher_type: std::marker::PhantomData }
    }
}

impl<H> HmacUtils for HmacUtilsImpl<H>
where
    H: Hasher,
    <<H as Hasher>::Digest as Digest>::Bytes: AsMut<[u8]> + AsRef<[u8]>,
{
    fn bits(&self) -> usize {
        H::Digest::DIGEST_LEN * 8
    }

    fn kdf_hash_length(&self, key: &[u8], label: &str, context: &[u8], bits: usize) -> Vec<u8> {
        kdf_hash_length::<H>(key, label, context, bits)
    }

    fn hkdf_extract(&self, salt: &[u8], ikm: &[u8]) -> Vec<u8> {
        hkdf_extract::<H>(salt, ikm)
    }

    fn hkdf_expand(&self, prk: &[u8], info: &str, length: usize) -> Vec<u8> {
        hkdf_expand::<H>(prk, info, length)
    }

    fn confirm(&self, key: &[u8], counter: u16, data: &[&[u8]]) -> Vec<u8> {
        confirm::<H>(key, counter, data)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::boringssl::{Bignum, BignumCtx, EcGroup, EcGroupId},
        hex::{decode, encode},
        mundane::hash::Sha256,
        std::convert::TryFrom,
    };

    // IEEE Std. 802.11-2020 J.10
    // The 802.11 spec does not specify test vectors for the individual HMAC constructions
    // explicitly; instead we test them against the test vectors for a step of an example SAE
    // exchange that exercises the construction.
    const TEST_GROUP: EcGroupId = EcGroupId::P256;
    const TEST_LABEL: &'static str = "SAE Hunting and Pecking";
    const TEST_H_1: &'static str =
        "a9025368ef78f7d65e8d4d556f0d1d0d758f2f7f1e116eb1d11307a7e8a9621a";
    const TEST_CAND_1: &'static str =
        "b8e89a725c57f18e8f68a7f72613e15f1c904938c38800efa01f1306f5e454b5";
    const TEST_H_2: &'static str =
        "954bbbf8923284e4ca164e3af0b9520ce53aa35be39020e9ccb23aff86df2226";
    const TEST_CAND_2: &'static str =
        "da6eb7b06a1ac5624974f90afdd6a8e9d5722634cf987c34defc91a9874e5658";
    const TEST_H_40: &'static str =
        "cde7b81eb539c87af5bf1be2402d315c45ad4c3db06c9c56b7f8b7daae5e5842";
    const TEST_CAND_40: &'static str =
        "2e12a1d615647963fd7aa4a905fd51b6f49a902fd917ef8f0ff200102699ecdb";
    const TEST_SSID: &'static str = "byteme";
    const TEST_IDENTIFIER: &'static str = "psk4internet";
    const TEST_PASSWORD: &'static str = "mekmitasdigoat";
    const TEST_INFO_1: &'static str = "SAE Hash to Element u1 P1";
    const TEST_INFO_2: &'static str = "SAE Hash to Element u2 P2";
    const TEST_PRK: &'static str =
        "3bd53fe9223dc0280fbfce17d7a3564064e20f48c6ec72246ce367b5569a22af";
    const TEST_OKM_1: &'static str =
        "a5044469ab16f25b6abf1e0e37a36b56f50be73369053df8db87989a6b66fd1a\
         491f1cdacbd07931620f83008ffc0ecc";
    const TEST_OKM_2: &'static str =
        "9b4e0d5b1879f253c5319615099b05aec5b06fa5e788bcfd1e9ea60d33436927\
         190814c322a62585c93c577bbaa3d307";

    // IEEE Std. 802.11-18/1104r0: "New Test Vectors for SAE"
    // Neither the 802.11-2016 nor the 802.11-2020 specs define a test vector result for the
    // confirm construction.  For this we use the proposed new test vectors in 802.11-18/1104r0.
    const TEST_LABEL_2: &'static str = "SAE KCK and PMK";
    const TEST_LOCAL_COMMIT: &'static str =
        "1300eb3bab1964e4a0ab05925ddf3339519138bc65d6cdc0f813dd6fd4344eb4\
         bfe44b5c21597658f4e3eddfb4b99f25b4d6540f32ff1fd5c530c60a79444861\
         0bc6de3d92bdbbd47d935980ca6cf8988ab6630be6764c885ceb9793970f6952\
         17eeff0d2170736b34696e7465726e6574";
    const TEST_PEER_COMMIT: &'static str =
        "13005564f045b2ea1e566cf1dd741f70d9be35d2df5b9a5502946ee03cf8dae2\
         7e1e05b8430eb7a99e24877ce69baf3dc580e309633d6b385f83ee1c3ec3591f\
         1a5393c06e805ddceb2fde50930dd7cfebb987c6ff9666af164eb5184d8e6662\
         ed6aff0d2170736b34696e7465726e6574";
    const TEST_KEYSEED: &'static str =
        "7457a00754dcc4e3dc2850c124d6bb8fa1699d7fa33bb0667d9c34eeb513deb9";
    const TEST_KCK: &'static str =
        "599d6f1e27548be8499dceed2feccf94818ce1c79f1b4eb3d6a53228a09bf3ed";
    const TEST_LOCAL_CONFIRM: &'static str =
        "010012d9d5c78c500526d36c41dbc56aedf2914cedddd7cad4a58c48f83dbde9\
         fc77";
    const TEST_PEER_CONFIRM: &'static str =
        "010002871cf906898b8060ec184143be77b8c08a8019b13eb6d0aef0d8383dfa\
         c2fd";
    const TEST_PMK: &'static str =
        "7aead86fba4c3221fc437f5f14d70d854ea5d5aac1690116793081eda4d557c5";

    #[test]
    fn test_kdf_sha256_256() {
        let bignumctx = BignumCtx::new().unwrap();
        let group = EcGroup::new(TEST_GROUP).unwrap();
        let p = group.get_params(&bignumctx).unwrap().p;
        let p_vec = p.to_be_vec(p.len());
        let p_bits = p.bits();

        let cand_1 =
            kdf_hash_length::<Sha256>(&hex::decode(TEST_H_1).unwrap(), TEST_LABEL, &p_vec, p_bits);
        assert_eq!(hex::encode(&cand_1), TEST_CAND_1);
        let cand_2 =
            kdf_hash_length::<Sha256>(&hex::decode(TEST_H_2).unwrap(), TEST_LABEL, &p_vec, p_bits);
        assert_eq!(hex::encode(&cand_2), TEST_CAND_2);
        let cand_40 =
            kdf_hash_length::<Sha256>(&hex::decode(TEST_H_40).unwrap(), TEST_LABEL, &p_vec, p_bits);
        assert_eq!(hex::encode(&cand_40), TEST_CAND_40);
    }

    #[test]
    fn test_kdf_sha256_512() {
        let bignumctx = BignumCtx::new().unwrap();
        let group = EcGroup::new(TEST_GROUP).unwrap();
        let r = group.get_order(&bignumctx).unwrap();

        // IEEE Std. 802.11-2020 12.4.5.4
        // TEST_LOCAL_COMMIT and TEST_PEER_COMMIT contain constructed SAE commit messages; we parse
        // their fields here.
        let local_commit_scalar = hex::decode(TEST_LOCAL_COMMIT).unwrap();
        let local_commit_scalar = &local_commit_scalar[2..2 + r.len()];
        let local_commit_scalar = Bignum::new_from_slice(local_commit_scalar).unwrap();
        let peer_commit_scalar = hex::decode(TEST_PEER_COMMIT).unwrap();
        let peer_commit_scalar = &peer_commit_scalar[2..2 + r.len()];
        let peer_commit_scalar = Bignum::new_from_slice(peer_commit_scalar).unwrap();

        let context = local_commit_scalar.mod_add(&peer_commit_scalar, &r, &bignumctx).unwrap();
        let q = 256;
        let kck_and_pmk = kdf_hash_length::<Sha256>(
            &hex::decode(TEST_KEYSEED).unwrap(),
            TEST_LABEL_2,
            &context.to_be_vec(r.len()),
            q + 256,
        );
        assert_eq!(kck_and_pmk.len(), (q + 256) / 8);
        assert_eq!(hex::encode(&kck_and_pmk[0..q / 8]), TEST_KCK);
        assert_eq!(hex::encode(&kck_and_pmk[q / 8..(q + 256) / 8]), TEST_PMK);
    }

    #[test]
    fn test_kdf_sha256_short() {
        let key = hex::decode("f0f0f0f0").unwrap();
        let label = "LABELED!";
        let context = hex::decode("babababa").unwrap();
        let hash = kdf_hash_length::<Sha256>(&key[..], label, &context[..], 128);
        assert_eq!(hash.len(), 16);
    }

    #[test]
    fn test_kdf_sha256_empty_data() {
        let key = hex::decode("f0f0f0f0").unwrap();
        let label = "LABELED!";
        let context = hex::decode("babababa").unwrap();
        let hash = kdf_hash_length::<Sha256>(&key[..], label, &context[..], 0);
        assert_eq!(hash.len(), 0);
    }

    #[test]
    fn test_kdf_sha256_all_empty() {
        let key = vec![];
        let label = "";
        let context = vec![];
        let hash = kdf_hash_length::<Sha256>(&key[..], label, &context[..], 0);
        assert_eq!(hash.len(), 0);
    }

    #[test]
    fn test_hkdf_extract() {
        let mut password_with_id: String = String::from(TEST_PASSWORD);
        password_with_id.push_str(TEST_IDENTIFIER);
        let pwd_seed = hkdf_extract::<Sha256>(TEST_SSID.as_bytes(), password_with_id.as_bytes());
        assert_eq!(hex::encode(pwd_seed), TEST_PRK);
    }

    #[test]
    fn test_hkdf_expand() {
        let bignumctx = BignumCtx::new().unwrap();
        let group = EcGroup::new(TEST_GROUP).unwrap();
        let p = group.get_params(&bignumctx).unwrap().p;
        let p_len = p.len();

        let okm_1 = hkdf_expand::<Sha256>(
            &hex::decode(TEST_PRK).unwrap(),
            TEST_INFO_1,
            p_len + (p_len / 2),
        );
        assert_eq!(hex::encode(&okm_1), TEST_OKM_1);
        let okm_2 = hkdf_expand::<Sha256>(
            &hex::decode(TEST_PRK).unwrap(),
            TEST_INFO_2,
            p_len + (p_len / 2),
        );
        assert_eq!(hex::encode(&okm_2), TEST_OKM_2);
    }

    #[test]
    fn test_confirm() {
        let bignumctx = BignumCtx::new().unwrap();
        let group = EcGroup::new(TEST_GROUP).unwrap();
        let r = group.get_order(&bignumctx).unwrap();

        // IEEE Std. 802.11-2020 12.4.7.4
        // TEST_LOCAL_COMMIT and TEST_PEER_COMMIT contain constructed SAE commit messages; we parse
        // their fields here.
        let local_commit_bytes = hex::decode(TEST_LOCAL_COMMIT).unwrap();
        let local_commit_scalar = &local_commit_bytes[2..2 + r.len()];
        let local_commit_element = &local_commit_bytes[2 + r.len()..2 + r.len() * 3];
        let peer_commit_bytes = hex::decode(TEST_PEER_COMMIT).unwrap();
        let peer_commit_scalar = &peer_commit_bytes[2..2 + r.len()];
        let peer_commit_element = &peer_commit_bytes[2 + r.len()..2 + r.len() * 3];

        // IEEE Std. 802.11-2020 12.4.7.5
        // TEST_LOCAL_CONFIRM and TEST_PEER_CONFIRM contain constructed SAE confirm messages; we
        // parse their fields here.
        let local_confirm_bytes = hex::decode(TEST_LOCAL_CONFIRM).unwrap();
        let local_send_confirm =
            u16::from_le_bytes(*<&[u8; 2]>::try_from(&local_confirm_bytes[0..2]).unwrap());
        let local_confirm_element = &local_confirm_bytes[2..2 + r.len()];
        let peer_confirm_bytes = hex::decode(TEST_PEER_CONFIRM).unwrap();
        let peer_send_confirm =
            u16::from_le_bytes(*<&[u8; 2]>::try_from(&peer_confirm_bytes[0..2]).unwrap());
        let peer_confirm_element = &peer_confirm_bytes[2..2 + r.len()];

        let local_confirm = confirm::<Sha256>(
            &hex::decode(TEST_KCK).unwrap(),
            local_send_confirm,
            &[local_commit_scalar, local_commit_element, peer_commit_scalar, peer_commit_element],
        );
        assert_eq!(hex::encode(&local_confirm), hex::encode(local_confirm_element));

        let peer_confirm = confirm::<Sha256>(
            &hex::decode(TEST_KCK).unwrap(),
            peer_send_confirm,
            &[peer_commit_scalar, peer_commit_element, local_commit_scalar, local_commit_element],
        );
        assert_eq!(hex::encode(&peer_confirm), hex::encode(peer_confirm_element));
    }
}
