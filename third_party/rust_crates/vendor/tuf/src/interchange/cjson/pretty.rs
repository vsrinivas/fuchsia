use serde::de::DeserializeOwned;
use serde::ser::Serialize;
use std::io::{Read, Write};

use super::Json;
use crate::interchange::DataInterchange;
use crate::Result;

/// Pretty JSON data interchange.
///
/// This is identical to [Json] in all manners except for the `to_writer` method. Instead of
/// writing the metadata in the canonical format, it instead pretty prints the metadata.
#[derive(Debug, Clone, PartialEq)]
pub struct JsonPretty;

impl DataInterchange for JsonPretty {
    type RawData = serde_json::Value;

    /// ```
    /// # use tuf::interchange::{DataInterchange, JsonPretty};
    /// assert_eq!(JsonPretty::extension(), "json");
    /// ```
    fn extension() -> &'static str {
        Json::extension()
    }

    /// ```
    /// # use tuf::interchange::{DataInterchange, JsonPretty};
    /// # use std::collections::HashMap;
    /// let jsn: &[u8] = br#"{"foo": "bar", "baz": "quux"}"#;
    /// let raw = JsonPretty::from_reader(jsn).unwrap();
    /// let out = JsonPretty::canonicalize(&raw).unwrap();
    /// assert_eq!(out, br#"{"baz":"quux","foo":"bar"}"#);
    /// ```
    fn canonicalize(raw_data: &Self::RawData) -> Result<Vec<u8>> {
        Json::canonicalize(raw_data)
    }

    /// ```
    /// # use serde_derive::Deserialize;
    /// # use serde_json::json;
    /// # use std::collections::HashMap;
    /// # use tuf::interchange::{DataInterchange, JsonPretty};
    /// #
    /// #[derive(Deserialize, Debug, PartialEq)]
    /// struct Thing {
    ///    foo: String,
    ///    bar: String,
    /// }
    ///
    /// let jsn = json!({"foo": "wat", "bar": "lol"});
    /// let thing = Thing { foo: "wat".into(), bar: "lol".into() };
    /// let de: Thing = JsonPretty::deserialize(&jsn).unwrap();
    /// assert_eq!(de, thing);
    /// ```
    fn deserialize<T>(raw_data: &Self::RawData) -> Result<T>
    where
        T: DeserializeOwned,
    {
        Json::deserialize(raw_data)
    }

    /// ```
    /// # use serde_derive::Serialize;
    /// # use serde_json::json;
    /// # use std::collections::HashMap;
    /// # use tuf::interchange::{DataInterchange, JsonPretty};
    /// #
    /// #[derive(Serialize)]
    /// struct Thing {
    ///    foo: String,
    ///    bar: String,
    /// }
    ///
    /// let jsn = json!({"foo": "wat", "bar": "lol"});
    /// let thing = Thing { foo: "wat".into(), bar: "lol".into() };
    /// let se: serde_json::Value = JsonPretty::serialize(&thing).unwrap();
    /// assert_eq!(se, jsn);
    /// ```
    fn serialize<T>(data: &T) -> Result<Self::RawData>
    where
        T: Serialize,
    {
        Json::serialize(data)
    }

    /// ```
    /// # use serde_json::json;
    /// # use tuf::interchange::{DataInterchange, JsonPretty};
    /// #
    /// let json = json!({
    ///     "o": {
    ///         "a": [1, 2, 3],
    ///         "s": "string",
    ///         "n": 123,
    ///         "t": true,
    ///         "f": false,
    ///         "0": null,
    ///     },
    /// });
    ///
    /// let mut buf = Vec::new();
    /// JsonPretty::to_writer(&mut buf, &json).unwrap();
    ///
    /// assert_eq!(&String::from_utf8(buf).unwrap(), r#"{
    ///   "o": {
    ///     "0": null,
    ///     "a": [
    ///       1,
    ///       2,
    ///       3
    ///     ],
    ///     "f": false,
    ///     "n": 123,
    ///     "s": "string",
    ///     "t": true
    ///   }
    /// }"#);
    /// ```
    fn to_writer<W, T: Sized>(writer: W, value: &T) -> Result<()>
    where
        W: Write,
        T: Serialize,
    {
        Ok(serde_json::to_writer_pretty(
            writer,
            &Self::serialize(value)?,
        )?)
    }

    /// ```
    /// # use tuf::interchange::{DataInterchange, JsonPretty};
    /// # use std::collections::HashMap;
    /// let jsn: &[u8] = br#"{"foo": "bar", "baz": "quux"}"#;
    /// let _: HashMap<String, String> = JsonPretty::from_reader(jsn).unwrap();
    /// ```
    fn from_reader<R, T>(rdr: R) -> Result<T>
    where
        R: Read,
        T: DeserializeOwned,
    {
        Json::from_reader(rdr)
    }

    /// ```
    /// # use tuf::interchange::{DataInterchange, JsonPretty};
    /// # use std::collections::HashMap;
    /// let jsn: &[u8] = br#"{"foo": "bar", "baz": "quux"}"#;
    /// let _: HashMap<String, String> = JsonPretty::from_slice(&jsn).unwrap();
    /// ```
    fn from_slice<T>(slice: &[u8]) -> Result<T>
    where
        T: DeserializeOwned,
    {
        Json::from_slice(slice)
    }
}
