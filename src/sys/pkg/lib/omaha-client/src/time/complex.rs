// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Trait Implementations for `ComplexTime`
pub mod complex_time_impls {
    use super::super::{ComplexTime, ReadableSystemTime};
    use std::fmt::Display;
    use std::ops::{Add, AddAssign, Sub, SubAssign};
    use std::time::Duration;

    /// `ComplexTime` implements `Display` to provide a human-readable, detailed, format for its
    /// values. It uses the `ReadableSystemTime` struct for its `SystemTime` component, and the
    /// `Debug` trait implementation of `Instant`, as that type's internals are not accessible, and
    /// it only implements `Debug`.
    ///
    /// # Example
    /// ```
    /// use omaha_client::time::ComplexTime;
    /// use std::time::{Duration, Instant, SystemTime};
    /// assert_eq!(
    ///     format!("{}", ComplexTime{
    ///                       wall: SystemTime::UNIX_EPOCH + Duration::from_nanos(994610096026420000),
    ///                       mono: Instant::now(),
    ///                   }),
    ///     "2001-07-08 16:34:56.026 UTC (994610096.026420000) and Instant{ tv_sec: SEC, tv_nsec: NSEC }"
    /// );
    ///```
    impl Display for ComplexTime {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            write!(f, "{} at {:?}", &ReadableSystemTime(self.wall), self.mono)
        }
    }

    impl Add<Duration> for ComplexTime {
        type Output = Self;

        fn add(self, dur: Duration) -> Self {
            Self { wall: self.wall + dur, mono: self.mono + dur }
        }
    }

    /// AddAssign implementation that relies on the above Add implementation.
    impl AddAssign<Duration> for ComplexTime {
        fn add_assign(&mut self, other: Duration) {
            *self = *self + other;
        }
    }

    impl Sub<Duration> for ComplexTime {
        type Output = Self;

        fn sub(self, dur: Duration) -> Self {
            Self { wall: self.wall - dur, mono: self.mono - dur }
        }
    }

    /// AddAssign implementation that relies on the above Add implementation.
    impl SubAssign<Duration> for ComplexTime {
        fn sub_assign(&mut self, other: Duration) {
            *self = *self - other;
        }
    }

    #[cfg(test)]
    mod tests {
        use super::super::super::PartialComplexTime;
        use super::super::system_time_conversion;
        use super::*;
        use std::time::{Duration, Instant, SystemTime};

        #[test]
        fn test_truncate_submicrosecond_walltime() {
            let time = ComplexTime { wall: SystemTime::now(), mono: Instant::now() };
            assert_eq!(
                time.truncate_submicrosecond_walltime().wall,
                system_time_conversion::micros_from_epoch_to_system_time(
                    system_time_conversion::checked_system_time_to_micros_from_epoch(time.wall)
                        .unwrap()
                )
            );
        }

        #[test]
        fn test_wall_duration_since() {
            let early = ComplexTime { wall: SystemTime::now(), mono: Instant::now() };
            let later = ComplexTime { wall: early.wall + Duration::from_secs(200), ..early };
            assert_eq!(later.wall_duration_since(early).unwrap(), Duration::from_secs(200))
        }

        #[test]
        fn test_is_after_or_eq_any() {
            let wall = SystemTime::now();
            let mono = Instant::now();
            let comp = ComplexTime { wall, mono };

            let dur = Duration::from_secs(60);
            let wall_after = wall + dur;
            let mono_after = mono + dur;
            let comp_after = comp + dur;

            let comp_wall_after_mono_not = ComplexTime::from((wall_after, mono));
            let comp_mono_after_wall_not = ComplexTime::from((wall, mono_after));

            // strictly after cases
            assert!(comp_after.is_after_or_eq_any(comp));
            assert!(comp_after.is_after_or_eq_any(PartialComplexTime::Wall(wall)));
            assert!(comp_after.is_after_or_eq_any(PartialComplexTime::Monotonic(mono)));
            assert!(comp_after.is_after_or_eq_any(PartialComplexTime::Complex(comp)));

            // reversed (note these are all negated)
            assert!(!comp.is_after_or_eq_any(comp_after));
            assert!(!comp.is_after_or_eq_any(PartialComplexTime::Wall(wall_after)));
            assert!(!comp.is_after_or_eq_any(PartialComplexTime::Monotonic(mono_after)));
            assert!(!comp.is_after_or_eq_any(PartialComplexTime::Complex(comp_after)));

            // strictly equal cases
            assert!(comp_after.is_after_or_eq_any(comp_after));
            assert!(comp_after.is_after_or_eq_any(PartialComplexTime::Wall(wall_after)));
            assert!(comp_after.is_after_or_eq_any(PartialComplexTime::Monotonic(mono_after)));
            assert!(comp_after.is_after_or_eq_any(PartialComplexTime::Complex(comp_after)));

            // wall is after, mono is not
            assert!(comp_wall_after_mono_not.is_after_or_eq_any(comp));

            // mono is after, wall is not
            assert!(comp_mono_after_wall_not.is_after_or_eq_any(comp));
        }

        #[test]
        fn test_complex_time_impl_add() {
            let earlier = ComplexTime { wall: SystemTime::now(), mono: Instant::now() };
            let dur = Duration::from_secs(60 * 60);

            let later = earlier + dur;

            let wall_duration_added = later.wall.duration_since(earlier.wall).unwrap();
            let mono_duration_added = later.mono.duration_since(earlier.mono);

            assert_eq!(wall_duration_added, dur);
            assert_eq!(mono_duration_added, dur);
        }

        #[test]
        fn test_complex_time_impl_add_assign() {
            let mut time = ComplexTime { wall: SystemTime::now(), mono: Instant::now() };
            let earlier = time;
            let dur = Duration::from_secs(60 * 60);

            time += dur;

            let wall_duration_added = time.wall.duration_since(earlier.wall).unwrap();
            let mono_duration_added = time.mono.duration_since(earlier.mono);

            assert_eq!(wall_duration_added, dur);
            assert_eq!(mono_duration_added, dur);
        }

        #[test]
        fn test_complex_time_impl_sub() {
            // If this test was executed early after boot, it's possible `Instant::now()` could be
            // less than 60*60 seconds. To make the tests more deterministic, we'll create a
            // synthetic now we'll use in tests that's at least 24 hours from the real `now()`
            // value.
            let mono = Instant::now() + Duration::from_secs(24 * 60 * 60);
            let time = ComplexTime { wall: SystemTime::now(), mono };
            let dur = Duration::from_secs(60 * 60);
            let earlier = time - dur;

            let wall_duration_subtracted = time.wall.duration_since(earlier.wall).unwrap();
            let mono_duration_subtracted = time.mono.duration_since(earlier.mono);

            assert_eq!(wall_duration_subtracted, dur);
            assert_eq!(mono_duration_subtracted, dur);
        }

        #[test]
        fn test_complex_time_impl_sub_assign() {
            // If this test was executed early after boot, it's possible `Instant::now()` could be
            // less than 60*60 seconds. To make the tests more deterministic, we'll create a
            // synthetic now we'll use in tests that's at least 24 hours from the real `now()`
            // value.
            let mono = Instant::now() + Duration::from_secs(24 * 60 * 60);
            let mut time = ComplexTime { wall: SystemTime::now(), mono };
            let before_sub = time;
            let dur = Duration::from_secs(60 * 60);

            time -= dur;

            let wall_duration_subtracted = before_sub.wall.duration_since(time.wall).unwrap();
            let mono_duration_subtracted = before_sub.mono.duration_since(time.mono);

            assert_eq!(wall_duration_subtracted, dur);
            assert_eq!(mono_duration_subtracted, dur);
        }
    }
}

