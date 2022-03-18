// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use pathdiff::diff_paths;
use std::path::{Component, Path, PathBuf};

/// Helper to make one path relative to a directory.
///
/// This is similar to GN's `rebase_path(path, new_base)`.
///
/// To do the calculation, both 'path' and 'base' are made absolute, using the
/// current working dir as the basis for converting a relative path to absolute,
/// and then the relative path from one to the other is computed.
pub fn path_relative_from(path: impl AsRef<Path>, base: impl AsRef<Path>) -> Result<PathBuf> {
    let path = normalized_absolute_path(&path).with_context(|| {
        format!("converting path to normalized absolute path: {}", path.as_ref().display())
    })?;
    let base = normalized_absolute_path(&base).with_context(|| {
        format!("converting base to normalized absolute path: {}", base.as_ref().display())
    })?;

    diff_paths(&path, &base).ok_or_else(|| {
        anyhow!("unable to compute relative path to {} from {}", path.display(), base.display())
    })
}

/// Helper to make a path relative to the path to a file.  This is the same as
/// [path_relative_from(file.parent()?)]
///
pub fn path_relative_from_file(path: impl AsRef<Path>, file: impl AsRef<Path>) -> Result<PathBuf> {
    let file = file.as_ref();
    let base = file.parent().ok_or_else(|| {
        anyhow!(
            "The path to the file to be relative to does not appear to be the path to a file: {}",
            file.display()
        )
    })?;
    path_relative_from(path, base)
}

fn normalized_absolute_path(path: impl AsRef<Path>) -> Result<PathBuf> {
    let path = path.as_ref();
    if path.is_relative() {
        normalize_path_impl(std::env::current_dir()?.join(path).components())
    } else {
        normalize_path_impl(path.components())
    }
}

/// Helper to resolve a path that's relative to some other path into a
/// normalized path.
///
/// # Example
///
/// a file at: `some/path/to/a/manifest.txt`
/// contains within it the path: `../some/internal/path`.
///
/// ```
///   use assembly_util::path_to_string::resolve_path;
///
///   let rebased = resolve_path("../some/internal/path", "some/path/to/some/manifest.txt")
///   assert_eq!(rebased.unwrap(), "some/path/to/some/internal/path")
/// ```
///
pub fn resolve_path_from_file(
    path: impl AsRef<Path>,
    resolve_from: impl AsRef<Path>,
) -> Result<PathBuf> {
    let resolve_from = resolve_from.as_ref();
    let resolve_from_dir = resolve_from
        .parent()
        .with_context(|| format!("Not a path to a file: {}", resolve_from.display()))?;
    resolve_path(path, resolve_from_dir)
}
/// Helper to resolve a path that's relative to some other path into a
/// normalized path.
///
/// # Example
///
/// a file at: `some/path/to/some/manifest_dir/some_file.txt`
/// contains within it the path: `../some/internal/path`.
///
/// ```
///   use assembly_util::path_to_string::resolve_path;
///
///   let rebased = resolve_path("../some/internal/path", "some/path/to/some/manifest_dir/")
///   assert_eq!(rebased.unwrap(), "some/path/to/some/internal/path")
/// ```
///
pub fn resolve_path(path: impl AsRef<Path>, resolve_from: impl AsRef<Path>) -> Result<PathBuf> {
    let path = path.as_ref();
    let resolve_from = resolve_from.as_ref();
    if path.is_absolute() {
        Ok(path.to_owned())
    } else {
        normalize_path_impl(resolve_from.components().chain(path.components())).with_context(|| {
            format!("resolving {} from {}", path.display(), resolve_from.display())
        })
    }
}

/// Given a path with internal `.` and `..`, normalize out those path segments.
///
/// This does not consult the filesystem to follow symlinks, it only operates
/// on the path components themselves.
pub fn normalize_path(path: impl AsRef<Path>) -> Result<PathBuf> {
    normalize_path_impl(path.as_ref().components())
        .with_context(|| format!("Normalizing: {}", path.as_ref().display()))
}

