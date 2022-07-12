// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ot::BadSystemTime;
use crate::prelude_internal::*;
use std::fmt::{Debug, Display, Formatter};
use std::ops::{Add, AddAssign};
use std::time::{Duration, SystemTime};

/// Type representing a Thread timestamp.
#[derive(Default, Clone, Copy, Ord, PartialOrd, PartialEq, Eq)]
#[repr(transparent)]
pub struct Timestamp(pub u64);

const MICROSECONDS_PER_SECOND: u64 = 1000000;
const FRACTIONS_PER_SECOND: u64 = 32768;
const UTC_BIT_MASK: u64 = 1;
const SUBSEC_MASK: u64 = 0xFFFE;
const MAX_SECONDS: u64 = 0xFFFFFFFFFFFF;

/// Takes a value from 0 to 1000000 and scales it to a value from 0 to 32768, rounded to the
/// nearest value.
const fn micros_to_fractional_second(mut micros: u64) -> u64 {
    micros += MICROSECONDS_PER_SECOND / (FRACTIONS_PER_SECOND * 2);
    (micros * FRACTIONS_PER_SECOND / MICROSECONDS_PER_SECOND) & 0xffff
}

/// Takes a value from 0 to 32768 and scales it to a value from 0 to 1000000.
const fn fractional_second_to_micros(fraction_of_second: u64) -> u64 {
    fraction_of_second * MICROSECONDS_PER_SECOND / FRACTIONS_PER_SECOND
}

impl Timestamp {
    /// Timestamp for the start of time.
    pub const EPOCH: Timestamp = Timestamp(0);

    /// Returns the timestamp representing this instant.
    pub fn now() -> Timestamp {
        Self::try_from_system_time(SystemTime::now()).unwrap()
    }

    /// Returns true if this timestamp is UTC, false otherwise.
    pub const fn is_utc(&self) -> bool {
        (self.0 & UTC_BIT_MASK) == UTC_BIT_MASK
    }

    /// Sets or clears the UTC bit.
    pub fn set_utc(&mut self, utc: bool) {
        self.0 &= !UTC_BIT_MASK;
        if utc {
            self.0 |= UTC_BIT_MASK;
        }
    }

    /// Returns this timestamp with the UTC bit changed as indicated.
    pub fn with_utc(mut self, utc: bool) -> Self {
        self.set_utc(utc);
        self
    }

    /// Tries to convert a [`std::time::SystemTime`] to a [`Timestamp`].
    ///
    /// Will fail if the given SystemTime cannot be represented as a Timestamp. This
    /// can happen if the `SystemTime` represents a time that is before the epoch or is too
    /// far in the future.
    pub fn try_from_system_time(system_time: SystemTime) -> Result<Timestamp, BadSystemTime> {
        system_time
            .duration_since(SystemTime::UNIX_EPOCH)
            .map_err(|_| BadSystemTime)
            .and_then(Timestamp::try_from_duration_since_epoch)
    }

    /// Tries to convert a [`std::time::Duration`] to a [`Timestamp`].
    ///
    /// Will fail if duration is negative or too large.
    pub fn try_from_duration_since_epoch(duration: Duration) -> Result<Timestamp, BadSystemTime> {
        let seconds = duration.as_secs();
        if seconds > MAX_SECONDS {
            return Err(BadSystemTime);
        }

        let seconds_shift_16 = seconds << 16;
        let micros: u64 = (duration - Duration::from_secs(seconds)).as_micros().try_into().unwrap();
        let fraction_of_second = micros_to_fractional_second(micros);

        Ok(Timestamp(seconds_shift_16 + (fraction_of_second << 1)))
    }

    /// Returns the timestamp as the number of seconds since the epoch.
    pub const fn as_secs(&self) -> u64 {
        self.0 >> 16
    }

    /// Returns the subsecond fraction of the timestamp, measured in 1/32768ths of a second.
    pub const fn subsec_fraction(&self) -> u64 {
        (self.0 & SUBSEC_MASK) >> 1
    }

    /// Returns the timestamp as the number of microseconds since the epoch.
    pub const fn as_micros(&self) -> u64 {
        self.as_secs() * MICROSECONDS_PER_SECOND
            + fractional_second_to_micros(self.subsec_fraction())
    }

    /// Converts this Timestamp into a [`std::time::SystemTime`];
    pub fn to_system_time(&self) -> SystemTime {
        SystemTime::UNIX_EPOCH + Duration::from_micros(self.as_micros())
    }

    /// Returns this timestamp as a duration since the UNIX epoch (`1970-01-01T00:00:00UTC`)
    pub fn to_duration_since_epoch(&self) -> Duration {
        self.to_system_time().duration_since(SystemTime::UNIX_EPOCH).unwrap()
    }

    /// Returns the timestamp as big-endian bytes.
    pub const fn to_be_bytes(&self) -> [u8; 8] {
        self.0.to_be_bytes()
    }