/// Conversions for `ComplexTime`.
///
/// This implements `From<T> for ComplexTime` for many T.
/// This implements `From<ComplexTime> for U` for many U (which are outside this module)
pub mod complex_time_type_conversions {
    use super::super::ComplexTime;
    use std::time::{Instant, SystemTime};

    // `From<T> for ComplexTime`

    impl From<(SystemTime, Instant)> for ComplexTime {
        fn from(t: (SystemTime, Instant)) -> ComplexTime {
            ComplexTime { wall: t.0, mono: t.1 }
        }
    }

    // `From<ComplexTime> for ...`

    impl From<ComplexTime> for SystemTime {
        fn from(complex: ComplexTime) -> SystemTime {
            complex.wall
        }
    }
    impl From<ComplexTime> for Instant {
        fn from(complex: ComplexTime) -> Instant {
            complex.mono
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;

        /// Test that the `ComplexTime` `From` implementations work correctly.
        #[test]
        fn test_from_std_time_tuple_for_complex_time() {
            let system_time = SystemTime::now();
            let instant = Instant::now();
            assert_eq!(
                ComplexTime::from((system_time, instant)),
                ComplexTime { wall: system_time, mono: instant }
            );
        }

        #[test]
        fn test_from_complex_time_for_instant() {
            let time = ComplexTime { wall: SystemTime::now(), mono: Instant::now() };
            let instant_from_time: Instant = Instant::from(time);
            let time_into_instant: Instant = time.into();

            assert_eq!(instant_from_time, time_into_instant);
            assert_eq!(instant_from_time, time.mono);
            assert_eq!(time_into_instant, time.mono);
        }

        #[test]
        fn test_from_complex_time_for_system_time() {
            let time = ComplexTime { wall: SystemTime::now(), mono: Instant::now() };
            let system_from_time: SystemTime = SystemTime::from(time);
            let time_into_system: SystemTime = time.into();

            assert_eq!(system_from_time, time_into_system);
            assert_eq!(system_from_time, time.wall);
            assert_eq!(time_into_system, time.wall);
        }
    }
}

/// Trait Implementations for `PartialComplexTime`
pub mod partial_complex_time_impls {
    use super::super::{PartialComplexTime, ReadableSystemTime};
    use std::fmt::Display;
    use std::ops::{Add, AddAssign, Sub, SubAssign};
    use std::time::Duration;

