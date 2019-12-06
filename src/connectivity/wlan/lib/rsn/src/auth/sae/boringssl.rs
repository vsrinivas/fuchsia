// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Wrappers for BoringSSL primitive types needed for SAE. Mundane does not provide implementations
// for these types. All calls into BoringSSL are unsafe.

use {
    boringssl_sys::{
        BN_CTX_free, BN_CTX_new, BN_add, BN_asc2bn, BN_bin2bn, BN_bn2bin, BN_bn2dec, BN_cmp,
        BN_copy, BN_equal_consttime, BN_free, BN_is_odd, BN_is_one, BN_is_zero, BN_mod_add,
        BN_mod_exp, BN_mod_mul, BN_mod_sqrt, BN_new, BN_num_bits, BN_num_bytes, BN_one,
        BN_rand_range, BN_rshift1, BN_set_u64, BN_sub, BN_zero, EC_GROUP_free,
        EC_GROUP_get_curve_GFp, EC_GROUP_get_order, EC_GROUP_new_by_curve_name, EC_POINT_add,
        EC_POINT_free, EC_POINT_get_affine_coordinates_GFp, EC_POINT_invert,
        EC_POINT_is_at_infinity, EC_POINT_mul, EC_POINT_new, EC_POINT_set_affine_coordinates_GFp,
        ERR_get_error, ERR_reason_error_string, NID_X9_62_prime256v1, OPENSSL_free, BIGNUM, BN_CTX,
        EC_GROUP, EC_POINT,
    },
    failure::{bail, Error},
    std::{cmp::Ordering, ffi::CString, fmt, ptr::NonNull},
};

fn ptr_or_error<T>(ptr: *mut T) -> Result<NonNull<T>, Error> {
    match NonNull::new(ptr) {
        Some(non_null) => Ok(non_null),
        None => bail!("Found null pointer from BoringSSL"),
    }
}

fn one_or_error(res: std::os::raw::c_int) -> Result<(), Error> {
    match res {
        1 => Ok(()),
        _ => unsafe {
            let error_code = ERR_get_error();
            let error_reason_ptr = ERR_reason_error_string(error_code);
            if error_reason_ptr.is_null() {
                bail!("BoringSSL failed to perform an operation.");
            }
            let error_reason = std::ffi::CStr::from_ptr(error_reason_ptr).to_string_lossy();
            bail!("BoringSSL failed to perform an operation: {}", error_reason);
            // ERR_reason_error_string returns a static ptr, so we don't need to free anything.
        },
    }
}

/// An arbitrary precision integral value.
pub struct Bignum(NonNull<BIGNUM>);

impl Drop for Bignum {
    fn drop(&mut self) {
        unsafe { BN_free(self.0.as_mut()) }
    }
}

/// A context structure to speed up Bignum operations.
pub struct BignumCtx(NonNull<BN_CTX>);

impl Drop for BignumCtx {
    fn drop(&mut self) {
        unsafe { BN_CTX_free(self.0.as_mut()) }
    }
}

impl BignumCtx {
    pub fn new() -> Result<Self, Error> {
        ptr_or_error(unsafe { BN_CTX_new() }).map(Self)
    }
}

impl Bignum {
    pub fn new() -> Result<Self, Error> {
        ptr_or_error(unsafe { BN_new() }).map(Self)
    }

    /// Returns a new Bignum with value zero.
    pub fn zero() -> Result<Self, Error> {
        let result = Self::new()?;
        unsafe {
            BN_zero(result.0.as_ptr());
        }
        Ok(result)
    }

    /// Returns a new Bignum with value one.
    pub fn one() -> Result<Self, Error> {
        let result = Self::new()?;
        one_or_error(unsafe { BN_one(result.0.as_ptr()) })?;
        Ok(result)
    }

    /// Returns a new Bignum with a cryptographically random value in the range [0, max).
    pub fn rand(max: &Bignum) -> Result<Self, Error> {
        let result = Self::new()?;
        one_or_error(unsafe { BN_rand_range(result.0.as_ptr(), max.0.as_ptr()) })?;
        Ok(result)
    }

