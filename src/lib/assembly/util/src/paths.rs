// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use camino::{Utf8Component, Utf8Path, Utf8PathBuf};
use pathdiff::diff_utf8_paths;
use serde::{Deserialize, Serialize};
use std::{
    hash::Hash,
    marker::PhantomData,
    path::{Path, PathBuf},
};

/// A base trait for TypePath's marker traits.
pub trait PathTypeMarker {
    /// A reference to an object that implements Display, and gives the
    /// displayable semantic type for this path.  This is used by the Debug
    /// implementation of `TypedPathBuf` to display the semantic type for the
    /// path:
    ///
    /// ```
    /// struct MarkerStructType;
    /// impl_path_type_marker!(MarkerStructType);
    ///
    /// let typed_path = TypedPathBuf<MarkerStructType>::from("some/path");
    /// println!("{:?}", typed_path);
    /// ```
    /// will print:
    ///
    /// ```text
    /// TypedPathBuf<MarkerStructType>("some/path")
    /// ```
    fn path_type_display() -> &'static dyn std::fmt::Display;
}

/// Implement the `PathTypeMarker` trait for a given marker-type struct.  This
/// mainly simplifies the creation of a display-string for the type.
#[macro_export]
macro_rules! impl_path_type_marker {
    // This macro takes an argument of the marker struct's type name, and then
    // provides an implementation of 'PathTypeMarker' for it.
    ($struct_name:ident) => {
        impl PathTypeMarker for $struct_name {
            fn path_type_display() -> &'static dyn std::fmt::Display {
                &stringify!($struct_name)
            }
        }
    };
}

/// A path, in valid utf-8, which carries a marker for what kind of path it is.
#[derive(Clone, Serialize, Deserialize)]
#[repr(transparent)]
#[serde(transparent)]
pub struct TypedPathBuf<P: PathTypeMarker> {
    #[serde(flatten)]
    inner: Utf8PathBuf,

    #[serde(skip)]
    _marker: PhantomData<P>,
}

/// This derefs into the typed version of utf8 path, not utf8 path itself, so
/// that it is easier to use in typed contexts, and makes the switchover to
/// a non-typed context more explicit.
///
/// This also causes any path manipulations (join, etc.) to be done without the
/// semantic type, so that the caller has to be explicit that it's still the
/// semantic type (using 'into()', for instance).
impl<P: PathTypeMarker> std::ops::Deref for TypedPathBuf<P> {
    type Target = Utf8PathBuf;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

impl<P: PathTypeMarker> TypedPathBuf<P> {
    /// Convert this TypedPathBuf into a standard (OsStr-based) `PathBuf`.  This
    /// both strips it of semantic type and that it's known to be Utf-8.
    pub fn into_std_path_buf(self) -> PathBuf {
        self.inner.into_std_path_buf()
    }
}

impl<P: PathTypeMarker> AsRef<Utf8Path> for TypedPathBuf<P> {
    fn as_ref(&self) -> &Utf8Path {
        self.inner.as_ref()
    }
}

impl<P: PathTypeMarker> AsRef<Path> for TypedPathBuf<P> {
    fn as_ref(&self) -> &Path {
        self.inner.as_ref()
    }
}

/// The Debug implementation displays like a type-struct that carries the marker
/// type for the path:
///
/// ```text
/// TypedPathBuf<MarkerStructType>("some/path")
/// ```
impl<P: PathTypeMarker> std::fmt::Debug for TypedPathBuf<P> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_tuple(&format!("TypedPathBuf<{}>", P::path_type_display()))
            .field(&self.inner.to_string())
            .finish()
    }
}

/// The Display implementation defers to the wrapped path.
impl<P: PathTypeMarker> std::fmt::Display for TypedPathBuf<P> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.inner.fmt(f)
    }
}