    /// `PartialComplexTime` implements `Display` to provide a human-readable, detailed, format for
    /// its values. It uses the `ReadableSystemTime` struct for its `SystemTime` component, and the
    /// `Debug` trait implementation of `Instant`, as that type's internals are not accessible, and
    /// it only implements `Debug`.
    ///
    /// # Example
    /// ```
    /// use std::time::{Duration, Instant, SystemTime};
    /// assert_eq!(
    ///     format!("{}", PartialComplexTime::Complex(ComplexTime{
    ///                       wall: SystemTime::UNIX_EPOCH + Duration::from_nanos(994610096026420000),
    ///                       mono: Instant::now()
    ///                   })),
    ///     "2001-07-08 16:34:56.026 UTC (994610096.026420000) and Instant{ tv_sec: SEC, tv_nsec: NSEC }"
    /// );
    ///
    /// assert_eq!(
    ///     format!("{}", PartialComplexTime::Wall(
    ///                       SystemTime::UNIX_EPOCH + Duration::from_nanos(994610096026420000),
    ///                   )),
    ///     "2001-07-08 16:34:56.026 UTC (994610096.026420000) and Instant{ tv_sec: SEC, tv_nsec: NSEC }"
    /// );
    ///
    /// assert_eq!(
    ///     format!("{}", PartialComplexTime::Monotonic(Instant::now())),
    ///     "2001-07-08 16:34:56.026 UTC (994610096.026420000) and Instant{ tv_sec: SEC, tv_nsec: NSEC }"
    /// );
    ///```
    impl Display for PartialComplexTime {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            match self {
                Self::Wall(w) => write!(f, "{} and No Monotonic", &ReadableSystemTime(*w)),
                Self::Monotonic(m) => write!(f, "No Wall and {:?}", m),
                Self::Complex(t) => Display::fmt(t, f),
            }
        }
    }

    /// An `Add` implementation for PartialComplexTime that adds the duration to each of the time
    /// values it holds.
    ///
    /// # Panics
    ///
    /// The Add<Duration> implementations for both SystemTime and Instant, which this uses, will
    /// panic on overflow.
    impl Add<Duration> for PartialComplexTime {
        type Output = Self;

        fn add(self, dur: Duration) -> Self {
            match self {
                Self::Wall(w) => Self::Wall(w + dur),
                Self::Monotonic(m) => Self::Monotonic(m + dur),
                Self::Complex(c) => Self::Complex(c + dur),
            }
        }
    }

    /// AddAssign implementation that relies on the above Add implementation.
    impl AddAssign<Duration> for PartialComplexTime {
        fn add_assign(&mut self, other: Duration) {
            *self = *self + other;
        }
    }