    /// Returns a new Bignum constructed from the given bytes. Bytes are given in order of
    /// decreasing significance.
    pub fn new_from_slice(bytes: &[u8]) -> Result<Self, Error> {
        if bytes.is_empty() {
            Self::new_from_u64(0)
        } else {
            ptr_or_error(unsafe {
                BN_bin2bn(&bytes[0] as *const u8, bytes.len(), std::ptr::null_mut())
            })
            .map(Self)
        }
    }

    /// Returns a new Bignum parsed from the given ascii string. The string may be given as a
    /// decimal value, or as a hex value preceded by '0x'.
    pub fn new_from_string(ascii: &str) -> Result<Self, Error> {
        let mut bignum = std::ptr::null_mut();
        let ascii = CString::new(ascii)?;
        one_or_error(unsafe { BN_asc2bn(&mut bignum as *mut *mut BIGNUM, ascii.as_ptr()) })?;
        ptr_or_error(bignum).map(Bignum)
    }

    pub fn new_from_u64(value: u64) -> Result<Self, Error> {
        let mut bignum = Self::new()?;
        one_or_error(unsafe { BN_set_u64(bignum.0.as_mut(), value) })?;
        Ok(bignum)
    }

    pub fn copy(&self) -> Result<Self, Error> {
        let mut copy = Self::new()?;
        ptr_or_error(unsafe { BN_copy(copy.0.as_mut(), self.0.as_ptr()) })?;
        Ok(copy)
    }

    /// Returns the sum of this Bignum and b.
    /// Recycles b for the output to avoid unnecessary heap allocations.
    pub fn add(&self, mut b: Self) -> Result<Self, Error> {
        one_or_error(unsafe { BN_add(b.0.as_mut(), self.0.as_ptr(), b.0.as_ptr()) })?;
        Ok(b)
    }

    /// Returns this Bignum minus b.
    /// Recycles b for the output to avoid unecessary heap allocations.
    pub fn sub(&self, mut b: Self) -> Result<Self, Error> {
        one_or_error(unsafe { BN_sub(b.0.as_mut(), self.0.as_ptr(), b.0.as_ptr()) })?;
        Ok(b)
    }

    /// Returns the result of `self + b mod m`.
    pub fn mod_add(&self, b: &Self, m: &Self, ctx: &BignumCtx) -> Result<Self, Error> {
        let mut result = Self::new()?;
        one_or_error(unsafe {
            BN_mod_add(
                result.0.as_mut(),
                self.0.as_ptr(),
                b.0.as_ptr(),
                m.0.as_ptr(),
                ctx.0.as_ptr(),
            )
        })?;
        Ok(result)
    }

    /// Retursn the result of `self * b mod m`.
    pub fn mod_mul(&self, b: &Self, m: &Self, ctx: &BignumCtx) -> Result<Self, Error> {
        let mut result = Self::new()?;
        one_or_error(unsafe {
            BN_mod_mul(
                result.0.as_mut(),
                self.0.as_ptr(),
                b.0.as_ptr(),
                m.0.as_ptr(),
                ctx.0.as_ptr(),
            )
        })?;
        Ok(result)
    }

    /// Returns the result of `self^p mod m`.
    pub fn mod_exp(&self, p: &Self, m: &Self, ctx: &BignumCtx) -> Result<Self, Error> {
        let mut result = Self::new()?;
        one_or_error(unsafe {
            BN_mod_exp(
                result.0.as_mut(),
                self.0.as_ptr(),
                p.0.as_ptr(),
                m.0.as_ptr(),
                ctx.0.as_ptr(),
            )
        })?;
        Ok(result)
    }

