// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        abs_moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
        child_moniker::{ChildMoniker, ChildMonikerBase},
    },
    serde::{
        de::{self, Deserializer, Visitor},
        Deserialize, Serialize, Serializer,
    },
    std::fmt,
};

impl Serialize for ChildMoniker {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serializer.serialize_str(self.as_str())
    }
}

struct ChildMonikerVisitor;

impl<'de> Visitor<'de> for ChildMonikerVisitor {
    type Value = ChildMoniker;

    fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("a child moniker of a component instance")
    }

    fn visit_str<E>(self, value: &str) -> Result<Self::Value, E>
    where
        E: de::Error,
    {
        match ChildMoniker::parse(value) {
            Ok(moniker) => Ok(moniker),
            Err(err) => Err(E::custom(format!("Failed to parse ChildMoniker: {}", err))),
        }
    }
}

impl<'de> Deserialize<'de> for ChildMoniker {
    fn deserialize<D>(deserializer: D) -> Result<ChildMoniker, D::Error>
    where
        D: Deserializer<'de>,
    {
        deserializer.deserialize_str(ChildMonikerVisitor)
    }
}

impl Serialize for AbsoluteMoniker {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serializer.serialize_str(&self.to_string())
    }
}

struct AbsoluteMonikerVisitor;

impl<'de> Visitor<'de> for AbsoluteMonikerVisitor {
    type Value = AbsoluteMoniker;

    fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("an absolute moniker of a component instance")
    }

    fn visit_str<E>(self, value: &str) -> Result<Self::Value, E>
    where
        E: de::Error,
    {
        match AbsoluteMoniker::parse_str(value) {
            Ok(moniker) => Ok(moniker),
            Err(err) => Err(E::custom(format!("Failed to parse AbsoluteMoniker: {}", err))),
        }
    }
}

impl<'de> Deserialize<'de> for AbsoluteMoniker {
    fn deserialize<D>(deserializer: D) -> Result<AbsoluteMoniker, D::Error>
    where
        D: Deserializer<'de>,
    {
        deserializer.deserialize_str(AbsoluteMonikerVisitor)
    }
}
