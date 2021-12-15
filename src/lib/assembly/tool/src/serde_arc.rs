// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Deserializer, Serialize, Serializer};
use std::sync::Arc;

/// Serialize an Arc<T> by dereferencing it rather than copying the contents.
pub fn serialize<T, S>(value: &Arc<T>, ser: S) -> Result<S::Ok, S::Error>
where
    T: Serialize,
    S: Serializer,
{
    (**value).serialize(ser)
}

/// Deserialize into an Arc<T>.
pub fn deserialize<'de, T, D>(de: D) -> Result<Arc<T>, D::Error>
where
    T: Deserialize<'de>,
    D: Deserializer<'de>,
{
    let value = T::deserialize(de)?;
    Ok(Arc::new(value))
}

#[cfg(test)]
mod test {
    use serde::{Deserialize, Serialize};
    use serde_json::json;
    use std::sync::Arc;

    #[derive(Serialize, Deserialize)]
    struct MyStruct {
        #[serde(with = "super")]
        value: Arc<u8>,
    }

    #[test]
    fn test_serialize() {
        let s = MyStruct { value: Arc::new(8) };
        let serialized = serde_json::to_value(&s).unwrap();
        let expected = json!({
            "value": 8,
        });
        assert_eq!(expected, serialized);
    }
}
