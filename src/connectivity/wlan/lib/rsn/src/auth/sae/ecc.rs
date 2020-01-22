// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        boringssl::{self, Bignum, BignumCtx, EcGroup, EcGroupId, EcGroupParams, EcPoint},
        internal::FiniteCyclicGroup,
        internal::SaeParameters,
    },
    crate::crypto_utils::kdf_sha256,
    anyhow::{bail, Error},
    log::warn,
    num::integer::Integer,
};

/// An elliptic curve group to be used as the finite cyclic group for SAE.
pub struct Group {
    group: EcGroup,
    bn_ctx: BignumCtx,
}

impl Group {
    /// Construct a new FCG using the curve parameters specified by the given ID
    /// for underlying operations.
    pub fn new(ec_group: EcGroupId) -> Result<Self, Error> {
        Ok(Self { group: EcGroup::new(ec_group)?, bn_ctx: BignumCtx::new()? })
    }
}

// The minimum number of times we must attempt to generate a PWE to avoid timing attacks.
// IEEE 802.11-2016 12.4.4.2.2 states that this number should be high enough that the probability
// of not finding a PWE is "sufficiently small". For a given odd prime modulus, there are
// (p + 1) / 2 quadratic residues. This means that for large p, there's a ~50% chance of finding a
// residue on each PWE iteration, so the probability of exceeding our number of iters is
// (1/2)^MIN_PWE_ITER. At 50 iterations, that's about 1 in 80 quadrillion... Seems sufficient.
const MIN_PWE_ITER: u8 = 50;
const KDF_LABEL: &'static str = "SAE Hunting and Pecking";

/// Computes the left side of an elliptic curve equation, y^2 = x^3 + ax + b mod p
fn compute_y_squared(x: &Bignum, curve: &EcGroupParams, ctx: &BignumCtx) -> Result<Bignum, Error> {
    // x^3 mod p
    let y = x.mod_exp(&Bignum::new_from_u64(3)?, &curve.p, ctx)?;
    // x^3 + ax mod p
    let y = y.mod_add(&curve.a.mod_mul(&x, &curve.p, ctx)?, &curve.p, ctx)?;
    // x^3 + ax + b mod p
    y.mod_add(&curve.b, &curve.p, ctx)
}

#[derive(PartialEq, Debug)]
enum LegendreSymbol {
    QuadResidue,
    NonQuadResidue,
    ZeroCongruent,
}

/// Computes (a | p), as defined by https://en.wikipedia.org/wiki/Legendre_symbol.
fn legendre(a: &Bignum, p: &Bignum, ctx: &BignumCtx) -> Result<LegendreSymbol, Error> {
    let exp = p.sub(Bignum::one()?)?.rshift1()?;
    let res = a.mod_exp(&exp, p, ctx)?;
    if res.is_one() {
        Ok(LegendreSymbol::QuadResidue)
    } else if res.is_zero() {
        Ok(LegendreSymbol::ZeroCongruent)
    } else {
        Ok(LegendreSymbol::NonQuadResidue)
    }
}

/// Returns a random tuple of (quadratic residue, non quadratic residue) mod p.
fn generate_qr_and_qnr(p: &Bignum, ctx: &BignumCtx) -> Result<(Bignum, Bignum), Error> {
    // Randomly selected values have a roughly 50% chance of being quadratic residues or
    // non-residues, so both of these loops will always terminate quickly.
    let mut qr = Bignum::rand(p)?;
    while legendre(&qr, p, ctx)? != LegendreSymbol::QuadResidue {
        qr = Bignum::rand(p)?;
    }

    let mut qnr = Bignum::rand(p)?;
    while legendre(&qnr, p, ctx)? != LegendreSymbol::NonQuadResidue {
        qnr = Bignum::rand(p)?;
    }

    Ok((qr, qnr))
}

