// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Returns an inspect path representation given a prefix file path and a sorted
/// path of node hierarchy subproperties.
///
/// Example:
///     format_parts("/some/path/root.inspect", vec!["root", "a", "b"]) will return
///     "/some/path/root.inspect#a/b"
///
pub fn format_parts(file_path: &str, parts: &[String]) -> String {
    if parts.is_empty() || (parts[0] == "root" && parts.len() == 1) {
        file_path.to_string()
    } else if parts[0] == "root" {
        format!("{}#{}", file_path, parts[1..].join("/"))
    } else {
        format!("{}#{}", file_path, parts.join("/"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_format_parts() {
        let path = "/my/inspect_file/path";
        assert_eq!(format_parts(path, &vec![]), path);
        assert_eq!(format_parts(path, &vec!["root".to_string()]), path);
        assert_eq!(
            format_parts(path, &vec!["root".to_string(), "some_node".to_string()]),
            format!("{}#some_node", path)
        );
        assert_eq!(
            format_parts(
                path,
                &vec!["root".to_string(), "some_node".to_string(), "some_property".to_string()]
            ),
            format!("{}#some_node/some_property", path)
        );
    }
}