    /// Returns a value `ret` such that `self mod m = ret^2 mod m`. Returns an error if no such
    /// value exists.
    pub fn mod_sqrt(&self, m: &Self, ctx: &BignumCtx) -> Result<Self, Error> {
        let mut result = Self::new()?;
        ptr_or_error(unsafe {
            BN_mod_sqrt(result.0.as_mut(), self.0.as_ptr(), m.0.as_ptr(), ctx.0.as_ptr())
        })?;
        Ok(result)
    }

    /// Returns `self >> 1`, aka `self / 2`.
    pub fn rshift1(&self) -> Result<Self, Error> {
        let mut result = Self::new()?;
        one_or_error(unsafe { BN_rshift1(result.0.as_mut(), self.0.as_ptr()) })?;
        Ok(result)
    }

    pub fn is_one(&self) -> bool {
        unsafe { BN_is_one(self.0.as_ptr()) == 1 }
    }

    pub fn is_zero(&self) -> bool {
        unsafe { BN_is_zero(self.0.as_ptr()) == 1 }
    }

    pub fn is_odd(&self) -> bool {
        unsafe { BN_is_odd(self.0.as_ptr()) == 1 }
    }

    /// Returns the length in bytes of the underlying byte array.
    pub fn len(&self) -> usize {
        unsafe { BN_num_bytes(self.0.as_ptr()) as usize }
    }

    /// Returns the number of significant bits in this Bignum, excluding leading zeroes. This is
    /// *not* equal to `len() * 8`.
    pub fn bits(&self) -> usize {
        unsafe { BN_num_bits(self.0.as_ptr()) as usize }
    }

    pub fn to_vec(&self) -> Vec<u8> {
        let len = self.len();
        let mut out = vec![0; len];
        if len != 0 {
            unsafe {
                BN_bn2bin(self.0.as_ptr(), &mut out[0] as *mut u8);
            }
        }
        out
    }
}

impl PartialEq for Bignum {
    fn eq(&self, other: &Self) -> bool {
        unsafe { BN_equal_consttime(self.0.as_ptr(), other.0.as_ptr()) == 1 }
    }
}
impl Eq for Bignum {}

impl std::cmp::Ord for Bignum {
    fn cmp(&self, other: &Self) -> Ordering {
        unsafe { BN_cmp(self.0.as_ptr(), other.0.as_ptr()) }.cmp(&0)
    }
}

impl PartialOrd for Bignum {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl From<Bignum> for Vec<u8> {
    fn from(bn: Bignum) -> Self {
        bn.to_vec()
    }
}

impl fmt::Display for Bignum {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        unsafe {
            let ptr = BN_bn2dec(self.0.as_ptr());
            // BoringSSL requires that we use its special free function, so we can't use CString.
            let res = std::ffi::CStr::from_ptr(ptr).to_string_lossy().fmt(f);
            OPENSSL_free(ptr as *mut ::std::os::raw::c_void);
            res
        }
    }
}

impl fmt::Debug for Bignum {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Bignum({})", self)
    }
}

/// Supported elliptic curve groups.
pub enum EcGroupId {
    P256,
}

impl EcGroupId {
    fn nid(&self) -> u32 {
        match self {
            EcGroupId::P256 => NID_X9_62_prime256v1,
        }
    }
}

/// An elliptic curve group, i.e. an equation of the form:
/// `y^2 = x^3 + ax + b  (mod p)`.
pub struct EcGroup(NonNull<EC_GROUP>);

pub struct EcGroupParams {
    // Prime modulo
    pub p: Bignum,

    // Elliptic curve parameters: y^2 = x^3 + ax + b
    pub a: Bignum,
    pub b: Bignum,
}

impl Drop for EcGroup {
    fn drop(&mut self) {
        unsafe { EC_GROUP_free(self.0.as_mut()) }
    }
}

impl EcGroup {
    pub fn new(id: EcGroupId) -> Result<Self, Error> {
        ptr_or_error(unsafe { EC_GROUP_new_by_curve_name(id.nid() as i32) }).map(Self)
    }

