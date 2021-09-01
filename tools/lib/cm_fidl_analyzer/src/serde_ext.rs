// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::component_tree::NodePath,
    serde::{
        de::{self, Deserializer, Visitor},
        Deserialize, Serialize, Serializer,
    },
    std::{error::Error, fmt},
};

/// Serialize `NodePath` into a path-like slash-separated a string
/// representation of the underlying child monikers.
//
/// Example: "/alpha:2/beta:0/gamma:1".
impl Serialize for NodePath {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let path = format!("/{}", self.as_vec().join("/"));
        serializer.serialize_str(&path)
    }
}

struct NodePathVisitor;

/// Deserialize `NodePath` from path-like slash-separated string of child
/// monikers.
impl<'de> Visitor<'de> for NodePathVisitor {
    type Value = NodePath;

    fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("A component tree node path")
    }

    fn visit_str<E>(self, value: &str) -> Result<Self::Value, E>
    where
        E: de::Error,
    {
        Ok(NodePath::new(
            value
                .split("/")
                .filter_map(
                    |component| {
                        if component.is_empty() {
                            None
                        } else {
                            Some(component.into())
                        }
                    },
                )
                .collect(),
        ))
    }
}

impl<'de> Deserialize<'de> for NodePath {
    fn deserialize<D>(deserializer: D) -> Result<NodePath, D::Error>
    where
        D: Deserializer<'de>,
    {
        deserializer.deserialize_str(NodePathVisitor)
    }
}

/// Error for use with serialization: Stores both structured error and message,
/// and assesses equality using structured error.
#[derive(Clone, Default, Deserialize, Serialize)]
pub struct ErrorWithMessage<E: Clone + Error + Serialize> {
    pub error: E,
    #[serde(default)]
    pub message: String,
}

impl<E: Clone + Error + PartialEq + Serialize> PartialEq<ErrorWithMessage<E>>
    for ErrorWithMessage<E>
{
    fn eq(&self, other: &Self) -> bool {
        // Ignore `message` when comparing.
        self.error == other.error
    }
}

impl<'de, E> From<E> for ErrorWithMessage<E>
where
    E: Clone + Deserialize<'de> + Error + Serialize,
{
    fn from(error: E) -> Self {
        Self::from(&error)
    }
}

impl<'de, E> From<&E> for ErrorWithMessage<E>
where
    E: Clone + Deserialize<'de> + Error + Serialize,
{
    fn from(error: &E) -> Self {
        let message = error.to_string();
        let error = error.clone();
        Self { error, message }
    }
}
