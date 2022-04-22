// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(deprecated)]

use {
    crate::{font_catalog::TypefaceInAssetIndex, serde_ext},
    anyhow::Error,
    manifest::v2,
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
    pub fallback_chain: Vec<SerializedFallbackChainEntry>,
    /// Runtime settings for the font provider service.
    #[serde(default)]
    pub settings: Settings,
}

/// Used solely as a deserialization aid.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Hash)]
#[serde(untagged)]
enum SerializedFallbackChainEntry {
    TypefaceId {
        file_name: String,
        index: Option<u32>,
    },
    FileName(String),
    /// Has a named field to differentiate from FileName.
    FullName {
        full_name: String,
    },
}

impl Into<FallbackChainEntry> for SerializedFallbackChainEntry {
    fn into(self) -> FallbackChainEntry {
        use SerializedFallbackChainEntry::*;
        match self {
            TypefaceId { file_name, index } => FallbackChainEntry::FileNameAndIndex {
                file_name,
                index: index.map(TypefaceInAssetIndex),
            },
            FileName(file_name) => FallbackChainEntry::without_index(file_name),
            FullName { full_name } => FallbackChainEntry::with_full_name(full_name),
        }
    }
}

/// Reference to a typeface (or all typefaces in one file) for use in specifying a fallback order.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Hash)]
#[serde(untagged)]
pub enum FallbackChainEntry {
    FileNameAndIndex {
        /// File name of the asset.
        file_name: String,
        /// Index of the typeface in the file. If absent, treat as all of the typefaces in the file.
        index: Option<TypefaceInAssetIndex>,
    },
    FullName(String),
}

impl FallbackChainEntry {
    /// Creates a new `FallbackChainEntry` representing a single typeface in the given file.
    ///
    /// - `file_name`: The file name of the font asset.
    /// - `index`: The index of the typeface in the given file. See [`TypefaceInAssetIndex`].
    pub(crate) fn with_index(
        file_name: impl Into<String>,
        index: impl Into<TypefaceInAssetIndex>,
    ) -> Self {
        Self::FileNameAndIndex { file_name: file_name.into(), index: Some(index.into()) }
    }

    /// Creates a new `FallbackChainEntry` representing _all_ of the typefaces in the given file.
    ///
    /// - `file_name`: The file name of the font asset.
    pub(crate) fn without_index(file_name: impl Into<String>) -> Self {
        Self::FileNameAndIndex { file_name: file_name.into(), index: None }
    }

    /// Creates a new `FallbackChainEntry` for a unique "full name".
    ///
    /// - `full_name`: The "full name" of the typeface from the font metadata.
    pub(crate) fn with_full_name(full_name: impl Into<String>) -> Self {
        Self::FullName(full_name.into())
    }
}

impl Into<FallbackChainEntry> for v2::TypefaceId {
    fn into(self) -> FallbackChainEntry {
        FallbackChainEntry::with_index(self.file_name, self.index)
    }
}

/// An in-memory representation of a ".fontcfg.json" file.
#[derive(Clone, Debug, Eq, PartialEq, Hash, Default)]
pub struct ProductConfig {
    pub fallback_chain: Vec<FallbackChainEntry>,
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
    pub fn iter_fallback_chain(&self) -> impl Iterator<Item = &FallbackChainEntry> {
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
        use FallbackChainEntry::*;

        let contents = json!(
        {
            "version": "1",
            "fallback_chain": [
                "a.ttf",
                "b.ttf",
                { "file_name": "c.ttf", "index": 1 },
                { "file_name": "d.ttf" },
                { "file_name": "e.ttf", "index": 0 },
                { "full_name": "Gamma Regular" }
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
                FileNameAndIndex { file_name: "a.ttf".to_string(), index: None },
                FileNameAndIndex { file_name: "b.ttf".to_string(), index: None },
                FileNameAndIndex {
                    file_name: "c.ttf".to_string(),
                    index: Some(TypefaceInAssetIndex(1)),
                },
                FileNameAndIndex { file_name: "d.ttf".to_string(), index: None },
                FileNameAndIndex {
                    file_name: "e.ttf".to_string(),
                    index: Some(TypefaceInAssetIndex(0)),
                },
                FullName("Gamma Regular".to_string()),
            ],
            settings: Settings { cache_size_bytes: Some(5_000_123) },
        };

        assert_eq!(actual, expected);

        Ok(())
    }
}