#[allow(unused)]
/// IEEE 802.11-2016 12.4.4.2.2
/// Uses the given quadratic residue and non-quadratic residue to determine whether the given value
/// is also a residue, without leaving potential timing attacks.
fn is_quadratic_residue_blind(
    v: &Bignum,
    p: &Bignum,
    qr: &Bignum,
    qnr: &Bignum,
    ctx: &BignumCtx,
) -> Result<bool, Error> {
    // r = (random() mod (p - 1)) + 1
    let r = Bignum::rand(&p.sub(Bignum::one()?)?)?.add(Bignum::one()?)?;
    let mut num = v.mod_mul(&r, p, ctx)?.mod_mul(&r, p, ctx)?;
    if num.is_odd() {
        let num = num.mod_mul(qr, p, ctx)?;
        Ok(legendre(&num, p, ctx)? == LegendreSymbol::QuadResidue)
    } else {
        let num = num.mod_mul(qnr, p, ctx)?;
        Ok(legendre(&num, p, ctx)? == LegendreSymbol::NonQuadResidue)
    }
}

impl FiniteCyclicGroup for Group {
    type Element = boringssl::EcPoint;

    // IEEE 802.11-2016 12.4.4.2.2
    fn generate_pwe(&self, params: &SaeParameters) -> Result<Self::Element, Error> {
        let group_params = self.group.get_params(&self.bn_ctx)?;
        let length = group_params.p.bits() as u16;
        let p_vec = group_params.p.to_vec();
        let (qr, qnr) = generate_qr_and_qnr(&group_params.p, &self.bn_ctx)?;
        // Our loop will set these two values.
        let mut x: Option<Bignum> = None;
        let mut save: Option<Vec<u8>> = None;

        let mut counter = 1;
        while counter <= MIN_PWE_ITER || x.is_none() {
            let pwd_seed = params.pwd_seed(counter);
            // IEEE 802.11-2016 9.4.2.25.3 Table 9-133 specifies SHA-256 as the hash function.
            let pwd_value = kdf_sha256(&pwd_seed[..], KDF_LABEL, &p_vec[..], length);
            // This is a candidate value for our PWE x-coord. We now determine whether or not it
            // has all of our desired properties to form a PWE.
            let pwd_value = Bignum::new_from_slice(&pwd_value[..])?;
            if pwd_value < group_params.p {
                let y_squared = compute_y_squared(&pwd_value, &group_params, &self.bn_ctx)?;
                if is_quadratic_residue_blind(&y_squared, &group_params.p, &qr, &qnr, &self.bn_ctx)?
                { // We have a valid x coord for our PWE! Save it if it's the first we've found.
                    if x.is_none() {
                        x = Some(pwd_value);
                        save = Some(pwd_seed);
                    }
                }
            }
            counter += 1;
        }

        // x and save are now guaranteed to contain values.
        let x = x.unwrap();
        let save = save.unwrap();

        // Finally compute the PWE.
        let y_squared = compute_y_squared(&x, &group_params, &self.bn_ctx)?;
        let mut y = y_squared.mod_sqrt(&group_params.p, &self.bn_ctx)?;
        // Use (p - y) if the LSB of save is not equal to the LSB of y.
        if save[save.len() - 1].is_odd() != y.is_odd() {
            y = group_params.p.copy()?.sub(y)?;
        }
        EcPoint::new_from_affine_coords(x, y, &self.group, &self.bn_ctx)
    }

    fn scalar_op(&self, scalar: &Bignum, element: &Self::Element) -> Result<Self::Element, Error> {
        element.mul(&self.group, &scalar, &self.bn_ctx)
    }

    fn elem_op(
        &self,
        element1: &Self::Element,
        element2: &Self::Element,
    ) -> Result<Self::Element, Error> {
        element1.add(&self.group, &element2, &self.bn_ctx)
    }

    fn inverse_op(&self, element: Self::Element) -> Result<Self::Element, Error> {
        element.invert(&self.group, &self.bn_ctx)
    }

    fn order(&self) -> Result<Bignum, Error> {
        self.group.get_order(&self.bn_ctx)
    }

    fn map_to_secret_value(&self, element: &Self::Element) -> Result<Option<Bignum>, Error> {
        // IEEE 802.11-2016 12.4.4.2.1 (end of section)
        if element.is_point_at_infinity(&self.group) {
            Ok(None)
        } else {
            let (x, _y) = element.to_affine_coords(&self.group, &self.bn_ctx)?;
            Ok(Some(x))
        }
    }

    // IEEE 802.11-2016 12.4.7.2.4
    fn element_to_octets(&self, element: &Self::Element) -> Result<Vec<u8>, Error> {
        let group_params = self.group.get_params(&self.bn_ctx)?;
        let length = group_params.p.len();
        let (x, y) = element.to_affine_coords(&self.group, &self.bn_ctx)?;
        let mut res = x.to_left_padded_vec(length);
        res.append(&mut y.to_left_padded_vec(length));
        Ok(res)
    }