    /// Returns the prime and curve parameters that constitute this group.
    pub fn get_params(&self, ctx: &BignumCtx) -> Result<EcGroupParams, Error> {
        let p = Bignum::new()?;
        let a = Bignum::new()?;
        let b = Bignum::new()?;

        one_or_error(unsafe {
            EC_GROUP_get_curve_GFp(
                self.0.as_ptr(),
                p.0.as_ptr(),
                a.0.as_ptr(),
                b.0.as_ptr(),
                ctx.0.as_ptr(),
            )
        })?;
        Ok(EcGroupParams { p, a, b })
    }

    /// Returns the order of this group, i.e. the number of integer points on the curve (mod p).
    pub fn get_order(&self, ctx: &BignumCtx) -> Result<Bignum, Error> {
        let order = Bignum::new()?;
        one_or_error(unsafe {
            EC_GROUP_get_order(self.0.as_ptr(), order.0.as_ptr(), ctx.0.as_ptr())
        })?;
        Ok(order)
    }
}

/// A single point on an elliptic curve.
pub struct EcPoint(NonNull<EC_POINT>);

impl Drop for EcPoint {
    fn drop(&mut self) {
        unsafe { EC_POINT_free(self.0.as_mut()) }
    }
}

impl EcPoint {
    pub fn new(group: &EcGroup) -> Result<Self, Error> {
        ptr_or_error(unsafe { EC_POINT_new(group.0.as_ptr()) }).map(Self)
    }

    /// Converts the given affine coordinates to a point on the given curve.
    pub fn new_from_affine_coords(
        x: Bignum,
        y: Bignum,
        group: &EcGroup,
        ctx: &BignumCtx,
    ) -> Result<Self, Error> {
        let point = Self::new(group)?;
        one_or_error(unsafe {
            EC_POINT_set_affine_coordinates_GFp(
                group.0.as_ptr(),
                point.0.as_ptr(),
                x.0.as_ptr(),
                y.0.as_ptr(),
                ctx.0.as_ptr(),
            )
        })?;
        Ok(point)
    }

    /// Converts a point on a curve to its affine coordinates.
    pub fn to_affine_coords(
        &self,
        group: &EcGroup,
        ctx: &BignumCtx,
    ) -> Result<(Bignum, Bignum), Error> {
        let x = Bignum::new()?;
        let y = Bignum::new()?;
        one_or_error(unsafe {
            EC_POINT_get_affine_coordinates_GFp(
                group.0.as_ptr(),
                self.0.as_ptr(),
                x.0.as_ptr(),
                y.0.as_ptr(),
                ctx.0.as_ptr(),
            )
        })?;
        Ok((x, y))
    }

    /// Multiplies the given point on the curve by m. This is equivalent to adding this point
    /// to itself m times.
    pub fn mul(&self, group: &EcGroup, m: &Bignum, ctx: &BignumCtx) -> Result<EcPoint, Error> {
        let result = Self::new(group)?;
        one_or_error(unsafe {
            EC_POINT_mul(
                group.0.as_ptr(),
                result.0.as_ptr(),
                std::ptr::null_mut(),
                self.0.as_ptr(),
                m.0.as_ptr(),
                ctx.0.as_ptr(),
            )
        })?;
        Ok(result)
    }

    /// Computes `self + b`. In visual terms, this means drawing a line between this point and b
    /// and finding the third point where this line intersects the curve.
    pub fn add(&self, group: &EcGroup, b: &EcPoint, ctx: &BignumCtx) -> Result<EcPoint, Error> {
        let result = Self::new(group)?;
        one_or_error(unsafe {
            EC_POINT_add(
                group.0.as_ptr(),
                result.0.as_ptr(),
                self.0.as_ptr(),
                b.0.as_ptr(),
                ctx.0.as_ptr(),
            )
        })?;
        Ok(result)
    }

    /// Inverts this point on the given curve. Mathematically this means `(x, -y)`.
    pub fn invert(self, group: &EcGroup, ctx: &BignumCtx) -> Result<EcPoint, Error> {
        one_or_error(unsafe {
            EC_POINT_invert(group.0.as_ptr(), self.0.as_ptr(), ctx.0.as_ptr())
        })?;
        Ok(self)
    }