fn normalize_path_impl<'a>(
    path_components: impl IntoIterator<Item = Component<'a>>,
) -> Result<PathBuf> {
    let result =
        path_components.into_iter().try_fold(Vec::new(), |mut components, component| {
            match component {
                // accumulate normal segments.
                value @ Component::Normal(_) => components.push(value),
                // Drop current directory segments.
                Component::CurDir => {}
                // Parent dir segments require special handling
                Component::ParentDir => {
                    // Inspect the last item in the acculuated path
                    let popped = components.pop();
                    match popped {
                        // acculator is empty, so just append the parent.
                        None => components.push(Component::ParentDir),

                        // If the last item was normal, then drop it.
                        Some(Component::Normal(_)) => {}

                        // The last item was a parent, and this is a parent, so push
                        // them BOTH onto the stack (we're going deeper).
                        Some(value @ Component::ParentDir) => {
                            components.push(value);
                            components.push(component);
                        }
                        // If the last item in the stack is an absolute path root
                        // then fail.
                        Some(Component::RootDir) | Some(Component::Prefix(_)) => {
                            return Err(anyhow!("Attempted to get parent of path root"))
                        }
                        // Never pushed to stack, can't happen.
                        Some(Component::CurDir) => unreachable!(),
                    }
                }
                // absolute path roots get pushed onto the stack, but only if empty.
                abs_root @ Component::RootDir | abs_root @ Component::Prefix(_) => {
                    if components.is_empty() {
                        components.push(abs_root);
                    } else {
                        return Err(anyhow!(
                            "Encountered a path root that wasn't in the root position"
                        ));
                    }
                }
            }
            Ok(components)
        })?;
    Ok(result.iter().collect())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{iter::FromIterator, path::PathBuf};

    #[test]
    fn resolve_path_from_file_simple() {
        let result = resolve_path_from_file("an/internal/path", "path/to/manifest.txt").unwrap();
        assert_eq!(result, PathBuf::from("path/to/an/internal/path"))
    }

    #[test]
    fn resolve_path_from_file_fails_root() {
        let result = resolve_path_from_file("an/internal/path", "/");
        assert!(result.is_err());
    }

    #[test]
    fn resolve_path_simple() {
        let result = resolve_path("an/internal/path", "path/to/manifest_dir").unwrap();
        assert_eq!(result, PathBuf::from("path/to/manifest_dir/an/internal/path"))
    }

    #[test]
    fn resolve_path_with_abs_manifest_path_stays_abs() {
        let result = resolve_path("an/internal/path", "/path/to/manifest_dir").unwrap();
        assert_eq!(result, PathBuf::from("/path/to/manifest_dir/an/internal/path"))
    }

    #[test]
    fn resolve_path_removes_cur_dirs() {
        let result = resolve_path("./an/./internal/path", "./path/to/./manifest_dir").unwrap();
        assert_eq!(result, PathBuf::from("path/to/manifest_dir/an/internal/path"))
    }

    #[test]
    fn resolve_path_with_simple_parent_dirs() {
        let result = resolve_path("../../an/internal/path", "path/to/manifest_dir").unwrap();
        assert_eq!(result, PathBuf::from("path/an/internal/path"))
    }

    #[test]
    fn resolve_path_with_parent_dirs_past_manifest_start() {
        let result = resolve_path("../../../../an/internal/path", "path/to/manifest_dir").unwrap();
        assert_eq!(result, PathBuf::from("../an/internal/path"))
    }

    #[test]
    fn resolve_path_with_abs_internal_path() {
        let result = resolve_path("/an/absolute/path", "path/to/manifest_dir").unwrap();
        assert_eq!(result, PathBuf::from("/an/absolute/path"))
    }

    #[test]
    fn resolve_path_fails_with_parent_dirs_past_abs_manifest() {
        let result = resolve_path("../../../../an/internal/path", "/path/to/manifest_dir");
        assert!(result.is_err())
    }

    #[test]
    fn test_relative_from_absolute_when_already_relative() {
        let cwd = std::env::current_dir().unwrap();

        let base = cwd.join("path/to/base/dir");
        let path = "path/but/to/another/dir";

        let relative_path = path_relative_from(path, base).unwrap();
        assert_eq!(relative_path, PathBuf::from("../../../but/to/another/dir"));
    }

    #[test]
    fn test_relative_from_absolute_when_absolute() {
        let cwd = std::env::current_dir().unwrap();

        let base = cwd.join("path/to/base/dir");
        let path = cwd.join("path/but/to/another/dir");

        let relative_path = path_relative_from(path, base).unwrap();
        assert_eq!(relative_path, PathBuf::from("../../../but/to/another/dir"));
    }

    #[test]
    fn test_relative_from_relative_when_absolute_and_different_from_root() {
        let base = "../some/relative/path";
        let path = "/an/absolute/path";

        // The relative path to an absolute path from a relative base (relative
        // to cwd), is the number of ParendDir components needed to reach the
        // root, and then the absolute path itself.  It's only this long when
        // the paths have nothing in common from the root.
        //
        // To compute this path, we need to convert the "normal" segments of the
        // cwd path into ParentDir ("..") components.

        let cwd = std::env::current_dir().unwrap();
        let expected_path = PathBuf::from_iter(
            cwd.components()
                .into_iter()
                .filter_map(|comp| match comp {
                    Component::Normal(_) => Some(Component::ParentDir),
                    _ => None,
                })
                // Skip one of the '..' segments, because the 'base' we are
                // using in this test starts with a '..', and normalizing
                // cwd.join(base) will remove the last component from cwd.
                .skip(1),
        )
        // Join that path with 'some/relative/path' converted to '..' segments
        .join("../../../")
        // And join that path with the 'path' itself.
        .join("an/absolute/path");

        let relative_path = path_relative_from(path, base).unwrap();
        assert_eq!(relative_path, expected_path);
    }

    #[test]
    fn test_relative_from_relative_when_absolute_and_shared_root_path() {
        let cwd = std::env::current_dir().unwrap();

        let base = "some/relative/path";
        let path = cwd.join("foo/bar");

        let relative_path = path_relative_from(path, base).unwrap();
        assert_eq!(relative_path, PathBuf::from("../../../foo/bar"));
    }

    #[test]
    fn test_relative_from_relative_when_relative() {
        let base = "some/relative/path";
        let path = "another/relative/path";

        let relative_path = path_relative_from(path, base).unwrap();
        assert_eq!(relative_path, PathBuf::from("../../../another/relative/path"));
    }

    #[test]
    fn test_relative_from_when_base_has_parent_component() {
        assert_eq!(
            path_relative_from("foo/bar", "baz/different_thing").unwrap(),
            PathBuf::from("../../foo/bar")
        );
        assert_eq!(
            path_relative_from("foo/bar", "baz/thing/../different_thing").unwrap(),
            PathBuf::from("../../foo/bar")
        );
    }

    #[test]
    fn test_relative_from_file_simple() {
        let file = "some/path/to/file.txt";
        let path = "some/path/to/data/file";

        let relative_path = path_relative_from_file(path, file).unwrap();
        assert_eq!(relative_path, PathBuf::from("data/file"));
    }

    #[test]
    fn test_relative_from_file_when_file_not_a_file() {
        let file = "/";
        let path = "some/path/to/data/file";

        let relative_path = path_relative_from_file(path, file);
        assert!(relative_path.is_err());
    }
}
