use std::collections::BTreeMap;
use std::io::{Read, Seek};

use {builder, Date, EventReader, PlistEvent, Result};

#[derive(Clone, Debug, PartialEq)]
pub enum Plist {
    Array(Vec<Plist>),
    Dictionary(BTreeMap<String, Plist>),
    Boolean(bool),
    Data(Vec<u8>),
    Date(Date),
    Real(f64),
    Integer(i64),
    String(String),
}

impl Plist {
    pub fn read<R: Read + Seek>(reader: R) -> Result<Plist> {
        let reader = EventReader::new(reader);
        Plist::from_events(reader)
    }

    pub fn from_events<T>(events: T) -> Result<Plist>
        where T: IntoIterator<Item = Result<PlistEvent>>
    {
        let iter = events.into_iter();
        let builder = builder::Builder::new(iter);
        builder.build()
    }

    pub fn into_events(self) -> Vec<PlistEvent> {
        let mut events = Vec::new();
        self.into_events_inner(&mut events);
        events
    }

    fn into_events_inner(self, events: &mut Vec<PlistEvent>) {
        match self {
            Plist::Array(array) => {
                events.push(PlistEvent::StartArray(Some(array.len() as u64)));
                for value in array {
                    value.into_events_inner(events);
                }
                events.push(PlistEvent::EndArray);
            }
            Plist::Dictionary(dict) => {
                events.push(PlistEvent::StartDictionary(Some(dict.len() as u64)));
                for (key, value) in dict {
                    events.push(PlistEvent::StringValue(key));
                    value.into_events_inner(events);
                }
                events.push(PlistEvent::EndDictionary);
            }
            Plist::Boolean(value) => events.push(PlistEvent::BooleanValue(value)),
            Plist::Data(value) => events.push(PlistEvent::DataValue(value)),
            Plist::Date(value) => events.push(PlistEvent::DateValue(value)),
            Plist::Real(value) => events.push(PlistEvent::RealValue(value)),
            Plist::Integer(value) => events.push(PlistEvent::IntegerValue(value)),
            Plist::String(value) => events.push(PlistEvent::StringValue(value)),
        }
    }

    /// If the `Plist` is an Array, returns the associated Vec.
    /// Returns None otherwise.
    pub fn as_array(&self) -> Option<&Vec<Plist>> {
        match *self {
            Plist::Array(ref array) => Some(array),
            _ => None,
        }
    }

    /// If the `Plist` is an Array, returns the associated mutable Vec.
    /// Returns None otherwise.
    pub fn as_array_mut(&mut self) -> Option<&mut Vec<Plist>> {
        match *self {
            Plist::Array(ref mut array) => Some(array),
            _ => None,
        }
    }

    /// If the `Plist` is a Dictionary, returns the associated BTreeMap.
    /// Returns None otherwise.
    pub fn as_dictionary(&self) -> Option<&BTreeMap<String, Plist>> {
        match *self {
            Plist::Dictionary(ref map) => Some(map),
            _ => None,
        }
    }

    /// If the `Plist` is a Dictionary, returns the associated mutable BTreeMap.
    /// Returns None otherwise.
    pub fn as_dictionary_mut(&mut self) -> Option<&mut BTreeMap<String, Plist>> {
        match *self {
            Plist::Dictionary(ref mut map) => Some(map),
            _ => None,
        }
    }

    /// If the `Plist` is a Boolean, returns the associated bool.
    /// Returns None otherwise.
    pub fn as_boolean(&self) -> Option<bool> {
        match *self {
            Plist::Boolean(v) => Some(v),
            _ => None,
        }
    }

    /// If the `Plist` is a Data, returns the underlying Vec.
    /// Returns None otherwise.
    ///
    /// This method consumes the `Plist`. If this is not desired, please use
    /// `as_data` method.
    pub fn into_data(self) -> Option<Vec<u8>> {
        match self {
            Plist::Data(data) => Some(data),
            _ => None,
        }
    }

    /// If the `Plist` is a Data, returns the associated Vec.
    /// Returns None otherwise.
    pub fn as_data(&self) -> Option<&[u8]> {
        match *self {
            Plist::Data(ref data) => Some(data),
            _ => None,
        }
    }

    /// If the `Plist` is a Date, returns the associated DateTime.
    /// Returns None otherwise.
    pub fn as_date(&self) -> Option<&Date> {
        match *self {
            Plist::Date(ref date) => Some(date),
            _ => None,
        }
    }

    /// If the `Plist` is a Real, returns the associated f64.
    /// Returns None otherwise.
    pub fn as_real(&self) -> Option<f64> {
        match *self {
            Plist::Real(v) => Some(v),
            _ => None,
        }
    }

    /// If the `Plist` is an Integer, returns the associated i64.
    /// Returns None otherwise.
    pub fn as_integer(&self) -> Option<i64> {
        match *self {
            Plist::Integer(v) => Some(v),
            _ => None,
        }
    }

    /// If the `Plist` is a String, returns the underlying String.
    /// Returns None otherwise.
    ///
    /// This method consumes the `Plist`. If this is not desired, please use
    /// `as_string` method.
    pub fn into_string(self) -> Option<String> {
        match self {
            Plist::String(v) => Some(v),
            _ => None,
        }
    }

