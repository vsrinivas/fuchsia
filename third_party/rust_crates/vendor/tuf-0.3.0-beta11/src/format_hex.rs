use data_encoding::HEXLOWER;
use serde::{self, Deserialize, Deserializer, Serializer};
use std::result::Result;

pub fn serialize<S>(value: &[u8], serializer: S) -> Result<S::Ok, S::Error>
where
    S: Serializer,
{
    serializer.serialize_str(&HEXLOWER.encode(value))
}

pub fn deserialize<'de, D>(deserializer: D) -> Result<Vec<u8>, D::Error>
where
    D: Deserializer<'de>,
{
    let s = String::deserialize(deserializer)?;
    HEXLOWER
        .decode(s.as_bytes())
        .map_err(serde::de::Error::custom)
}
