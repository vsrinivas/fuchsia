// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Elliptic Curve-based cryptographic algorithms.

mod curve;

pub use public::ec::curve::{Curve, EllipticCurve, P256, P384, P521};

use public::ec::curve::DynamicCurve;
use public::ec::inner::EcKey;
use public::{inner::Key, PrivateKey, PublicKey};
use util::Sealed;
use Error;

mod inner {
    use boringssl::{self, BoringError, CHeapWrapper, CStackWrapper};
    use public::ec::curve::Curve;
    use public::inner::BoringKey;
    use Error;

    // A convenience wrapper around boringssl::EC_KEY.
    //
    // EcKey maintains the following invariants:
    // - The key is valid.
    // - The key is on the curve C (unless C is DynamicCurve, in which case any
    //   supported curve is valid).
    //
    // This is marked pub and put in this (non-public) module so that using it in impls of
    // the Key trait don't result in public-in-private errors.
    #[derive(Clone)]
    pub struct EcKey<C: Curve> {
        pub key: CHeapWrapper<boringssl::EC_KEY>,
        curve: C,
    }

    impl<C: Curve> EcKey<C> {
        pub fn generate(curve: C) -> Result<EcKey<C>, BoringError> {
            let mut key = CHeapWrapper::default();
            key.ec_key_set_group(&curve.group()).unwrap();
            key.ec_key_generate_key()?;
            Ok(EcKey { key, curve })
        }

        pub fn curve(&self) -> C {
            self.curve
        }

        /// Creates an `EcKey` from a BoringSSL `EC_KEY`.
        ///
        /// If `C` is not `DynamicCurve`, `from_EC_KEY` validates that `key`'s
        /// curve is `C`.
        #[allow(non_snake_case)]
        pub fn from_EC_KEY(key: CHeapWrapper<boringssl::EC_KEY>) -> Result<EcKey<C>, Error> {
            // If C is DynamicCurve, then from_group is infallible.
            let curve = C::from_group(key.ec_key_get0_group().unwrap())?;
            Ok(EcKey { key, curve })
        }
    }

    impl<C: Curve> BoringKey for EcKey<C> {
        fn pkey_assign(&self, pkey: &mut CHeapWrapper<boringssl::EVP_PKEY>) -> Result<(), Error> {
            pkey.evp_pkey_assign_ec_key(self.key.clone())
                .map_err(From::from)
        }

        fn pkey_get(pkey: &mut CHeapWrapper<boringssl::EVP_PKEY>) -> Result<Self, Error> {
            let key = pkey.evp_pkey_get1_ec_key()?;
            EcKey::from_EC_KEY(key)
        }

        fn parse_private_key(cbs: &mut CStackWrapper<boringssl::CBS>) -> Result<EcKey<C>, Error> {
            // The last argument is a group. If it's not None, then it is either
            // used as the group or, if the DER encoding also contains a group,
            // the encoded group is validated against the group passed as an
            // argument. If C is one of the static curve types, static_group
            // returns an EC_GROUP object. If C is DynamicCurve, it returns
            // None, so no group validation will be performed. Note that this
            // validation is mostly redundant - similar validation is performed
            // in EcKey::from_EC_KEY - however, it's not fully redundant, since
            // it allows keys to be parsed which have no group so long as the
            // caller has specified one of the static curve types.
            let key = CHeapWrapper::ec_key_parse_private_key(cbs, C::static_group())?;
            EcKey::from_EC_KEY(key)
        }

        fn marshal_private_key(
            &self, cbb: &mut CStackWrapper<boringssl::CBB>,
        ) -> Result<(), Error> {
            self.key.ec_key_marshal_private_key(cbb).map_err(From::from)
        }
    }

    #[cfg(test)]
    mod tests {
        use std::mem;

        use super::*;
        use public::ec::{P256, P384, P521};

        #[test]
        fn test_refcount() {
            fn test<C: Curve>(curve: C) {
                let key = EcKey::generate(curve).unwrap();
                for i in 0..8 {
                    // make i clones and then free them all
                    let mut keys = Vec::new();
                    for _ in 0..i {
                        keys.push(key.clone());
                    }
                    mem::drop(keys);
                }
                mem::drop(key);
            }

            test(P256);
            test(P384);
            test(P521);
        }
    }
}

