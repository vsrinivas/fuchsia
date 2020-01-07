//! Serde `Serialize` and `Deserialize` implementations for `MaybeOwned`.
use serde::{Deserialize, Deserializer, Serialize, Serializer};

use MaybeOwned;
use MaybeOwned::*;

impl<'a, T> Serialize for MaybeOwned<'a, T>
where
    T: Serialize,
{
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match *self {
            Owned(ref v) => v.serialize(serializer),
            Borrowed(v) => v.serialize(serializer),
        }
    }
}

impl<'a, 'de, T> Deserialize<'de> for MaybeOwned<'a, T>
where
    T: Deserialize<'de>,
{
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        T::deserialize(deserializer).map(Owned)
    }
}
