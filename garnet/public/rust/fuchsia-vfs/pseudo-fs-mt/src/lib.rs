// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A library to create "pseudo" file systems.  These file systems are backed by in process
//! callbacks.  Examples are: component configuration, debug information or statistics.

#![recursion_limit = "1024"]

use proc_macro_hack::proc_macro_hack;

pub mod test_utils;

pub mod common;

pub mod execution_scope;
pub mod path;
pub mod registry;

pub mod directory;
pub mod file;
pub mod service;

pub mod tree_builder;

// --- pseudo_directory ---

// pseudo_directory! uses helper functions that live in this module.  It needs to be accessible
// from the outside of this crate.
#[doc(hidden)]
pub mod pseudo_directory;

/// Builds a pseudo directory using a simple DSL, potentially containing files and nested pseudo directories.
///
/// A directory is described using a sequence of rules of the following form:
///
///   <name> `=>` <something that implements DirectoryEntry>
///
/// separated by commas, with an optional trailing comma.
///
/// It generates a nested pseudo directory, using [`directory::simple()`] then adding all the
/// specified entries in it, by calling [`directory::Simple::add_entry`].
///
/// Note: Names specified as literals (both `str` and `[u8]`) are compared during compilation time,
/// so you should get a nice error message, if you specify the same entry name twice.  As entry
/// names can be specified as expressions, you can easily work around this check - you will still
/// get an error, but it would be a `panic!` in this case.  In any case the error message will
/// contain details of the location of the generating macro and the duplicate entry name.
///
/// # Examples
///
/// This will construct a small tree of read-only files:
/// ```
/// let root = pseudo_directory! {
///     "etc" => pseudo_directory! {
///         "fstab" => read_only_static(b"/dev/fs /"),
///         "passwd" => read_only_static(b"[redacted]"),
///         "shells" => read_only_static(b"/bin/bash"),
///         "ssh" => pseudo_directory! {
///           "sshd_config" => read_only_static(b"# Empty"),
///         },
///     },
///     "uname" => read_only_static(b"Fuchsia"),
/// };
/// ```
///
/// An example of a tree with a writable file:
/// ```
/// let write_count = &RefCell::new(0);
/// let root = pseudo_directory! {
///     "etc" => pseudo_directory! {
///         "sshd_config" => read_write(
///           || Ok(b"# Empty".to_vec()),
///           100,
///           |content| {
///               let mut count = write_count.borrow_mut();
///               assert_eq!(*&content, format!("Port {}", 22 + *count).as_bytes());
///               *count += 1;
///               Ok(())
///           }),
///     },
/// };
/// ```
///
/// You can specify the POSIX attributes for the pseudo directory, by providing the attributes as
/// an expression, fater a "protection_attributes" keyword followed by a comma, with a `;`
/// separating it from the entry definitions:
/// ```
/// let root = pseudo_directory! {
///     "etc" => pseudo_directory! {
///         protection_attributes: S_IXOTH | S_IROTH | S_IXGRP | S_IRGRP | S_IXUSR | S_IRUSR;
///         "fstab" => read_only_attr(S_IROTH | S_IRGRP | S_IRUSR,
///                                   || Ok(b"/dev/fs /".to_vec())),
///         "passwd" => read_only_attr(S_IRUSR, || Ok(b"[redacted]".to_vec())),
///     },
/// };
/// ```
#[proc_macro_hack(support_nested)]
pub use fuchsia_vfs_pseudo_fs_mt_macros::pseudo_directory;

// This allows the pseudo_directory! macro to use absolute paths within this crate to refer to the
// helper functions. External crates that use pseudo_directory! will rely on the pseudo_directory
// export above.
extern crate self as fuchsia_vfs_pseudo_fs_mt;
