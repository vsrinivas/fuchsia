// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type that represents a path in a file system.  Objects of this type own a String that holds the
//! "full" path and provide an iterator that goes over individual components of the path.  This
//! approach is used to allow passing the path string, from one `open()` method to the next,
//! without the need to copy the path itself.

use {
    fidl_fuchsia_io::{MAX_FILENAME, MAX_PATH},
    fuchsia_zircon::Status,
};

#[derive(Clone, Debug)]
pub struct Path {
    is_dir: bool,
    inner: String,
    next: usize,
}

impl Path {
    /// Returns a path for dot, i.e. the current directory.  The `next` will return None for this
    /// path.
    pub fn dot() -> Path {
        // We use an empty string to avoid allocations.  as_ref() handles this correctly below and
        // will return ".".
        Path { is_dir: false, inner: String::new(), next: 0 }
    }

    pub fn is_dot(&self) -> bool {
        self.inner.is_empty()
    }

    /// Splits a `path` string into components, also checking if it is in a canonical form,
    /// disallowing any ".." components, as well as empty component names.  A lone "/" is translated
    /// to "." (the canonical form).
    pub fn validate_and_split<Source>(path: Source) -> Result<Path, Status>
    where
        Source: Into<String>,
    {
        let path = path.into();

        // Make sure that we don't accept paths longer than POSIX's PATH_MAX, plus one character
        // which accounts for the null terminator.
        if (path.len() as u64) > std::cmp::min(MAX_PATH, libc::PATH_MAX as u64 - 1) {
            return Err(Status::BAD_PATH);
        }

        match path.as_str() {
            "." | "/" => Ok(Self::dot()),
            _ => {
                let is_dir = path.ends_with('/');

                // Allow a leading slash.
                let next = if path.len() > 1 && path.starts_with('/') { 1 } else { 0 };

                let mut check = path[next..].split('/');

                // Allow trailing slash to indicate a directory.
                if is_dir {
                    let _ = check.next_back();
                }

                // Disallow empty components, ".", and ".."s.  Path is expected to be
                // canonicalized.  See fxbug.dev/28436 for discussion of empty components.
                for c in check {
                    if c.is_empty() || c == ".." || c == "." {
                        return Err(Status::INVALID_ARGS);
                    }
                    if c.len() as u64 > MAX_FILENAME {
                        return Err(Status::BAD_PATH);
                    }
                }

                Ok(Path { is_dir, inner: path, next })
            }
        }
    }

    /// Returns `true` when there are no more compoenents left in this `Path`.
    pub fn is_empty(&self) -> bool {
        self.next >= self.inner.len()
    }

    /// Returns `true` if the canonical path contains '/' as the last symbol.  Note that is_dir is
    /// `false` for ".", even though a directory is implied.  The canonical form for "/" is ".".
    pub fn is_dir(&self) -> bool {
        self.is_dir
    }

    /// Returns `true` when the path contains only one component - that is, it is not empty and
    /// contains no `/` characters.
    pub fn is_single_component(&self) -> bool {
        let end = if self.is_dir { self.inner.len() - 1 } else { self.inner.len() };
        self.next < self.inner.len() && self.inner[self.next..end].find('/').is_none()
    }

    /// Returns a reference to a portion of the string that names the next component, and move the
    /// internal pointer to point to the next component.  See also [`Path::peek()`].
    ///
    /// Also see [`Path::next_with_ref()`] if you want to use `self` while holding a reference to
    /// the returned name.
    pub fn next(&mut self) -> Option<&str> {
        self.next_with_ref().1
    }

    /// Rust does not allow usage of `self` while the returned reference is alive, even when the
    /// reference is actually shared.  See, for example,
    ///
    ///     https://internals.rust-lang.org/t/relaxing-the-borrow-checker-for-fn-mut-self-t/3256
    ///
    /// for additional details.  So if the caller wants to call any other methods on the `path`
    /// after calling `next()` while still holding a reference to the returned name they can use
    /// this method as a workaround.  When Rust is extended to cover this use case, `next_with_ref`
    /// should be merged into [`Self::next()`].
    pub fn next_with_ref(&mut self) -> (&Self, Option<&str>) {
        match self.inner[self.next..].find('/') {
            Some(i) => {
                let from = self.next;
                self.next = self.next + i + 1;
                (self, Some(&self.inner[from..from + i]))
            }
            None => {
                if self.next >= self.inner.len() {
                    (self, None)
                } else {
                    let from = self.next;
                    self.next = self.inner.len();
                    (self, Some(&self.inner[from..]))
                }
            }
        }
    }

