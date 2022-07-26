// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A simple URL parser for gs:// used for Google Cloud Storage (GCS).

use anyhow::{bail, Result};

/// Join one or more entries onto the url path.
///
/// Unlike Url::join, this will include the last path entry, even if no trailing
/// slash is present.
///
/// E.g.
/// extend_url_path("gs://foo/bar", "blah/baz.json") will yield
/// "gs://foo/bar/blah/baz.json". Compare to (with missing "bar/"):
/// Url("gs://foo/bar").join("blah/baz.json") => "gs://foo/blah/baz.json"
pub fn extend_url_path(base: &mut url::Url, add: &str) -> Result<()> {
    base.path_segments_mut()
        .map_err(|_| anyhow::anyhow!("cannot be base"))?
        .pop_if_empty()
        .extend(add.split("/").collect::<Vec<&str>>());
    Ok(())
}

/// Split a url into (bucket, object) tuple.
///
/// Example: `gs://bucket/object/path` will return ("bucket", "object/path").
/// Returns errors for incorrect prefix or missing slash between bucket and
/// object.
pub fn split_gs_url(gs_url: &str) -> Result<(&str, &str)> {
    // Uri will create a local which we cannot return references to:
    //    let url = Uri::try_from(gs_url)
    // A regex is more than needed for this simple format.
    // Instead this simple parser is used: skip past the prefix and find the
    //   third slash.
    const PREFIX: &str = "gs://";
    if !gs_url.starts_with(PREFIX) {
        bail!("A gs url must start with \"{}\". Incorrect: {:?}", PREFIX, gs_url);
    }
    // The `starts_with` above proves this unwrap is infallible.
    let past = gs_url.get(PREFIX.len()..).unwrap();
    if let Some(end_of_bucket) = past.find('/') {
        // The `find` just above proves both these unwraps are infallible.
        Ok((past.get(..end_of_bucket).unwrap(), past.get(end_of_bucket + 1..).unwrap()))
    } else {
        bail!(
            "A gs url requires at least three slashes, \
            e.g. gs://bucket/object. Incorrect: {:?}",
            gs_url
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_split_gs_url() {
        assert_eq!(
            split_gs_url(&"gs://foo/bar/blah".to_string()).expect("bar/blah"),
            ("foo", "bar/blah")
        );
        assert_eq!(split_gs_url(&"gs://foo/".to_string()).expect("empty object"), ("foo", ""));
        assert_eq!(split_gs_url(&"gs:///".to_string()).expect("empty object"), ("", ""));
        assert!(split_gs_url(&"gs://foo".to_string()).is_err());
        assert!(split_gs_url(&"gs://".to_string()).is_err());
        assert!(split_gs_url(&"g".to_string()).is_err());
        assert!(split_gs_url(&"".to_string()).is_err());
    }

    #[test]
    fn test_extend_url_path() {
        let mut my_url = url::Url::parse("http://example.com").expect("url parse");
        let add = "test1";
        extend_url_path(&mut my_url, add).expect("extend url with test1");
        assert_eq!(my_url.as_str(), "http://example.com/test1");
        extend_url_path(&mut my_url, add).expect("extend url with test1");
        assert_eq!(my_url.as_str(), "http://example.com/test1/test1");

        let mut my_url = url::Url::parse("http://example.com/dir1").expect("url parse");
        let add = "dir2/test2";
        extend_url_path(&mut my_url, add).expect("extend url with dir2/test2");
        assert_eq!(my_url.as_str(), "http://example.com/dir1/dir2/test2");

        let mut my_url = url::Url::parse("http://example.com/dir1/").expect("url parse");
        let add = "dir2/test3";
        extend_url_path(&mut my_url, add).expect("extend url with dir2/test3");
        assert_eq!(my_url.as_str(), "http://example.com/dir1/dir2/test3");
    }

    #[should_panic(expected = "cannot be base")]
    #[test]
    fn test_extend_url_path_fail() {
        let mut my_url = url::Url::parse("fake:example").expect("url parse");
        let add = "test1";
        extend_url_path(&mut my_url, add).expect("extend url with test1");
    }
}
