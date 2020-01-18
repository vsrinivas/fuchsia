// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::ops::{Add, AddAssign, Sub, SubAssign};

#[derive(PartialEq, Eq, Hash, Debug, Clone, Copy)]
pub struct DecibelMilliWatt(pub i8);

// In the past dBm values were simply added up when computing a moving average rather than first
// converting dBm to mWatt. To prevent such mistakes in the future, provide an implementation for
// adding and subtracting dBm values.
impl Add for DecibelMilliWatt {
    type Output = Self;

    fn add(self, rhs: Self) -> Self::Output {
        (FemtoWatt::from(self) + FemtoWatt::from(rhs)).into()
    }
}

impl AddAssign for DecibelMilliWatt {
    fn add_assign(&mut self, rhs: Self) {
        *self = (FemtoWatt::from(*self) + FemtoWatt::from(rhs)).into();
    }
}

impl Sub for DecibelMilliWatt {
    type Output = Self;

    fn sub(self, rhs: Self) -> Self::Output {
        (FemtoWatt::from(self) - FemtoWatt::from(rhs)).into()
    }
}

impl SubAssign for DecibelMilliWatt {
    fn sub_assign(&mut self, rhs: Self) {
        *self = (FemtoWatt::from(*self) - FemtoWatt::from(rhs)).into();
    }
}

#[derive(PartialEq, Debug, Clone, Copy)]
pub struct FemtoWatt(pub u64);

impl Add for FemtoWatt {
    type Output = Self;

    fn add(self, rhs: Self) -> Self::Output {
        Self(self.0 + rhs.0)
    }
}

impl AddAssign for FemtoWatt {
    fn add_assign(&mut self, rhs: Self) {
        *self = Self(self.0 + rhs.0);
    }
}

impl Sub for FemtoWatt {
    type Output = Self;

    fn sub(self, rhs: Self) -> Self::Output {
        Self(if self.0 > rhs.0 { self.0 - rhs.0 } else { 0 })
    }
}

impl SubAssign for FemtoWatt {
    fn sub_assign(&mut self, rhs: Self) {
        *self = Self(if self.0 > rhs.0 { self.0 - rhs.0 } else { 0 });
    }
}

impl From<FemtoWatt> for DecibelMilliWatt {
    /// Uses absolute value of femtoWatt.
    fn from(fw: FemtoWatt) -> Self {
        // Note: Negative power returns an invalid value.
        if fw.0 == 0 {
            DecibelMilliWatt(std::i8::MIN)
        } else {
            let dbm = 10.0 * ((fw.0 as f64).log10() - 12.0);
            DecibelMilliWatt(dbm.round() as i8)
        }
    }
}

/// Rust's i8 clamp requires unstable library feature 'clamp'.
fn clamp<T: PartialOrd>(v: T, lower: T, upper: T) -> T {
    if v < lower {
        lower
    } else if v > upper {
        upper
    } else {
        v
    }
}

/// Polynomial approximation of f(x)=2^x in 24:8 fixed point format minimizing the maximum relative
/// error for fractional inputs in the range [0, 1] below 2%.
fn fp_exp2(x_fp: u64) -> u64 {
    // f(x) = A + x * (B + x * (C + x * (D + x * E)))
    const A_FP: u64 = 256;
    const B_FP: u64 = 177;
    const C_FP: u64 = 61;
    const D_FP: u64 = 13;
    const E_FP: u64 = 3;

    // Note: adjustment of scaling factor needed for multiplications in fixed point format if
    // same scaling factor is used.
    let mut r_fp = D_FP + ((x_fp * E_FP) >> 8);
    r_fp = C_FP + ((x_fp * r_fp) >> 8);
    r_fp = B_FP + ((x_fp * r_fp) >> 8);
    r_fp = A_FP + ((x_fp * r_fp) >> 8);
    r_fp
}