    /// Returns true if this is the point at infinity (i.e. the zero element for point addition).
    pub fn is_point_at_infinity(&self, group: &EcGroup) -> bool {
        unsafe { EC_POINT_is_at_infinity(group.0.as_ptr(), self.0.as_ptr()) == 1 }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn bn(value: &str) -> Bignum {
        Bignum::new_from_string(value).unwrap()
    }

    #[test]
    fn bignum_lifetime() {
        // Create and drop some bignums to test that we don't crash.
        for _ in 0..10 {
            let bignum = Bignum::new().unwrap();
            std::mem::drop(bignum);
        }
    }

    #[test]
    fn bignum_new() {
        assert_eq!(Bignum::new_from_string("100").unwrap(), bn("100"));
        assert_eq!(Bignum::new_from_u64(100).unwrap(), bn("100"));
        assert_eq!(Bignum::new_from_slice(&[0xff, 0xff][..]).unwrap(), bn("65535"));
    }

    #[test]
    fn bignum_format() {
        // Bignum::from_string should support both decimal and hex formats.
        assert_eq!(format!("{}", bn("100")), "100");
        assert_eq!(format!("{}", bn("0x100")), "256");
    }

    #[test]
    fn bignum_add() {
        let bn1 = bn("1000000000000000000000");
        let bn2 = bn("1000000000001234567890");
        let sum = bn1.add(bn2).unwrap();
        assert_eq!(sum, bn("2000000000001234567890"));

        let bn1 = bn("-1000000000000000000000");
        let bn2 = bn("1000000000001234567890");
        let sum = bn1.add(bn2).unwrap();
        assert_eq!(sum, bn("1234567890"));
    }

    #[test]
    fn bignum_mod_add() {
        let mut ctx = BignumCtx::new().unwrap();
        let bn1 = bn("1000000000000000000000");
        let bn2 = bn("1000000000001234567890");
        let m = bn("2000000000000000000000");
        let value = bn1.mod_add(&bn2, &m, &mut ctx).unwrap();
        assert_eq!(value, bn("1234567890"));
    }

    #[test]
    fn bignum_mod_mul() {
        let ctx = BignumCtx::new().unwrap();
        let value = bn("4").mod_mul(&bn("5"), &bn("12"), &ctx).unwrap();
        assert_eq!(value, bn("8"));
    }

    #[test]
    fn bignum_mod_exp() {
        let ctx = BignumCtx::new().unwrap();
        let value = bn("4").mod_exp(&bn("2"), &bn("10"), &ctx).unwrap();
        assert_eq!(value, bn("6"));
    }

    #[test]
    fn bignum_mod_sqrt() {
        let ctx = BignumCtx::new().unwrap();
        let m = bn("13"); // base must be prime.
                          // https://en.wikipedia.org/wiki/Quadratic_residue#Table_of_quadratic_residues
        let quadratic_residues = [1, 3, 4, 9, 10, 12];
        for i in 1..12 {
            let i_bn = Bignum::new_from_u64(i).unwrap();
            let sqrt = i_bn.mod_sqrt(&m, &ctx);
            if quadratic_residues.contains(&i) {
                assert!(sqrt.is_ok());
                assert_eq!(sqrt.unwrap().mod_exp(&bn("2"), &m, &ctx).unwrap(), i_bn);
            } else {
                assert!(sqrt.is_err());
            }
        }
    }

    #[test]
    fn bignum_mod_sqrt_non_prime() {
        let ctx = BignumCtx::new().unwrap();
        let m = bn("100");
        // 16 is a sqrt, but 100 is not prime so BoringSSL complains.
        assert!(bn("16").mod_sqrt(&m, &ctx).is_err())
    }

    #[test]
    fn bignum_rshift1() {
        assert_eq!(bn("100").rshift1().unwrap(), bn("50"));
        assert_eq!(bn("101").rshift1().unwrap(), bn("50"));
    }

    #[test]
    fn bignum_simple_fns() {
        assert!(bn("1").is_one());
        assert!(!bn("100000").is_one());
        assert!(Bignum::one().unwrap().is_one());

        assert!(bn("0").is_zero());
        assert!(!bn("1").is_zero());
        assert!(Bignum::zero().unwrap().is_zero());

        assert!(bn("1000001").is_odd());
        assert!(!bn("1000002").is_odd());
    }

    #[test]
    fn bignum_ord() {
        let neg = bn("-100");
        let zero = bn("0");
        let pos = bn("100");

        assert!(neg < zero);
        assert!(pos > neg);
        assert_eq!(neg, neg);
        assert_eq!(zero, zero);
        assert_eq!(pos, pos);
    }

    #[test]
    fn bignum_to_vec() {
        assert_eq!(bn("100").to_vec(), vec![100]);
        assert_eq!(bn("0xff00").to_vec(), vec![0xff, 0x00]);
        assert_eq!(bn("0").to_vec(), vec![]);
    }

    // RFC 5903 section 3.1
    const P: &'static str = "0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF";
    const B: &'static str = "0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B";
    const ORDER: &'static str =
        "0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551";
    const GX: &'static str = "0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296";
    const GY: &'static str = "0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5";

    // RFC 5903 section 8.1
    const I: &'static str = "0xC88F01F510D9AC3F70A292DAA2316DE544E9AAB8AFE84049C62A9C57862D1433";
    const GIX: &'static str = "0xDAD0B65394221CF9B051E1FECA5787D098DFE637FC90B9EF945D0C3772581180";
    const GIY: &'static str = "0x5271A0461CDB8252D61F1C456FA3E59AB1F45B33ACCF5F58389E0577B8990BB3";

    #[test]
    fn ec_group_params() {
        let group = EcGroup::new(EcGroupId::P256).unwrap();
        let ctx = BignumCtx::new().unwrap();
        let params = group.get_params(&ctx).unwrap();
        let order = group.get_order(&ctx).unwrap();

        // RFC 5903 section 3.1
        assert_eq!(params.p, bn(P));
        // 'a' is specified as -3, but boringssl returns the positive value congruent to -3 mod p.
        assert_eq!(params.a, params.p.sub(bn("3")).unwrap());
        assert_eq!(params.b, bn(B));
        assert_eq!(order, bn(ORDER));
    }

    #[test]
    fn ec_point_to_coords() {
        let group = EcGroup::new(EcGroupId::P256).unwrap();
        let ctx = BignumCtx::new().unwrap();
        let point = EcPoint::new_from_affine_coords(bn(GIX), bn(GIY), &group, &ctx).unwrap();
        let (x, y) = point.to_affine_coords(&group, &ctx).unwrap();
        assert_eq!(x, bn(GIX));
        assert_eq!(y, bn(GIY));
    }

    #[test]
    fn ec_wrong_coords_to_point_err() {
        let group = EcGroup::new(EcGroupId::P256).unwrap();
        let ctx = BignumCtx::new().unwrap();
        let result =
            EcPoint::new_from_affine_coords(bn(GIX).add(bn("1")).unwrap(), bn(GIY), &group, &ctx);
        assert!(result.is_err());
        result.map_err(|e| assert!(format!("{:?}", e).contains("POINT_IS_NOT_ON_CURVE")));
    }

    #[test]
    fn ec_point_mul() {
        let group = EcGroup::new(EcGroupId::P256).unwrap();
        let ctx = BignumCtx::new().unwrap();
        let g = EcPoint::new_from_affine_coords(bn(GX), bn(GY), &group, &ctx).unwrap();
        let gi = g.mul(&group, &bn(I), &ctx).unwrap();
        let (gix, giy) = gi.to_affine_coords(&group, &ctx).unwrap();
        assert_eq!(gix, bn(GIX));
        assert_eq!(giy, bn(GIY));
    }
}