    // IEEE 802.11-2016 12.4.7.2.5
    fn element_from_octets(&self, octets: &[u8]) -> Result<Option<Self::Element>, Error> {
        let group_params = self.group.get_params(&self.bn_ctx)?;
        let length = group_params.p.len();
        if octets.len() != length * 2 {
            warn!("element_from_octets called with wrong number of octets");
            return Ok(None);
        }
        let x = Bignum::new_from_slice(&octets[0..length])?;
        let y = Bignum::new_from_slice(&octets[length..])?;
        Ok(EcPoint::new_from_affine_coords(x, y, &self.group, &self.bn_ctx).ok())
    }

    // Default implementation for scalar_size.

    fn element_size(&self) -> Result<usize, Error> {
        let group_params = self.group.get_params(&self.bn_ctx)?;
        Ok(group_params.p.len() * 2)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use wlan_common::mac::MacAddr;

    // IEEE 802.11-2016 provides an incorrect SAE test vector.
    // IEEE 802.11-18/1104r0: New Test Vectors for SAE
    const TEST_PWD: &'static str = "mekmitasdigoatpsk4internet";
    const TEST_STA_A: MacAddr = [0x82, 0x7b, 0x91, 0x9d, 0xd4, 0xb9];
    const TEST_STA_B: MacAddr = [0x1e, 0xec, 0x49, 0xea, 0x64, 0x88];
    const TEST_PWE_X: [u8; 32] = [
        0x28, 0x21, 0x67, 0xaa, 0x6b, 0xa3, 0x80, 0xc5, 0x3a, 0x9c, 0x52, 0x30, 0xc0, 0xb5, 0x3b,
        0x1e, 0xbc, 0x45, 0x69, 0x83, 0xcc, 0x05, 0xf0, 0xf2, 0xb8, 0x69, 0xa7, 0x5f, 0x7f, 0x23,
        0x2e, 0x91,
    ];
    const TEST_PWE_Y: [u8; 32] = [
        0xcc, 0x8a, 0x54, 0x50, 0x65, 0xe3, 0xa7, 0x6c, 0x67, 0x31, 0xa0, 0xf4, 0x47, 0x1a, 0x33,
        0x84, 0x42, 0xef, 0x3c, 0xc3, 0xc9, 0x78, 0xe7, 0x87, 0xe0, 0xd5, 0xcd, 0xc9, 0x07, 0xdd,
        0x4e, 0x9c,
    ];

    fn make_group() -> Group {
        let group = boringssl::EcGroup::new(boringssl::EcGroupId::P256).unwrap();
        let bn_ctx = boringssl::BignumCtx::new().unwrap();
        Group { group, bn_ctx }
    }

    fn bn(value: u64) -> Bignum {
        Bignum::new_from_u64(value).unwrap()
    }

    #[test]
    fn generate_pwe() {
        let group = make_group();
        let params = SaeParameters {
            h: super::super::h,
            cn: super::super::cn,
            password: Vec::from(TEST_PWD),
            sta_a_mac: TEST_STA_A,
            sta_b_mac: TEST_STA_B,
        };
        let pwe = group.generate_pwe(&params).unwrap();
        let (x, y) = pwe.to_affine_coords(&group.group, &group.bn_ctx).unwrap();
        assert_eq!(x.to_vec(), TEST_PWE_X);
        assert_eq!(y.to_vec(), TEST_PWE_Y);
    }

    #[test]
    fn test_legendre() {
        // Test cases from the table in https://en.wikipedia.org/wiki/Legendre_symbol
        let ctx = BignumCtx::new().unwrap();
        assert_eq!(legendre(&bn(13), &bn(23), &ctx).unwrap(), LegendreSymbol::QuadResidue);
        assert_eq!(legendre(&bn(19), &bn(23), &ctx).unwrap(), LegendreSymbol::NonQuadResidue);
        assert_eq!(legendre(&bn(26), &bn(13), &ctx).unwrap(), LegendreSymbol::ZeroCongruent);
    }

    #[test]
    fn generate_qr_qnr() {
        // With prime 3, the only possible qr is 1 and qnr is 2.
        let ctx = BignumCtx::new().unwrap();
        let (qr, qnr) = generate_qr_and_qnr(&bn(3), &ctx).unwrap();
        assert_eq!(qr, bn(1));
        assert_eq!(qnr, bn(2));
    }

    #[test]
    fn quadratic_residue_blind() {
        // Test cases from the table in https://en.wikipedia.org/wiki/Legendre_symbol
        let qr_table = [
            false, true, false, false, true, false, true, false, false, true, true, false, false,
            false, true, true, true, true, false, true, false, true, true, true, true, true, true,
            false, false, true, false,
        ];
        let prime = bn(67);
        let ctx = BignumCtx::new().unwrap();
        let (qr, qnr) = generate_qr_and_qnr(&prime, &ctx).unwrap();
        qr_table.iter().enumerate().for_each(|(i, is_residue)| {
            assert_eq!(
                qr_table[i],
                is_quadratic_residue_blind(&bn(i as u64), &prime, &qr, &qnr, &ctx).unwrap()
            )
        });
    }

    #[test]
    fn test_element_to_octets() {
        let x = Bignum::new_from_slice(&TEST_PWE_X[..]).unwrap();
        let y = Bignum::new_from_slice(&TEST_PWE_Y[..]).unwrap();
        let group = make_group();
        let element = EcPoint::new_from_affine_coords(x, y, &group.group, &group.bn_ctx).unwrap();

        let octets = group.element_to_octets(&element).unwrap();
        let mut expected = TEST_PWE_X.to_vec();
        expected.extend_from_slice(&TEST_PWE_Y[..]);
        assert_eq!(octets, expected);
    }

    #[test]
    fn test_element_to_octets_padding() {
        let group = make_group();
        let params = group.group.get_params(&group.bn_ctx).unwrap();
        // We compute a point on the curve with a short x coordinate -- the
        // generated octets should still be 64 in length, zero padded.
        let x = bn(0xffffffff);
        let y = compute_y_squared(&x, &params, &group.bn_ctx)
            .unwrap()
            .mod_sqrt(&params.p, &group.bn_ctx)
            .unwrap();
        let element = EcPoint::new_from_affine_coords(x, y, &group.group, &group.bn_ctx).unwrap();

        let octets = group.element_to_octets(&element).unwrap();
        let mut expected_x = vec![0x00; 28];
        expected_x.extend_from_slice(&[0xff; 4]);
        assert_eq!(octets.len(), 64);
        assert_eq!(&octets[0..32], &expected_x[0..32]);
    }

    #[test]
    fn test_element_from_octets() {
        let mut octets = TEST_PWE_X.to_vec();
        octets.extend_from_slice(&TEST_PWE_Y[..]);
        let group = make_group();
        let element = group.element_from_octets(&octets).unwrap();
        assert!(element.is_some());
        let element = element.unwrap();

        let expected_x = Bignum::new_from_slice(&TEST_PWE_X[..]).unwrap();
        let expected_y = Bignum::new_from_slice(&TEST_PWE_Y[..]).unwrap();
        let (x, y) = element.to_affine_coords(&group.group, &group.bn_ctx).unwrap();

        assert_eq!(x, expected_x);
        assert_eq!(y, expected_y);
    }

    #[test]
    fn test_element_from_octets_padded() {
        let mut octets = TEST_PWE_X.to_vec();
        octets.extend_from_slice(&TEST_PWE_Y[..]);
        octets.extend_from_slice(&[0xff; 10]);
        let group = make_group();
        let element = group.element_from_octets(&octets).unwrap();
        assert!(element.is_none());
    }

    #[test]
    fn test_element_from_octets_truncated() {
        let mut octets = TEST_PWE_X.to_vec();
        octets.extend_from_slice(&TEST_PWE_Y[..]);
        octets.truncate(octets.len() - 10);
        let group = make_group();
        let element = group.element_from_octets(&octets).unwrap();
        assert!(element.is_none());
    }

    #[test]
    fn test_element_from_octets_bad_point() {
        let mut octets = TEST_PWE_X.to_vec();
        octets.extend_from_slice(&TEST_PWE_Y[..]);
        let idx = octets.len() - 1;
        octets[idx] += 1; // This is no longer the right Y value for this X.
        let group = make_group();
        let element = group.element_from_octets(&octets).unwrap();
        assert!(element.is_none());
    }
}