    /// A `Sub` implementation for PartialComplexTime that adds the duration to each of the time
    /// values it holds.
    ///
    /// # Panics
    ///
    /// The Sub<Duration> implementations for both SystemTime and Instant, which this uses, will
    /// panic on overflow.
    impl Sub<Duration> for PartialComplexTime {
        type Output = Self;
        fn sub(self, dur: Duration) -> Self {
            match self {
                Self::Wall(w) => Self::Wall(w - dur),
                Self::Monotonic(m) => Self::Monotonic(m - dur),
                Self::Complex(c) => Self::Complex(c - dur),
            }
        }
    }

    /// SubAssign implementation that relies on the above Add implementation.
    impl SubAssign<Duration> for PartialComplexTime {
        fn sub_assign(&mut self, other: Duration) {
            *self = *self - other;
        }
    }
    #[cfg(test)]
    mod tests {
        use super::super::super::ComplexTime;
        use super::*;
        use std::time::{Instant, SystemTime};

        #[test]
        fn test_partial_complex_time_impl_add() {
            let wall = SystemTime::now();
            let mono = Instant::now();
            let comp = ComplexTime { wall, mono };

            let partial_wall = PartialComplexTime::Wall(wall);
            let partial_mono = PartialComplexTime::Monotonic(mono);
            let partial_comp = PartialComplexTime::Complex(comp);

            let dur = Duration::from_secs(60 * 60);

            let later_partial_wall = partial_wall + dur;
            let later_partial_mono = partial_mono + dur;
            let later_partial_comp = partial_comp + dur;

            match later_partial_wall {
                PartialComplexTime::Wall(w) => assert_eq!(w.duration_since(wall).unwrap(), dur),
                x => panic!("{:?} is not a PartialComplexTime::Wall", x),
            };
            match later_partial_mono {
                PartialComplexTime::Monotonic(m) => assert_eq!(m.duration_since(mono), dur),
                x => panic!("{:?} is not a PartialComplexTime::Monotonic", x),
            };
            match later_partial_comp {
                PartialComplexTime::Complex(c) => {
                    assert_eq!(c.wall.duration_since(wall).unwrap(), dur);
                    assert_eq!(c.mono.duration_since(mono), dur);
                }
                x => panic!("{:?} is not a PartialComplexTime::Complex", x),
            };
        }

        #[test]
        fn test_partial_complex_time_impl_add_assign() {
            let wall = SystemTime::now();
            let mono = Instant::now();
            let comp = ComplexTime { wall, mono };

            let mut partial_wall = PartialComplexTime::Wall(wall);
            let mut partial_mono = PartialComplexTime::Monotonic(mono);
            let mut partial_comp = PartialComplexTime::Complex(comp);

            let dur = Duration::from_secs(60 * 60);

            // perform the add-assign
            partial_wall += dur;
            partial_mono += dur;
            partial_comp += dur;

            match partial_wall {
                PartialComplexTime::Wall(w) => assert_eq!(w.duration_since(wall).unwrap(), dur),
                x => panic!("{:?} is not a PartialComplexTime::Wall", x),
            };
            match partial_mono {
                PartialComplexTime::Monotonic(m) => assert_eq!(m.duration_since(mono), dur),
                x => panic!("{:?} is not a PartialComplexTime::Monotonic", x),
            };
            match partial_comp {
                PartialComplexTime::Complex(c) => {
                    assert_eq!(c.wall.duration_since(comp.wall).unwrap(), dur);
                    assert_eq!(c.mono.duration_since(comp.mono), dur);
                }
                x => panic!("{:?} is not a PartialComplexTime::Complex", x),
            };
        }

        #[test]
        fn test_partial_complex_time_impl_sub() {
            let wall = SystemTime::now();
            // If this test was executed early after boot, it's possible `Instant::now()` could be
            // less than 60*60 seconds. To make the tests more deterministic, we'll create a
            // synthetic now we'll use in tests that's at least 24 hours from the real `now()`
            // value.
            let mono = Instant::now() + Duration::from_secs(24 * 60 * 60);
            let comp = ComplexTime { wall, mono };

            let partial_wall = PartialComplexTime::Wall(wall);
            let partial_mono = PartialComplexTime::Monotonic(mono);
            let partial_comp = PartialComplexTime::Complex(comp);

            let dur = Duration::from_secs(60 * 60);

            let earlier_partial_wall = partial_wall - dur;
            let earlier_partial_mono = partial_mono - dur;
            let earlier_partial_comp = partial_comp - dur;

            match earlier_partial_wall {
                PartialComplexTime::Wall(w) => assert_eq!(wall.duration_since(w).unwrap(), dur),
                x => panic!("{:?} is not a PartialComplexTime::Wall", x),
            };
            match earlier_partial_mono {
                PartialComplexTime::Monotonic(m) => assert_eq!(mono.duration_since(m), dur),
                x => panic!("{:?} is not a PartialComplexTime::Monotonic", x),
            };
            match earlier_partial_comp {
                PartialComplexTime::Complex(c) => {
                    assert_eq!(wall.duration_since(c.wall).unwrap(), dur);
                    assert_eq!(mono.duration_since(c.mono), dur);
                }
                x => panic!("{:?} is not a PartialComplexTime::Complex", x),
            };
        }

