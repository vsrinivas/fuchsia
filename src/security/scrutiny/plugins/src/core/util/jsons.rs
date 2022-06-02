// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::util::types::Protocol,
    serde::{
        de::{self, Deserializer, Error as _, MapAccess, Visitor},
        ser::{SerializeMap as _, Serializer},
        Deserialize, Serialize,
    },
    serde_json::Value,
    std::{collections::HashMap, fmt},
    url::Url,
};

/// JSON structure of a blob retrieved from package server via merkle hash that
/// defines a service package.
/// There is no guarantee that all blobs vended from the package server follow this
/// format. A caller is responsible for correctly identifying the expected blob
/// format.
#[derive(Deserialize)]
pub struct ServicePackageDefinition {
    /// Map of a service name to a provider (i.e., component URL) of that service.
    pub services: Option<HashMap<String, Value>>,
    /// List of component Urls started on sys realm launch
    pub apps: Option<Vec<String>>,
}

/// Serialize service-protocol => server-url mappings. A custom strategy is necessary because
/// `url::Url` does not implement `serde::Serialize`. Protocol names and URLs are serialized as
/// `strings.
pub fn serialize_services<S>(
    services: &HashMap<Protocol, Url>,
    serializer: S,
) -> Result<S::Ok, S::Error>
where
    S: Serializer,
{
    let mut map = serializer.serialize_map(Some(services.len()))?;
    for (protocol, url) in services {
        map.serialize_entry(protocol, &url.to_string())?;
    }
    map.end()
}

/// Desrialize service-protocol => server-url mappings. A custom strategy is necessary because
/// `url::Url` does not implement `serde::Deserialize`. Protocol names and URLs are deserialized
/// from strings.
pub fn deserialize_services<'de, D>(deserializer: D) -> Result<HashMap<Protocol, Url>, D::Error>
where
    D: Deserializer<'de>,
{
    struct MapVisitor;

    impl<'de> Visitor<'de> for MapVisitor {
        type Value = HashMap<Protocol, Url>;

        fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            formatter.write_str("services map")
        }

        fn visit_map<A>(self, mut map_access: A) -> Result<Self::Value, A::Error>
        where
            A: MapAccess<'de>,
        {
            let mut map = HashMap::with_capacity(map_access.size_hint().unwrap_or(0));
            loop {
                let entry: Option<(Protocol, String)> = map_access.next_entry()?;
                match entry {
                    None => break,
                    Some((protocol, url_string)) => {
                        let url = Url::parse(&url_string).map_err(|err| {
                            A::Error::custom(&format!(
                                "Failed to parse URL from string: {}: {}",
                                url_string, err
                            ))
                        })?;
                        map.insert(protocol, url);
                    }
                }
            }
            Ok(map)
        }
    }

    let visitor = MapVisitor;
    deserializer.deserialize_any(visitor)
}

pub fn serialize_url<S>(url: &Url, serializer: S) -> Result<S::Ok, S::Error>
where
    S: Serializer,
{
    serializer.serialize_str(&url.to_string())
}

pub fn deserialize_url<'de, D>(deserializer: D) -> Result<Url, D::Error>
where
    D: Deserializer<'de>,
{
    struct UrlVisitor;

    impl<'de> Visitor<'de> for UrlVisitor {
        type Value = Url;

        fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            formatter.write_str("url")
        }

        fn visit_str<E>(self, url_str: &str) -> Result<Self::Value, E>
        where
            E: de::Error,
        {
            Url::parse(url_str).map_err(|err| {
                E::custom(format!("Failed to parse URL from string: {}: {}", url_str, err))
            })
        }
    }

    let visitor = UrlVisitor;
    deserializer.deserialize_str(visitor)
}

/// JSON structure that contains the sandbox definition defined for builtin packages.
///
/// Path: //builtins.json -> packages[] -> manifest
#[derive(Deserialize)]
pub struct Manifest {
    pub sandbox: Sandbox,
}

/// JSON structure of a cmx file contained in a FAR archive, read via a FAR reader.
///
/// Path: //far_file
#[derive(Deserialize, Serialize)]
pub struct CmxJson {
    pub sandbox: Option<Sandbox>,
}

/// JSON structure of the sandbox defined by the cmx file defining a component.
///
/// Path: //far_file -> sandbox
#[derive(Deserialize, Serialize)]
pub struct Sandbox {
    pub dev: Option<Vec<String>>,
    pub services: Option<Vec<String>>,
    pub system: Option<Vec<String>>,
    pub pkgfs: Option<Vec<String>>,
    pub features: Option<Vec<String>>,
}
