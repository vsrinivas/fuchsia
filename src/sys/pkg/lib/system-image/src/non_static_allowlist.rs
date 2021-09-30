// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::errors::AllowListError,
    fuchsia_pkg::PackageName,
    std::{collections::HashSet, str::FromStr},
};

/// `NonStaticAllowList` is populated from a list of which dynamic packages are allowed
/// to appear in `pkgfs/packages` and allowed to expose executable handles to blobs in
/// `pkgfs/versions`. By default, is located in the `system_image` package at
/// `data/pkgfs_packages_non_static_packages_allowlist.txt`.
#[derive(Debug)]
pub struct NonStaticAllowList {
    contents: HashSet<PackageName>,
}

impl NonStaticAllowList {
    /// Parses a `NonStaticAllowList` from a utf8-encoded list of package names, separated with \n.
    /// Ignores empty lines and lines beginning with '#'.
    pub fn from_reader(source: &[u8]) -> Result<Self, AllowListError> {
        let contents = std::str::from_utf8(source)?
            .split("\n")
            .filter(|line| !(line.is_empty() || line.starts_with("#")))
            .map(PackageName::from_str)
            .collect::<Result<HashSet<_>, _>>()?;
        return Ok(Self { contents });
    }

    /// Get the contents of the allowlist.
    pub fn contents(&self) -> &HashSet<PackageName> {
        &self.contents
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_pkg::PackageNameError, matches::assert_matches};

    fn into_hashset(names: &[&str]) -> HashSet<PackageName> {
        names
            .into_iter()
            .map(|s| PackageName::from_str(*s))
            .collect::<Result<HashSet<_>, _>>()
            .unwrap()
    }

    macro_rules! success_tests {
        ($($test_name:ident: $input:expr, $expected:expr,)*) => {
            $(
                #[test]
                fn $test_name() {
                    let allowlist = NonStaticAllowList::from_reader($input.as_bytes()).unwrap();
                    assert_eq!(allowlist.contents, into_hashset(&$expected));
                }
            )*
        }
    }

    success_tests! {
        empty: "", [],
        single_line: "foobar", ["foobar"],
        multiple_lines: "package-name\nanother-package\n", ["package-name", "another-package"],
        newline_at_beginning: "\nfoobar", ["foobar"],
        newline_at_beginning_empty_allowlist: "\n#comment", [],
        empty_allowlist_with_a_newline_at_eof: "# comment \n", [],
        allowlist_without_a_newline_at_eof: "#Test\nls\ncurl", ["ls", "curl"],
        ignores_empty_lines: "foo\n\nbar\n", ["foo", "bar"],
        allowlist_with_a_comment: "#Test\nls\ncurl\n", ["ls", "curl"],
    }

    #[test]
    fn invalid_package_name() {
        let contents = "invalid!NAME".as_bytes();
        assert_matches!(
            NonStaticAllowList::from_reader(contents),
            Err(AllowListError::PackageName(PackageNameError::InvalidCharacter {
                invalid_name
            })) if invalid_name == "invalid!NAME"
        );
    }

    #[test]
    fn invalid_utf8_encoding() {
        // Invalid utf8 sequence.
        let contents = [0x80];
        assert_matches!(
            NonStaticAllowList::from_reader(&contents),
            Err(AllowListError::Encoding(_))
        );
    }
}
