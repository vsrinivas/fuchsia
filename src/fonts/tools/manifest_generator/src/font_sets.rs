// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Deserialization for pairs of `.all_fonts.json` and `.local_fonts.json` files.
//!
//! A pair of these files is generated per target product using the `generated_file` GN rule (see
//! "//src/fonts/build/fonts.gni").

use {
    crate::serde_ext::{self, LoadError},
    serde_derive::Deserialize,
    std::{
        collections::{btree_map::Iter as BTreeMapIter, BTreeMap, BTreeSet},
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

#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
pub(crate) struct FontSets {
    map: BTreeMap<String, FontSet>,
}

impl FontSets {
    pub fn load_from_all_and_local_paths<T: AsRef<Path>>(
        all_path: T,
        local_path: T,
    ) -> Result<FontSets, LoadError> {
        let all_file_names: BTreeSet<String> = serde_ext::load_from_path(all_path)?;
        let local_file_names: BTreeSet<String> = serde_ext::load_from_path(local_path)?;

        let mut map = BTreeMap::new();
        for file_name in all_file_names {
            map.insert(file_name, FontSet::Download);
        }
        // The Local set takes precedence.
        for file_name in local_file_names {
            map.insert(file_name, FontSet::Local);
        }

        Ok(FontSets { map })
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
    fn test_load_from_all_and_local_paths() -> Result<(), Error> {
        let all_fonts_contents =
            json!(["a.ttf", "b.ttf", "c.ttf", "d.ttf", "e.ttf", "f.ttf"]).to_string();
        let mut all_fonts_file = NamedTempFile::new()?;
        all_fonts_file.write_all(all_fonts_contents.as_bytes())?;

        let local_fonts_contents = json!(["a.ttf", "b.ttf", "c.ttf"]).to_string();
        let mut local_fonts_file = NamedTempFile::new()?;
        local_fonts_file.write_all(local_fonts_contents.as_bytes())?;

        let font_sets = FontSets::load_from_all_and_local_paths(
            all_fonts_file.path(),
            local_fonts_file.path(),
        )?;

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
