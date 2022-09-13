// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    pathdiff::diff_paths,
    std::path::{Path, PathBuf},
    tracing::warn,
};

/// Attempt to build a canonical path of the form `base`/`path`. If `path` is absolute,
/// then `base` is ignored. If paths do not actually exist on the filesystem, the resulting path
/// may not be canonicalized; e.g., may contain `a/../b` instead of simply `b`. This limitation
/// stems from delegating to `std::path::Path::canonicalize` which conflates resolving
/// intermediate path elements and resolving symbolic links (yielding a failure if the no file
/// with the given path exists on the filesystem).
pub fn join_and_canonicalize<P1: AsRef<Path>, P2: AsRef<Path>>(base: P1, path: P2) -> PathBuf {
    let base_ref = base.as_ref();
    let path_ref = path.as_ref();
    if path_ref.is_relative() {
        canonicalize(base_ref.join(path_ref), "joined path for join_and_canonicalize")
    } else {
        canonicalize(path_ref, "absolute path for join_and_canonicalize")
    }
}

/// Attempt to construct a relative path for `path` relative to `base`. If `path` is relative, then
/// `base` is ignored. Limitations of the underlying algorithm cause this function to fallback on
/// returning `path` unchanged when either:
/// 1. `path` is relative, or
/// 2. `base` contains a relative parent component, `..`, that cannot be canonicalized.
pub fn relativize_path<P1: AsRef<Path>, P2: AsRef<Path>>(base: P1, path: P2) -> PathBuf {
    let path_ref = path.as_ref();
    if path_ref.is_relative() {
        return path_ref.to_path_buf();
    }

    let base_ref = base.as_ref();
    let base_path = canonicalize(base_ref, "base path for relativize");
    diff_paths(path_ref, &base_path).unwrap_or_else(|| {
        warn!(
            path = ?path_ref,
            base = ?base_ref,
            canonical_base = ?base_path,
            "Failed to relativize path; returning path unchanged",
        );
        path_ref.to_path_buf()
    })
}

fn canonicalize<P: AsRef<Path>>(path: P, path_name: &str) -> PathBuf {
    let path_ref = path.as_ref();
    match path_ref.canonicalize() {
        Ok(path) => path,
        Err(err) => {
            warn!(%path_name, ?path_ref, %err, "Failed to canonicalize");
            path_ref.to_path_buf()
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::{join_and_canonicalize, relativize_path},
        std::{
            fs::{create_dir_all, File},
            path::PathBuf,
        },
        tempfile::tempdir,
    };

    fn path_buf(path_str: &str) -> PathBuf {
        PathBuf::from(String::from(path_str))
    }

    #[fuchsia::test]
    fn resolve_absolute_base_absolute_path() {
        let base = path_buf("/base");
        let path = path_buf("/path");
        let expected = path_buf("/path");

        assert_eq!(join_and_canonicalize(&base, &path), expected);
    }

    #[fuchsia::test]
    fn resolve_absolute_base_relative_path() {
        let base = path_buf("/base");
        let path = path_buf("path");
        let expected = path_buf("/base/path");

        assert_eq!(join_and_canonicalize(&base, &path), expected);
    }

    #[fuchsia::test]
    fn resolve_absolute_base_unresolved_relative_parent_path() {
        let base = path_buf("/path/does/not/exist/on/test/machine/out/product.board");
        let path = path_buf("../../path");
        let expected =
            path_buf("/path/does/not/exist/on/test/machine/out/product.board/../../path");

        assert_eq!(join_and_canonicalize(&base, &path), expected);
    }

    #[fuchsia::test]
    fn resolve_absolute_base_resolved_relative_parent_path() {
        let base_root = tempdir().unwrap().into_path();
        let base = base_root.join("out/product.board");
        let path = path_buf("../../path");
        // `join_and_canonicalize("/tmp-dir/out/product.board", "../../path") == "/tmp-dir/path"`.
        let expected = base_root.join("path");

        // Ensure canonicalization by creating all directories and files involved in canonicalized
        // paths.
        create_dir_all(&base).unwrap();
        File::create(&expected).unwrap();

        assert_eq!(join_and_canonicalize(&base, &path), expected);
    }

    #[fuchsia::test]
    fn resolve_absolute_base_resolved_absolute_path_resolved() {
        let base = tempdir().unwrap().into_path();
        let path = tempdir().unwrap().into_path();
        // `join_and_canonicalize("/tmp-dir-1", "/tmp-dir-2") == "/tmp-dir-2"`.
        let expected = path.clone();

        assert_eq!(join_and_canonicalize(&base, &path), expected);
    }

    #[fuchsia::test]
    fn relativize_absolute_base_absolute_path() {
        let base = path_buf("/root/build/dir");
        let path = path_buf("/root/build/dir/file");
        let expected = path_buf("file");

        assert_eq!(relativize_path(&base, &path), expected);
    }

    #[fuchsia::test]
    fn relativize_absolute_base_relative_path() {
        let base = path_buf("/root/build/dir");
        let path = path_buf("root/build/dir/file");
        // Relative `path` => return `path` unchanged.
        let expected = path_buf("root/build/dir/file");

        assert_eq!(relativize_path(&base, &path), expected);
    }

    #[fuchsia::test]
    fn relativize_relative_base_relative_path() {
        let base = path_buf("root/build/dir");
        let path = path_buf("root/build/dir/file");
        // Relative `path` => return `path` unchanged.
        let expected = path_buf("root/build/dir/file");

        assert_eq!(relativize_path(&base, &path), expected);
    }

    #[fuchsia::test]
    fn relativize_non_canonicalized_base_absolute_path() {
        let base = PathBuf::from(String::from(
            "/path/does/not/exist/on/test/machine/out/product.board/../product.board",
        ));
        let path = PathBuf::from(String::from(
            "/path/does/not/exist/on/test/machine/out/product.board/file",
        ));
        // Non-canonicalized `..` in `base` => return `path` unchanged.
        let expected = PathBuf::from(String::from(
            "/path/does/not/exist/on/test/machine/out/product.board/file",
        ));

        assert_eq!(relativize_path(&base, &path), expected);
    }

    #[fuchsia::test]
    fn relativize_canonicalized_base_absolute_path() {
        let base_root = tempdir().unwrap().into_path();
        let canonical_base = base_root.join("out/product.board");
        create_dir_all(&canonical_base).unwrap();
        let non_canonical_base = base_root.join("out/product.board/../../out/product.board");
        let path = base_root.join("out/product.board/../../file");
        // ```
        // relativize_path(
        //     ".../out/board.product/../../out/board.product",
        //     ".../out/board.product/../../file"
        // ) == "../../file"
        // ```
        let expected = path_buf("../../file");

        assert_eq!(relativize_path(&non_canonical_base, &path), expected);
    }
}
