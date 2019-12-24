// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    char_set::CharSet,
    font_info::{FontAssetSource, FontInfo, FontInfoLoader},
    std::{collections::BTreeSet, convert::TryInto},
};

/// An implementation of [`FakeFontInfoLoader`] that doesn't read the font file, but instead claims
/// that the font file contains exactly the code points that appear in the file name, plus the
/// `index`.
pub(crate) struct FakeFontInfoLoaderImpl {}

impl FakeFontInfoLoaderImpl {
    pub fn new() -> Self {
        FakeFontInfoLoaderImpl {}
    }
}

impl FontInfoLoader for FakeFontInfoLoaderImpl {
    fn load_font_info<S, E>(&self, source: S, index: u32) -> Result<FontInfo, Error>
    where
        S: TryInto<FontAssetSource, Error = E>,
        E: Sync + Send + Into<Error>,
    {
        let source: FontAssetSource = source.try_into().map_err(|e| e.into())?;
        let path = match source {
            FontAssetSource::Stream(_) => {
                return Err(format_err!("FakeFontInfoLoaderImpl only supports file paths"));
            }
            FontAssetSource::FilePath(path) => path,
        };

        let file_name = path.split('/').last().unwrap();
        let mut chars: BTreeSet<u32> = file_name.chars().map(|c| c as u32).collect();
        chars.insert(index);
        let chars: Vec<u32> = chars.into_iter().collect();

        let char_set = CharSet::new(chars);

        Ok(FontInfo { char_set })
    }
}

#[cfg(test)]
mod tests {
    use {super::*, char_collection::char_collect, pretty_assertions::assert_eq};

    #[test]
    fn test_load_font_info() -> Result<(), Error> {
        let loader = FakeFontInfoLoaderImpl::new();
        let source = FontAssetSource::FilePath(
            "DIRECTORY/abcdefghijklmnopqrstuvwxyz-0123456789.ttf".to_string(),
        );

        let actual = loader.load_font_info(source, 0x999)?;
        let expected =
            FontInfo { char_set: char_collect!('a'..='z', '0'..='9', '-', '.', '\u{999}').into() };

        assert_eq!(actual, expected);
        Ok(())
    }
}
