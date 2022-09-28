//! Structures and functions to aid in various TUF data pouf formats.

pub(crate) mod pouf1;
pub use pouf1::Pouf1;

use serde::de::DeserializeOwned;
use serde::ser::Serialize;

use crate::Result;

/// The format used for data interchange, serialization, and deserialization.
pub trait Pouf: Sync {
    /// The type of data that is contained in the `signed` portion of metadata.
    type RawData: Serialize + DeserializeOwned + PartialEq;

    /// The data pouf's extension.
    fn extension() -> &'static str;

    /// A function that canonicalizes data to allow for deterministic signatures.
    fn canonicalize(raw_data: &Self::RawData) -> Result<Vec<u8>>;

    /// Deserialize from `RawData`.
    fn deserialize<T>(raw_data: &Self::RawData) -> Result<T>
    where
        T: DeserializeOwned;

    /// Serialize into `RawData`.
    fn serialize<T>(data: &T) -> Result<Self::RawData>
    where
        T: Serialize;

    /// Read a struct from a stream.
    fn from_slice<T>(slice: &[u8]) -> Result<T>
    where
        T: DeserializeOwned;
}