        #[test]
        fn test_partial_complex_time_impl_sub_assign() {
            let wall = SystemTime::now();
            // If this test was executed early after boot, it's possible `Instant::now()` could be
            // less than 60*60 seconds. To make the tests more deterministic, we'll create a
            // synthetic now we'll use in tests that's at least 24 hours from the real `now()`
            // value.
            let mono = Instant::now() + Duration::from_secs(24 * 60 * 60);
            let comp = ComplexTime { wall, mono };

            let mut partial_wall = PartialComplexTime::Wall(wall);
            let mut partial_mono = PartialComplexTime::Monotonic(mono);
            let mut partial_comp = PartialComplexTime::Complex(comp);

            let dur = Duration::from_secs(60 * 60);

            // perform the add-assign
            partial_wall -= dur;
            partial_mono -= dur;
            partial_comp -= dur;

            match partial_wall {
                PartialComplexTime::Wall(w) => assert_eq!(wall.duration_since(w).unwrap(), dur),
                x => panic!("{:?} is not a PartialComplexTime::Wall", x),
            };
            match partial_mono {
                PartialComplexTime::Monotonic(m) => assert_eq!(mono.duration_since(m), dur),
                x => panic!("{:?} is not a PartialComplexTime::Monotonic", x),
            };
            match partial_comp {
                PartialComplexTime::Complex(c) => {
                    assert_eq!(wall.duration_since(c.wall).unwrap(), dur);
                    assert_eq!(mono.duration_since(c.mono), dur);
                }
                x => panic!("{:?} is not a PartialComplexTime::Complex", x),
            };
        }
    }
}

/// Conversions for `PartialComplexTime`.
///
/// This implements `From<T> for PartialComplexTime` for many T.
/// This implements `From<PartialComplexTime> for U` for many U (which are outside this module)
pub mod partial_complex_time_type_conversions {
    use super::super::{ComplexTime, PartialComplexTime};
    use std::time::{Instant, SystemTime};

    // `From<T> for PartialComplexTime`

    impl From<ComplexTime> for PartialComplexTime {
        fn from(t: ComplexTime) -> Self {
            PartialComplexTime::Complex(t)
        }
    }

    // Provided so that fn's that take `impl Into<Option<PartialComplexTime>>` can easily take a
    // ComplexTime without spelling out the whole conversion (mostly applies to builders)
    impl From<ComplexTime> for Option<PartialComplexTime> {
        fn from(t: ComplexTime) -> Self {
            Some(PartialComplexTime::from(t))
        }
    }

    impl From<SystemTime> for PartialComplexTime {
        fn from(w: SystemTime) -> PartialComplexTime {
            PartialComplexTime::Wall(w)
        }
    }

    impl From<Instant> for PartialComplexTime {
        fn from(m: Instant) -> PartialComplexTime {
            PartialComplexTime::Monotonic(m)
        }
    }