impl From<DecibelMilliWatt> for FemtoWatt {
    /// Converts dBm to femtoWatts by approximation.
    ///
    /// FemtoWatts are approximated and within a maximum relative error of < 3% for dBm inputs in
    /// the range [-100, 48]. Inputs must lay within [-120, 48] dBm.
    ///
    /// Resulting femtoWatts are always less than 2^56.
    fn from(dbm: DecibelMilliWatt) -> Self {
        let dbm = clamp(dbm.0, -120, 48) as i16;
        // mWatts = 10^(dBm / 10)
        //        = 10^(0.1 * dBm)
        // Femtowatts = 10^12 * mWatts
        //            = 10^12 * 10^(0.1 * dBm)
        //            = 10^(0.1 * (120 + dBm))
        //
        // Convert to base 2:
        // 2^x = 10^(0.1 * (120 + dBm))
        // log(2^x) = log(10^(0.1 * (120 + dBm)))
        // xlog(2) = 0.1 * (120 + dBm) * log(10)
        // x = 0.1 * (120 + dBm) * log(10) / log(2)
        // x = C * T where T = 0.1 * log(10) / log(2)
        //                 C = 120 + dBm
        // T in 24:8 fixed point format: 0.1 * log(10)/log(2) << 8 = 85
        const T_FP: u64 = 85;
        let c = (120 + dbm) as u64;
        // x in 24:8 fixed point format.
        // Scaling factor is the product of the scaling factors of c & T_FP respectively.
        let x_fp = c * T_FP;

        // Fixed point pow-function of x:
        // 2^x = a * b where a = 2^(integer part of C * t)
        //                   b = 2^(fraction part of C * t)
        let a = 1_u64 << (x_fp >> 8_u64);
        let b_fp = fp_exp2(x_fp & 0xFF_u64);
        // Scaling factor is the product of the scaling factors of a & b respectively.
        // Convert back to regular integer representation.
        Self((a * b_fp) >> 8)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    pub fn fp_exp2_maximum_error() {
        let mut x = 0.0;
        while x < 1.0 {
            x += 0.1;

            let approx = fp_exp2((x * 256.0) as u64) as f64 / 256.0;
            let actual = 2.0_f64.powf(x);
            let error = ((approx - actual).abs() / actual);
            if error > 0.02 {
                panic!("exceeded maximum expected error of 2% for: {} (error: {})", x, error);
            }
        }
    }

    #[test]
    pub fn dbm_femtowatt_conversion() {
        for dbm in -100_i8..=48 {
            let approx_fw: FemtoWatt = DecibelMilliWatt(dbm).into();

            // Test dbm -> femtoWatt
            let actual_fw = 10_f64.powf((dbm as f64) / 10.0) * 1e+12;
            let error = ((approx_fw.0 as f64 - actual_fw).abs() / actual_fw);
            if error > 0.03 {
                panic!("exceeded maximum expected error of 3% for: {} (error: {})", dbm, error);
            }

            // Test femtoWatt -> dbm
            assert_eq!(dbm, DecibelMilliWatt::from(approx_fw).0);
        }
    }

    #[test]
    pub fn dbm_femtowatt_conversion_bounds() {
        for dbm in std::i8::MIN..=-120 {
            let actual_fw: FemtoWatt = DecibelMilliWatt(dbm).into();
            let expected_fw: FemtoWatt = DecibelMilliWatt(-120).into();
            assert_eq!(actual_fw.0, expected_fw.0);
        }

        for dbm in 48..=std::i8::MAX {
            let actual_fw: FemtoWatt = DecibelMilliWatt(dbm).into();
            let expected_fw: FemtoWatt = DecibelMilliWatt(48).into();
            assert_eq!(actual_fw.0, expected_fw.0);
        }
    }

    #[test]
    pub fn dbm_femtowatt_maximum_size() {
        for dbm in std::i8::MIN..=-std::i8::MAX {
            let actual_fw: FemtoWatt = DecibelMilliWatt(dbm).into();
            assert!(actual_fw.0 < (1_u64 << 56));
        }
    }

    #[test]
    pub fn add_sub_dbm() {
        // 0.00158489319 mWatt + 19.9526232 mWatt = 19.9542081 mWatt
        // 19.9542081 mWatt = 13.000344971 dbm
        let dbm = DecibelMilliWatt(-28) + DecibelMilliWatt(13);
        assert_eq!(dbm.0, 13);

        // 19.95262315 mWatt - 10 mWatt = 9.9526232 mWatt
        // 9.9526232 mWatt = 9.9793756227 dbm
        let dbm = DecibelMilliWatt(13) - DecibelMilliWatt(10);
        assert_eq!(dbm.0, 10);

        // 31.622776602 - 10 mWatt = 21.6227766 mWatt
        // 21.6227766 mWatt = 13.349114613 dbm
        let dbm = DecibelMilliWatt(15) - DecibelMilliWatt(10);
        assert_eq!(dbm.0, 13);

        // Avoid underflow
        assert_eq!((DecibelMilliWatt(1) - DecibelMilliWatt(2)).0, -128);
    }

    #[test]
    pub fn add_sub_fwatt() {
        let fw = FemtoWatt(10) + FemtoWatt(20);
        assert_eq!(fw.0, 30);
        let mut fw = FemtoWatt(10);
        fw += FemtoWatt(20);
        assert_eq!(fw.0, 30);

        let fw = FemtoWatt(20) - FemtoWatt(10);
        assert_eq!(fw.0, 10);
        let mut fw = FemtoWatt(20);
        fw -= FemtoWatt(10);
        assert_eq!(fw.0, 10);

        let fw = FemtoWatt(10) - FemtoWatt(20);
        assert_eq!(fw.0, 0);
        let mut fw = FemtoWatt(10);
        fw -= FemtoWatt(20);
        assert_eq!(fw.0, 0);
    }
}
