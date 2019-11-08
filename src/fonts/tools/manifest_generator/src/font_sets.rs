// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Deserialization for `.font_sets.json` files.
//!
//! A `.font_sets.json` file is generated per target product using the `write_file` GN rule.

use {
    crate::serde_ext::{self, LoadError},
    serde_derive::Deserialize,
    std::{
        collections::{btree_map::Iter as BTreeMapIter, BTreeMap},
        path::Path,
    },
};

/// Describes which set a font belongs to, local or downloadable.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Hash)]
#[serde(rename_all = "lowercase")]
pub(crate) enum FontSet {
    /// Font files in the local set. Bundled directly in the font server's `/config/data`.
    Local,
    /// Available to download as a Fuchsia package (`fuchsia-pkg://fuchsia.com/font-package-...`).
    Download,
}

/// Possible versions of [FontSets].
#[derive(Debug, Deserialize)]
#[serde(tag = "version")]
enum FontSetsWrapper {
    #[serde(rename = "1")]
    Version1(FontSetsInternal),
}

/// Classification of font files into "local" and "downloadable", indicating whether they are
/// built directly into the font server package in the OTA image or only fetched on demand.
#[derive(Debug, Deserialize)]
struct FontSetsInternal {
    /// See ['FontSet::Local`]
    local: Vec<String>,
    /// See [`FontSet::Download`]
    #[serde(default)]
    download: Vec<String>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
pub(crate) struct FontSets {
    map: BTreeMap<String, FontSet>,
}

impl From<FontSetsInternal> for FontSets {
    fn from(source: FontSetsInternal) -> Self {
        let mut map = BTreeMap::new();
        let FontSetsInternal { local, download } = source;
        for file_name in download {
            map.insert(file_name, FontSet::Download);
        }
        // If a file name appears in both Local and Download, the Local set takes precedence.
        for file_name in local {
            map.insert(file_name, FontSet::Local);
        }
        FontSets { map }
    }
}

impl FontSets {
    pub fn load_from_path<T: AsRef<Path>>(path: T) -> Result<FontSets, LoadError> {
        match serde_ext::load_from_path(path) {
            Ok(FontSetsWrapper::Version1(font_sets_internal)) => Ok(font_sets_internal.into()),
            Err(err) => Err(err),
        }
    }

    pub fn get_font_set(&self, file_name: &str) -> Option<&FontSet> {
        self.map.get(file_name)
    }

    pub fn iter(&self) -> impl Iterator<Item = (&String, &FontSet)> {
        self.into_iter()
    }
}

impl<'a> IntoIterator for &'a FontSets {
    type Item = (&'a String, &'a FontSet);
    type IntoIter = BTreeMapIter<'a, String, FontSet>;

    fn into_iter(self) -> Self::IntoIter {
        self.map.iter()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, failure::Error, pretty_assertions::assert_eq, serde_json::json, std::io::Write,
        tempfile::NamedTempFile,
    };

    #[test]
    fn test_load_from_path() -> Result<(), Error> {
        let contents = json!(
        {
            "version": "1",
            "local": [
                "a.ttf",
                "b.ttf",
                "c.ttf"
            ],
            "download": [
                "c.ttf",
                "d.ttf",
                "e.ttf",
                "f.ttf"
            ]
        }
        )
        .to_string();

        let mut file = NamedTempFile::new()?;
        file.write_all(contents.as_bytes())?;

        let font_sets = FontSets::load_from_path(file.path())?;

        assert_eq!(font_sets.get_font_set("a.ttf"), Some(&FontSet::Local));
        assert_eq!(font_sets.get_font_set("b.ttf"), Some(&FontSet::Local));
        assert_eq!(font_sets.get_font_set("c.ttf"), Some(&FontSet::Local));
        assert_eq!(font_sets.get_font_set("d.ttf"), Some(&FontSet::Download));
        assert_eq!(font_sets.get_font_set("e.ttf"), Some(&FontSet::Download));
        assert_eq!(font_sets.get_font_set("f.ttf"), Some(&FontSet::Download));
        assert_eq!(font_sets.get_font_set("404.ttf"), None);

        Ok(())
    }
}
