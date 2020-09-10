// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    itertools::Itertools,
    std::{fmt, str::FromStr},
};

/// This is a utility wrapper around Omaha-style versions - in the form of A.B.C.D, A.B.C, A.B or A.
#[derive(Clone, Copy, Eq, Ord, PartialOrd, PartialEq)]
pub struct Version([u32; 4]);

impl fmt::Display for Version {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0.iter().format("."))
    }
}

impl fmt::Debug for Version {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // The Debug trait just forwards to the Display trait implementation for this type
        fmt::Display::fmt(self, f)
    }
}

#[derive(Debug, thiserror::Error)]
struct TooManyNumbersError;

impl fmt::Display for TooManyNumbersError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("Too many numbers in version, the maximum is 4.")
    }
}

impl FromStr for Version {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let nums = s.split('.').map(|s| s.parse::<u32>());

        let mut array: [u32; 4] = [0; 4];
        for (i, v) in nums.enumerate() {
            if i >= 4 {
                return Err(TooManyNumbersError.into());
            }
            array[i] = v?;
        }
        Ok(Version(array))
    }
}

macro_rules! impl_from {
    ($($t:ty),+) => {
        $(
            impl From<$t> for Version {
                fn from(v: $t) -> Self {
                    let mut array: [u32; 4] = [0; 4];
                    array.split_at_mut(v.len()).0.copy_from_slice(&v);
                    Version(array)
                }
            }
        )+
    }
}
impl_from!([u32; 1], [u32; 2], [u32; 3], [u32; 4]);

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn test_version_display() {
        let version = Version::from([1, 2, 3, 4]);
        assert_eq!("1.2.3.4", version.to_string());

        let version = Version::from([0, 6, 4, 7]);
        assert_eq!("0.6.4.7", version.to_string());
    }

    #[test]
    fn test_version_debug() {
        let version = Version::from([1, 2, 3, 4]);
        assert_eq!("1.2.3.4", format!("{:?}", version));

        let version = Version::from([0, 6, 4, 7]);
        assert_eq!("0.6.4.7", format!("{:?}", version));
    }

    #[test]
    fn test_version_parse() {
        let version = Version::from([1, 2, 3, 4]);
        assert_eq!("1.2.3.4".parse::<Version>().unwrap(), version);

        let version = Version::from([6, 4, 7]);
        assert_eq!("6.4.7".parse::<Version>().unwrap(), version);

        let version = Version::from([999]);
        assert_eq!("999".parse::<Version>().unwrap(), version);
    }

    #[test]
    fn test_version_parse_leading_zeros() {
        let version = Version::from([1, 2, 3, 4]);
        assert_eq!("1.02.003.0004".parse::<Version>().unwrap(), version);

        let version = Version::from([6, 4, 7]);
        assert_eq!("06.4.07".parse::<Version>().unwrap(), version);

        let version = Version::from([999]);
        assert_eq!("0000999".parse::<Version>().unwrap(), version);
    }

    #[test]
    fn test_version_parse_error() {
        assert!("1.2.3.4.5".parse::<Version>().is_err());
        assert!("1.2.".parse::<Version>().is_err());
        assert!(".1.2".parse::<Version>().is_err());
        assert!("-1".parse::<Version>().is_err());
        assert!("abc".parse::<Version>().is_err());
        assert!(".".parse::<Version>().is_err());
        assert!("".parse::<Version>().is_err());
        assert!("999999999999999999999999".parse::<Version>().is_err());
    }

    #[test]
    fn test_version_to_string() {
        assert_eq!(&"1.2".parse::<Version>().unwrap().to_string(), "1.2.0.0");
        assert_eq!(&"1.2.3.4".parse::<Version>().unwrap().to_string(), "1.2.3.4");
        assert_eq!(&"1".parse::<Version>().unwrap().to_string(), "1.0.0.0");
        assert_eq!(&"3.2.1".parse::<Version>().unwrap().to_string(), "3.2.1.0");
    }

    #[test]
    fn test_version_compare() {
        assert!(Version::from([1, 2, 3, 4]) < Version::from([2, 0, 3]));
        assert!(Version::from([1, 2, 3]) < Version::from([1, 2, 3, 4]));
        assert!(Version::from([1, 0]) == Version::from([1, 0, 0]));
        assert!(!(Version::from([1, 0]) > Version::from([1, 0, 0])));
        assert!(!(Version::from([1, 0]) < Version::from([1, 0, 0])));
        assert!(Version::from([1]) == Version::from([1, 0, 0, 0]));
        assert!(Version::from([0]) == Version::from([0, 0, 0, 0]));
        assert!(Version::from([0, 1, 0]) > Version::from([0, 0, 1, 0]));
        assert!(Version::from([0]) < Version::from([0, 0, 1, 0]));
        assert!(Version::from([1]) < Version::from([1, 0, 1, 0]));
        assert!(Version::from([1, 0]) < Version::from([1, 0, 0, 1]));
        assert!(Version::from([1, 0, 0]) > Version::from([0, 1, 2, 0]));
    }
}
