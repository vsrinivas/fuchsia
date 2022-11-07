#![doc = include_str!("../README.md")]

//! ## `serde` support
//!
//! When the `serde` feature of this crate is enabled, the [`EncodedPoint`]
//! type receives impls of [`serde::Serialize`] and [`serde::Deserialize`].
//!
//! Additionally, when both the `alloc` and `serde` features are enabled, the
//! serializers/deserializers will autodetect if a "human friendly" textual
//! encoding is being used, and if so encode the points as hexadecimal.

#![no_std]
#![cfg_attr(docsrs, feature(doc_cfg))]
#![doc(
    html_logo_url = "https://raw.githubusercontent.com/RustCrypto/meta/master/logo.svg",
    html_favicon_url = "https://raw.githubusercontent.com/RustCrypto/meta/master/logo.svg",
    html_root_url = "https://docs.rs/sec1/0.2.1"
)]
#![forbid(unsafe_code, clippy::unwrap_used)]
#![warn(missing_docs, rust_2018_idioms, unused_qualifications)]

#[cfg(feature = "alloc")]
extern crate alloc;
#[cfg(feature = "std")]
extern crate std;

pub mod point;

mod error;
mod parameters;
mod private_key;
mod traits;

pub use der;

pub use self::{
    error::{Error, Result},
    parameters::EcParameters,
    point::EncodedPoint,
    private_key::EcPrivateKey,
    traits::DecodeEcPrivateKey,
};

pub use generic_array::typenum::consts;

#[cfg(feature = "alloc")]
pub use crate::{private_key::document::EcPrivateKeyDocument, traits::EncodeEcPrivateKey};

#[cfg(feature = "pem")]
#[cfg_attr(docsrs, doc(cfg(feature = "pem")))]
pub use der::pem::{self, LineEnding};

#[cfg(feature = "pkcs8")]
#[cfg_attr(docsrs, doc(cfg(feature = "pkcs8")))]
pub use pkcs8;

#[cfg(feature = "pkcs8")]
use pkcs8::ObjectIdentifier;

/// Algorithm [`ObjectIdentifier`] for elliptic curve public key cryptography
/// (`id-ecPublicKey`).
///
/// <http://oid-info.com/get/1.2.840.10045.2.1>
#[cfg(feature = "pkcs8")]
#[cfg_attr(docsrs, doc(cfg(feature = "pkcs8")))]
pub const ALGORITHM_OID: ObjectIdentifier = ObjectIdentifier::new("1.2.840.10045.2.1");
