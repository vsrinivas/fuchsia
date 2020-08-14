// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Below are definitions of newtypes corresponding to measurement units, including several that are
/// passed across node boundaries in Messages.
///
/// Functionality has been added opportunistically so far and is not at all complete. A fully
/// developed measurement unit library would probably require const generics, so the compiler can
/// interpret exponents applied to different unit types.
use std::ops;

/// Defines a new measurement unit, with an underlying scalar type. mul_scalar and div_scalar are
/// defined to eliminate some common needs for shedding the unit type.
macro_rules! define_unit {
    ( $unit_type:ident, $scalar_type: ident ) => {
        #[derive(Clone, Copy, Debug, Default, PartialEq, PartialOrd)]
        pub struct $unit_type(pub $scalar_type);

        #[allow(dead_code)]
        impl $unit_type {
            pub fn mul_scalar(&self, other: $scalar_type) -> Self {
                Self(self.0 * other)
            }
            pub fn div_scalar(&self, other: $scalar_type) -> Self {
                Self(self.0 / other)
            }
        }
    };
}

// Unit definitions
define_unit!(Celsius, f64);
define_unit!(Farads, f64);
define_unit!(Hertz, f64);
define_unit!(Seconds, f64);
define_unit!(Volts, f64);
define_unit!(Watts, f64);
define_unit!(Nanoseconds, i64);

// An unsigned integer in the range [0 - x], where x is an upper bound defined by the
// thermal_limiter crate.
define_unit!(ThermalLoad, u32);

// Addition and subtraction is implemented for types for which it is useful. Some, but not all,
// other unit types could reasonably support these operations.
macro_rules! define_arithmetic {
    ( $unit_type:ident ) => {
        impl ops::Add for $unit_type {
            type Output = Self;
            fn add(self, other: Self) -> Self::Output {
                Self(self.0 + other.0)
            }
        }

        impl ops::AddAssign for $unit_type {
            fn add_assign(&mut self, other: Self) {
                self.0 += other.0;
            }
        }

        impl ops::Sub for $unit_type {
            type Output = Self;
            fn sub(self, other: Self) -> Self::Output {
                Self(self.0 - other.0)
            }
        }
    };

    ( $unit_type:ident, $($more:ident),+) => {
        define_arithmetic!($unit_type);
        define_arithmetic!($($more),+);
    };
}
define_arithmetic!(Seconds, Nanoseconds, Celsius, Watts);

impl From<Nanoseconds> for Seconds {
    fn from(nanos: Nanoseconds) -> Self {
        Seconds(nanos.0 as f64 / 1e9)
    }
}

impl From<Seconds> for Nanoseconds {
    fn from(seconds: Seconds) -> Self {
        Nanoseconds((seconds.0 * 1e9) as i64)
    }
}

impl From<Seconds> for fuchsia_zircon::Duration {
    fn from(seconds: Seconds) -> fuchsia_zircon::Duration {
        fuchsia_zircon::Duration::from_nanos(Nanoseconds::from(seconds).0)
    }
}

impl From<Seconds> for fuchsia_async::Time {
    fn from(seconds: Seconds) -> fuchsia_async::Time {
        fuchsia_async::Time::from_nanos(Nanoseconds::from(seconds).0)
    }
}

// Define multiplication and division involving Seconds and Hertz.
impl ops::Mul<Hertz> for Seconds {
    type Output = f64;
    fn mul(self, rhs: Hertz) -> Self::Output {
        self.0 * rhs.0
    }
}

impl ops::Mul<Seconds> for Hertz {
    type Output = f64;
    fn mul(self, rhs: Seconds) -> Self::Output {
        self.0 * rhs.0
    }
}

impl ops::Div<Seconds> for f64 {
    type Output = Hertz;
    fn div(self, rhs: Seconds) -> Self::Output {
        Hertz(self / rhs.0)
    }
}

impl ops::Div<Hertz> for f64 {
    type Output = Seconds;
    fn div(self, rhs: Hertz) -> Self::Output {
        Seconds(self / rhs.0)
    }
}