    impl From<(SystemTime, Instant)> for PartialComplexTime {
        fn from(t: (SystemTime, Instant)) -> PartialComplexTime {
            PartialComplexTime::Complex(ComplexTime::from(t))
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use std::time::{Duration, Instant, SystemTime};

        #[test]
        fn test_from_complex_time_for_partial_complex_time() {
            let complex = ComplexTime { wall: SystemTime::now(), mono: Instant::now() };
            assert_eq!(PartialComplexTime::from(complex), PartialComplexTime::Complex(complex));
        }

        #[test]
        fn test_from_complex_time_for_option_partial_complex_time() {
            let complex = ComplexTime { wall: SystemTime::now(), mono: Instant::now() };
            assert_eq!(
                Option::<PartialComplexTime>::from(complex),
                Some(PartialComplexTime::Complex(complex))
            );
        }

        #[test]
        fn test_from_system_time_for_partial_complex_time() {
            let system_time = SystemTime::now();
            assert_eq!(
                PartialComplexTime::from(system_time),
                PartialComplexTime::Wall(system_time)
            );
        }

        #[test]
        fn test_from_instant_for_partial_complex_time() {
            let instant = Instant::now();
            assert_eq!(PartialComplexTime::from(instant), PartialComplexTime::Monotonic(instant));
        }

        #[test]
        fn test_from_std_time_tuple_for_partial_complex_time() {
            let system_time = SystemTime::now();
            let instant = Instant::now();

            assert_eq!(
                PartialComplexTime::from(system_time),
                PartialComplexTime::Wall(system_time)
            );
            assert_eq!(PartialComplexTime::from(instant), PartialComplexTime::Monotonic(instant));
            assert_eq!(
                PartialComplexTime::from((system_time, instant)),
                PartialComplexTime::Complex(ComplexTime { wall: system_time, mono: instant })
            );
        }

        // `From<PartialComplexTime> for ...`

        #[test]
        fn test_checked_to_system_time() {
            let system_time = SystemTime::now();
            let instant = Instant::now();

            assert_eq!(
                PartialComplexTime::Wall(system_time).checked_to_system_time(),
                Some(system_time)
            );
            assert_eq!(PartialComplexTime::Monotonic(instant).checked_to_system_time(), None);
            assert_eq!(
                PartialComplexTime::Complex((system_time, instant).into()).checked_to_system_time(),
                Some(system_time)
            );
        }

        #[test]
        fn test_checked_to_micros_from_partial_complex_time() {
            let system_time = SystemTime::UNIX_EPOCH + Duration::from_micros(123456789);
            let instant = Instant::now();

            assert_eq!(
                123456789,
                PartialComplexTime::Wall(system_time).checked_to_micros_since_epoch().unwrap()
            );
            assert_eq!(
                123456789,
                PartialComplexTime::Complex(ComplexTime::from((system_time, instant)))
                    .checked_to_micros_since_epoch()
                    .unwrap()
            );
            assert_eq!(
                None,
                PartialComplexTime::Monotonic(instant).checked_to_micros_since_epoch()
            );
        }

        #[test]
        fn test_checked_to_micros_from_partial_complex_time_before_epoch() {
            let system_time = SystemTime::UNIX_EPOCH - Duration::from_micros(123456789);
            let instant = Instant::now();

            assert_eq!(
                -123456789,
                PartialComplexTime::Wall(system_time).checked_to_micros_since_epoch().unwrap()
            );
            assert_eq!(
                -123456789,
                PartialComplexTime::Complex(ComplexTime::from((system_time, instant)))
                    .checked_to_micros_since_epoch()
                    .unwrap()
            );
            assert_eq!(
                None,
                PartialComplexTime::Monotonic(instant).checked_to_micros_since_epoch()
            );
        }

        #[test]
        fn test_checked_to_micros_from_partial_complex_time_overflow_is_none() {
            let system_time = SystemTime::UNIX_EPOCH + 2 * Duration::from_micros(u64::MAX);
            assert_eq!(None, PartialComplexTime::Wall(system_time).checked_to_micros_since_epoch());
        }

        #[test]
        fn test_checked_to_micros_from_partial_complex_time_negative_overflow_is_none() {
            let system_time = SystemTime::UNIX_EPOCH - 2 * Duration::from_micros(u64::MAX);
            assert_eq!(None, PartialComplexTime::Wall(system_time).checked_to_micros_since_epoch());
        }

        #[test]
        fn test_complete_with() {
            let system_time = SystemTime::UNIX_EPOCH - Duration::from_micros(100);
            let instant = Instant::now();
            let complex = ComplexTime::from((system_time, instant));

            let wall = PartialComplexTime::Wall(system_time);
            let mono = PartialComplexTime::Monotonic(instant);
            let comp = PartialComplexTime::Complex(complex);

            let other = complex + Duration::from_micros(500);

            assert_eq!(wall.complete_with(other), ComplexTime::from((system_time, other.mono)));
            assert_eq!(mono.complete_with(other), ComplexTime::from((other.wall, instant)));
            assert_eq!(comp.complete_with(other), complex);
        }

        #[test]
        fn test_destructure() {
            let system_time = SystemTime::now();
            let instant = Instant::now();
            let complex = ComplexTime::from((system_time, instant));

            let wall = PartialComplexTime::Wall(system_time);
            let mono = PartialComplexTime::Monotonic(instant);
            let comp = PartialComplexTime::Complex(complex);

            assert_eq!(wall.destructure(), (Some(system_time), None));
            assert_eq!(mono.destructure(), (None, Some(instant)));
            assert_eq!(comp.destructure(), (Some(system_time), Some(instant)));
        }
    }
}

/// Module to ease the conversion betwee SystemTime and i64 microseconds from the from UNIX Epoch.
pub mod system_time_conversion {
    use std::convert::TryFrom;
    use std::time::{Duration, SystemTime};

