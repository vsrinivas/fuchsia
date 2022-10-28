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
#[derive(Debug, PartialEq, Eq)]
pub struct NonStaticAllowList {
    contents: HashSet<PackageName>,
}

impl NonStaticAllowList {
    /// Parses a `NonStaticAllowList` from a utf8-encoded list of package names, separated with \n.
    /// Ignores empty lines and lines beginning with '#'.
    pub fn parse(source: &[u8]) -> Result<Self, AllowListError> {
        let contents = std::str::from_utf8(source)?
            .split('\n')
            .filter(|line| !(line.is_empty() || line.starts_with('#')))
            .map(PackageName::from_str)
            .collect::<Result<HashSet<_>, _>>()?;
        Ok(Self { contents })
    }

    /// Create an empty allow _list.
    pub fn empty() -> Self {
        Self { contents: HashSet::new() }
    }

    /// Determines if this allowlist allows the given package.
    pub fn allows(&self, name: &PackageName) -> bool {
        self.contents.contains(name)
    }

    /// Record the contents of the allow list in the provided `node`.
    pub fn record_inspect(&self, node: &fuchsia_inspect::Node) {
        for name in &self.contents {
            node.record_string(name.as_ref(), "");
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches, fuchsia_pkg::PackagePathSegmentError};

    fn into_hashset(names: &[&str]) -> HashSet<PackageName> {
        names.iter().map(|s| PackageName::from_str(s)).collect::<Result<HashSet<_>, _>>().unwrap()
    }

    fn allowlist_allows(allowlist: &NonStaticAllowList, name: &str) -> bool {
        let name = PackageName::from_str(name).unwrap();
        allowlist.allows(&name)
    }

    macro_rules! success_tests {
        ($($test_name:ident: $input:expr, $expected:expr,)*) => {
            $(
                #[test]
                fn $test_name() {
                    let allowlist = NonStaticAllowList::parse($input.as_bytes()).unwrap();
                    let expected = into_hashset(&$expected);
                    for name in &expected {
                        assert!(allowlist.allows(name));
                    }
                    assert_eq!(allowlist.contents, expected);
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
    fn allows_is_contains() {
        let allowlist = NonStaticAllowList { contents: into_hashset(&["foo", "bar"]) };
        assert!(allowlist_allows(&allowlist, "foo"));
        assert!(allowlist_allows(&allowlist, "bar"));

        assert!(!allowlist_allows(&allowlist, "foobar"));
        assert!(!allowlist_allows(&allowlist, "baz"));
    }

    #[test]
    fn invalid_package_name() {
        let contents = "invalid!NAME".as_bytes();
        assert_matches!(
            NonStaticAllowList::parse(contents),
            Err(AllowListError::PackageName(PackagePathSegmentError::InvalidCharacter {
                character
            })) if character == '!'
        );
    }

    #[test]
    fn invalid_utf8_encoding() {
        // Invalid utf8 sequence.
        let contents = [0x80];
        assert_matches!(NonStaticAllowList::parse(&contents), Err(AllowListError::Encoding(_)));
    }

    #[test]
    fn record_inspect() {
        let inspector = fuchsia_inspect::Inspector::new();
        let allow_list = NonStaticAllowList::parse(b"some-name\nother-name").unwrap();

        allow_list.record_inspect(inspector.root());

        fuchsia_inspect::assert_data_tree!(inspector, root: {
            "some-name": "",
            "other-name": ""
        });
    }
}
