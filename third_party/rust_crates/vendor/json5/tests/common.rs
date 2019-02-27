use json5;
use serde;

use std::f64;

#[allow(dead_code)]
pub fn deserializes_to<'a, T>(s: &'a str, v: T)
where
    T: ::std::fmt::Debug + ::std::cmp::PartialEq + serde::de::Deserialize<'a>,
{
    match json5::from_str::<T>(s) {
        Ok(value) => assert_eq!(value, v),
        Err(err) => panic!(format!("{}", err)),
    }
}

#[allow(dead_code)]
pub fn deserializes_to_nan<'a>(s: &'a str) {
    match json5::from_str::<f64>(s) {
        Ok(value) => assert!(value.is_nan()),
        Err(err) => panic!(format!("{}", err)),
    }
}

#[allow(dead_code)]
pub fn deserializes_with_error<'a, T>(s: &'a str, _: T, error_expected: &'a str)
where
    T: ::std::fmt::Debug + ::std::cmp::PartialEq + serde::de::Deserialize<'a>,
{
    match json5::from_str::<T>(s) {
        Ok(val) => panic!(format!("error expected!, got {:?}", val)),
        Err(err) => assert_eq!(format!("{}", err), error_expected),
    }
}

#[allow(dead_code)]
pub fn serializes_to<T>(v: T, s: &'static str)
where
    T: ::std::fmt::Debug + ::std::cmp::PartialEq + serde::ser::Serialize,
{
    match json5::to_string::<T>(&v) {
        Ok(value) => assert_eq!(value, s),
        Err(err) => panic!(format!("{}", err)),
    }
}