    /// Convert a SystemTime into microseconds from the unix epoch, returning None on overflow.
    /// Valid over roughly +/- 30,000 years from 1970-01-01 UTC.
    pub fn checked_system_time_to_micros_from_epoch(time: SystemTime) -> Option<i64> {
        match time.duration_since(SystemTime::UNIX_EPOCH) {
            Ok(duration_since_epoch) => {
                // Safely convert to i64 microseconds or return None.
                let micros: u128 = duration_since_epoch.as_micros();
                i64::try_from(micros).ok()
            }
            Err(e) => {
                // Safely convert to i64 microseconds (negative), or return None.
                let micros: u128 = e.duration().as_micros();
                i64::try_from(micros).ok().and_then(i64::checked_neg)
            }
        }
    }

    /// Convert micro seconds from the unix epoch to SystemTime.
    pub fn micros_from_epoch_to_system_time(micros: i64) -> SystemTime {
        // Duration is always unsigned, so negative values need to be handled separately from
        // positive values
        if micros > 0 {
            let duration = Duration::from_micros(micros as u64);
            SystemTime::UNIX_EPOCH + duration
        } else {
            let duration = Duration::from_micros((micros as u64).wrapping_neg());
            SystemTime::UNIX_EPOCH - duration
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;

        #[test]
        fn test_system_time_to_micros() {
            let system_time = SystemTime::UNIX_EPOCH + Duration::from_micros(123456789);
            assert_eq!(checked_system_time_to_micros_from_epoch(system_time).unwrap(), 123456789)
        }

        #[test]
        fn test_system_time_to_micros_negative() {
            let system_time = SystemTime::UNIX_EPOCH - Duration::from_micros(123456789);
            assert_eq!(checked_system_time_to_micros_from_epoch(system_time).unwrap(), -123456789)
        }

        #[test]
        fn test_system_time_to_micros_overflow_is_none() {
            let system_time = SystemTime::UNIX_EPOCH + 2 * Duration::from_micros(u64::MAX);
            assert_eq!(checked_system_time_to_micros_from_epoch(system_time), None);
        }

        #[test]
        fn test_system_time_to_micros_negative_overflow_is_none() {
            let system_time = SystemTime::UNIX_EPOCH - 2 * Duration::from_micros(u64::MAX);
            assert_eq!(checked_system_time_to_micros_from_epoch(system_time), None);
        }

        #[test]
        fn test_system_time_from_micros() {
            let system_time = SystemTime::UNIX_EPOCH + Duration::from_micros(123456789);
            assert_eq!(micros_from_epoch_to_system_time(123456789), system_time);
        }

        #[test]
        fn test_system_time_from_micros_negative() {
            let system_time = SystemTime::UNIX_EPOCH - Duration::from_micros(123456789);
            assert_eq!(micros_from_epoch_to_system_time(-123456789), system_time);
        }

        #[test]
        fn test_system_time_from_micros_positive_max() {
            let system_time = SystemTime::UNIX_EPOCH + Duration::from_micros(i64::MAX as u64);
            assert_eq!(micros_from_epoch_to_system_time(i64::MAX), system_time);
        }

        #[test]
        fn test_system_time_from_micros_negative_min() {
            let system_time = SystemTime::UNIX_EPOCH - Duration::from_micros((i64::MAX as u64) + 1);
            assert_eq!(micros_from_epoch_to_system_time(i64::MIN), system_time);
        }
    }
}