/// Implement From<> for path-like sources.  Note that these also will infer the
/// semantic type, which while useful in some contexts, can cause issues in
/// places where multiple different type markers are used:
///
/// ```
/// fn some_func(source: TypedPathBuf<Source>,     TypedPathBuf<Destination>);
///
/// // This infers the types of the paths:
/// some_func("source_path".into(), "destination_path".into());
///
/// // allowing this error:
/// some_func("destination_path".into(), "source_path",into());
///
/// // In these cases, it's best to strongly type one or both of them:
/// some_func(TypedPathBuf<Source>::from("source_path"), "destination_path".into());
///
/// // or (better)
/// some_func(TypedPathBuf<Source>::from("source_path"),
///           TypedPathBuf<Destination>::from("destination_path"));
/// ```
// inner module used to group impls and to add above documentation.
mod from_impls {
    use super::*;

    impl<P: PathTypeMarker> From<Utf8PathBuf> for TypedPathBuf<P> {
        fn from(path: Utf8PathBuf) -> Self {
            Self { inner: path, _marker: PhantomData }
        }
    }

    impl<P: PathTypeMarker> From<TypedPathBuf<P>> for Utf8PathBuf {
        fn from(path: TypedPathBuf<P>) -> Self {
            path.inner
        }
    }

    impl<P: PathTypeMarker> From<TypedPathBuf<P>> for PathBuf {
        fn from(path: TypedPathBuf<P>) -> Self {
            path.inner.into()
        }
    }

    impl<P: PathTypeMarker> From<String> for TypedPathBuf<P> {
        fn from(s: String) -> TypedPathBuf<P> {
            TypedPathBuf::from(Utf8PathBuf::from(s))
        }
    }

    impl<P: PathTypeMarker> From<&str> for TypedPathBuf<P> {
        fn from(s: &str) -> TypedPathBuf<P> {
            TypedPathBuf::from(Utf8PathBuf::from(s))
        }
    }

    impl<P: PathTypeMarker> std::str::FromStr for TypedPathBuf<P> {
        type Err = String;

        fn from_str(s: &str) -> Result<Self, Self::Err> {
            Ok(Self::from(s))
        }
    }
}

// These comparison implementations are required because #[derive(...)] will not
// derive these if `P` doesn't implement them, but `P` has no reason to
// implement them, so these implementations just pass through to the Utf8PathBuf
// implementations.

impl<P: PathTypeMarker> PartialOrd for TypedPathBuf<P> {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        self.inner.partial_cmp(&other.inner)
    }
}

impl<P: PathTypeMarker> Ord for TypedPathBuf<P> {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.inner.cmp(&other.inner)
    }
}

impl<P: PathTypeMarker> PartialEq for TypedPathBuf<P> {
    fn eq(&self, other: &Self) -> bool {
        self.inner == other.inner
    }
}

impl<P: PathTypeMarker> Eq for TypedPathBuf<P> {}

impl<P: PathTypeMarker> Hash for TypedPathBuf<P> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.inner.hash(state);
    }
}

/// Helper to make one path relative to a directory.
///
/// This is similar to GN's `rebase_path(path, new_base)`.
///
/// To do the calculation, both 'path' and 'base' are made absolute, using the
/// current working dir as the basis for converting a relative path to absolute,
/// and then the relative path from one to the other is computed.
pub fn path_relative_from(
    path: impl AsRef<Utf8Path>,
    base: impl AsRef<Utf8Path>,
) -> Result<Utf8PathBuf> {
    let path = normalized_absolute_path(&path).with_context(|| {
        format!("converting path to normalized absolute path: {}", path.as_ref())
    })?;
    let base = normalized_absolute_path(&base).with_context(|| {
        format!("converting base to normalized absolute path: {}", base.as_ref())
    })?;

    diff_utf8_paths(&path, &base)
        .ok_or_else(|| anyhow!("unable to compute relative path to {} from {}", path, base))
}

/// Helper to convert an absolute path into a path relative to the current directory
pub fn path_relative_from_current_dir(path: impl AsRef<Utf8Path>) -> Result<Utf8PathBuf> {
    let current_dir = std::env::current_dir()?;
    path_relative_from(path, Utf8PathBuf::try_from(current_dir)?)
}