    /// Returns the timestamp as little-endian bytes.
    pub const fn to_le_bytes(&self) -> [u8; 8] {
        self.0.to_le_bytes()
    }

    /// Returns the timestamp as an instance of `chrono::naive::NaiveDateTime`.
    pub fn to_naive_date_time(&self) -> chrono::naive::NaiveDateTime {
        let duration = self.to_duration_since_epoch();
        chrono::naive::NaiveDateTime::from_timestamp(
            duration.as_secs().try_into().unwrap(),
            duration.subsec_nanos().try_into().unwrap(),
        )
    }
}

impl TryFrom<SystemTime> for Timestamp {
    type Error = BadSystemTime;

    fn try_from(value: SystemTime) -> std::result::Result<Self, Self::Error> {
        Timestamp::try_from_system_time(value)
    }
}

impl From<Timestamp> for SystemTime {
    fn from(ts: Timestamp) -> Self {
        ts.to_system_time()
    }
}

impl From<Timestamp> for u64 {
    fn from(ts: Timestamp) -> Self {
        ts.0
    }
}

impl From<u64> for Timestamp {
    fn from(ts: u64) -> Self {
        Timestamp(ts)
    }
}

impl Debug for Timestamp {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        let utc = if self.is_utc() { "UTC" } else { "" };
        write!(f, "{:12X} ({:?}{})", self.0, self.to_naive_date_time(), utc)
    }
}

impl Display for Timestamp {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        let utc = if self.is_utc() { " UTC" } else { "" };
        write!(f, "{}{}", self.to_naive_date_time(), utc)
    }
}

impl AddAssign<Duration> for Timestamp {
    fn add_assign(&mut self, duration: Duration) {
        self.0 += Self::try_from_duration_since_epoch(duration).unwrap().0;
    }
}

impl Add<Duration> for Timestamp {
    type Output = Timestamp;

    fn add(mut self, rhs: Duration) -> Self::Output {
        self += rhs;
        self
    }
}

#[cfg(test)]
mod test {
    use super::*;
    const NANOSECONDS_PER_SECOND: u64 = 1000000000;

    #[test]
    fn test_timestamp() {
        assert_eq!(Timestamp::EPOCH, SystemTime::UNIX_EPOCH.try_into().unwrap());
        assert_eq!(
            Timestamp(0x10000).to_system_time(),
            SystemTime::UNIX_EPOCH + Duration::from_secs(1)
        );
        assert_eq!(
            Timestamp(0x10000),
            (SystemTime::UNIX_EPOCH + Duration::from_secs(1)).try_into().unwrap()
        );
        assert_eq!(
            Timestamp(0x8000),
            (SystemTime::UNIX_EPOCH + Duration::from_millis(500)).try_into().unwrap()
        );
        assert_eq!(
            Timestamp(0x8000).to_system_time(),
            (SystemTime::UNIX_EPOCH + Duration::from_millis(500))
        );
        assert_eq!(
            Timestamp(0xFFFE),
            (SystemTime::UNIX_EPOCH
                + Duration::from_nanos(NANOSECONDS_PER_SECOND * 0xFFFE / 0x10000))
            .try_into()
            .unwrap()
        );
        assert_eq!(
            Timestamp(0x10000),
            (SystemTime::UNIX_EPOCH + Duration::from_nanos(999999999)).try_into().unwrap()
        );
        assert_eq!(
            Timestamp(0x20000),
            (SystemTime::UNIX_EPOCH + Duration::from_nanos(1999999999)).try_into().unwrap()
        );

        assert_eq!(Timestamp(1).to_be_bytes(), [0, 0, 0, 0, 0, 0, 0, 1]);
        assert_eq!(Timestamp(1).to_le_bytes(), [1, 0, 0, 0, 0, 0, 0, 0]);

        assert_eq!(
            format!("{:?}", Timestamp::from(0x62CC8DE70000)),
            "62CC8DE70000 (2022-07-11T20:53:59)".to_string()
        );

        assert_eq!(Timestamp::from(0x62CC8DE70000).to_string(), "2022-07-11 20:53:59".to_string());

        assert_eq!(
            Timestamp::from(0x62CC8DE70001).to_string(),
            "2022-07-11 20:53:59 UTC".to_string()
        );

        assert!(!Timestamp::from(0u64).is_utc());
        assert!(Timestamp::from(1u64).is_utc());

        assert!(Timestamp(0).with_utc(true).is_utc());
        assert!(!Timestamp(1).with_utc(false).is_utc());

        assert!(Timestamp::try_from_system_time(SystemTime::UNIX_EPOCH - Duration::from_secs(1))
            .is_err());
        assert_eq!(Timestamp::EPOCH + Duration::from_secs(1), Timestamp(0x10000));
        println!("now={:?}", Timestamp::now());
    }
}
