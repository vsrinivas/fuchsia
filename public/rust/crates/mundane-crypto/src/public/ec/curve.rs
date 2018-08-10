// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::borrow::Cow;
use std::fmt::{self, Debug, Display, Formatter};
use std::os::raw::c_int;

use boringssl::{self, BoringError, CRef};
use util::Sealed;
use Error;

/// The meat of the `Curve` trait.
///
/// We put the meat of the trait - an inner `Curve` trait which actually has
/// methods on it - in a separate, private module because we don't want these
/// methods to be visible to users.
mod inner {
    use Error;

    use boringssl::{self, CRef};
    use util::Sealed;

    /// An elliptic curve.
    ///
    /// `Curve` is implemented by `P256`, `P384`, `P521`, and `DynamicCurve`.
    /// The former three curves are exposed to the user, while the latter is
    /// only ever exposed via an `impl Curve` return value. This ensures that,
    /// so long as code is generating EC keys, the curve type must be static,
    /// while still allowing for parsing a key without knowing the key's curve
    /// at compile time.
    ///
    /// Since generating a new key from a particular curve requires not just a
    /// type parameter for that curve, but an instance of that type, and since
    /// there's no way for the user to get a `DynamicCurve` instance, the only
    /// way to generate a new key on a curve is to use one of the static curve
    /// types. The static curve types all implement `Default`, so code which is
    /// generic over `C: Curve + Default` can still generate keys.
    pub trait Curve: Sized + Sealed {
        /// Returns a NID, or `None` for `DynamicCurve`.
        fn static_nid() -> Option<i32>;

        /// Gets the NID of this curve.
        fn nid(&self) -> i32;

        /// Returns the group named by `Self::static_nid()` or `None`.
        fn static_group() -> Option<CRef<'static, boringssl::EC_GROUP>> {
            Self::static_nid().map(|nid| CRef::ec_group_new_by_curve_name(nid).unwrap())
        }

        /// Gets the group named by `self.nid()`.
        fn group(&self) -> CRef<'static, boringssl::EC_GROUP> {
            CRef::ec_group_new_by_curve_name(self.nid()).unwrap()
        }

        /// Constructs an instance of this curve from an `EC_GROUP`.
        ///
        /// If this is a particular curve type (`P256`, `P384`, or `P521`),
        /// `from_group` returns `Err` if `group` is not equal to the curve's
        /// group. If this is `DynamicCurve`, then it uses `group` as the group,
        /// and always returns `Ok` for any of `P256`, `P384`, and `P521`, and
        /// `Err` for any other curve.
        fn from_group(group: CRef<boringssl::EC_GROUP>) -> Result<Self, Error>;
    }
}

/// An elliptic curve.
///
/// `Curve` is implemented by [`P256`], [`P384`], and [`P521`]. The P-224 curve
/// is considered insecure, and thus is not supported.
///
/// For implementation detail reasons, `Curve` does not require `Default`.
/// However, all of `P256`, `P384`, and `P521` implement `Default`. If code
/// wishes to be generic over curve type and still be able to call
/// `EcPrivKey::generate`, which takes a curve argument, it is sufficient to
/// require a `C: Curve + Default` bound; it will be compatible with all of the
/// curve types.
///
/// ```rust
/// # use mundane::public::ec::{Curve, EcPrivKey};
/// # use mundane::public::PrivateKey;
/// # use mundane::Error;
/// fn generate<C: Curve + Default>() -> Result<EcPrivKey<C>, Error> {
///     EcPrivKey::generate(C::default())
/// }
/// ```
pub trait Curve: Sized + Copy + Clone + Display + Debug + self::inner::Curve {
    /// Gets a dynamic representation of this curve.
    ///
    /// This is useful when you have a generic `Curve` type parameter, and want
    /// to inspect which curve it is.
    #[must_use]
    fn curve(&self) -> EllipticCurve;
}

