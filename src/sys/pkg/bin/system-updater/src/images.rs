// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};

fn parse_images(buf: impl AsRef<str>) -> Vec<String> {
    buf.as_ref()
        .split('\n')
        .filter_map(|line| {
            let line = line.trim();
            if line.is_empty() {
                return None;
            }
            Some(line.to_owned())
        })
        .collect()
}

/// Read and parse the static images file indicating which images should be paved, if they are
/// present in the update package.
pub async fn load_image_list() -> Result<Vec<String>, Error> {
    let file = io_util::file::open_in_namespace("/pkg/data/images", io_util::OPEN_RIGHT_READABLE)
        .context("open /pkg/data/images")?;

    let buf = io_util::file::read_to_string(&file).await.context("read /pkg/data/images")?;

    Ok(parse_images(buf))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_single_image() {
        let expected = vec!["foo".to_owned()];

        assert_eq!(parse_images("foo"), expected);
        assert_eq!(parse_images("foo\n"), expected);
    }

    #[test]
    fn parse_multiple_preserves_order() {
        let expected = vec!["foo".to_owned(), "bar".to_owned(), "baz".to_owned()];

        assert_eq!(parse_images("foo\nbar\nbaz"), expected);
        assert_eq!(parse_images("foo\nbar\nbaz\n"), expected);
    }

    #[test]
    fn parse_trims_empty_lines() {
        let expected = vec!["foo".to_owned(), "bar".to_owned(), "baz".to_owned()];

        assert_eq!(parse_images("foo\nbar\n\nbaz"), expected);
    }

    #[test]
    fn parse_trims_whitespace() {
        let expected = vec!["hello".to_owned(), "world".to_owned()];

        assert_eq!(parse_images("  hello\t  \nworld     \n"), expected);
    }

    #[test]
    fn parse_trims_whitespace_lines() {
        let expected = vec!["hello".to_owned(), "world".to_owned()];

        assert_eq!(parse_images(" \n\t\nhello\n \nworld"), expected);
    }

    #[test]
    fn parse_actual_images_contents() {
        let contents = r#"bootloader
firmware[_type]
zbi
zbi.signed
fuchsia.vbmeta
zedboot
zedboot.signed
recovery.vbmeta
"#;

        assert_eq!(
            parse_images(contents),
            vec![
                "bootloader".to_owned(),
                "firmware[_type]".to_owned(),
                "zbi".to_owned(),
                "zbi.signed".to_owned(),
                "fuchsia.vbmeta".to_owned(),
                "zedboot".to_owned(),
                "zedboot.signed".to_owned(),
                "recovery.vbmeta".to_owned()
            ]
        );
    }
}