/// An elliptic curve public key.
///
/// `EcPubKey` is a public key over the curve `C`.
pub struct EcPubKey<C: Curve> {
    inner: EcKey<C>,
}

impl<C: Curve> EcPubKey<C> {
    /// Gets the curve of this key.
    ///
    /// `curve` is useful when you have an `EcPubKey` with a generic [`Curve`]
    /// parameter. This allows you to call [`EcPrivKey::generate`], which
    /// requires a [`Curve`] argument.
    ///
    /// # Examples
    ///
    /// ```rust
    /// use mundane::public::ec::{Curve, EcPrivKey, EcPubKey};
    /// use mundane::public::PrivateKey;
    ///
    /// fn generate_another<C: Curve>(key: EcPubKey<C>) -> EcPubKey<C> {
    ///     EcPrivKey::generate(key.curve()).unwrap().public()
    /// }
    /// ```
    #[must_use]
    pub fn curve(&self) -> C {
        self.inner.curve()
    }
}

impl<C: Curve> Sealed for EcPubKey<C> {}

impl<C: Curve> Key for EcPubKey<C> {
    type Boring = EcKey<C>;
    fn get_boring(&self) -> &EcKey<C> {
        &self.inner
    }
    fn from_boring(inner: EcKey<C>) -> EcPubKey<C> {
        EcPubKey { inner }
    }
}

impl<C: Curve> PublicKey for EcPubKey<C> {
    type Private = EcPrivKey<C>;
}

/// An elliptic curve private key.
///
/// `EcPrivKey` is a private key over the curve `C`.
pub struct EcPrivKey<C: Curve> {
    inner: EcKey<C>,
}

impl<C: Curve> EcPrivKey<C> {
    /// Generates a new private key.
    #[must_use]
    pub fn generate(curve: C) -> Result<EcPrivKey<C>, Error> {
        Ok(EcPrivKey {
            inner: EcKey::generate(curve)?,
        })
    }

    /// Gets the curve of this key.
    ///
    /// `curve` is useful when you have an `EcPrivKey` with a generic [`Curve`]
    /// parameter. This allows you to call [`EcPrivKey::generate`], which
    /// requires a [`Curve`] argument.
    ///
    /// # Examples
    ///
    /// ```rust
    /// use mundane::public::ec::{Curve, EcPrivKey};
    ///
    /// fn generate_another<C: Curve>(key: EcPrivKey<C>) -> EcPrivKey<C> {
    ///     EcPrivKey::generate(key.curve()).unwrap()
    /// }
    /// ```
    #[must_use]
    pub fn curve(&self) -> C {
        self.inner.curve()
    }
}

impl<C: Curve> Sealed for EcPrivKey<C> {}

impl<C: Curve> Key for EcPrivKey<C> {
    type Boring = EcKey<C>;
    fn get_boring(&self) -> &EcKey<C> {
        &self.inner
    }
    fn from_boring(inner: EcKey<C>) -> EcPrivKey<C> {
        EcPrivKey { inner }
    }
}

impl<C: Curve> PrivateKey for EcPrivKey<C> {
    type Public = EcPubKey<C>;

    fn public(&self) -> EcPubKey<C> {
        EcPubKey {
            inner: self.inner.clone(),
        }
    }
}

/// Parses a public key in DER format with any curve.
///
/// `parse_public_key_der_any_curve` is like [`::public::parse_public_key_der`],
/// but it accepts any [`Curve`] rather than a particular, static curve.
///
/// Since `parse_public_key_der` takes a `PublicKey` type argument, and
/// [`EcPubKey`] requires a static [`Curve`] type parameter,
/// `parse_public_key_der` can only be called when the curve is known ahead of
/// time. `parse_public_key_der_any_curve`, on the other hand, accepts any
/// curve. Because the curve is not known statically, the [`Curve`] parameter to
/// [`EcPubKey`] is only exposed as `impl Curve`.
///
/// Because the curve is not known statically, one must be specified in the DER
/// input.
#[must_use]
pub fn parse_public_key_der_any_curve(bytes: &[u8]) -> Result<EcPubKey<impl Curve>, Error> {
    ::public::parse_public_key_der::<EcPubKey<DynamicCurve>>(bytes)
}

