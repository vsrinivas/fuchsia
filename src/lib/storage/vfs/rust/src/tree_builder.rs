// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A helper to build a tree of directory nodes.  It is useful in case when a nested tree is
//! desired, with specific nodes to be inserted as the leafs of this tree.  It is similar to the
//! functionality provided by the [`pseudo_directory`] macro, except that the macro expects the
//! tree structure to be defined at compile time, while this helper allows the tree structure to be
//! dynamic.

use crate::directory::{entry::DirectoryEntry, helper::DirectlyMutable, immutable};

use {
    fidl_fuchsia_io::MAX_FILENAME,
    itertools::Itertools,
    std::{collections::HashMap, fmt, marker::PhantomData, slice::Iter, sync::Arc},
    thiserror::Error,
};

/// Represents a paths provided to [`TreeBuilder::add_entry()`].  See [`TreeBuilder`] for details.
// I think it would be a bit more straightforward to have two different types that implement a
// `Path` trait, `OwnedPath` and `SharedPath`.  But, `add_entry` then needs two type variables: one
// for the type of the value passed in, and one for the type of the `Path` trait (either
// `OwnedPath` or `SharedPath`).  Type inference fails with two variables requiring explicit type
// annotation.  And that defeats the whole purpose of the overloading in the API.
//
//     pub fn add_entry<'path, 'components: 'path, F, P: 'path>(
//         &mut self,
//         path: F,
//         entry: Arc<dyn DirectoryEntry>,
//     ) -> Result<(), Error>
//
// Instead we capture the underlying implementation of the path in the `Impl` type and just wrap
// our type around it.  `'components` and `AsRef` constraints on the struct itself are not actually
// needed, but it makes it more the usage a bit easier to understand.
pub struct Path<'components, Impl>
where
    Impl: AsRef<[&'components str]>,
{
    path: Impl,
    _components: PhantomData<&'components str>,
}

impl<'components, Impl> Path<'components, Impl>
where
    Impl: AsRef<[&'components str]>,
{
    fn iter<'path>(&'path self) -> Iter<'path, &'components str>
    where
        'components: 'path,
    {
        self.path.as_ref().iter()
    }
}

impl<'component> From<&'component str> for Path<'component, Vec<&'component str>> {
    fn from(component: &'component str) -> Self {
        Path { path: vec![component], _components: PhantomData }
    }
}

impl<'components, Impl> From<Impl> for Path<'components, Impl>
where
    Impl: AsRef<[&'components str]>,
{
    fn from(path: Impl) -> Self {
        Path { path, _components: PhantomData }
    }
}

impl<'components, Impl> fmt::Display for Path<'components, Impl>
where
    Impl: AsRef<[&'components str]>,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.iter().format("/"))
    }
}

pub enum TreeBuilder {
    Directory(HashMap<String, TreeBuilder>),
    Leaf(Arc<dyn DirectoryEntry>),
}

/// Collects a number of [`DirectoryEntry`] nodes and corresponding paths and the constructs a tree
/// of [`immutable::simple::Simple`] directories that hold these nodes.  This is a companion tool,
/// related to the [`pseudo_directory!`] macro, except that it is collecting the paths dynamically,
/// while the [`pseudo_directory!`] expects the tree to be specified at compilation time.
///
/// Note that the final tree is build as a result of the [`build()`] method that consumes the
/// builder.  You would need to use the [`directory::Simple::add_entry()`] interface to add any new
/// nodes afterwards (a [`directory::controlled::Controller`] APIs).
impl TreeBuilder {
    /// Constructs an empty builder.  It is always an empty [`Simple`] directory.
    pub fn empty_dir() -> Self {
        TreeBuilder::Directory(HashMap::new())
    }

    /// Adds a [`DirectoryEntry`] at the specified path.  It can be either a file or a directory.
    /// In case it is a directory, this builder cannot add new child nodes inside of the added
    /// directory.  Any `entry` is treated as an opaque "leaf" as far as the builder is concerned.
    pub fn add_entry<'components, P: 'components, PathImpl>(
        &mut self,
        path: P,
        entry: Arc<dyn DirectoryEntry>,
    ) -> Result<(), Error>
    where
        P: Into<Path<'components, PathImpl>>,
        PathImpl: AsRef<[&'components str]>,
    {
        let path = path.into();
        let traversed = vec![];
        let mut rest = path.iter();
        match rest.next() {
            None => Err(Error::EmptyPath),
            Some(name) => self.add_path(
                &path,
                traversed,
                name,
                rest,
                |entries, name, full_path, _traversed| match entries
                    .insert(name.to_string(), TreeBuilder::Leaf(entry))
                {
                    None => Ok(()),
                    Some(TreeBuilder::Directory(_)) => {
                        Err(Error::LeafOverDirectory { path: full_path.to_string() })
                    }
                    Some(TreeBuilder::Leaf(_)) => {
                        Err(Error::LeafOverLeaf { path: full_path.to_string() })
                    }
                },
            ),
        }
    }

    /// Adds an empty directory into the generated tree at the specified path.  The difference with
    /// the [`add_entry`] that adds an entry that is a directory is that the builder can can only
    /// add leaf nodes.  In other words, code like this will fail:
    ///
    /// ```should_panic
    /// use crate::{
    ///     directory::immutable::simple,
    ///     file::pcb::asynchronous::read_only_static,
    /// };
    ///
    /// let mut tree = TreeBuilder::empty_dir();
    /// tree.add_entry(&["dir1"], simple());
    /// tree.add_entry(&["dir1", "nested"], read_only_static(b"A file"));
    /// ```
    ///
    /// The problem is that the builder does not see "dir1" as a directory, but as a leaf node that
    /// it cannot descend into.
    ///
    /// If you use `add_empty_dir()` instead, it would work:
    ///
    /// ```
    /// use crate::{
    ///     directory::immutable::simple,
    ///     file::pcb::asynchronous::read_only_static,
    /// };
    ///
    /// let mut tree = TreeBuilder::empty_dir();
    /// tree.add_empty_dir(&["dir1"]);
    /// tree.add_entry(&["dir1", "nested"], read_only_static(b"A file"));
    /// ```
    pub fn add_empty_dir<'components, P: 'components, PathImpl>(
        &mut self,
        path: P,
    ) -> Result<(), Error>
    where
        P: Into<Path<'components, PathImpl>>,
        PathImpl: AsRef<[&'components str]>,
    {
        let path = path.into();
        let traversed = vec![];
        let mut rest = path.iter();
        match rest.next() {
            None => Err(Error::EmptyPath),
            Some(name) => self.add_path(
                &path,
                traversed,
                name,
                rest,
                |entries, name, full_path, traversed| match entries
                    .entry(name.to_string())
                    .or_insert_with(|| TreeBuilder::Directory(HashMap::new()))
                {
                    TreeBuilder::Directory(_) => Ok(()),
                    TreeBuilder::Leaf(_) => Err(Error::EntryInsideLeaf {
                        path: full_path.to_string(),
                        traversed: traversed.iter().join("/"),
                    }),
                },
            ),
        }
    }

    fn add_path<'path, 'components: 'path, PathImpl, Inserter>(
        &mut self,
        full_path: &'path Path<'components, PathImpl>,
        mut traversed: Vec<&'components str>,
        name: &'components str,
        mut rest: Iter<'path, &'components str>,
        inserter: Inserter,
    ) -> Result<(), Error>
    where
        PathImpl: AsRef<[&'components str]>,
        Inserter: FnOnce(
            &mut HashMap<String, TreeBuilder>,
            &str,
            &Path<'components, PathImpl>,
            Vec<&'components str>,
        ) -> Result<(), Error>,
    {
        if name.len() as u64 >= MAX_FILENAME {
            return Err(Error::ComponentNameTooLong {
                path: full_path.to_string(),
                component: name.to_string(),
                component_len: name.len(),
                max_len: (MAX_FILENAME - 1) as usize,
            });
        }

        if name.contains('/') {
            return Err(Error::SlashInComponent {
                path: full_path.to_string(),
                component: name.to_string(),
            });
        }

        match self {
            TreeBuilder::Directory(entries) => match rest.next() {
                None => inserter(entries, name, full_path, traversed),
                Some(next_component) => {
                    traversed.push(name);
                    match entries.get_mut(name) {
                        None => {
                            let mut child = TreeBuilder::Directory(HashMap::new());
                            child.add_path(full_path, traversed, next_component, rest, inserter)?;
                            let existing = entries.insert(name.to_string(), child);
                            assert!(existing.is_none());
                            Ok(())
                        }
                        Some(children) => {
                            children.add_path(full_path, traversed, next_component, rest, inserter)
                        }
                    }
                }
            },
            TreeBuilder::Leaf(_) => Err(Error::EntryInsideLeaf {
                path: full_path.to_string(),
                traversed: traversed.iter().join("/"),
            }),
        }
    }

    /// Consumes the builder, producing a tree with all the nodes provided to [`add_entry()`] at
    /// their respective locations.  The tree itself is built using [`directory::simple::Simple`]
    /// nodes, and the top level is a directory.
    pub fn build(self) -> Arc<immutable::Simple> {
        match self {
            TreeBuilder::Directory(mut entries) => {
                let res = immutable::simple();
                for (name, child) in entries.drain() {
                    res.clone()
                        .add_entry(&name, child.build_dyn())
                        .map_err(|status| format!("Status: {}", status))
                        .expect(
                            "Internal error.  We have already checked all the entry names. \
                             There should be no collisions, nor overly long names.",
                        );
                }
                res
            }
            TreeBuilder::Leaf(_) => {
                panic!("Leaf nodes should not be buildable through the public API.")
            }
        }
    }

    fn build_dyn(self) -> Arc<dyn DirectoryEntry> {
        match self {
            TreeBuilder::Directory(mut entries) => {
                let res = immutable::simple();
                for (name, child) in entries.drain() {
                    res.clone()
                        .add_entry(&name, child.build_dyn())
                        .map_err(|status| format!("Status: {}", status))
                        .expect(
                            "Internal error.  We have already checked all the entry names. \
                             There should be no collisions, nor overly long names.",
                        );
                }
                res
            }
            TreeBuilder::Leaf(entry) => entry,
        }
    }
}

#[derive(Debug, Error, PartialEq, Eq)]
pub enum Error {
    #[error("`add_entry` requires a non-empty path")]
    EmptyPath,

    #[error(
        "Path component contains a forward slash.\n\
                   Path: {}\n\
                   Component: '{}'",
        path,
        component
    )]
    SlashInComponent { path: String, component: String },

    #[error(
        "Path component name is too long - {} characters.  Maximum is {}.\n\
                   Path: {}\n\
                   Component: '{}'",
        component_len,
        max_len,
        path,
        component
    )]
    ComponentNameTooLong { path: String, component: String, component_len: usize, max_len: usize },

    #[error(
        "Trying to insert a leaf over an existing directory.\n\
                   Path: {}",
        path
    )]
    LeafOverDirectory { path: String },

    #[error(
        "Trying to overwrite one leaf with another.\n\
                   Path: {}",
        path
    )]
    LeafOverLeaf { path: String },

    #[error(
        "Trying to insert an entry inside a leaf.\n\
                   Leaf path: {}\n\
                   Path been inserted: {}",
        path,
        traversed
    )]
    EntryInsideLeaf { path: String, traversed: String },
}

