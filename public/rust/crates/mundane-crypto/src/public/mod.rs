// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Public key cryptography.

pub mod ec;

use boringssl::{CHeapWrapper, CStackWrapper};
use public::inner::BoringKey;
use util::Sealed;
use Error;

/// The public component of a public/private key pair.
pub trait PublicKey: Sealed + Sized + self::inner::Key {
    /// The type of the private component.
    type Private: PrivateKey<Public = Self>;
}

/// The private component of a public/private key pair.
pub trait PrivateKey: Sealed + Sized + self::inner::Key {
    /// The type of the public component.
    type Public: PublicKey<Private = Self>;

    /// Gets the public key corresponding to this private key.
    #[must_use]
    fn public(&self) -> Self::Public;
}

mod inner {
    use boringssl::{self, CHeapWrapper, CStackWrapper};
    use Error;

    /// A wrapper around a BoringSSL key object.
    pub trait BoringKey: Sized {
        // evp_pkey_assign_xxx
        fn pkey_assign(&self, pkey: &mut CHeapWrapper<boringssl::EVP_PKEY>) -> Result<(), Error>;

        // evp_pkey_get_xxx
        fn pkey_get(pkey: &mut CHeapWrapper<boringssl::EVP_PKEY>) -> Result<Self, Error>;

        // xxx_parse_private_key
        fn parse_private_key(cbs: &mut CStackWrapper<boringssl::CBS>) -> Result<Self, Error>;

        // xxx_marshal_private_key
        fn marshal_private_key(&self, cbb: &mut CStackWrapper<boringssl::CBB>)
            -> Result<(), Error>;
    }

    /// Properties shared by both public and private keys of a given type.
    pub trait Key {
        /// The underlying BoringSSL object wrapper type.
        type Boring: BoringKey;

        fn get_boring(&self) -> &Self::Boring;

        fn from_boring(Self::Boring) -> Self;
    }
}

/// Marshals a public key in DER format.
///
/// `marshal_public_key_der` marshals a public key as a DER-encoded
/// SubjectPublicKeyInfo structure as defined in [RFC 5280].
///
/// [RFC 5280]: https://tools.ietf.org/html/rfc5280
#[must_use]
pub fn marshal_public_key_der<P: PublicKey>(key: &P) -> Vec<u8> {
    let mut evp_pkey = CHeapWrapper::default();
    key.get_boring().pkey_assign(&mut evp_pkey).unwrap();
    let mut cbb = CStackWrapper::cbb_new(64).unwrap();
    evp_pkey
        .evp_marshal_public_key(&mut cbb)
        .expect("failed to marshal public key");
    cbb.cbb_with_data(<[u8]>::to_vec).unwrap()
}

/// Marshals a private key in DER format.
///
/// `marshal_private_key_der` marshal a private key as a DER-encoded structure.
/// The exact structure encoded depends on the type of key:
/// - For an EC key, it is an ECPrivateKey structure as defined in [RFC 5915].
/// - For an RSA key, it is an RSAPrivateKey structure as defined in [RFC 3447].
///
/// [RFC 5915]: https://tools.ietf.org/html/rfc5915
/// [RFC 3447]: https://tools.ietf.org/html/rfc3447
#[must_use]
pub fn marshal_private_key_der<P: PrivateKey>(key: &P) -> Vec<u8> {
    let mut cbb = CStackWrapper::cbb_new(64).unwrap();
    key.get_boring()
        .marshal_private_key(&mut cbb)
        .expect("failed to marshal private key");
    cbb.cbb_with_data(<[u8]>::to_vec).unwrap()
}

/// Parses a public key in DER format.
///
/// `parse_public_key_der` parses a public key from a DER-encoded
/// SubjectPublicKeyInfo structure as defined in [RFC 5280].
///
/// # Elliptic Curve Keys
///
/// For Elliptic Curve keys (`EcPubKey`), the curve itself is validated. If the
/// curve is not known ahead of time, and any curve must be supported at
/// runtime, use the [`::public::ec::parse_public_key_der_any_curve`] function.
///
/// [RFC 5280]: https://tools.ietf.org/html/rfc5280
#[must_use]
pub fn parse_public_key_der<P: PublicKey>(bytes: &[u8]) -> Result<P, Error> {
    CStackWrapper::cbs_with_temp_buffer(bytes, |cbs| {
        let mut evp_pkey = CHeapWrapper::evp_parse_public_key(cbs)?;
        let key = P::Boring::pkey_get(&mut evp_pkey)?;
        if cbs.cbs_len() > 1 {
            return Err(Error::new("malformed DER input".to_string()));
        }
        Ok(P::from_boring(key))
    })
}

/// Parses a private key in DER format.
///
/// `parse_private_key_der` parses a private key from a DER-encoded format. The
/// exact structure expected depends on the type of key:
/// - For an EC key, it is an ECPrivateKey structure as defined in [RFC 5915].
/// - For an RSA key, it is an RSAPrivateKey structure as defined in [RFC 3447].
///
/// # Elliptic Curve Keys
///
/// For Elliptic Curve keys (`EcPrivKey`), the curve itself is validated. If the
/// curve is not known ahead of time, and any curve must be supported at
/// runtime, use the [`::public::ec::parse_private_key_der_any_curve`] function.
///
/// [RFC 5915]: https://tools.ietf.org/html/rfc5915
/// [RFC 3447]: https://tools.ietf.org/html/rfc3447
#[must_use]
pub fn parse_private_key_der<P: PrivateKey>(bytes: &[u8]) -> Result<P, Error> {
    CStackWrapper::cbs_with_temp_buffer(bytes, |cbs| {
        let key = P::Boring::parse_private_key(cbs)?;
        if cbs.cbs_len() > 1 {
            return Err(Error::new("malformed DER input".to_string()));
        }
        Ok(P::from_boring(key))
    })
}