/// Parses a private key in DER format with any curve.
///
/// `parse_private_key_der_any_curve` is like
/// [`::public::parse_private_key_der`], but it accepts any [`Curve`] rather than
/// a particular, static curve.
///
/// Since `parse_private_key_der` takes a `PrivateKey` type argument, and
/// [`EcPrivKey`] requires a static [`Curve`] type parameter,
/// `parse_private_key_der` can only be called when the curve is known ahead of
/// time. `parse_private_key_der_any_curve`, on the other hand, accepts any
/// curve. Because the curve is not known statically, the [`Curve`] parameter to
/// [`EcPrivKey`] is only exposed as `impl Curve`.
///
/// Because the curve is not known statically, one must be specified in the DER
/// input.
#[must_use]
pub fn parse_private_key_der_any_curve(bytes: &[u8]) -> Result<EcPrivKey<impl Curve>, Error> {
    ::public::parse_private_key_der::<EcPrivKey<DynamicCurve>>(bytes)
}

/// The Elliptic Curve Digital Signature Algorithm.
pub mod ecdsa {
    use boringssl;
    use public::ec::{Curve, EcPrivKey, EcPubKey};
    use Error;

    // The maximum length of an ECDSA signature over P-521. Since this isn't
    // exposed in the API, we can increase later if we add support for curves
    // with larger signatures.
    //
    // This was calculated with the following equation, which is thanks to
    // agl@google.com:
    //
    //  r = s = (521 + 7)/8              # Bytes to store the integers r and s
    //        = 66
    //  DER encoded bytes =   (1         # type byte 0x02
    //                      +  1         # length byte
    //                      +  1         # possible 0 padding
    //                      +  66) * 2   # one for each of r and s
    //                      +  1         # ASN.1 SEQUENCE type byte
    //                      +  2         # outer length
    //                    =    141
    const MAX_SIGNATURE_LEN: usize = 141;

    /// A DER-encoded ECDSA signature.
    pub struct EcdsaSignature {
        bytes: [u8; MAX_SIGNATURE_LEN],
        // Invariant: len is in [0; MAX_SIGNATURE_LEN). If len is 0, it
        // indicates an invalid signature. Invalid signatures can be produced
        // when a caller invokes from_bytes with a byte slice longer than
        // MAX_SIGNATURE_LEN. Such signatures cannot possibly have been
        // generated by an ECDSA signature over any of the curves we support,
        // and so it could not possibly be valid. In other words, it would never
        // be correct for ecdsa_verify to return true when invoked on such a
        // signature.
        //
        // However, if we were to simply truncate the byte slice and store a
        // subset of it, then we might open ourselves up to attacks in which an
        // attacker induces a mismatch between the signature that the caller
        // /thinks/ is being verified and the signature that is /actually/ being
        // verified. Thus, it's important that we always reject such signatures.
        //
        // Finally, it's OK for us to use 0 as the sentinal value to mean
        // "invalid signature" because ECDSA can never produce a 0-byte
        // signature. Thus, we will never produce a 0-byte signature from
        // ecdsa_sign, and similarly, if the caller constructs a 0-byte
        // signature using from_bytes, it's correct for us to treat it as
        // invalid.
        len: usize,
    }

    impl EcdsaSignature {
        /// Constructs an `EcdsaSignature` from raw bytes.
        #[must_use]
        pub fn from_bytes(bytes: &[u8]) -> EcdsaSignature {
            if bytes.len() > MAX_SIGNATURE_LEN {
                // see comment on the len field for why we do this
                return Self::empty();
            }
            let mut ret = Self::empty();
            (&mut ret.bytes[..bytes.len()]).copy_from_slice(bytes);
            ret.len = bytes.len();
            ret
        }

        /// Gets the raw bytes of this `EcdsaSignature`.
        #[must_use]
        pub fn bytes(&self) -> &[u8] {
            &self.bytes[..self.len]
        }