    /// If the `Plist` is a String, returns the associated str.
    /// Returns None otherwise.
    pub fn as_string(&self) -> Option<&str> {
        match *self {
            Plist::String(ref v) => Some(v),
            _ => None,
        }
    }
}

impl From<Vec<Plist>> for Plist {
    fn from(from: Vec<Plist>) -> Plist {
        Plist::Array(from)
    }
}

impl From<BTreeMap<String, Plist>> for Plist {
    fn from(from: BTreeMap<String, Plist>) -> Plist {
        Plist::Dictionary(from)
    }
}

impl From<bool> for Plist {
    fn from(from: bool) -> Plist {
        Plist::Boolean(from)
    }
}

impl<'a> From<&'a bool> for Plist {
    fn from(from: &'a bool) -> Plist {
        Plist::Boolean(*from)
    }
}

impl From<Date> for Plist {
    fn from(from: Date) -> Plist {
        Plist::Date(from)
    }
}

impl<'a> From<&'a Date> for Plist {
    fn from(from: &'a Date) -> Plist {
        Plist::Date(from.clone())
    }
}

impl From<f64> for Plist {
    fn from(from: f64) -> Plist {
        Plist::Real(from)
    }
}

impl From<f32> for Plist {
    fn from(from: f32) -> Plist {
        Plist::Real(from as f64)
    }
}

impl From<i64> for Plist {
    fn from(from: i64) -> Plist {
        Plist::Integer(from)
    }
}

impl From<i32> for Plist {
    fn from(from: i32) -> Plist {
        Plist::Integer(from as i64)
    }
}

impl From<i16> for Plist {
    fn from(from: i16) -> Plist {
        Plist::Integer(from as i64)
    }
}

impl From<i8> for Plist {
    fn from(from: i8) -> Plist {
        Plist::Integer(from as i64)
    }
}

impl From<u32> for Plist {
    fn from(from: u32) -> Plist {
        Plist::Integer(from as i64)
    }
}

impl From<u16> for Plist {
    fn from(from: u16) -> Plist {
        Plist::Integer(from as i64)
    }
}

impl From<u8> for Plist {
    fn from(from: u8) -> Plist {
        Plist::Integer(from as i64)
    }
}

impl<'a> From<&'a f64> for Plist {
    fn from(from: &'a f64) -> Plist {
        Plist::Real(*from)
    }
}

impl<'a> From<&'a f32> for Plist {
    fn from(from: &'a f32) -> Plist {
        Plist::Real(*from as f64)
    }
}

impl<'a> From<&'a i64> for Plist {
    fn from(from: &'a i64) -> Plist {
        Plist::Integer(*from)
    }
}

impl<'a> From<&'a i32> for Plist {
    fn from(from: &'a i32) -> Plist {
        Plist::Integer(*from as i64)
    }
}

impl<'a> From<&'a i16> for Plist {
    fn from(from: &'a i16) -> Plist {
        Plist::Integer(*from as i64)
    }
}

impl<'a> From<&'a i8> for Plist {
    fn from(from: &'a i8) -> Plist {
        Plist::Integer(*from as i64)
    }
}

impl<'a> From<&'a u32> for Plist {
    fn from(from: &'a u32) -> Plist {
        Plist::Integer(*from as i64)
    }
}

impl<'a> From<&'a u16> for Plist {
    fn from(from: &'a u16) -> Plist {
        Plist::Integer(*from as i64)
    }
}

impl<'a> From<&'a u8> for Plist {
    fn from(from: &'a u8) -> Plist {
        Plist::Integer(*from as i64)
    }
}

impl From<String> for Plist {
    fn from(from: String) -> Plist {
        Plist::String(from)
    }
}

impl<'a> From<&'a str> for Plist {
    fn from(from: &'a str) -> Plist {
        Plist::String(from.into())
    }
}

#[cfg(test)]
mod tests {
    use super::Plist;

    #[test]
    fn test_plist_access() {
        use std::collections::BTreeMap;
        use chrono::prelude::*;
        use super::Date;

        let vec = vec![Plist::Real(0.0)];
        let mut array = Plist::Array(vec.clone());
        assert_eq!(array.as_array(), Some(&vec.clone()));
        assert_eq!(array.as_array_mut(), Some(&mut vec.clone()));

        let mut map = BTreeMap::new();
        map.insert("key1".to_owned(), Plist::String("value1".to_owned()));
        let mut dict = Plist::Dictionary(map.clone());
        assert_eq!(dict.as_dictionary(), Some(&map.clone()));
        assert_eq!(dict.as_dictionary_mut(), Some(&mut map.clone()));

        assert_eq!(Plist::Boolean(true).as_boolean(), Some(true));

        let slice: &[u8] = &[1, 2, 3];
        assert_eq!(Plist::Data(slice.to_vec()).as_data(), Some(slice));
        assert_eq!(Plist::Data(slice.to_vec()).into_data(),
                   Some(slice.to_vec()));

        let date: Date = Utc::now().into();
        assert_eq!(Plist::Date(date.clone()).as_date(), Some(&date));

        assert_eq!(Plist::Real(0.0).as_real(), Some(0.0));
        assert_eq!(Plist::Integer(1).as_integer(), Some(1));
        assert_eq!(Plist::String("2".to_owned()).as_string(), Some("2"));
        assert_eq!(Plist::String("t".to_owned()).into_string(),
                   Some("t".to_owned()));
    }
}