#[cfg(test)]
mod tests {
    use super::{Error, TreeBuilder};

    // Macros are exported into the root of the crate.
    use crate::{
        assert_close, assert_event, assert_read, assert_read_dirents,
        assert_read_dirents_one_listing, assert_read_dirents_path_one_listing,
        open_as_file_assert_content, open_get_directory_proxy_assert_ok,
        open_get_file_proxy_assert_ok, open_get_proxy_assert,
    };

    use crate::{
        directory::{immutable::simple, test_utils::run_server_client},
        file::pcb::asynchronous::read_only_static,
    };

    use {
        fidl_fuchsia_io::{MAX_FILENAME, OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE},
        vfs_macros::pseudo_directory,
    };

    #[test]
    fn two_files() {
        let mut tree = TreeBuilder::empty_dir();
        tree.add_entry("a", read_only_static(b"A content")).unwrap();
        tree.add_entry("b", read_only_static(b"B content")).unwrap();

        let root = tree.build();

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            assert_read_dirents_one_listing!(
                root, 1000,
                { DIRECTORY, b"." },
                { FILE, b"a" },
                { FILE, b"b" },
            );
            open_as_file_assert_content!(
                &root,
                OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
                "a",
                "A content"
            );
            open_as_file_assert_content!(
                &root,
                OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
                "b",
                "B content"
            );

            assert_close!(root);
        });
    }

    #[test]
    fn overlapping_paths() {
        let mut tree = TreeBuilder::empty_dir();
        tree.add_entry(&["one", "two"], read_only_static(b"A")).unwrap();
        tree.add_entry(&["one", "three"], read_only_static(b"B")).unwrap();
        tree.add_entry("four", read_only_static(b"C")).unwrap();

        let root = tree.build();

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            assert_read_dirents_one_listing!(
                root, 1000,
                { DIRECTORY, b"." },
                { FILE, b"four" },
                { DIRECTORY, b"one" },
            );
            assert_read_dirents_path_one_listing!(
                &root, "one", 1000,
                { DIRECTORY, b"." },
                { FILE, b"three" },
                { FILE, b"two" },
            );

            open_as_file_assert_content!(
                &root,
                OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
                "one/two",
                "A"
            );
            open_as_file_assert_content!(
                &root,
                OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
                "one/three",
                "B"
            );
            open_as_file_assert_content!(
                &root,
                OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
                "four",
                "C"
            );

            assert_close!(root);
        });
    }

    #[test]
    fn directory_leaf() {
        let etc = pseudo_directory! {
            "fstab" => read_only_static(b"/dev/fs /"),
            "ssh" => pseudo_directory! {
                "sshd_config" => read_only_static(b"# Empty"),
            },
        };

        let mut tree = TreeBuilder::empty_dir();
        tree.add_entry("etc", etc).unwrap();
        tree.add_entry("uname", read_only_static(b"Fuchsia")).unwrap();

        let root = tree.build();

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            assert_read_dirents_one_listing!(
                root, 1000,
                { DIRECTORY, b"." },
                { DIRECTORY, b"etc" },
                { FILE, b"uname" },
            );
            assert_read_dirents_path_one_listing!(
                &root, "etc", 1000,
                { DIRECTORY, b"." },
                { FILE, b"fstab" },
                { DIRECTORY, b"ssh" },
            );
            assert_read_dirents_path_one_listing!(
                &root, "etc/ssh", 1000,
                { DIRECTORY, b"." },
                { FILE, b"sshd_config" },
            );

            open_as_file_assert_content!(
                &root,
                OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
                "etc/fstab",
                "/dev/fs /"
            );
            open_as_file_assert_content!(
                &root,
                OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
                "etc/ssh/sshd_config",
                "# Empty"
            );
            open_as_file_assert_content!(
                &root,
                OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
                "uname",
                "Fuchsia"
            );

            assert_close!(root);
        });
    }

    #[test]
    fn add_empty_dir_populate_later() {
        let mut tree = TreeBuilder::empty_dir();
        tree.add_empty_dir(&["one", "two"]).unwrap();
        tree.add_entry(&["one", "two", "three"], read_only_static(b"B")).unwrap();

        let root = tree.build();

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            assert_read_dirents_one_listing!(
                root, 1000,
                { DIRECTORY, b"." },
                { DIRECTORY, b"one" },
            );
            assert_read_dirents_path_one_listing!(
                &root, "one", 1000,
                { DIRECTORY, b"." },
                { DIRECTORY, b"two" },
            );
            assert_read_dirents_path_one_listing!(
                &root, "one/two", 1000,
                { DIRECTORY, b"." },
                { FILE, b"three" },
            );

            open_as_file_assert_content!(
                &root,
                OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
                "one/two/three",
                "B"
            );

            assert_close!(root);
        });
    }

    #[test]
    fn add_empty_dir_already_exists() {
        let mut tree = TreeBuilder::empty_dir();
        tree.add_entry(&["one", "two", "three"], read_only_static(b"B")).unwrap();
        tree.add_empty_dir(&["one", "two"]).unwrap();

        let root = tree.build();

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            assert_read_dirents_one_listing!(
                root, 1000,
                { DIRECTORY, b"." },
                { DIRECTORY, b"one" },
            );
            assert_read_dirents_path_one_listing!(
                &root, "one", 1000,
                { DIRECTORY, b"." },
                { DIRECTORY, b"two" },
            );
            assert_read_dirents_path_one_listing!(
                &root, "one/two", 1000,
                { DIRECTORY, b"." },
                { FILE, b"three" },
            );

            open_as_file_assert_content!(
                &root,
                OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
                "one/two/three",
                "B"
            );

            assert_close!(root);
        });
    }

    #[test]
    fn lone_add_empty_dir() {
        let mut tree = TreeBuilder::empty_dir();
        tree.add_empty_dir(&["just-me"]).unwrap();

        let root = tree.build();

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            assert_read_dirents_one_listing!(
                root, 1000,
                { DIRECTORY, b"." },
                { DIRECTORY, b"just-me" },
            );
            assert_read_dirents_path_one_listing!(
                &root, "just-me", 1000,
                { DIRECTORY, b"." },
            );
            assert_close!(root);
        });
    }

    #[test]
    fn add_empty_dir_inside_add_empty_dir() {
        let mut tree = TreeBuilder::empty_dir();
        tree.add_empty_dir(&["container"]).unwrap();
        tree.add_empty_dir(&["container", "nested"]).unwrap();

        let root = tree.build();

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            assert_read_dirents_one_listing!(
                root, 1000,
                { DIRECTORY, b"." },
                { DIRECTORY, b"container" },
            );
            assert_read_dirents_path_one_listing!(
                &root, "container", 1000,
                { DIRECTORY, b"." },
                { DIRECTORY, b"nested" },
            );
            assert_read_dirents_path_one_listing!(
                &root, "container/nested", 1000,
                { DIRECTORY, b"." },
            );
            assert_close!(root);
        });
    }

    #[test]
    fn error_empty_path_in_add_entry() {
        let mut tree = TreeBuilder::empty_dir();
        let err = tree
            .add_entry(vec![], read_only_static(b"Invalid"))
            .expect_err("Empty paths are not allowed.");
        assert_eq!(err, Error::EmptyPath);
    }

    #[test]
    fn error_slash_in_component() {
        let mut tree = TreeBuilder::empty_dir();
        let err = tree
            .add_entry("a/b", read_only_static(b"Invalid"))
            .expect_err("Slash in path component name.");
        assert_eq!(
            err,
            Error::SlashInComponent { path: "a/b".to_string(), component: "a/b".to_string() }
        );
    }

    #[test]
    fn error_slash_in_second_component() {
        let mut tree = TreeBuilder::empty_dir();
        let err = tree
            .add_entry(&["a", "b/c"], read_only_static(b"Invalid"))
            .expect_err("Slash in path component name.");
        assert_eq!(
            err,
            Error::SlashInComponent { path: "a/b/c".to_string(), component: "b/c".to_string() }
        );
    }

    #[test]
    fn error_component_name_too_long() {
        let mut tree = TreeBuilder::empty_dir();

        let long_component = "abcdefghij".repeat(MAX_FILENAME as usize / 10 + 1);

        let path: &[&str] = &["a", &long_component, "b"];
        let err = tree
            .add_entry(path, read_only_static(b"Invalid"))
            .expect_err("Individual component names may not exceed MAX_FILENAME bytes.");
        assert_eq!(
            err,
            Error::ComponentNameTooLong {
                path: format!("a/{}/b", long_component),
                component: long_component.clone(),
                component_len: long_component.len(),
                max_len: (MAX_FILENAME - 1) as usize,
            }
        );
    }

    #[test]
    fn error_leaf_over_directory() {
        let mut tree = TreeBuilder::empty_dir();

        tree.add_entry(&["top", "nested", "file"], read_only_static(b"Content")).unwrap();
        let err = tree
            .add_entry(&["top", "nested"], read_only_static(b"Invalid"))
            .expect_err("A leaf may not be constructed over a directory.");
        assert_eq!(err, Error::LeafOverDirectory { path: "top/nested".to_string() });
    }

    #[test]
    fn error_leaf_over_leaf() {
        let mut tree = TreeBuilder::empty_dir();

        tree.add_entry(&["top", "nested", "file"], read_only_static(b"Content")).unwrap();
        let err = tree
            .add_entry(&["top", "nested", "file"], read_only_static(b"Invalid"))
            .expect_err("A leaf may not be constructed over another leaf.");
        assert_eq!(err, Error::LeafOverLeaf { path: "top/nested/file".to_string() });
    }

    #[test]
    fn error_entry_inside_leaf() {
        let mut tree = TreeBuilder::empty_dir();

        tree.add_entry(&["top", "file"], read_only_static(b"Content")).unwrap();
        let err = tree
            .add_entry(&["top", "file", "nested"], read_only_static(b"Invalid"))
            .expect_err("A leaf may not be constructed over another leaf.");
        assert_eq!(
            err,
            Error::EntryInsideLeaf {
                path: "top/file/nested".to_string(),
                traversed: "top/file".to_string()
            }
        );
    }

    #[test]
    fn error_entry_inside_leaf_directory() {
        let mut tree = TreeBuilder::empty_dir();

        // Even when a leaf is itself a directory the tree builder cannot insert a nested entry.
        tree.add_entry(&["top", "file"], simple()).unwrap();
        let err = tree
            .add_entry(&["top", "file", "nested"], read_only_static(b"Invalid"))
            .expect_err("A leaf may not be constructed over another leaf.");
        assert_eq!(
            err,
            Error::EntryInsideLeaf {
                path: "top/file/nested".to_string(),
                traversed: "top/file".to_string()
            }
        );
    }
}
