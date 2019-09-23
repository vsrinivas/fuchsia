// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Deserialization for `.font_sets.json` files.
//!
//! A `.font_sets.json` file is generated per target product using the `write_file` GN rule.

use {
    crate::serde_ext::{self, LoadError},
    manifest::v2::PackageSet,
    serde_derive::Deserialize,
    std::{
        collections::{btree_map::Iter as BTreeMapIter, BTreeMap},
        path::Path,
    },
};

/// Possible versions of [FontSets].
#[derive(Debug, Deserialize)]
#[serde(tag = "version")]
enum FontSetsWrapper {
    #[serde(rename = "1")]
    Version1(FontSetsInternal),
}

/// Classification of font files into "base" and "universe", indicating whether they are included
/// in the base OTA image or only fetched on demand.
#[derive(Debug, Deserialize)]
struct FontSetsInternal {
    /// Font file names in the "base" set. These are included in the base OTA image.
    #[serde(default)]
    base: Vec<String>,
    /// Font file names in the "universe" set. These are fetched on demand from an update server.
    #[serde(default)]
    universe: Vec<String>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
pub(crate) struct FontSets {
    map: BTreeMap<String, PackageSet>,
}

impl From<FontSetsInternal> for FontSets {
    fn from(source: FontSetsInternal) -> Self {
        let mut map = BTreeMap::new();
        let FontSetsInternal { base, universe } = source;
        for file_name in universe {
            map.insert(file_name, PackageSet::Universe);
        }
        // If a file name appears in both Base and Universe, the Base set takes precedence.
        for file_name in base {
            map.insert(file_name, PackageSet::Base);
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

    pub fn get_package_set(&self, file_name: &str) -> Option<&PackageSet> {
        self.map.get(file_name)
    }

    pub fn iter(&self) -> impl Iterator<Item = (&String, &PackageSet)> {
        self.into_iter()
    }
}

impl<'a> IntoIterator for &'a FontSets {
    type Item = (&'a String, &'a PackageSet);
    type IntoIter = BTreeMapIter<'a, String, PackageSet>;

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
            "base": [
                "a.ttf",
                "b.ttf",
                "c.ttf"
            ],
            "universe": [
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

        assert_eq!(font_sets.get_package_set("a.ttf"), Some(&PackageSet::Base));
        assert_eq!(font_sets.get_package_set("b.ttf"), Some(&PackageSet::Base));
        assert_eq!(font_sets.get_package_set("c.ttf"), Some(&PackageSet::Base));
        assert_eq!(font_sets.get_package_set("d.ttf"), Some(&PackageSet::Universe));
        assert_eq!(font_sets.get_package_set("e.ttf"), Some(&PackageSet::Universe));
        assert_eq!(font_sets.get_package_set("f.ttf"), Some(&PackageSet::Universe));
        assert_eq!(font_sets.get_package_set("404.ttf"), None);

        Ok(())
    }
}
