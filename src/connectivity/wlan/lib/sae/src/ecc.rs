// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        boringssl::{self, Bignum, BignumCtx, EcGroup, EcGroupId, EcGroupParams, EcPoint},
        internal::FiniteCyclicGroup,
        internal::SaeParameters,
        PweMethod,
    },
    anyhow::{bail, Error},
    ieee80211::MacAddr,
    log::warn,
    num::{integer::Integer, ToPrimitive},
};

/// An elliptic curve group to be used as the finite cyclic group for SAE.
pub struct Group {
    id: EcGroupId,
    group: EcGroup,
    bn_ctx: BignumCtx,
}

impl Group {
    /// Construct a new FCG using the curve parameters specified by the given ID
    /// for underlying operations.
    pub fn new(ec_group: EcGroupId) -> Result<Self, Error> {
        Ok(Self { id: ec_group.clone(), group: EcGroup::new(ec_group)?, bn_ctx: BignumCtx::new()? })
    }
}

/// Concatenates two MAC addresses in canonical order (largest one first)
fn concat_mac_addrs(sta_a_mac: &MacAddr, sta_b_mac: &MacAddr) -> Vec<u8> {
    let mut result: Vec<u8> = Vec::with_capacity(sta_a_mac.len() + sta_b_mac.len());
    match sta_a_mac.cmp(sta_b_mac) {
        std::cmp::Ordering::Less => {
            result.extend_from_slice(sta_b_mac);
            result.extend_from_slice(sta_a_mac);
        }
        _ => {
            result.extend_from_slice(sta_a_mac);
            result.extend_from_slice(sta_b_mac);
        }
    };

    result
}

// The minimum number of times we must attempt to generate a PWE to avoid timing attacks.
// IEEE Std 802.11-2016 12.4.4.2.2 states that this number should be high enough that the
// probability of not finding a PWE is "sufficiently small". For a given odd prime modulus, there
// are (p + 1) / 2 quadratic residues. This means that for large p, there's a ~50% chance of
// finding a residue on each PWE iteration, so the probability of exceeding our number of iters is
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

