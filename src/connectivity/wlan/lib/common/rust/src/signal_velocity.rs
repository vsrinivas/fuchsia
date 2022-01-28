// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::energy::*,
    anyhow::{format_err, Error},
    std::convert::TryFrom,
};

/// Calculates the rate of change across a vector of dBm measurements by determining
/// the slope of the line of best fit using least squares regression. Return is technically
/// dBm(i8)/t where t is the unit of time used in the vector. Returns error if integer overflows.
///
/// Note: This is the linear velocity (not the logarithmic velocity), since 1dBm/t is
/// depends on initial dBm, but it is a useful abstraction for monitoring real-world signal changes.
///
/// Intended to be used for RSSI Values, ranging from -128 to -1.
pub fn calculate_dbm_linear_velocity(
    historical_dbm: Vec<DecibelMilliWatt>,
) -> Result<DecibelMilliWatt, Error> {
    let n = i32::try_from(historical_dbm.len()).expect("overflow");
    if n < 2 {
        return Err(format_err!("At least two data points required to calculate velocity"));
    }
    // Using i32 for the calculations, to allow more room for preventing overflows
    let mut sum_x: i32 = 0;
    let mut sum_y: i32 = 0;
    let mut sum_xy: i32 = 0;
    let mut sum_x2: i32 = 0;

    // Least squares regression summations, returning an error if there are any overflows
    for (i, y) in historical_dbm.iter().enumerate() {
        if y.0 >= 0 {
            return Err(format_err!(
                "Function is intended for RSSI values, which should be -128 < n < 0"
            ));
        }
        let x = i32::try_from(i).map_err(|_| format_err!("failed to convert index to i32"))?;
        sum_x = sum_x.checked_add(x).ok_or_else(|| format_err!("overflow of X summation"))?;
        sum_y =
            sum_y.checked_add(y.0 as i32).ok_or_else(|| format_err!("overflow of Y summation"))?;
        sum_xy = sum_xy
            .checked_add(x.checked_mul(y.0 as i32).ok_or_else(|| format_err!("overflow of X * Y"))?)
            .ok_or_else(|| format_err!("overflow of XY summation"))?;
        sum_x2 = sum_x2
            .checked_add(x.checked_mul(x).ok_or_else(|| format_err!("overflow of X**2"))?)
            .ok_or_else(|| format_err!("overflow of X2 summation"))?;
    }

    // Calculate velocity from summations, returning an error if there are any overflows. Note that
    // in practice, the try_from should never fail, since the input values are bound from 0 to -128.
    let velocity: i8 = i8::try_from(
        (n.checked_mul(sum_xy).ok_or_else(|| format_err!("overflow in n * sum_xy"))?
            - sum_x.checked_mul(sum_y).ok_or_else(|| format_err!("overflow in sum_x * sum_y"))?)
            / (n.checked_mul(sum_x2).ok_or_else(|| format_err!("overflow in n * sum_x2"))?
                - sum_x.checked_mul(sum_x).ok_or_else(|| format_err!("overflow in sum_x**2"))?),
    )
    .map_err(|_| format_err!("failed to convert final velocity to i8"))?;
    Ok(DecibelMilliWatt(velocity))
}

#[cfg(test)]
mod test {
    use super::*;

    /// Vector argument must have length >=2, and be negative.
    #[test]
    fn test_insufficient_args() {
        assert!(calculate_dbm_linear_velocity(vec![]).is_err());
        assert!(calculate_dbm_linear_velocity(vec![DecibelMilliWatt(-60)]).is_err());
        assert!(calculate_dbm_linear_velocity(vec![DecibelMilliWatt(-60), DecibelMilliWatt(20)])
            .is_err());
    }

    #[test]
    fn test_calculate_negative_velocity() {
        assert_eq!(
            calculate_dbm_linear_velocity(vec![DecibelMilliWatt(-60), DecibelMilliWatt(-75)])
                .expect("failed to calculate"),
            DecibelMilliWatt(-15)
        );
        assert_eq!(
            calculate_dbm_linear_velocity(vec![
                DecibelMilliWatt(-40),
                DecibelMilliWatt(-50),
                DecibelMilliWatt(-58),
                DecibelMilliWatt(-64)
            ])
            .expect("failed to calculate"),
            DecibelMilliWatt(-8)
        );
    }

    #[test]
    fn test_calculate_positive_velocity() {
        assert_eq!(
            calculate_dbm_linear_velocity(vec![DecibelMilliWatt(-48), DecibelMilliWatt(-45)])
                .expect("failed to calculate"),
            DecibelMilliWatt(3)
        );
        assert_eq!(
            calculate_dbm_linear_velocity(vec![
                DecibelMilliWatt(-70),
                DecibelMilliWatt(-55),
                DecibelMilliWatt(-45),
                DecibelMilliWatt(-30)
            ])
            .expect("failed to calculate"),
            DecibelMilliWatt(13)
        );
    }

    #[test]
    fn test_calculate_constant_zero_velocity() {
        assert_eq!(
            calculate_dbm_linear_velocity(vec![
                DecibelMilliWatt(-25),
                DecibelMilliWatt(-25),
                DecibelMilliWatt(-25),
                DecibelMilliWatt(-25),
                DecibelMilliWatt(-25),
                DecibelMilliWatt(-25)
            ])
            .expect("failed to calculate"),
            DecibelMilliWatt(0)
        );
    }

    #[test]
    fn test_calculate_oscillating_zero_velocity() {
        assert_eq!(
            calculate_dbm_linear_velocity(vec![
                DecibelMilliWatt(-35),
                DecibelMilliWatt(-45),
                DecibelMilliWatt(-35),
                DecibelMilliWatt(-25),
                DecibelMilliWatt(-35),
                DecibelMilliWatt(-45),
                DecibelMilliWatt(-35),
            ])
            .expect("failed to calculate"),
            DecibelMilliWatt(0)
        );
    }

    #[test]
    fn test_calculate_min_max_velocity() {
        assert_eq!(
            calculate_dbm_linear_velocity(vec![DecibelMilliWatt(-1), DecibelMilliWatt(-128)])
                .expect("failed to calculate"),
            DecibelMilliWatt(-127)
        );
        assert_eq!(
            calculate_dbm_linear_velocity(vec![DecibelMilliWatt(-128), DecibelMilliWatt(-1)])
                .expect("failed to calculate"),
            DecibelMilliWatt(127)
        );
    }
}
