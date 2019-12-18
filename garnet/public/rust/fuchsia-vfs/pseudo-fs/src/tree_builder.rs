// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A helper to build a tree of directory nodes.  It is useful in case when a nested tree is
//! desired, with specific nodes to be inserted as the leafs of this tree.  It is similar to the
//! functionality provided by the [`pseudo_directory`] macro, except that the macro expects the
//! tree structure to be defined at compile time, while this helper allows the tree structure to be
//! dynamic.

use crate::directory::{self, entry::DirectoryEntry};

use {
    failure::Fail,
    fidl_fuchsia_io::MAX_FILENAME,
    itertools::Itertools,
    std::{collections::HashMap, fmt, marker::PhantomData, slice::Iter},
};

/// Represents a paths provided to [`TreeBuilder::add_entry()`].  See [`TreeBuilder`] for details.
// I think it would be a bit more straightforward to have two different types that implement a
// `Path` trait, `OwnedPath` and `SharedPath`.  But, `add_entry` then needs two type variables: one
// for the type of the value passed in, and one for the type of the `Path` trait (either
// `OwnedPath` or `SharedPath`).  Type inference fails with two variables requiring explicit type
// annotation.  And that defeats the whole purpose of the overloading in the API.
//
//     pub fn add_entry<'path, 'components: 'path, F, P: 'path, DE>(
//         &mut self,
//         path: F,
//         entry: DE,
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

