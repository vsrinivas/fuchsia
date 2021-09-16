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

/// Defines aspects of a measurement unit that are applicable regardless of the scalar type.
/// mul_scalar and div_scalar are defined to eliminate some common needs for shedding the unit type.
macro_rules! define_unit_base {
    ( $unit_type:ident, $scalar_type:ident ) => {
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

/// Defines a measurement unit with an underlying float scalar type. This allows definition of
/// associated functions that delegate to those of the underlying type.
macro_rules! define_float_unit {
    ( $unit_type:ident, $scalar_type: ident ) => {
        define_unit_base!($unit_type, $scalar_type);

        #[allow(dead_code)]
        impl $unit_type {
            pub fn min(a: Self, b: Self) -> Self {
                Self($scalar_type::min(a.0, b.0))
            }

            pub fn max(a: Self, b: Self) -> Self {
                Self($scalar_type::max(a.0, b.0))
            }
        }
    };
}

/// Defines a measurement unit, with an underlying scalar type.
macro_rules! define_unit {
    ( $unit_type:ident, f32 ) => {
        define_float_unit!($unit_type, f32);
    };
    ( $unit_type:ident, f64 ) => {
        define_float_unit!($unit_type, f64);
    };
    ( $unit_type:ident, $scalar_type:ident ) => {
        define_unit_base!($unit_type, $scalar_type);
    };
}

// Standard unit types.
define_unit!(Celsius, f64);
define_unit!(Farads, f64);
define_unit!(Hertz, f64);
define_unit!(Seconds, f64);
define_unit!(Volts, f64);
define_unit!(Watts, f64);
define_unit!(Nanoseconds, i64);
define_unit!(Microseconds, i64);
define_unit!(Milliseconds, i64);

// An unsigned integer in the range [0 - x], where x is an upper bound defined by the
// thermal_limiter crate.
define_unit!(ThermalLoad, u32);

// Normalized performance units. The normalization is chosen such that 1 NormPerf is equivalent to a
// performance scale of 1.0 with respect to the Fuchsia kernel scheduler.
define_unit!(NormPerfs, f64);

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

        impl std::iter::Sum for $unit_type {
            fn sum<I: Iterator<Item = Self>>(iter: I) -> Self {
                iter.fold(Self::default(), |a, b| a + b)
            }
        }
    };

    ( $unit_type:ident, $($more:ident),+ ) => {
        define_arithmetic!($unit_type);
        define_arithmetic!($($more),+);
    };
}
define_arithmetic!(Seconds, Nanoseconds, Celsius, NormPerfs, Watts);

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

impl From<Seconds> for Microseconds {
    fn from(seconds: Seconds) -> Self {
        Microseconds((seconds.0 * 1e6) as i64)
    }
}

impl From<Nanoseconds> for Microseconds {
    fn from(nanos: Nanoseconds) -> Self {
        Microseconds(nanos.0 / 1000)
    }
}

impl From<Seconds> for Milliseconds {
    fn from(seconds: Seconds) -> Self {
        Milliseconds((seconds.0 * 1e3) as i64)
    }
}

impl From<Nanoseconds> for Milliseconds {
    fn from(nanos: Nanoseconds) -> Self {
        Milliseconds(nanos.0 / 1_000_000)
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

impl From<fuchsia_async::Time> for Nanoseconds {
    fn from(time: fuchsia_async::Time) -> Nanoseconds {
        Nanoseconds(time.into_nanos())
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

/// Describes a processor performance state.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PState {
    pub frequency: Hertz,
    pub voltage: Volts,
}

// Representation of a CPU performance scale in fixed-point form, suitable for input to the kernel.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CpuPerformanceScale {
    pub integer_part: u32,
    pub fractional_part: u32,
}

// NormPerfs are normalized to the same scale used by the kernel scheduler. Hence, they are the
// units from which a `CpuPerformanceScale` should generally be created.
impl std::convert::TryFrom<NormPerfs> for CpuPerformanceScale {
    type Error = anyhow::Error;

    fn try_from(value: NormPerfs) -> Result<Self, Self::Error> {
        let (fraction, integer) = libm::modf(value.0);
        if integer > std::u32::MAX as f64 {
            anyhow::bail!("Integer part {} exceeds std::u32::MAX", integer);
        }
        let integer_part = integer as u32;
        let fractional_part = libm::ldexp(fraction, 32) as u32;
        Ok(CpuPerformanceScale { integer_part, fractional_part })
    }
}

// Single element of input for a zx_system_get_set_performance_info syscall.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CpuPerformanceInfo {
    pub logical_cpu_number: u32,
    pub performance_scale: CpuPerformanceScale,
}
