// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::typeface::{Collection as TypefaceCollection, TypefaceInfoAndCharSet},
    fidl_fuchsia_fonts::GenericFontFamily,
    unicase::UniCase,
};

#[derive(Debug)]
pub enum FamilyOrAlias {
    Family(FontFamily),
    /// Represents an alias to a `Family` whose name is the associated [`UniCase`]`<`[`String`]`>`.
    Alias(UniCase<String>),
}

#[derive(Debug)]
pub struct FontFamily {
    pub name: String,
    pub faces: TypefaceCollection,
    pub generic_family: Option<GenericFontFamily>,
}

impl FontFamily {
    pub fn new(name: String, generic_family: Option<GenericFontFamily>) -> FontFamily {
        FontFamily { name, faces: TypefaceCollection::new(), generic_family }
    }

    /// Get owned copies of the family's typefaces as `TypefaceInfo`
    pub fn extract_faces<'a>(&'a self) -> impl Iterator<Item = TypefaceInfoAndCharSet> + 'a {
        // Convert Vec<Arc<Typeface>> to Vec<TypefaceInfo>
        self.faces
            .faces
            .iter()
            // Copy most fields from `Typeface` and use the canonical family name
            .map(move |face| TypefaceInfoAndCharSet::from_typeface(face, self.name.clone()))
    }
}
