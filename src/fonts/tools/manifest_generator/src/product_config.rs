// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(deprecated)]

use {
    crate::{font_catalog::TypefaceInAssetIndex, serde_ext},
    anyhow::Error,
    serde::Deserialize,
    std::path::Path,
};

/// Serializable representation of a ".fontcfg.json" file, which includes fallback chains.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
#[serde(tag = "version")]
enum ProductConfigWrapper {
    #[serde(rename = "1")]
    Version1(ProductConfigInternal),
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Hash)]
struct ProductConfigInternal {
    /// A sequence of typeface identifiers representing a fallback chain.
    pub fallback_chain: Vec<TypefaceIdOrFileName>,
    /// Runtime settings for the font provider service.
    #[serde(default)]
    pub settings: Settings,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Hash)]
#[serde(untagged)]
enum TypefaceIdOrFileName {
    TypefaceId(TypefaceId),
    FileName(String),
}

impl Into<TypefaceId> for TypefaceIdOrFileName {
    fn into(self) -> TypefaceId {
        match self {
            TypefaceIdOrFileName::TypefaceId(typeface_id) => typeface_id,
            TypefaceIdOrFileName::FileName(file_name) => TypefaceId { file_name, index: None },
        }
    }
}

/// Reference to a typeface, for use in specifying a fallback order.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Hash)]
pub struct TypefaceId {
    /// File name of the asset.
    pub file_name: String,
    /// Index of the typeface in the file. If absent, treat as all of the typefaces in the file.
    pub index: Option<TypefaceInAssetIndex>,
}

/// An in-memory representation of a ".fontcfg.json" file.
#[derive(Clone, Debug, Eq, PartialEq, Hash, Default)]
pub struct ProductConfig {
    pub fallback_chain: Vec<TypefaceId>,
    pub settings: Settings,
}

impl ProductConfig {
    /// Loads the `ProductConfig` from a ".fontcfg.json" or ".fontcfg.json5" file.
    pub fn load_from_path<P: AsRef<Path>>(path: P) -> Result<Self, Error> {
        let wrapper: ProductConfigWrapper = serde_ext::load_from_path(path)?;
        match wrapper {
            ProductConfigWrapper::Version1(ProductConfigInternal { fallback_chain, settings }) => {
                let fallback_chain = fallback_chain
                    .into_iter()
                    .map(|id_or_filename| id_or_filename.into())
                    .collect();
                Ok(ProductConfig { fallback_chain, settings })
            }
        }
    }

    /// Iterates over the `TypefaceId`s in the configuration's font fallback chain.
    pub fn iter_fallback_chain(&self) -> impl Iterator<Item = &TypefaceId> {
        self.fallback_chain.iter()
    }
}

/// Runtime settings for the font provider service.
#[derive(Clone, Debug, Eq, PartialEq, Hash, Default, Deserialize)]
pub struct Settings {
    /// Maximum size of the font service's in-memory asset cache, in bytes.
    pub cache_size_bytes: Option<u64>,
}

#[cfg(test)]
mod tests {

    use {
        super::*, anyhow::Error, pretty_assertions::assert_eq, serde_json::json, std::io::Write,
        tempfile::NamedTempFile,
    };

    #[test]
    fn test_load_from_path() -> Result<(), Error> {
        let contents = json!(
        {
            "version": "1",
            "fallback_chain": [
                "a.ttf",
                "b.ttf",
                { "file_name": "c.ttf", "index": 1 },
                { "file_name": "d.ttf" },
                { "file_name": "e.ttf", "index": 0 }
            ],
            "settings": {
                "cache_size_bytes": 5000123
            }
        })
        .to_string();
        let mut file = NamedTempFile::new()?;
        file.write_all(contents.as_bytes())?;

        let actual = ProductConfig::load_from_path(file.path())?;
        let expected = ProductConfig {
            fallback_chain: vec![
                TypefaceId { file_name: "a.ttf".to_string(), index: None },
                TypefaceId { file_name: "b.ttf".to_string(), index: None },
                TypefaceId { file_name: "c.ttf".to_string(), index: Some(TypefaceInAssetIndex(1)) },
                TypefaceId { file_name: "d.ttf".to_string(), index: None },
                TypefaceId { file_name: "e.ttf".to_string(), index: Some(TypefaceInAssetIndex(0)) },
            ],
            settings: Settings { cache_size_bytes: Some(5_000_123) },
        };

        assert_eq!(actual, expected);

        Ok(())
    }
}
