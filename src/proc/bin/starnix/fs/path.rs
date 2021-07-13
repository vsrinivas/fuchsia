// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub type FsString = Vec<u8>;
pub type FsStr = [u8];

/// Collects elements of the slice from the end that match the given predicate.
///
/// Returns a pair of slices, the first of which is the elements that were not
/// collected (i.e., whose last element does not match the predicate) and the
/// second of which is the elements that were collected (i.e., that match the
/// predicate).
#[cfg(test)]
fn rcollect<T, F>(slice: &[T], pred: F) -> (&[T], &[T])
where
    F: Fn(&T) -> bool,
{
    let mut remaining = slice;
    while let Some(last) = remaining.last() {
        if !pred(last) {
            break;
        }
        remaining = &remaining[..remaining.len() - 1];
    }
    let collected = &slice[remaining.len()..];
    (remaining, collected)
}

/// Split the path into its dirname and basename.
///
/// Intended to match Python's os.path.split.
#[cfg(test)]
pub fn split(path: &FsStr) -> (&FsStr, &FsStr) {
    let (remaining, basename) = rcollect(path, |b| b != &b'/');
    let (dirname, slashes) = rcollect(remaining, |b| b == &b'/');
    if dirname.is_empty() {
        (slashes, basename)
    } else {
        (dirname, basename)
    }
}

/// The directory name portion of the path.
///
/// Intended to match Python's os.path.dirname.
#[cfg(test)]
pub fn dirname(path: &FsStr) -> &FsStr {
    let (dirname, _basename) = split(path);
    dirname
}

/// The filename portion of the path.
///
/// Intended to match Python's os.path.basename.
#[cfg(test)]
pub fn basename(path: &FsStr) -> &FsStr {
    let (_dirname, basename) = split(path);
    basename
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_dirname() {
        assert_eq!(b"", dirname(b""));
        assert_eq!(b"foo/bar", dirname(b"foo/bar/baz"));
        assert_eq!(b"/foo/bar", dirname(b"/foo/bar/baz"));
        assert_eq!(b"/foo", dirname(b"/foo/bar"));
        assert_eq!(b"/", dirname(b"/"));
        assert_eq!(b"/////", dirname(b"/////"));
        assert_eq!(b"/", dirname(b"/foo"));
        assert_eq!(b"/foo", dirname(b"/foo//"));
        assert_eq!(b"/foo", dirname(b"/foo//bar"));
        assert_eq!(b"/foo//bar", dirname(b"/foo//bar//baz"));
    }

    #[test]
    fn test_basename() {
        assert_eq!(b"", basename(b""));
        assert_eq!(b"baz", basename(b"foo/bar/baz"));
        assert_eq!(b"baz", basename(b"/foo/bar/baz"));
        assert_eq!(b"bar", basename(b"/foo/bar"));
        assert_eq!(b"", basename(b"/"));
        assert_eq!(b"", basename(b"/////"));
        assert_eq!(b"foo", basename(b"/foo"));
        assert_eq!(b"", basename(b"/foo//"));
        assert_eq!(b"bar", basename(b"/foo//bar"));
        assert_eq!(b"baz", basename(b"/foo//bar//baz"));
    }
}
