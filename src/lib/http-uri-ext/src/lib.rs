// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use http::uri::{self, Uri};

pub trait HttpUriExt {
    /// Normalizes empty paths to `/`, appends `/` to `self`'s path if it does not end with one,
    /// then appends `path`, preserving any query parameters. Does nothing if `path` is the empty
    /// string.
    ///
    /// Will only error if asked to add a path to a `Uri` without a scheme (because `Uri` requires
    /// a scheme if a path is present), or if `path` contains invalid URI characters.
    fn extend_dir_with_path(self, path: &str) -> Result<Uri, Error>;
}

impl HttpUriExt for Uri {
    fn extend_dir_with_path(self, path: &str) -> Result<Uri, Error> {
        if path.is_empty() {
            return Ok(self);
        }
        let mut base_parts = self.into_parts();
        let (base_path, query) = match &base_parts.path_and_query {
            Some(path_and_query) => (path_and_query.path(), path_and_query.query()),
            None => ("/", None),
        };
        let new_path_and_query = if base_path.ends_with("/") {
            if let Some(query) = query {
                format!("{}{}?{}", base_path, path, query)
            } else {
                format!("{}{}", base_path, path)
            }
        } else {
            if let Some(query) = query {
                format!("{}/{}?{}", base_path, path, query)
            } else {
                format!("{}/{}", base_path, path)
            }
        };
        base_parts.path_and_query = Some(new_path_and_query.parse()?);
        Ok(Uri::from_parts(base_parts)?)
    }
}

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("invalid uri: {0}")]
    InvalidUri(#[from] uri::InvalidUri),
    #[error("invalid uri parts: {0}")]
    InvalidUriParts(#[from] uri::InvalidUriParts),
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_uri_from_path_and_query(path_and_query: Option<&str>) -> Uri {
        let mut parts = uri::Parts::default();
        parts.path_and_query = path_and_query.map(|p| p.parse().unwrap());
        Uri::from_parts(parts).unwrap()
    }

    fn assert_expected_path(base: Option<&str>, added: &str, expected: Option<&str>) {
        let uri = make_uri_from_path_and_query(base).extend_dir_with_path(added).unwrap();
        assert_eq!(
            uri.into_parts().path_and_query.map(|p| p.to_string()),
            expected.map(|s| s.to_string())
        );
    }

    #[test]
    fn no_query_empty_argument() {
        assert_expected_path(None, "", None);
        assert_expected_path(Some(""), "", None);
        assert_expected_path(Some("/"), "", Some("/"));
        assert_expected_path(Some("/a"), "", Some("/a"));
        assert_expected_path(Some("/a/"), "", Some("/a/"));
    }

    #[test]
    fn has_query_empty_argument() {
        assert_expected_path(Some("?k=v"), "", Some("/?k=v"));
        assert_expected_path(Some("/?k=v"), "", Some("/?k=v"));
        assert_expected_path(Some("/a?k=v"), "", Some("/a?k=v"));
        assert_expected_path(Some("/a/?k=v"), "", Some("/a/?k=v"));
    }

    #[test]
    fn no_query_has_argument() {
        assert_expected_path(None, "c", Some("/c"));
        assert_expected_path(Some(""), "c", Some("/c"));
        assert_expected_path(Some("/"), "c", Some("/c"));
        assert_expected_path(Some("/a"), "c", Some("/a/c"));
        assert_expected_path(Some("/a/"), "c", Some("/a/c"));
    }

    #[test]
    fn has_query_has_argument() {
        assert_expected_path(Some("?k=v"), "c", Some("/c?k=v"));
        assert_expected_path(Some("/?k=v"), "c", Some("/c?k=v"));
        assert_expected_path(Some("/a?k=v"), "c", Some("/a/c?k=v"));
        assert_expected_path(Some("/a/?k=v"), "c", Some("/a/c?k=v"));
    }
}