/// Helper to make a path relative to the path to a file.  This is the same as
/// [path_relative_from(file.parent()?)]
///
pub fn path_relative_from_file(
    path: impl AsRef<Utf8Path>,
    file: impl AsRef<Utf8Path>,
) -> Result<Utf8PathBuf> {
    let file = file.as_ref();
    let base = file.parent().ok_or_else(|| {
        anyhow!(
            "The path to the file to be relative to does not appear to be the path to a file: {}",
            file
        )
    })?;
    path_relative_from(path, base)
}

fn normalized_absolute_path(path: impl AsRef<Utf8Path>) -> Result<Utf8PathBuf> {
    let path = path.as_ref();
    if path.is_relative() {
        let current_dir = std::env::current_dir()?;
        normalize_path_impl(Utf8PathBuf::try_from(current_dir)?.join(path).components())
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
    path: impl AsRef<Utf8Path>,
    resolve_from: impl AsRef<Utf8Path>,
) -> Result<Utf8PathBuf> {
    let resolve_from = resolve_from.as_ref();
    let resolve_from_dir =
        resolve_from.parent().with_context(|| format!("Not a path to a file: {}", resolve_from))?;
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
pub fn resolve_path(
    path: impl AsRef<Utf8Path>,
    resolve_from: impl AsRef<Utf8Path>,
) -> Result<Utf8PathBuf> {
    let path = path.as_ref();
    let resolve_from = resolve_from.as_ref();
    if path.is_absolute() {
        Ok(path.to_owned())
    } else {
        normalize_path_impl(resolve_from.components().chain(path.components()))
            .with_context(|| format!("resolving {} from {}", path, resolve_from))
    }
}

/// Given a path with internal `.` and `..`, normalize out those path segments.
///
/// This does not consult the filesystem to follow symlinks, it only operates
/// on the path components themselves.
pub fn normalize_path(path: impl AsRef<Utf8Path>) -> Result<Utf8PathBuf> {
    let path = path.as_ref();
    normalize_path_impl(path.components()).with_context(|| format!("Normalizing: {}", path))
}

fn normalize_path_impl<'a>(
    path_components: impl IntoIterator<Item = Utf8Component<'a>>,
) -> Result<Utf8PathBuf> {
    let result =
        path_components.into_iter().try_fold(Vec::new(), |mut components, component| {
            match component {
                // accumulate normal segments.
                value @ Utf8Component::Normal(_) => components.push(value),
                // Drop current directory segments.
                Utf8Component::CurDir => {}
                // Parent dir segments require special handling
                Utf8Component::ParentDir => {
                    // Inspect the last item in the acculuated path
                    let popped = components.pop();
                    match popped {
                        // acculator is empty, so just append the parent.
                        None => components.push(Utf8Component::ParentDir),

                        // If the last item was normal, then drop it.
                        Some(Utf8Component::Normal(_)) => {}

                        // The last item was a parent, and this is a parent, so push
                        // them BOTH onto the stack (we're going deeper).
                        Some(value @ Utf8Component::ParentDir) => {
                            components.push(value);
                            components.push(component);
                        }
                        // If the last item in the stack is an absolute path root
                        // then fail.
                        Some(Utf8Component::RootDir) | Some(Utf8Component::Prefix(_)) => {
                            return Err(anyhow!("Attempted to get parent of path root"))
                        }
                        // Never pushed to stack, can't happen.
                        Some(Utf8Component::CurDir) => unreachable!(),
                    }
                }
                // absolute path roots get pushed onto the stack, but only if empty.
                abs_root @ Utf8Component::RootDir | abs_root @ Utf8Component::Prefix(_) => {
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
    use std::iter::FromIterator;
    use std::str::FromStr;

    struct TestPathType {}
    impl_path_type_marker!(TestPathType);

    #[test]
    fn make_typed_path_from_string() {
        let original: String = "/this/is/a/string".to_string();
        let typed = TypedPathBuf::<TestPathType>::from_str(&original).unwrap();
        assert_eq!(typed.to_string(), original);
    }

    #[test]
    fn make_typed_path_from_str() {
        let original: &str = "/this/is/a/string";
        let typed = TypedPathBuf::<TestPathType>::from_str(&original).unwrap();
        assert_eq!(typed.to_string(), original);
    }

    #[test]
    fn path_type_deserialization() {
        #[derive(Debug, Deserialize)]
        struct Sample {
            pub path: TypedPathBuf<TestPathType>,
        }
        let parsed: Sample = serde_json::from_str("{ \"path\": \"this/is/a/path\"}").unwrap();
        assert_eq!(parsed.path, TypedPathBuf::<TestPathType>::from("this/is/a/path"));
    }

    #[test]
    fn path_type_serialization() {
        #[derive(Debug, Serialize)]
        struct Sample {
            pub path: TypedPathBuf<TestPathType>,
        }
        let sample = Sample { path: "this/is/a/path".into() };
        let expected = serde_json::json!({ "path": "this/is/a/path"});
        assert_eq!(serde_json::to_value(sample).unwrap(), expected);
    }

    #[test]
    fn typed_path_debug_impl() {
        let typed = TypedPathBuf::<TestPathType>::from("some/path");
        assert_eq!(format!("{:?}", typed), "TypedPathBuf<TestPathType>(\"some/path\")");
    }

    #[test]
    fn typed_path_display_impl() {
        let typed = TypedPathBuf::<TestPathType>::from("some/path");
        assert_eq!(format!("{}", typed), "some/path");
    }

    #[test]
    fn typed_path_buf_into_path_buf() {
        let typed = TypedPathBuf::<TestPathType>::from("some/path");
        assert_eq!(typed.into_std_path_buf(), Utf8PathBuf::from("some/path"));
    }

    #[test]
    fn typed_path_derefs_into_utf8_path() {
        let typed = TypedPathBuf::<TestPathType>::from("some/path");
        let utf8_path = Utf8PathBuf::from("some/path");
        assert_eq!(*typed, utf8_path);
    }

    #[test]
    fn typed_path_as_ref_utf8path() {
        let original = TypedPathBuf::<TestPathType>::from("a/path");
        let path: &Utf8Path = original.as_ref();
        assert_eq!(path, Utf8Path::new("a/path"))
    }

    #[test]
    fn typed_path_as_ref_path() {
        let original = TypedPathBuf::<TestPathType>::from("a/path");
        let path: &Path = original.as_ref();
        assert_eq!(path, Path::new("a/path"))
    }

    #[test]
    fn resolve_path_from_file_simple() {
        let result = resolve_path_from_file("an/internal/path", "path/to/manifest.txt").unwrap();
        assert_eq!(result, Utf8PathBuf::from("path/to/an/internal/path"))
    }

    #[test]
    fn resolve_path_from_file_fails_root() {
        let result = resolve_path_from_file("an/internal/path", "/");
        assert!(result.is_err());
    }

    #[test]
    fn resolve_path_simple() {
        let result = resolve_path("an/internal/path", "path/to/manifest_dir").unwrap();
        assert_eq!(result, Utf8PathBuf::from("path/to/manifest_dir/an/internal/path"))
    }

    #[test]
    fn resolve_path_with_abs_manifest_path_stays_abs() {
        let result = resolve_path("an/internal/path", "/path/to/manifest_dir").unwrap();
        assert_eq!(result, Utf8PathBuf::from("/path/to/manifest_dir/an/internal/path"))
    }

    #[test]
    fn resolve_path_removes_cur_dirs() {
        let result = resolve_path("./an/./internal/path", "./path/to/./manifest_dir").unwrap();
        assert_eq!(result, Utf8PathBuf::from("path/to/manifest_dir/an/internal/path"))
    }

    #[test]
    fn resolve_path_with_simple_parent_dirs() {
        let result = resolve_path("../../an/internal/path", "path/to/manifest_dir").unwrap();
        assert_eq!(result, Utf8PathBuf::from("path/an/internal/path"))
    }

    #[test]
    fn resolve_path_with_parent_dirs_past_manifest_start() {
        let result = resolve_path("../../../../an/internal/path", "path/to/manifest_dir").unwrap();
        assert_eq!(result, Utf8PathBuf::from("../an/internal/path"))
    }

    #[test]
    fn resolve_path_with_abs_internal_path() {
        let result = resolve_path("/an/absolute/path", "path/to/manifest_dir").unwrap();
        assert_eq!(result, Utf8PathBuf::from("/an/absolute/path"))
    }

    #[test]
    fn resolve_path_fails_with_parent_dirs_past_abs_manifest() {
        let result = resolve_path("../../../../an/internal/path", "/path/to/manifest_dir");
        assert!(result.is_err())
    }

    #[test]
    fn test_relative_from_absolute_when_already_relative() {
        let cwd = Utf8PathBuf::try_from(std::env::current_dir().unwrap()).unwrap();

        let base = cwd.join("path/to/base/dir");
        let path = "path/but/to/another/dir";

        let relative_path = path_relative_from(path, base).unwrap();
        assert_eq!(relative_path, Utf8PathBuf::from("../../../but/to/another/dir"));
    }

    #[test]
    fn test_relative_from_absolute_when_absolute() {
        let cwd = Utf8PathBuf::try_from(std::env::current_dir().unwrap()).unwrap();

        let base = cwd.join("path/to/base/dir");
        let path = cwd.join("path/but/to/another/dir");

        let relative_path = path_relative_from(path, base).unwrap();
        assert_eq!(relative_path, Utf8PathBuf::from("../../../but/to/another/dir"));
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

        let cwd = Utf8PathBuf::try_from(std::env::current_dir().unwrap()).unwrap();

        let expected_path = Utf8PathBuf::from_iter(
            cwd.components()
                .into_iter()
                .filter_map(|comp| match comp {
                    Utf8Component::Normal(_) => Some(Utf8Component::ParentDir),
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
        let cwd = Utf8PathBuf::try_from(std::env::current_dir().unwrap()).unwrap();

        let base = "some/relative/path";
        let path = cwd.join("foo/bar");

        let relative_path = path_relative_from(path, base).unwrap();
        assert_eq!(relative_path, Utf8PathBuf::from("../../../foo/bar"));
    }

    #[test]
    fn test_relative_from_relative_when_relative() {
        let base = "some/relative/path";
        let path = "another/relative/path";

        let relative_path = path_relative_from(path, base).unwrap();
        assert_eq!(relative_path, Utf8PathBuf::from("../../../another/relative/path"));
    }

    #[test]
    fn test_relative_from_when_base_has_parent_component() {
        assert_eq!(
            path_relative_from("foo/bar", "baz/different_thing").unwrap(),
            Utf8PathBuf::from("../../foo/bar")
        );
        assert_eq!(
            path_relative_from("foo/bar", "baz/thing/../different_thing").unwrap(),
            Utf8PathBuf::from("../../foo/bar")
        );
    }

    #[test]
    fn test_relative_from_file_simple() {
        let file = "some/path/to/file.txt";
        let path = "some/path/to/data/file";

        let relative_path = path_relative_from_file(path, file).unwrap();
        assert_eq!(relative_path, Utf8PathBuf::from("data/file"));
    }

    #[test]
    fn test_relative_from_file_when_file_not_a_file() {
        let file = "/";
        let path = "some/path/to/data/file";

        let relative_path = path_relative_from_file(path, file);
        assert!(relative_path.is_err());
    }

    #[test]
    fn test_relative_from_current_dir() {
        let cwd = Utf8PathBuf::try_from(std::env::current_dir().unwrap()).unwrap();

        let base = "some/relative/path";
        let path = cwd.join(base);

        let relative_path = path_relative_from_current_dir(path).unwrap();
        assert_eq!(relative_path, Utf8PathBuf::from(base));
    }
}