/// Any of P-256, P384, and P-521.
///
/// `EllipticCurve` is useful when you have a generic [`Curve`] type parameter,
/// and want to inspect which curve it is using the [`Curve::curve`] method. It
/// is also returned from the `curve` method on `EcPubKey` and `EcPrivKey`,
/// which is useful for the same reason.
///
/// `EllipticCurve` does not implement `Curve`; it cannot be used as a type
/// parameter for `EcPrivKey` or `EcPubKey`. It is only meant for querying which
/// curve type is in use in a particular key.
///
/// # Examples
///
/// ```rust
/// # use mundane::public::ec::parse_private_key_der_any_curve;
/// # use mundane::Error;
/// fn curve_name_from_der(der: &[u8]) -> Result<String, Error> {
///     let key = parse_private_key_der_any_curve(der)?;
///     Ok(format!("{}", key.curve()))
/// }
/// ```
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub enum EllipticCurve {
    /// The P-256 curve.
    P256,
    /// The P-384 curve.
    P384,
    /// The P-521 curve.
    P521,
}

impl Display for EllipticCurve {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        write!(
            f,
            "{}",
            match self {
                EllipticCurve::P256 => "P-256",
                EllipticCurve::P384 => "P-384",
                EllipticCurve::P521 => "P-521",
            }
        )
    }
}

/// The P-256 curve.
#[derive(Copy, Clone, Default, Debug, Eq, PartialEq, Hash)]
pub struct P256;
/// The P-384 curve.
#[derive(Copy, Clone, Default, Debug, Eq, PartialEq, Hash)]
pub struct P384;
/// The P-521 curve.
#[derive(Copy, Clone, Default, Debug, Eq, PartialEq, Hash)]
pub struct P521;

impl Display for P256 {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        write!(f, "P-256")
    }
}
impl Display for P384 {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        write!(f, "P-384")
    }
}
impl Display for P521 {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        write!(f, "P-521")
    }
}

const NID_P256: i32 = boringssl::NID_X9_62_prime256v1 as i32;
const NID_P384: i32 = boringssl::NID_secp384r1 as i32;
const NID_P521: i32 = boringssl::NID_secp521r1 as i32;

macro_rules! impl_curve {
    ($name:ident, $str:expr, $nid:ident) => {
        impl self::inner::Curve for $name {
            fn static_nid() -> Option<i32> {
                Some($nid)
            }
            fn nid(&self) -> i32 {
                $nid
            }
            fn from_group(group: boringssl::CRef<boringssl::EC_GROUP>) -> Result<Self, ::Error> {
                let nid = group.ec_group_get_curve_name();
                if nid != $nid {
                    return Err(::Error::new(format!(
                        concat!("unexpected curve: got {}; want ", $str),
                        nid_name(nid).unwrap(),
                    )));
                }
                Ok($name)
            }
        }

        impl Sealed for $name {}
        impl Curve for $name {
            fn curve(&self) -> ::public::ec::curve::EllipticCurve {
                ::public::ec::curve::EllipticCurve::$name
            }
        }
    };
}

impl_curve!(P256, "P-256", NID_P256);
impl_curve!(P384, "P-384", NID_P384);
impl_curve!(P521, "P-521", NID_P521);

/// Any of P-256, P384, and P-521.
#[derive(Copy, Clone)]
pub struct DynamicCurve(EllipticCurve);

impl Sealed for DynamicCurve {}

impl self::inner::Curve for DynamicCurve {
    fn static_nid() -> Option<i32> {
        None
    }

    fn nid(&self) -> i32 {
        match self.0 {
            EllipticCurve::P256 => NID_P256,
            EllipticCurve::P384 => NID_P384,
            EllipticCurve::P521 => NID_P521,
        }
    }

    fn from_group(group: CRef<boringssl::EC_GROUP>) -> Result<DynamicCurve, Error> {
        let nid = group.ec_group_get_curve_name();
        match nid {
            self::NID_P256 => Ok(DynamicCurve(EllipticCurve::P256)),
            self::NID_P384 => Ok(DynamicCurve(EllipticCurve::P384)),
            self::NID_P521 => Ok(DynamicCurve(EllipticCurve::P521)),
            _ => Err(Error::new(format!(
                "unsupported curve: {}",
                nid_name(nid).unwrap()
            ))),
        }
    }
}

impl Curve for DynamicCurve {
    fn curve(&self) -> EllipticCurve {
        self.0
    }
}

impl Display for DynamicCurve {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        write!(f, "{}", self.0)
    }
}

impl Debug for DynamicCurve {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        write!(f, "{:?}", self.0)
    }
}

fn nid_name(nid: c_int) -> Result<Cow<'static, str>, BoringError> {
    Ok(boringssl::ec_curve_nid2nist(nid)?.to_string_lossy())
}