pub enum TreeBuilder<'entries> {
    Directory(HashMap<String, TreeBuilder<'entries>>),
    Leaf(Box<dyn DirectoryEntry + 'entries>),
}

/// Collects a number of [`DirectoryEntry`] nodes and corresponding paths and the constructs a tree
/// of [`directory::simple::Simple`] directories that hold these nodes.  This is a companion tool,
/// related to the [`pseudo_directory!`] macro, except that it is collecting the paths dynamically,
/// while the [`pseudo_directory!`] expects the tree to be specified at compilation time.
///
/// Note that the final tree is build as a result of the [`build()`] method that consumes the
/// builder.  You would need to use the [`directory::Simple::add_entry()`] interface to add any new
/// nodes afterwards (a [`directory::controlled::Controller`] APIs).
impl<'entries> TreeBuilder<'entries> {
    /// Constructs an empty builder.  It is always an empty [`Simple`] directory.
    pub fn empty_dir() -> Self {
        TreeBuilder::Directory(HashMap::new())
    }

    /// Adds a [`DirectoryEntry`] at the specified path.  It can be either a file or a directory.
    /// In case it is a directory, this builder can not add new child nodes inside of the added
    /// directory.  Any `entry` is treated as an opaque "leaf" as far as the builder is concerned.
    ///
    /// Also see [`add_boxed_entry()`].
    pub fn add_entry<'components, P: 'components, PathImpl, DE>(
        &mut self,
        path: P,
        entry: DE,
    ) -> Result<(), Error>
    where
        P: Into<Path<'components, PathImpl>>,
        PathImpl: AsRef<[&'components str]>,
        DE: DirectoryEntry + 'entries,
    {
        self.add_boxed_entry(path, Box::new(entry))
    }

    /// Identical to [`add_entry()`] except that the `entry` is [`Box`]ed.
    pub fn add_boxed_entry<'components, P: 'components, PathImpl>(
        &mut self,
        path: P,
        entry: Box<dyn DirectoryEntry + 'entries>,
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
            Some(name) => self.add_entry_impl(path.iter(), traversed, name, rest, entry),
        }
    }

    fn add_entry_impl<'path, 'components: 'path>(
        &mut self,
        mut full_path: Iter<'path, &'components str>,
        mut traversed: Vec<&'components str>,
        name: &'components str,
        mut rest: Iter<'path, &'components str>,
        entry: Box<dyn DirectoryEntry + 'entries>,
    ) -> Result<(), Error> {
        if name.len() as u64 >= MAX_FILENAME {
            return Err(Error::ComponentNameTooLong {
                path: full_path.join("/"),
                component: name.to_string(),
                component_len: name.len(),
                max_len: (MAX_FILENAME - 1) as usize,
            });
        }

        if name.contains('/') {
            return Err(Error::SlashInComponent {
                path: full_path.join("/"),
                component: name.to_string(),
            });
        }

        match self {
            TreeBuilder::Directory(entries) => match rest.next() {
                None => match entries.insert(name.to_string(), TreeBuilder::Leaf(entry)) {
                    None => Ok(()),
                    Some(TreeBuilder::Directory(_)) => {
                        Err(Error::LeafOverDirectory { path: full_path.join("/") })
                    }
                    Some(TreeBuilder::Leaf(_)) => {
                        Err(Error::LeafOverLeaf { path: full_path.join("/") })
                    }
                },
                Some(next_component) => {
                    traversed.push(name);
                    match entries.get_mut(name) {
                        None => {
                            let mut child = TreeBuilder::Directory(HashMap::new());
                            child.add_entry_impl(
                                full_path,
                                traversed,
                                next_component,
                                rest,
                                entry,
                            )?;
                            let existing = entries.insert(name.to_string(), child);
                            assert!(existing.is_none());
                            Ok(())
                        }
                        Some(children) => children.add_entry_impl(
                            full_path,
                            traversed,
                            next_component,
                            rest,
                            entry,
                        ),
                    }
                }
            },
            TreeBuilder::Leaf(_) => Err(Error::EntryInsideLeaf {
                path: full_path.join("/"),
                traversed: traversed.iter().join("/"),
            }),
        }
    }

    /// Consumes the builder, producing a tree with all the nodes provided to [`add_entry()`] at
    /// their respective locations.  The tree itself is built using [`directory::simple::Simple`]
    /// nodes, and the top level is a directory.
    pub fn build(self) -> directory::simple::Simple<'entries> {
        match self {
            TreeBuilder::Directory(mut entries) => {
                let mut res = directory::simple::empty();
                for (name, child) in entries.drain() {
                    res.add_boxed_entry(&name, child.build_dyn())
                        .map_err(|(status, _entry)| format!("Status: {}", status))
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

    fn build_dyn(self) -> Box<dyn DirectoryEntry + 'entries> {
        match self {
            TreeBuilder::Directory(mut entries) => {
                let mut res = directory::simple::empty();
                for (name, child) in entries.drain() {
                    res.add_boxed_entry(&name, child.build_dyn())
                        .map_err(|(status, _entry)| format!("Status: {}", status))
                        .expect(
                            "Internal error.  We have already checked all the entry names. \
                             There should be no collisions, nor overly long names.",
                        );
                }
                Box::new(res)
            }
            TreeBuilder::Leaf(entry) => entry,
        }
    }
}

#[derive(Debug, Fail, PartialEq, Eq)]
pub enum Error {
    #[fail(display = "`add_entry` requires a non-empty path")]
    EmptyPath,

    #[fail(
        display = "Path compoent contains a forward slash.\n\
                   Path: {}\n\
                   Component: '{}'",
        path, component
    )]
    SlashInComponent { path: String, component: String },

    #[fail(
        display = "Path component name is too long - {} characters.  Maximum is {}.\n\
                   Path: {}\n\
                   Component: '{}'",
        component_len, max_len, path, component
    )]
    ComponentNameTooLong { path: String, component: String, component_len: usize, max_len: usize },

    #[fail(
        display = "Trying to insert a leaf over an existing directory.\n\
                   Path: {}",
        path
    )]
    LeafOverDirectory { path: String },

    #[fail(
        display = "Trying to overwrite one leaf with another.\n\
                   Path: {}",
        path
    )]
    LeafOverLeaf { path: String },

    #[fail(
        display = "Trying to insert an entry inside a leaf.\n\
                   Leaf path: {}\n\
                   Path been inserted: {}",
        path, traversed
    )]
    EntryInsideLeaf { path: String, traversed: String },
}

#[cfg(test)]
mod tests {
    use super::{Error, TreeBuilder};