        fn is_valid(&self) -> bool {
            self.len != 0
        }

        fn empty() -> EcdsaSignature {
            EcdsaSignature {
                bytes: [0u8; MAX_SIGNATURE_LEN],
                len: 0,
            }
        }
    }

    /// Computes an ECDSA signature.
    #[must_use]
    pub fn ecdsa_sign<C: Curve>(
        key: &EcPrivKey<C>, digest: &[u8],
    ) -> Result<EcdsaSignature, Error> {
        let mut sig = EcdsaSignature::empty();
        sig.len = boringssl::ecdsa_sign(digest, &mut sig.bytes[..], &key.inner.key)?;
        Ok(sig)
    }

    /// Verifies an ECDSA signature.
    ///
    /// `ecdsa_verify` returns `true` if the signature is authentic, and false
    /// if it is inauthentic or if there was an error in processing.
    #[must_use]
    pub fn ecdsa_verify<C: Curve>(key: &EcPubKey<C>, digest: &[u8], sig: &EcdsaSignature) -> bool {
        if !sig.is_valid() {
            // see comment on EcdsaSignature::len for why we do this
            return false;
        }
        boringssl::ecdsa_verify(digest, sig.bytes(), &key.inner.key)
    }

    #[cfg(test)]
    mod tests {
        use super::{super::*, *};

        #[test]
        fn test_smoke() {
            // Sign the digest, verify the signature, and return the signature.
            // Also verify that, if the wrong signature is used, the signature
            // fails to verify. Also verify that EcdsaSignature::from_bytes
            // works.
            fn sign_and_verify<C: Curve>(key: &EcPrivKey<C>, digest: &[u8]) -> EcdsaSignature {
                let sig = ecdsa_sign(&key, digest).unwrap();
                assert!(ecdsa_verify(&key.public(), digest, &sig));
                let sig2 = ecdsa_sign(&key, sig.bytes()).unwrap();
                assert!(!ecdsa_verify(&key.public(), digest, &sig2));
                EcdsaSignature::from_bytes(sig.bytes())
            }

            let p256 = EcPrivKey::generate(P256).unwrap();
            let p384 = EcPrivKey::generate(P384).unwrap();
            let p521 = EcPrivKey::generate(P521).unwrap();

            // Sign an empty digest, and verify the signature. Use the signature
            // as the next digest to test, and repeat many times.
            let mut digest = Vec::new();
            for _ in 0..4 {
                digest = sign_and_verify(&p256, &digest).bytes().to_vec();
                digest = sign_and_verify(&p384, &digest).bytes().to_vec();
                digest = sign_and_verify(&p521, &digest).bytes().to_vec();
            }
        }