impl Group {
    // IEEE Std 802.11-2020 12.4.4.2.2
    fn generate_pwe_loop(&self, params: &SaeParameters) -> Result<EcPoint, Error> {
        if params.password_id.is_some() {
            // IEEE Std 802.11-2020 12.4.4.3.2
            bail!("Password ID cannot be used with looping PWE generation");
        }

        let group_params = self.group.get_params(&self.bn_ctx)?;
        let length = group_params.p.bits();
        let p_vec = group_params.p.to_be_vec(group_params.p.len());
        let (qr, qnr) = generate_qr_and_qnr(&group_params.p, &self.bn_ctx)?;
        // Our loop will set these two values.
        let mut x: Option<Bignum> = None;
        let mut save: Option<Vec<u8>> = None;

        let mut counter = 1;
        while counter <= MIN_PWE_ITER || x.is_none() {
            let pwd_seed = {
                let salt = concat_mac_addrs(&params.sta_a_mac, &params.sta_b_mac);
                let mut ikm = params.password.clone();
                ikm.push(counter as u8);
                params.hmac.hkdf_extract(&salt[..], &ikm[..])
            };
            let pwd_value =
                params.hmac.kdf_hash_length(&pwd_seed[..], KDF_LABEL, &p_vec[..], length);
            // This is a candidate value for our PWE x-coord. We now determine whether or not it
            // has all of our desired properties to form a PWE.
            let pwd_value = Bignum::new_from_slice(&pwd_value[..])?;
            if pwd_value < group_params.p {
                let y_squared = compute_y_squared(&pwd_value, &group_params, &self.bn_ctx)?;
                if is_quadratic_residue_blind(&y_squared, &group_params.p, &qr, &qnr, &self.bn_ctx)?
                {
                    // We have a valid x coord for our PWE! Save it if it's the first we've found.
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

    /// draft-irtf-cfrg-hash-to-curve-11 F.2, 8.2, 8.3, 8.4
    /// Returns the parameters to the SSWU map-to-curve function.  z is the curve-specific parameter,
    /// given in the IRTC memo; c1 and c2 are derived parameters.
    fn generate_sswu_z_c1_c2(&self) -> Result<(Bignum, Bignum, Bignum), Error> {
        let group_params = self.group.get_params(&self.bn_ctx)?;
        let z = match self.id {
            EcGroupId::P256 => Bignum::new_from_u64(10)?.set_negative(),
            EcGroupId::P384 => Bignum::new_from_u64(12)?.set_negative(),
            EcGroupId::P521 => Bignum::new_from_u64(4)?.set_negative(),
            _ => bail!("No Z value for SAE self id: {}", self.id.to_u16().unwrap_or(0xFFFFu16)),
        };
        let c1 = group_params
            .b
            .mod_mul(
                &group_params.a.mod_inverse(&group_params.p, &self.bn_ctx)?,
                &group_params.p,
                &self.bn_ctx,
            )?
            .set_negative();
        let c2 = z.mod_inverse(&group_params.p, &self.bn_ctx)?.set_negative();
        Ok((z, c1, c2))
    }

    /// draft-irtf-cfrg-hash-to-curve-11 F.2
    /// Implements the Simplified Shallue-van de Woestijne-Ulas map-to-curve function, for any curve
    /// in the form:
    ///     y^2 = g(x) = x^3 + A * x + B
    /// over GF(p) where A != 0 and B != 0.  We use the straight-line version from draft 11 since it
    /// more straightforward to implement here.
    ///
    /// Parameters are defined as following:
    ///     u: pwd-value specified in IEEE Std 802.11-2020 12.4.4.3.3
    ///     z, c1, c2: Constants specific to a given elliptic curve. Use generate_sswu_z_c1_c2
    ///     qr, qnr: Random values generated by generate_qr_and_qnr
    /// z, c1, c2, qr and qnr are passed here to avoid extra work when calling calculate_sswu
    /// multiple times.
    // TODO(fxbug.dev/91949): implement this using only constant-time Bignum operations.
    fn calculate_sswu(
        &self,
        u: &Bignum,
        z: &Bignum,
        c1: &Bignum,
        c2: &Bignum,
        qr: &Bignum,
        qnr: &Bignum,
    ) -> Result<EcPoint, Error> {
        let group_params = self.group.get_params(&self.bn_ctx)?;
        let p = &group_params.p;
        let p_2 = p.sub(Bignum::new_from_u64(2)?)?;

        let tv1 = z.mod_mul(&u.mod_square(p, &self.bn_ctx)?, p, &self.bn_ctx)?; // tv1 = z * u^2
        let tv2 = tv1.mod_square(p, &self.bn_ctx)?; // tv2 = tv1^2
        let x1 = tv1.mod_add(&tv2, p, &self.bn_ctx)?; // x1 = tv + tv2
        let x1 = x1.mod_exp(&p_2, p, &self.bn_ctx)?; // x1 = inv0(x1)
        let e1 = x1.is_zero(); // e1 = x1 == 0
        let x1 = x1.mod_add(&Bignum::one()?, p, &self.bn_ctx)?; // x1 = x1 + 1
        let x1 = if e1 { c2 } else { &x1 }; // x1 = CMOV(x1, c2, e1)
        let x1 = x1.mod_mul(c1, p, &self.bn_ctx)?; // x1 = x1 * c1
        let gx1 = x1.mod_square(&group_params.p, &self.bn_ctx)?; // gx1 = x1^2
        let gx1 = gx1.mod_add(&group_params.a, p, &self.bn_ctx)?; // gx1 = gx1 + A
        let gx1 = gx1.mod_mul(&x1, p, &self.bn_ctx)?; // gx1 = gx1 * x1
        let gx1 = gx1.mod_add(&group_params.b, p, &self.bn_ctx)?; // gx1 = gx1 + B
        let x2 = tv1.mod_mul(&x1, p, &self.bn_ctx)?; // x2 = tv1 * x1
        let tv2 = tv1.mod_mul(&tv2, p, &self.bn_ctx)?; // tv2 = tv1 * tv2
        let gx2 = gx1.mod_mul(&tv2, p, &self.bn_ctx)?; // gx2 = gx1 * tv2
        let e2 = is_quadratic_residue_blind(&gx1, p, qr, qnr, &self.bn_ctx)?; // e2 = is_square(gx1)
        let x = if e2 { x1 } else { x2 }; // x = CMOV(x2, x1, e2)
        let y2 = if e2 { gx1 } else { gx2 }; // y2 = CMOV(gx2, gx1, e2)
        let y = y2.mod_sqrt(p, &self.bn_ctx)?; // y = sqrt(y2)
        let e3 = (u.is_odd() == y.is_odd()); // e3 = sgn0(u) == sgn0(y) (is_odd() in GF(p))
        let negative_y = p.sub(y.copy()?)?.mod_nonnegative(p, &self.bn_ctx)?;
        let y = if e3 { y } else { negative_y }; // y = CMOV(-y, y, e3)

        EcPoint::new_from_affine_coords(x, y, &self.group, &self.bn_ctx)
    }

    /// IEEE Std 802.11-2020 12.4.4.2.3
    /// Returns the secret PT used to generate the PWE.
    fn generate_pt(&self, params: &SaeParameters) -> Result<EcPoint, Error> {
        let group_params = self.group.get_params(&self.bn_ctx)?;
        let p = &group_params.p;
        let len = p.len() + (p.len() / 2);

        let mut password_with_id = params.password.clone();
        match &params.password_id {
            Some(password_id) => password_with_id.extend_from_slice(&password_id),
            _ => (),
        };
        let pwd_seed = params.hmac.hkdf_extract(&params.ssid, &password_with_id);

        let (z, c1, c2) = self.generate_sswu_z_c1_c2()?;
        let (qr, qnr) = generate_qr_and_qnr(p, &self.bn_ctx)?;

        let pwd_value_1 = params.hmac.hkdf_expand(&pwd_seed, "SAE Hash to Element u1 P1", len);
        let u1 = Bignum::new_from_slice(&pwd_value_1)?;
        let u1 = u1.mod_nonnegative(p, &self.bn_ctx)?;
        let p1 = self.calculate_sswu(&u1, &z, &c1, &c2, &qr, &qnr)?;

        let pwd_value_2 = params.hmac.hkdf_expand(&pwd_seed, "SAE Hash to Element u2 P2", len);
        let u2 = Bignum::new_from_slice(&pwd_value_2)?;
        let u2 = u2.mod_nonnegative(p, &self.bn_ctx)?;
        let p2 = self.calculate_sswu(&u2, &z, &c1, &c1, &qr, &qnr)?;

        self.elem_op(&p1, &p2)
    }

    // IEEE Std 802.11-2020 12.4.5.2
    fn generate_pwe_direct(&self, params: &SaeParameters) -> Result<EcPoint, Error> {
        // The secret element pt is used for each connection on this SSID and password, and could
        // potentially be cached and reused.  However, in actual use we generate a new Group
        // instance for each call to generate_pwe(), so we do not do any caching here.
        let pt = self.generate_pt(params)?;

        // Now generate the PWE from the PT and MAC addresses.
        let salt = vec![0u8; params.hmac.bits() / 8];
        let ikm = concat_mac_addrs(&params.sta_a_mac, &params.sta_b_mac);
        let val = Bignum::new_from_slice(&params.hmac.hkdf_extract(&salt, &ikm))?;
        let val = val
            .mod_nonnegative(
                &self.group.get_order(&self.bn_ctx)?.sub(Bignum::one()?)?,
                &self.bn_ctx,
            )?
            .add(Bignum::one()?)?;

        self.scalar_op(&val, &pt)
    }
}

impl FiniteCyclicGroup for Group {
    type Element = boringssl::EcPoint;

    fn group_id(&self) -> u16 {
        self.id.to_u16().unwrap()
    }

    fn generate_pwe(&self, params: &SaeParameters) -> Result<Self::Element, Error> {
        match params.pwe_method {
            PweMethod::Loop => self.generate_pwe_loop(params),
            PweMethod::Direct => self.generate_pwe_direct(params),
        }
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

    fn map_to_secret_value(&self, element: &Self::Element) -> Result<Option<Vec<u8>>, Error> {
        // IEEE Std 802.11-2016 12.4.4.2.1 (end of section)
        if element.is_point_at_infinity(&self.group) {
            Ok(None)
        } else {
            let group_params = self.group.get_params(&self.bn_ctx)?;
            let (x, _y) = element.to_affine_coords(&self.group, &self.bn_ctx)?;
            Ok(Some(x.to_be_vec(group_params.p.len())))
        }
    }

    // IEEE Std 802.11-2016 12.4.7.2.4
    fn element_to_octets(&self, element: &Self::Element) -> Result<Vec<u8>, Error> {
        let group_params = self.group.get_params(&self.bn_ctx)?;
        let length = group_params.p.len();
        let (x, y) = element.to_affine_coords(&self.group, &self.bn_ctx)?;
        let mut res = x.to_be_vec(length);
        res.append(&mut y.to_be_vec(length));
        Ok(res)
    }

    // IEEE Std 802.11-2016 12.4.7.2.5
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
    use {
        super::*,
        crate::hmac_utils::HmacUtilsImpl,
        ieee80211::{MacAddr, Ssid},
        mundane::hash::Sha256,
        std::convert::TryFrom,
    };

    #[derive(Debug)]
    struct SswuTestVector {
        curve: EcGroupId,
        u: &'static str,
        q_x: &'static str,
        q_y: &'static str,
    }

    // IEEE Std 802.11-2020 J.10
    // SAE test vectors (common)
    const TEST_GROUP: EcGroupId = EcGroupId::P256;
    const TEST_SSID: &'static str = "byteme";
    const TEST_PWD: &'static str = "mekmitasdigoat";
    const TEST_PWD_ID: &'static str = "psk4internet";

    // IEEE Std 802.11-2020 J.10
    // Test vectors for looping PWE generation
    const TEST_LOOP_STA_A: MacAddr = [0x4d, 0x3f, 0x2f, 0xff, 0xe3, 0x87];
    const TEST_LOOP_STA_B: MacAddr = [0xa5, 0xd8, 0xaa, 0x95, 0x8e, 0x3c];
    const TEST_LOOP_PWE_X: &'static str =
        "da6eb7b06a1ac5624974f90afdd6a8e9d5722634cf987c34defc91a9874e5658";
    const TEST_LOOP_PWE_Y: &'static str =
        "f4fefd130bd5be08fe68af3e4a290272ec065fd3671f3c25bf8ec419ddc9b822";

    // IEEE Std 802.11-2020 J.10
    // Test vectors for direct PWE generation
    const TEST_DIRECT_STA_A: MacAddr = [0x00, 0x09, 0x5b, 0x66, 0xec, 0x1e];
    const TEST_DIRECT_STA_B: MacAddr = [0x00, 0x0b, 0x6b, 0xd9, 0x02, 0x46];
    const TEST_DIRECT_Z: &'static str =
        "ffffffff00000001000000000000000000000000fffffffffffffffffffffff5";
    const TEST_DIRECT_C1: &'static str =
        "73976747e368dbf83bf93f1c7cdd823ecc5f023b441be5a76944bebf629b756e";
    const TEST_DIRECT_C2: &'static str =
        "e666666580000000e666666666666666666666674ccccccccccccccccccccccc";
    const TEST_DIRECT_U1: &'static str =
        "dc941bc3c6a2b4948b6c61d55590ecb1f0c51c4b1bebaff677e593698d5a53c6";
    const TEST_DIRECT_U2: &'static str =
        "1b8375a518bc21396ad6a65e5597e0bf80d793b6d66e2534a6e7dfe3ee22616f";
    const TEST_DIRECT_P1_X: &'static str =
        "a07c260764a13445ff8cd97c5acc644e7119bde51bad42583eed6f4109639e6b";
    const TEST_DIRECT_P1_Y: &'static str =
        "3bdc8df0d32337936c74df604933a454142251c53c576c0351b28deaf9428d7e";
    const TEST_DIRECT_P2_X: &'static str =
        "72cd2a967a837fea5051f0133db46227775ba09f7b6dfb99ae7a8ef22c7d34a0";
    const TEST_DIRECT_P2_Y: &'static str =
        "864390d797d352b368d311af515bde116fe54459fec867ee18a8a1619ca3ff59";
    const TEST_DIRECT_PT_X: &'static str =
        "b6e38c98750c684b5d17c3d8c9a4100b39931279187ca6cced5f37ef46ddfa97";
    const TEST_DIRECT_PT_Y: &'static str =
        "5687e972e50f73e3898861e7edad21bea7d5f622df88243bb804920ae8e647fa";
    const TEST_DIRECT_PWE_X: &'static str =
        "c93049b9e64000f848201649e999f2b5c22dea69b5632c9df4d633b8aa1f6c1e";
    const TEST_DIRECT_PWE_Y: &'static str =
        "73634e94b53d82e7383a8d258199d9dc1a5ee8269d060382ccbf33e614ff59a0";

    // draft-irtf-cfrg-hash-to-curve-11 J.1.1, J.2.1, J.3.1
    // Test vectors for SSWU on various curves.
    const TEST_SSWU_CURVES: &'static [SswuTestVector] = &[
        SswuTestVector {
            curve: EcGroupId::P256,
            u: "ad5342c66a6dd0ff080df1da0ea1c04b96e0330dd89406465eeba11582515009",
            q_x: "ab640a12220d3ff283510ff3f4b1953d09fad35795140b1c5d64f313967934d5",
            q_y: "dccb558863804a881d4fff3455716c836cef230e5209594ddd33d85c565b19b1",
        },
        SswuTestVector {
            curve: EcGroupId::P256,
            u: "8c0f1d43204bd6f6ea70ae8013070a1518b43873bcd850aafa0a9e220e2eea5a",
            q_x: "51cce63c50d972a6e51c61334f0f4875c9ac1cd2d3238412f84e31da7d980ef5",
            q_y: "b45d1a36d00ad90e5ec7840a60a4de411917fbe7c82c3949a6e699e5a1b66aac",
        },
        SswuTestVector {
            curve: EcGroupId::P256,
            u: "afe47f2ea2b10465cc26ac403194dfb68b7f5ee865cda61e9f3e07a537220af1",
            q_x: "5219ad0ddef3cc49b714145e91b2f7de6ce0a7a7dc7406c7726c7e373c58cb48",
            q_y: "7950144e52d30acbec7b624c203b1996c99617d0b61c2442354301b191d93ecf",
        },
        SswuTestVector {
            curve: EcGroupId::P256,
            u: "379a27833b0bfe6f7bdca08e1e83c760bf9a338ab335542704edcd69ce9e46e0",
            q_x: "019b7cb4efcfeaf39f738fe638e31d375ad6837f58a852d032ff60c69ee3875f",
            q_y: "589a62d2b22357fed5449bc38065b760095ebe6aeac84b01156ee4252715446e",
        },
        SswuTestVector {
            curve: EcGroupId::P384,
            u: "425c1d0b099ffa6c15069b08299e6e21a204e08c2a0627f5afc24215d19e45bc\
                47d70da5972ff77e33f176b5e18e8485",
            q_x: "4589af7986491d42b7ee23726c57abeade65c7b8eba12d07fbce48065a01a78c\
                  4b018c739034d9fabc2c4ef6176c7c40",
            q_y: "5b2985027c29802bf2afdb8a3c95fa655ad3189a2118209bd285d420268bf71e\
                  610c9533e3f4f438ba4b64f66f6fbed9",
        },
        SswuTestVector {
            curve: EcGroupId::P384,
            u: "cbefdd543ed48b5a9bbbd460f559d23b388aa72157279ba02069231881eb2a94\
                7d887a5b1e0a6173bc92a5700f679a14",
            q_x: "cbd6c34a12a266b447b444b303d577cd5d61e3c0af19d4676ababb470bb79574\
                  1ebf167caa9f0910a4fcc899134596d7",
            q_y: "63df08d5d3aa8090cbb94222b34aad35e1b11414d3aef8f1a26205c81b4d15bb\
                  be4faf25d77924705bf09afd8812d2f0",
        },
        SswuTestVector {
            curve: EcGroupId::P521,
            u: "01e5f09974e5724f25286763f00ce76238c7a6e03dc396600350ee2c4135fb17\
                dc555be99a4a4bae0fd303d4f66d984ed7b6a3ba386093752a855d26d559d69e\
                7e9e",
            q_x: "00b70ae99b6339fffac19cb9bfde2098b84f75e50ac1e80d6acb954e4534af5f\
                  0e9c4a5b8a9c10317b8e6421574bae2b133b4f2b8c6ce4b3063da1d91d34fa2b\
                  3a3c",
            q_y: "007f368d98a4ddbf381fb354de40e44b19e43bb11a1278759f4ea7b485e1b6db\
                  33e750507c071250e3e443c1aaed61f2c28541bb54b1b456843eda1eb15ec2a9\
                  b36e",
        },
        SswuTestVector {
            curve: EcGroupId::P521,
            u: "00ae593b42ca2ef93ac488e9e09a5fe5a2f6fb330d18913734ff602f2a761fca\
                af5f596e790bcc572c9140ec03f6cccc38f767f1c1975a0b4d70b392d95a0c72\
                78aa",
            q_x: "01143d0e9cddcdacd6a9aafe1bcf8d218c0afc45d4451239e821f5d2a56df92b\
                  e942660b532b2aa59a9c635ae6b30e803c45a6ac871432452e685d661cd41cf6\
                  7214",
            q_y: "00ff75515df265e996d702a5380defffab1a6d2bc232234c7bcffa433cd8aa79\
                  1fbc8dcf667f08818bffa739ae25773b32073213cae9a0f2a917a0b1301a242d\
                  da0c",
        },
    ];

    // Generated by a run of generate_qr_and_qnr()
    const TEST_DIRECT_QR: &'static str =
        "22d92ad59d5e2681443903612413e0da06650cf2ec4278fd1f4308418a2041b0";
    const TEST_DIRECT_QNR: &'static str =
        "07204a4749c26085a78cea57031524c21575d114d71f0e2ca7d742d7d99fdbe6";

    fn make_group() -> Group {
        let group = boringssl::EcGroup::new(TEST_GROUP).unwrap();
        let bn_ctx = boringssl::BignumCtx::new().unwrap();
        Group { id: TEST_GROUP, group, bn_ctx }
    }

    fn bn(value: u64) -> Bignum {
        Bignum::new_from_u64(value).unwrap()
    }

    #[test]
    fn get_group_id() {
        let group = make_group();
        assert_eq!(group.group_id(), TEST_GROUP.to_u16().unwrap());
    }

    #[test]
    fn generate_pwe_loop() {
        let group = make_group();
        let group_params = group.group.get_params(&group.bn_ctx).unwrap();
        let params = SaeParameters {
            hmac: Box::new(HmacUtilsImpl::<Sha256>::new()),
            pwe_method: PweMethod::Loop,
            ssid: Ssid::try_from(TEST_SSID).unwrap(),
            password: Vec::from(TEST_PWD),
            password_id: None,
            sta_a_mac: TEST_LOOP_STA_A,
            sta_b_mac: TEST_LOOP_STA_B,
        };
        let pwe = group.generate_pwe(&params).unwrap();
        let (x, y) = pwe.to_affine_coords(&group.group, &group.bn_ctx).unwrap();
        assert_eq!(x.to_be_vec(group_params.p.len()), hex::decode(TEST_LOOP_PWE_X).unwrap());
        assert_eq!(y.to_be_vec(group_params.p.len()), hex::decode(TEST_LOOP_PWE_Y).unwrap());

        // The PWE should not change depending on the order of mac addresses.
        let params =
            SaeParameters { sta_a_mac: TEST_LOOP_STA_B, sta_b_mac: TEST_LOOP_STA_A, ..params };
        let pwe = group.generate_pwe(&params).unwrap();
        let (x, y) = pwe.to_affine_coords(&group.group, &group.bn_ctx).unwrap();
        assert_eq!(x.to_be_vec(group_params.p.len()), hex::decode(TEST_LOOP_PWE_X).unwrap());
        assert_eq!(y.to_be_vec(group_params.p.len()), hex::decode(TEST_LOOP_PWE_Y).unwrap());
    }

    #[test]
    fn generate_pwe_direct() {
        let group = make_group();
        let group_params = group.group.get_params(&group.bn_ctx).unwrap();
        let params = SaeParameters {
            hmac: Box::new(HmacUtilsImpl::<Sha256>::new()),
            pwe_method: PweMethod::Direct,
            ssid: Ssid::try_from(TEST_SSID).unwrap(),
            password: Vec::from(TEST_PWD),
            password_id: Some(Vec::from(TEST_PWD_ID)),
            sta_a_mac: TEST_DIRECT_STA_A,
            sta_b_mac: TEST_DIRECT_STA_B,
        };
        let pwe = group.generate_pwe(&params).unwrap();
        let (x, y) = pwe.to_affine_coords(&group.group, &group.bn_ctx).unwrap();
        assert_eq!(x.to_be_vec(group_params.p.len()), hex::decode(TEST_DIRECT_PWE_X).unwrap());
        assert_eq!(y.to_be_vec(group_params.p.len()), hex::decode(TEST_DIRECT_PWE_Y).unwrap());

        // The PWE should not change depending on the order of mac addresses.
        let params =
            SaeParameters { sta_a_mac: TEST_DIRECT_STA_B, sta_b_mac: TEST_DIRECT_STA_A, ..params };
        let pwe = group.generate_pwe(&params).unwrap();
        let (x, y) = pwe.to_affine_coords(&group.group, &group.bn_ctx).unwrap();
        assert_eq!(x.to_be_vec(group_params.p.len()), hex::decode(TEST_DIRECT_PWE_X).unwrap());
        assert_eq!(y.to_be_vec(group_params.p.len()), hex::decode(TEST_DIRECT_PWE_Y).unwrap());
    }

    #[test]
    fn generate_pwe_loop_no_pwd_id() {
        let group = make_group();
        let group_params = group.group.get_params(&group.bn_ctx).unwrap();
        let params = SaeParameters {
            hmac: Box::new(HmacUtilsImpl::<Sha256>::new()),
            pwe_method: PweMethod::Loop,
            ssid: Ssid::try_from(TEST_SSID).unwrap(),
            password: Vec::from(TEST_PWD),
            password_id: Some(Vec::from(TEST_PWD_ID)),
            sta_a_mac: TEST_LOOP_STA_A,
            sta_b_mac: TEST_LOOP_STA_B,
        };
        let pwe = group.generate_pwe(&params);
        // IEEE Std 802.11-2020: password ID cannot be used with PWE generation by looping
        assert!(pwe.is_err());
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
    fn calculate_sswu() {
        // Fixed test vectors and intermediates from IEEE Std 802.11-2020
        {
            let group = make_group();
            let group_params = group.group.get_params(&group.bn_ctx).unwrap();
            let p = &group_params.p;

            let z = Bignum::new_from_slice(&hex::decode(TEST_DIRECT_Z).unwrap()).unwrap();
            let c1 = Bignum::new_from_slice(&hex::decode(TEST_DIRECT_C1).unwrap()).unwrap();
            let c2 = Bignum::new_from_slice(&hex::decode(TEST_DIRECT_C2).unwrap()).unwrap();
            let qr = Bignum::new_from_slice(&hex::decode(TEST_DIRECT_QR).unwrap()).unwrap();
            let qnr = Bignum::new_from_slice(&hex::decode(TEST_DIRECT_QNR).unwrap()).unwrap();

            let u1 = Bignum::new_from_slice(&hex::decode(TEST_DIRECT_U1).unwrap()).unwrap();
            let p1 = group.calculate_sswu(&u1, &z, &c1, &c2, &qr, &qnr).unwrap();
            let (p1_x, p1_y) = p1.to_affine_coords(&group.group, &group.bn_ctx).unwrap();
            assert_eq!(hex::encode(p1_x.to_be_vec(p.len())), TEST_DIRECT_P1_X);
            assert_eq!(hex::encode(p1_y.to_be_vec(p.len())), TEST_DIRECT_P1_Y);

            let u2 = Bignum::new_from_slice(&hex::decode(TEST_DIRECT_U2).unwrap()).unwrap();
            let p2 = group.calculate_sswu(&u2, &z, &c1, &c2, &qr, &qnr).unwrap();
            let (p2_x, p2_y) = p2.to_affine_coords(&group.group, &group.bn_ctx).unwrap();
            assert_eq!(hex::encode(p2_x.to_be_vec(p.len())), TEST_DIRECT_P2_X);
            assert_eq!(hex::encode(p2_y.to_be_vec(p.len())), TEST_DIRECT_P2_Y);
        }

        // SSWU test vectors from draft-irtf-cfrg-hash-to-curve-11
        for vector in TEST_SSWU_CURVES {
            let group = boringssl::EcGroup::new(vector.curve).unwrap();
            let bn_ctx = boringssl::BignumCtx::new().unwrap();
            let group = Group { id: vector.curve, group, bn_ctx };
            let group_params = group.group.get_params(&group.bn_ctx).unwrap();
            let p = &group_params.p;

            let (z, c1, c2) = group.generate_sswu_z_c1_c2().unwrap();
            let (qr, qnr) = generate_qr_and_qnr(p, &group.bn_ctx).unwrap();
            let u = Bignum::new_from_slice(&hex::decode(vector.u).unwrap()).unwrap();
            let (q_x, q_y) = group
                .calculate_sswu(&u, &z, &c1, &c2, &qr, &qnr)
                .unwrap()
                .to_affine_coords(&group.group, &group.bn_ctx)
                .unwrap();
            assert_eq!(
                hex::encode(q_x.to_be_vec(p.len())),
                vector.q_x,
                "test vector: {:?}",
                vector
            );
            assert_eq!(
                hex::encode(q_y.to_be_vec(p.len())),
                vector.q_y,
                "test vector: {:?}",
                vector
            );
        }
    }

    #[test]
    fn generate_pt() {
        let group = make_group();
        let params = SaeParameters {
            hmac: Box::new(HmacUtilsImpl::<Sha256>::new()),
            pwe_method: PweMethod::Direct,
            ssid: Ssid::try_from(TEST_SSID).unwrap(),
            password: Vec::from(TEST_PWD),
            password_id: Some(Vec::from(TEST_PWD_ID)),
            sta_a_mac: TEST_DIRECT_STA_A,
            sta_b_mac: TEST_DIRECT_STA_B,
        };

        let pt = group.generate_pt(&params).unwrap();
        let (pt_x, pt_y) = pt.to_affine_coords(&group.group, &group.bn_ctx).unwrap();
        assert_eq!(hex::encode(pt_x.to_be_vec(0)), TEST_DIRECT_PT_X);
        assert_eq!(hex::encode(pt_y.to_be_vec(0)), TEST_DIRECT_PT_Y);
    }

    #[test]
    fn test_element_to_octets() {
        let x = Bignum::new_from_slice(&hex::decode(TEST_LOOP_PWE_X).unwrap()).unwrap();
        let y = Bignum::new_from_slice(&hex::decode(TEST_LOOP_PWE_Y).unwrap()).unwrap();
        let group = make_group();
        let element = EcPoint::new_from_affine_coords(x, y, &group.group, &group.bn_ctx).unwrap();

        let octets = group.element_to_octets(&element).unwrap();
        let mut expected = hex::decode(TEST_LOOP_PWE_X).unwrap();
        expected.extend_from_slice(&hex::decode(TEST_LOOP_PWE_Y).unwrap());
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
        let mut octets = hex::decode(TEST_LOOP_PWE_X).unwrap();
        octets.extend_from_slice(&hex::decode(TEST_LOOP_PWE_Y).unwrap());
        let group = make_group();
        let element = group.element_from_octets(&octets).unwrap();
        assert!(element.is_some());
        let element = element.unwrap();

        let expected_x = Bignum::new_from_slice(&hex::decode(TEST_LOOP_PWE_X).unwrap()).unwrap();
        let expected_y = Bignum::new_from_slice(&hex::decode(TEST_LOOP_PWE_Y).unwrap()).unwrap();
        let (x, y) = element.to_affine_coords(&group.group, &group.bn_ctx).unwrap();

        assert_eq!(x, expected_x);
        assert_eq!(y, expected_y);
    }

    #[test]
    fn test_element_from_octets_padded() {
        let mut octets = hex::decode(TEST_LOOP_PWE_X).unwrap();
        octets.extend_from_slice(&hex::decode(TEST_LOOP_PWE_Y).unwrap());
        octets.extend_from_slice(&[0xff; 10]);
        let group = make_group();
        let element = group.element_from_octets(&octets).unwrap();
        assert!(element.is_none());
    }

    #[test]
    fn test_element_from_octets_truncated() {
        let mut octets = hex::decode(TEST_LOOP_PWE_X).unwrap();
        octets.extend_from_slice(&hex::decode(TEST_LOOP_PWE_Y).unwrap());
        octets.truncate(octets.len() - 10);
        let group = make_group();
        let element = group.element_from_octets(&octets).unwrap();
        assert!(element.is_none());
    }

    #[test]
    fn test_element_from_octets_bad_point() {
        let mut octets = hex::decode(TEST_LOOP_PWE_X).unwrap();
        octets.extend_from_slice(&hex::decode(TEST_LOOP_PWE_Y).unwrap());
        let idx = octets.len() - 1;
        octets[idx] += 1; // This is no longer the right Y value for this X.
        let group = make_group();
        let element = group.element_from_octets(&octets).unwrap();
        assert!(element.is_none());
    }
}