    // Macros are exported into the root of the crate.
    use crate::{assert_close, assert_read_dirents, open_as_file_assert_content};

    use crate::{
        directory::{simple, test_utils::run_server_client},
        file::simple::read_only_static,
    };

    use {
        fidl_fuchsia_io::{MAX_FILENAME, OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE},
        proc_macro_hack::proc_macro_hack,
    };

    // Create level import of this macro does not affect nested modules.  And as attributes can
    // only be applied to the whole "use" directive, this need to be present here and need to be
    // separate form the above.  "use crate::pseudo_directory" generates a warning referring to
    // "issue #52234 <https://github.com/rust-lang/rust/issues/52234>".
    #[proc_macro_hack(support_nested)]
    use fuchsia_vfs_pseudo_fs_macros::pseudo_directory;

    #[test]
    fn simple() {
        let mut tree = TreeBuilder::empty_dir();
        tree.add_entry("a", read_only_static("A content")).unwrap();
        tree.add_entry("b", read_only_static("B content")).unwrap();

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
        tree.add_entry(&["one", "two"], read_only_static("A")).unwrap();
        tree.add_entry(&["one", "three"], read_only_static("B")).unwrap();
        tree.add_entry("four", read_only_static("C")).unwrap();

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
            "fstab" => read_only_static("/dev/fs /"),
            "ssh" => pseudo_directory! {
                "sshd_config" => read_only_static("# Empty"),
            },
        };

        let mut tree = TreeBuilder::empty_dir();
        tree.add_entry("etc", etc).unwrap();
        tree.add_entry("uname", read_only_static("Fuchsia")).unwrap();

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
    fn error_empty_path_in_add_entry() {
        let mut tree = TreeBuilder::empty_dir();
        let err = tree
            .add_entry(vec![], read_only_static("Invalid"))
            .expect_err("Empty paths are not allowed.");
        assert_eq!(err, Error::EmptyPath);
    }

    #[test]
    fn error_slash_in_compoenent() {
        let mut tree = TreeBuilder::empty_dir();
        let err = tree
            .add_entry("a/b", read_only_static("Invalid"))
            .expect_err("Slash in path compoenent name.");
        assert_eq!(
            err,
            Error::SlashInComponent { path: "a/b".to_string(), component: "a/b".to_string() }
        );
    }

    #[test]
    fn error_slash_in_second_compoenent() {
        let mut tree = TreeBuilder::empty_dir();
        let err = tree
            .add_entry(&["a", "b/c"], read_only_static("Invalid"))
            .expect_err("Slash in path compoenent name.");
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
            .add_entry(path, read_only_static("Invalid"))
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

        tree.add_entry(&["top", "nested", "file"], read_only_static("Content")).unwrap();
        let err = tree
            .add_entry(&["top", "nested"], read_only_static("Invalid"))
            .expect_err("A leaf may not be constructed over a directory.");
        assert_eq!(err, Error::LeafOverDirectory { path: "top/nested".to_string() });
    }

    #[test]
    fn error_leaf_over_leaf() {
        let mut tree = TreeBuilder::empty_dir();

        tree.add_entry(&["top", "nested", "file"], read_only_static("Content")).unwrap();
        let err = tree
            .add_entry(&["top", "nested", "file"], read_only_static("Invalid"))
            .expect_err("A leaf may not be constructed over another leaf.");
        assert_eq!(err, Error::LeafOverLeaf { path: "top/nested/file".to_string() });
    }

    #[test]
    fn error_entry_inside_leaf() {
        let mut tree = TreeBuilder::empty_dir();

        tree.add_entry(&["top", "file"], read_only_static("Content")).unwrap();
        let err = tree
            .add_entry(&["top", "file", "nested"], read_only_static("Invalid"))
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

        // Even when a leaf is itself a directory the tree builder can not insert a nested entry.
        tree.add_entry(&["top", "file"], simple::empty()).unwrap();
        let err = tree
            .add_entry(&["top", "file", "nested"], read_only_static("Invalid"))
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