        #[test]
        fn test_invalid_signature() {
            fn test_is_invalid(sig: &EcdsaSignature) {
                assert_eq!(sig.len, 0);
                assert!(!sig.is_valid());
                assert!(!ecdsa_verify(
                    &EcPrivKey::generate(P521).unwrap().public(),
                    &[],
                    &sig
                ));
            }
            test_is_invalid(&EcdsaSignature::from_bytes(&[0; MAX_SIGNATURE_LEN + 1]));
            test_is_invalid(&EcdsaSignature::from_bytes(&[]));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use public::ec::ecdsa::{ecdsa_sign, ecdsa_verify};
    use public::{marshal_private_key_der, marshal_public_key_der, parse_private_key_der,
                 parse_public_key_der};
    use util::should_fail;

    #[test]
    fn test_generate() {
        EcPrivKey::generate(P256).unwrap();
        EcPrivKey::generate(P384).unwrap();
        EcPrivKey::generate(P521).unwrap();
    }

    #[test]
    fn test_marshal_parse() {
        fn test<C: Curve>(curve: C) {
            const HASH: &[u8] = &[0, 1, 2, 3, 4, 5, 6, 7];
            let key = EcPrivKey::generate(curve).unwrap();

            let parsed_key: EcPrivKey<C> =
                parse_private_key_der(&marshal_private_key_der(&key)).unwrap();
            let parsed_key_any_curve =
                parse_private_key_der_any_curve(&marshal_private_key_der(&key)).unwrap();
            let pubkey = key.public();
            let parsed_pubkey: EcPubKey<C> =
                parse_public_key_der(&marshal_public_key_der(&pubkey)).unwrap();
            let parsed_pubkey_any_curve =
                parse_public_key_der_any_curve(&marshal_public_key_der(&pubkey)).unwrap();

            fn sign_and_verify<C1: Curve, C2: Curve>(
                privkey: &EcPrivKey<C1>, pubkey: &EcPubKey<C2>,
            ) {
                let sig = ecdsa_sign(&privkey, HASH).unwrap();
                assert!(ecdsa_verify(&pubkey, HASH, &sig));
            }

            // Sign and verify with every pair of keys to make sure we parsed
            // the same key we marshaled.
            sign_and_verify(&key, &pubkey);
            sign_and_verify(&key, &parsed_pubkey);
            sign_and_verify(&key, &parsed_pubkey_any_curve);
            sign_and_verify(&parsed_key, &pubkey);
            sign_and_verify(&parsed_key, &parsed_pubkey);
            sign_and_verify(&parsed_key, &parsed_pubkey_any_curve);
            sign_and_verify(&parsed_key_any_curve, &pubkey);
            sign_and_verify(&parsed_key_any_curve, &parsed_pubkey);
            sign_and_verify(&parsed_key_any_curve, &parsed_pubkey_any_curve);

            let _ = marshal_public_key_der::<EcPubKey<C>>;
            let _ = parse_public_key_der::<EcPubKey<C>>;
        }

        test(P256);
        test(P384);
        test(P521);
    }

    #[test]
    fn test_parse_fail() {
        // Test that invalid input is rejected.
        fn test_parse_invalid<C: Curve>() {
            should_fail(
                parse_private_key_der::<EcPrivKey<C>>(&[]),
                "parse_private_key_der",
                "elliptic curve routines:OPENSSL_internal:DECODE_ERROR",
            );
            should_fail(
                parse_public_key_der::<EcPubKey<C>>(&[]),
                "parse_public_key_der",
                "public key routines:OPENSSL_internal:DECODE_ERROR",
            );
            // These (xxx_any_curve) are redundant since we also call with C =
            // DynamicCurve (xxx_any_curve just calls xxx with C =
            // DynamicCurve), but better safe than sorry.
            should_fail(
                parse_private_key_der_any_curve(&[]),
                "parse_private_key_der_any_curve",
                "elliptic curve routines:OPENSSL_internal:DECODE_ERROR",
            );
            should_fail(
                parse_public_key_der_any_curve(&[]),
                "parse_public_key_der_any_curve",
                "public key routines:OPENSSL_internal:DECODE_ERROR",
            );
        }

        test_parse_invalid::<P256>();
        test_parse_invalid::<P384>();
        test_parse_invalid::<P521>();
        test_parse_invalid::<DynamicCurve>();

        // Test that, when a particular curve is expected, other curves are
        // rejected.
        fn test_parse_wrong_curve<C1: Default + Curve, C2: Curve>() {
            let privkey = EcPrivKey::generate(C1::default()).unwrap();
            let key_der = marshal_private_key_der(&privkey);
            should_fail(
                parse_private_key_der::<EcPrivKey<C2>>(&key_der),
                "parse_private_key_der",
                "elliptic curve routines:OPENSSL_internal:GROUP_MISMATCH",
            );
            let key_der = marshal_public_key_der(&privkey.public());
            should_fail(
                parse_public_key_der::<EcPubKey<C2>>(&key_der),
                "parse_public_key_der",
                "mundane: unexpected curve:",
            );
        }

        // All pairs of curves, (X, Y), such that X != Y.
        test_parse_wrong_curve::<P256, P384>();
        test_parse_wrong_curve::<P256, P521>();
        test_parse_wrong_curve::<P384, P256>();
        test_parse_wrong_curve::<P384, P521>();
        test_parse_wrong_curve::<P521, P256>();
        test_parse_wrong_curve::<P521, P384>();
    }
}
