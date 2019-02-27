use chrono::{DateTime, Duration, TimeZone, Utc};
use std::fmt;
use std::str::FromStr;

use {Error, Result};

/// A UTC timestamp. Used for serialization to and from the plist date type.
#[derive(Clone, Debug, PartialEq)]
pub struct Date {
    inner: DateTime<Utc>,
}

impl Date {
    #[doc(hidden)]
    pub fn from_seconds_since_plist_epoch(timestamp: f64) -> Result<Date> {
        // Seconds since 1/1/2001 00:00:00.

        if timestamp.is_nan() {
            return Err(Error::InvalidData);
        }

        let millis = timestamp * 1_000.0;
        // Chrono's Duration can only millisecond values between ::std::i64::MIN and
        // ::std::i64::MAX.
        if millis > ::std::i64::MAX as f64 || millis < ::std::i64::MIN as f64 {
            return Err(Error::InvalidData);
        }

        let whole_millis = millis.floor();
        let submilli_nanos = ((millis - whole_millis) * 1_000_000.0).floor();

        let dur = Duration::milliseconds(whole_millis as i64);
        let dur = dur + Duration::nanoseconds(submilli_nanos as i64);

        let plist_epoch = Utc.ymd(2001, 1, 1).and_hms(0, 0, 0);
        let date = plist_epoch.checked_add_signed(dur).ok_or(Error::InvalidData)?;

        Ok(Date { inner: date })
    }
}

impl From<DateTime<Utc>> for Date {
    fn from(date: DateTime<Utc>) -> Self {
        Date { inner: date }
    }
}

impl Into<DateTime<Utc>> for Date {
    fn into(self) -> DateTime<Utc> {
        self.inner
    }
}

impl fmt::Display for Date {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{:?}", self.inner)
    }
}

impl FromStr for Date {
    type Err = ();

    fn from_str(s: &str) -> ::std::result::Result<Self, Self::Err> {
        let date = DateTime::parse_from_rfc3339(s).map_err(|_| ())?;
        Ok(Date { inner: date.with_timezone(&Utc) })
    }
}

#[cfg(feature = "serde")]
pub mod serde_impls {
    use serde_base::de::{Deserialize, Deserializer, Error, Visitor, Unexpected};
    use serde_base::ser::{Serialize, Serializer};
    use std::fmt;
    use std::str::FromStr;

    use Date;

    pub const DATE_NEWTYPE_STRUCT_NAME: &'static str = "PLIST-DATE";

    impl Serialize for Date {
        fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
            where S: Serializer
        {
            let date_str = self.to_string();
            serializer.serialize_newtype_struct(DATE_NEWTYPE_STRUCT_NAME, &date_str)
        }
    }

    struct DateNewtypeVisitor;

    impl<'de> Visitor<'de> for DateNewtypeVisitor {
        type Value = Date;

        fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
            formatter.write_str("a plist date newtype")
        }

        fn visit_newtype_struct<D>(self, deserializer: D) -> Result<Self::Value, D::Error>
            where D: Deserializer<'de>
        {
            deserializer.deserialize_str(DateStrVisitor)
        }
    }

    struct DateStrVisitor;

    impl<'de> Visitor<'de> for DateStrVisitor {
        type Value = Date;

        fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
            formatter.write_str("a plist date string")
        }

        fn visit_str<E>(self, v: &str) -> Result<Self::Value, E>
            where E: Error
        {
            Date::from_str(v).map_err(|_| E::invalid_value(Unexpected::Str(v), &self))
        }
    }

    impl<'de> Deserialize<'de> for Date {
        fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
            where D: Deserializer<'de>
        {
            deserializer.deserialize_newtype_struct(DATE_NEWTYPE_STRUCT_NAME, DateNewtypeVisitor)
        }
    }
}