    /// Returns a reference to a position of the string that names the next component, without
    /// moving the internal pointer.  So calling `peek()` multiple times in a row would return the
    /// same result.  See also [`Self::next()`].
    pub fn peek(&self) -> Option<&str> {
        match self.inner[self.next..].find('/') {
            Some(i) => Some(&self.inner[self.next..self.next + i]),
            None => {
                if self.next >= self.inner.len() {
                    None
                } else {
                    Some(&self.inner[self.next..])
                }
            }
        }
    }

    /// Converts this `Path` into a `String` holding the rest of the path.  Note that if there are
    /// no more components, this will return an empty string, which is *not* a valid path for
    /// fuchsia.io.
    pub fn into_string(mut self) -> String {
        self.inner.drain(0..self.next);
        self.inner
    }

    /// Like `into_string` but returns a reference and the path returned is valid for fuchsia.io
    /// i.e. if there are no remaining components, "." is returned.
    fn remainder(&self) -> &str {
        if self.is_empty() {
            "."
        } else {
            &self.inner[self.next..]
        }
    }
}

impl PartialEq for Path {
    fn eq(&self, other: &Self) -> bool {
        self.remainder() == other.remainder()
    }
}
impl Eq for Path {}

impl AsRef<str> for Path {
    fn as_ref(&self) -> &str {
        self.remainder()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    macro_rules! simple_construction_test {
        (path: $str:expr, $path:ident => $body:block) => {
            match Path::validate_and_split($str) {
                Ok($path) => $body,
                Err(status) => panic!("'{}' construction failed: {}", stringify!(path), status),
            }
        };
        (path: $str:expr, mut $path:ident => $body:block) => {
            match Path::validate_and_split($str) {
                Ok(mut $path) => $body,
                Err(status) => panic!("'{}' construction failed: {}", stringify!(path), status),
            }
        };
    }

    macro_rules! negative_construction_test {
        (path: $path:expr, $details:expr, $status:expr) => {
            match Path::validate_and_split($path) {
                Ok(path) => {
                    panic!("Constructed '{}' with {}: {:?}", stringify!($path), $details, path)
                }
                Err(status) => assert_eq!(status, $status),
            }
        };
    }

    fn path(s: &str) -> Path {
        match Path::validate_and_split(s) {
            Ok(path) => path,
            Err(e) => panic!("'{}' construction failed: {}", s, e),
        }
    }

    #[test]
    fn empty() {
        negative_construction_test! {
            path: "",
            "empty path",
            Status::INVALID_ARGS
        };
    }

    #[test]
    fn forward_slash_only() {
        simple_construction_test! {
            path: "/",
            mut path => {
                assert!(path.is_empty());
                assert!(!path.is_dir());  // It is converted into ".".
                assert!(!path.is_single_component());
                assert_eq!(path.as_ref(), ".");
                assert_eq!(path.peek(), None);
                assert_eq!(path.next(), None);
                assert_eq!(path.as_ref(), ".");
                assert_eq!(path.into_string(), String::new());
            }
        };
    }

    #[test]
    fn one_component_short() {
        simple_construction_test! {
            path: "a",
            mut path => {
                assert!(!path.is_empty());
                assert!(!path.is_dir());
                assert!(path.is_single_component());
                assert_eq!(path.peek(), Some("a"));
                assert_eq!(path.peek(), Some("a"));
                assert_eq!(path.next(), Some("a"));
                assert_eq!(path.peek(), None);
                assert_eq!(path.next(), None);
                assert_eq!(path.as_ref(), ".");
                assert_eq!(path.into_string(), String::new());
            }
        };
    }

    #[test]
    fn one_component() {
        simple_construction_test! {
            path: "some",
            mut path => {
                assert!(!path.is_empty());
                assert!(!path.is_dir());
                assert!(path.is_single_component());
                assert_eq!(path.peek(), Some("some"));
                assert_eq!(path.peek(), Some("some"));
                assert_eq!(path.next(), Some("some"));
                assert_eq!(path.peek(), None);
                assert_eq!(path.next(), None);
                assert_eq!(path.as_ref(), ".");
                assert_eq!(path.into_string(), String::new());
            }
        };
    }

    #[test]
    fn one_component_dir() {
        simple_construction_test! {
            path: "some/",
            mut path => {
                assert!(!path.is_empty());
                assert!(path.is_dir());
                assert!(path.is_single_component());
                assert_eq!(path.peek(), Some("some"));
                assert_eq!(path.peek(), Some("some"));
                assert_eq!(path.next(), Some("some"));
                assert_eq!(path.peek(), None);
                assert_eq!(path.next(), None);
                assert_eq!(path.as_ref(), ".");
                assert_eq!(path.into_string(), String::new());
            }
        };
    }

    #[test]
    fn two_component_short() {
        simple_construction_test! {
            path: "a/b",
            mut path => {
                assert!(!path.is_empty());
                assert!(!path.is_dir());
                assert!(!path.is_single_component());
                assert_eq!(path.peek(), Some("a"));
                assert_eq!(path.peek(), Some("a"));
                assert_eq!(path.next(), Some("a"));
                assert_eq!(path.peek(), Some("b"));
                assert_eq!(path.peek(), Some("b"));
                assert_eq!(path.next(), Some("b"));
                assert_eq!(path.peek(), None);
                assert_eq!(path.next(), None);
                assert_eq!(path.as_ref(), ".");
                assert_eq!(path.into_string(), String::new());
            }
        };
    }

    #[test]
    fn two_component() {
        simple_construction_test! {
            path: "some/path",
            mut path => {
                assert!(!path.is_empty());
                assert!(!path.is_dir());
                assert!(!path.is_single_component());
                assert_eq!(path.peek(), Some("some"));
                assert_eq!(path.peek(), Some("some"));
                assert_eq!(path.next(), Some("some"));
                assert_eq!(path.peek(), Some("path"));
                assert_eq!(path.peek(), Some("path"));
                assert_eq!(path.next(), Some("path"));
                assert_eq!(path.peek(), None);
                assert_eq!(path.next(), None);
                assert_eq!(path.as_ref(), ".");
                assert_eq!(path.into_string(), String::new());
            }
        };
    }

    #[test]
    fn two_component_dir() {
        simple_construction_test! {
            path: "some/path/",
            mut path => {
                assert!(!path.is_empty());
                assert!(path.is_dir());
                assert!(!path.is_single_component());
                assert_eq!(path.peek(), Some("some"));
                assert_eq!(path.peek(), Some("some"));
                assert_eq!(path.next(), Some("some"));
                assert_eq!(path.peek(), Some("path"));
                assert_eq!(path.peek(), Some("path"));
                assert_eq!(path.next(), Some("path"));
                assert_eq!(path.peek(), None);
                assert_eq!(path.next(), None);
                assert_eq!(path.as_ref(), ".");
                assert_eq!(path.into_string(), String::new());
            }
        };
    }

    #[test]
    fn into_string_half_way() {
        simple_construction_test! {
            path: "into/string/half/way",
            mut path => {
                assert!(!path.is_empty());
                assert!(!path.is_dir());
                assert!(!path.is_single_component());
                assert_eq!(path.peek(), Some("into"));
                assert_eq!(path.peek(), Some("into"));
                assert_eq!(path.next(), Some("into"));
                assert_eq!(path.peek(), Some("string"));
                assert_eq!(path.peek(), Some("string"));
                assert_eq!(path.next(), Some("string"));
                assert_eq!(path.peek(), Some("half"));
                assert_eq!(path.peek(), Some("half"));
                assert_eq!(path.as_ref(), "half/way");
                assert_eq!(path.into_string(), "half/way".to_string());
            }
        };
    }

    #[test]
    fn into_string_half_way_dir() {
        simple_construction_test! {
            path: "into/string/half/way/",
            mut path => {
                assert!(!path.is_empty());
                assert!(path.is_dir());
                assert!(!path.is_single_component());
                assert_eq!(path.peek(), Some("into"));
                assert_eq!(path.peek(), Some("into"));
                assert_eq!(path.next(), Some("into"));
                assert_eq!(path.peek(), Some("string"));
                assert_eq!(path.peek(), Some("string"));
                assert_eq!(path.next(), Some("string"));
                assert_eq!(path.peek(), Some("half"));
                assert_eq!(path.peek(), Some("half"));
                assert_eq!(path.as_ref(), "half/way/");
                assert_eq!(path.into_string(), "half/way/".to_string());
            }
        };
    }

    #[test]
    fn into_string_dir_last_component() {
        simple_construction_test! {
            path: "into/string/",
            mut path => {
                assert!(!path.is_empty());
                assert!(path.is_dir());
                assert!(!path.is_single_component());
                assert_eq!(path.peek(), Some("into"));
                assert_eq!(path.peek(), Some("into"));
                assert_eq!(path.next(), Some("into"));
                assert_eq!(path.peek(), Some("string"));
                assert_eq!(path.peek(), Some("string"));
                assert_eq!(path.next(), Some("string"));
                assert_eq!(path.peek(), None);
                assert_eq!(path.as_ref(), ".");
                assert_eq!(path.into_string(), "".to_string());
            }
        };
    }

    #[test]
    fn no_empty_components() {
        negative_construction_test! {
            path: "//",
            "empty components",
            Status::INVALID_ARGS
        };
    }

    #[test]
    fn absolute_paths() {
        simple_construction_test! {
            path: "/a/b/c",
            mut path => {
                assert!(!path.is_empty());
                assert!(!path.is_dir());
                assert!(!path.is_single_component());
                assert_eq!(path.as_ref(), "a/b/c");
                assert_eq!(path.clone().into_string(), "a/b/c");
                assert_eq!(path.peek(), Some("a"));
                assert_eq!(path.peek(), Some("a"));
                assert_eq!(path.next(), Some("a"));
                assert_eq!(path.next(), Some("b"));
                assert_eq!(path.next(), Some("c"));
                assert_eq!(path.peek(), None);
                assert_eq!(path.next(), None);
                assert_eq!(path.as_ref(), ".");
                assert_eq!(path.into_string(), "".to_string());
            }
        }
    }

    #[test]
    fn dot_components() {
        negative_construction_test! {
            path: "a/./b",
            "'.' components",
            Status::INVALID_ARGS
        };
    }

    #[test]
    fn dot_dot_components() {
        negative_construction_test! {
            path: "a/../b",
            "'..' components",
            Status::INVALID_ARGS
        };
    }

    #[test]
    fn too_long_filename() {
        let string = "a".repeat(MAX_FILENAME as usize + 1);
        negative_construction_test! {
            path: &string,
            "filename too long",
            Status::BAD_PATH
        };
    }

    #[test]
    fn too_long_path() {
        let filename = "a".repeat(MAX_FILENAME as usize);
        let mut path = String::new();
        while path.len() < MAX_PATH as usize {
            path.push('/');
            path.push_str(&filename);
        }
        assert_eq!(path.len(), MAX_PATH as usize);
        negative_construction_test! {
            path: &path,
            "path too long",
            Status::BAD_PATH
        };
    }

    #[test]
    fn long_path() {
        let mut path = "a/".repeat((MAX_PATH as usize - 1) / 2);
        if path.len() < MAX_PATH as usize - 1 {
            path.push('a');
        }
        assert_eq!(path.len(), MAX_PATH as usize - 1);
        simple_construction_test! {
            path: &path,
            mut path => {
                assert!(!path.is_empty());
                assert_eq!(path.next(), Some("a"));
            }
        };
    }

    #[test]
    fn long_filename() {
        let string = "a".repeat(MAX_FILENAME as usize);
        simple_construction_test! {
            path: &string,
            mut path => {
                assert!(!path.is_empty());
                assert!(path.is_single_component());
                assert_eq!(path.next(), Some(string.as_str()));
            }
        };
    }

    #[test]
    fn dot() {
        for mut path in
            [Path::dot(), Path::validate_and_split(".").expect("validate_and_split failed")]
        {
            assert!(path.is_dot());
            assert!(path.is_empty());
            assert!(!path.is_dir());
            assert!(!path.is_single_component());
            assert_eq!(path.next(), None);
            assert_eq!(path.peek(), None);
            assert_eq!(path.as_ref(), ".");
            assert_eq!(path.as_ref(), ".");
            assert_eq!(path.into_string(), "");
        }
    }

    #[test]
    fn eq_compares_remainder() {
        let mut pos = path("a/b/c");

        assert_eq!(pos, path("a/b/c"));
        assert_ne!(pos, path("b/c"));
        assert_ne!(pos, path("c"));
        assert_ne!(pos, path("."));

        assert_eq!(pos.next(), Some("a"));

        assert_ne!(pos, path("a/b/c"));
        assert_eq!(pos, path("b/c"));
        assert_ne!(pos, path("c"));
        assert_ne!(pos, path("."));

        assert_eq!(pos.next(), Some("b"));

        assert_ne!(pos, path("a/b/c"));
        assert_ne!(pos, path("b/c"));
        assert_eq!(pos, path("c"));
        assert_ne!(pos, path("."));

        assert_eq!(pos.next(), Some("c"));

        assert_ne!(pos, path("a/b/c"));
        assert_ne!(pos, path("b/c"));
        assert_ne!(pos, path("c"));
        assert_eq!(pos, path("."));
    }

    #[test]
    fn eq_considers_is_dir() {
        let mut pos_not = path("a/b");
        let mut pos_dir = path("a/b/");

        assert_ne!(pos_not, pos_dir);
        assert_eq!(pos_not, path("a/b"));
        assert_eq!(pos_dir, path("a/b/"));

        pos_not.next();
        pos_dir.next();

        assert_ne!(pos_not, pos_dir);
        assert_eq!(pos_not, path("b"));
        assert_eq!(pos_dir, path("b/"));

        pos_not.next();
        pos_dir.next();

        // once all that is left is ".", now they are equivalent
        assert_eq!(pos_not, pos_dir);
        assert_eq!(pos_not, path("."));
        assert_eq!(pos_dir, path("."));
    }

    #[test]
    fn eq_does_not_consider_absolute_different_from_relative() {
        let abs = path("/a/b");
        let rel = path("a/b");

        assert_eq!(abs, rel);
        assert_ne!(abs, path("different/path"));
        assert_ne!(rel, path("different/path"));
    }

    #[test]
    fn as_ref_is_remainder() {
        let mut path = Path::validate_and_split(".").unwrap();
        assert_eq!(path.as_ref(), ".");
        path.next();
        assert_eq!(path.as_ref(), ".");

        let mut path = Path::validate_and_split("a/b/c").unwrap();
        assert_eq!(path.as_ref(), "a/b/c");
        path.next();
        assert_eq!(path.as_ref(), "b/c");
        path.next();
        assert_eq!(path.as_ref(), "c");
        path.next();
        assert_eq!(path.as_ref(), ".");

        let mut path = Path::validate_and_split("/a/b/c").unwrap();
        assert_eq!(path.as_ref(), "a/b/c");
        path.next();
        assert_eq!(path.as_ref(), "b/c");
        path.next();
        assert_eq!(path.as_ref(), "c");
        path.next();
        assert_eq!(path.as_ref(), ".");

        let mut path = Path::validate_and_split("/a/b/c/").unwrap();
        assert_eq!(path.as_ref(), "a/b/c/");
        path.next();
        assert_eq!(path.as_ref(), "b/c/");
        path.next();
        assert_eq!(path.as_ref(), "c/");
        path.next();
        assert_eq!(path.as_ref(), ".");
    }
}
