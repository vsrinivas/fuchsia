// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, std::collections::HashMap};

/// Turns a component name + version into a package url of the
/// form 'fuchsia-pkg://fuchsia.com/<component_name>'.
pub fn to_package_url(component_name: &str) -> Result<String> {
    Ok(format!("fuchsia-pkg://fuchsia.com/{}", strip_version(component_name)))
}

fn strip_version(component_name: &str) -> &str {
    component_name.split('/').next().unwrap()
}

/// Parses a meta/contents dictionary string which is expected to
/// be a line-separated list of dictionary entires with an '='
/// delimiting the key and value pairs. No trimming is done on the
/// content. If all lines are invalid, an empty dictionary is
/// returned.
///
/// eg:
///    foo=bar
///    baz=bin
///    version=1
pub fn to_meta_contents_dict(contents: &str) -> HashMap<String, String> {
    let mut dict: HashMap<String, String> = HashMap::new();
    for line_split in contents.split('\n') {
        if line_split.len() == 0 {
            continue;
        }
        if let Some(eq_index) = line_split.find('=') {
            // It's fine to slice from eq_index+1 even if '=' is the last character in the &str,
            // which will give us the empty string.
            dict.insert(
                String::from(&line_split[..eq_index]),
                String::from(&line_split[eq_index + 1..]),
            );
        } else {
            println!("Warning: Did not find a valid line parsing meta/contents dictionary.");
            continue;
        }
    }
    dict
}

#[cfg(test)]
mod tests {

    #[test]
    fn to_package_url_strips_component_version() {
        assert_eq!(
            super::to_package_url("foo_bar/0").unwrap(),
            "fuchsia-pkg://fuchsia.com/foo_bar"
        );
        assert_eq!(super::to_package_url("aries/1").unwrap(), "fuchsia-pkg://fuchsia.com/aries");
        assert_eq!(
            super::to_package_url("vermillion/999999").unwrap(),
            "fuchsia-pkg://fuchsia.com/vermillion"
        );
    }

    #[ignore] // TODO: When we have better input validation, we should reactivate this.
    #[test]
    fn to_package_url_fails_bad_component_name() {
        // Are these actually meant to be errors? I do not know what is a valid component name.
        assert!(super::to_package_url("foo/bar/0").is_err()); // Multiple '/' characters
        assert!(super::to_package_url("000/00").is_err()); // First character of component name is a digit
        assert!(super::to_package_url("aries").is_err()); // No version number or '/'
        assert!(super::to_package_url("vermillion/").is_err()); // No version number but contains trailing '/'
        assert!(super::to_package_url("/0").is_err()); // No component name but contains version number
    }

    #[test]
    fn to_meta_contents_dict_ignores_empty_lines() {
        let dict_str = "foo=bar
        aries=taurus

        pink=purple";
        let dict = super::to_meta_contents_dict(dict_str);
        assert_eq!(dict.len(), 3);
    }

    #[test]
    fn to_meta_contents_dict_with_all_invalid_entries_returns_empty_map() {
        let dict_str = "what";
        let dict = super::to_meta_contents_dict(dict_str);
        assert_eq!(dict.len(), 0);
    }

    #[test]
    fn to_meta_contents_dict_ignores_invalid_lines() {
        // TODO: Right now, anything after the first '=' is taken as the dictionary value
        // Should we enforce that there is only one '=' character when parsing meta/contents?
        let dict_str = "foo=bar
        baz
        azul=blue=青
        aries=taurus";
        let dict = super::to_meta_contents_dict(dict_str);
        assert_eq!(dict.len(), 3);
    }
}
