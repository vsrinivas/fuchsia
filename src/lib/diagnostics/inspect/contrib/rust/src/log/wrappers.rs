// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::WriteInspect;

use crate::nodes::NodeWriter;

/// Wrapper to log bytes in an `inspect_log!` or `inspect_insert!` macro.
///
/// This wrapper is defined because a default `WriteInspect` implementation isn't provided for
/// an array or slice of bytes. Such default implementation was left out so that the user has
/// to explicitly choose whether to log bytes slice as a string or a byte vector in Inspect.
pub struct InspectBytes<'a>(pub &'a [u8]);

impl<'a> WriteInspect for InspectBytes<'a> {
    fn write_inspect(&self, writer: &mut NodeWriter<'_>, key: &str) {
        writer.create_bytes(key, self.0);
    }
}

/// Wrapper to log a list of items in `inspect_log!` or `inspect_insert!` macro. Each item
/// in the list must be a type that implements `WriteInspect`
///
/// Example:
/// ```
/// let list = ["foo", "bar", "baz"];
/// inspect_insert!(node_writer, some_list: list);
/// ```
///
/// The above code snippet would create the following child under node_writer:
/// ```
/// some_list:
///   0: "foo"
///   1: "bar"
///   2: "baz"
/// ```
pub struct InspectList<'a, T>(pub &'a [T]);

impl<'a, T> WriteInspect for InspectList<'a, T>
where
    T: WriteInspect,
{
    fn write_inspect(&self, writer: &mut NodeWriter<'_>, key: &str) {
        let mut child = writer.create_child(key);
        for (i, val) in self.0.iter().enumerate() {
            val.write_inspect(&mut child, &i.to_string());
        }
    }
}

/// Wrapper around a list `[T]` and a closure function `F` that determines how to map
/// and log each value of `T` in `inspect_log!` or `inspect_insert!` macro.
///
/// Example:
/// ```
/// let list = ["foo", "bar", "baz"]
/// let list_mapped = InspectListClosure(&list, |mut node_writer, key, item| {
///     let mapped_item = format!("super{}", item);
///     inspect_insert!(node_writer, var key: mapped_item);
/// });
/// inspect_insert!(node_writer, some_list: list_mapped);
/// ```
///
/// The above code snippet would create the following child under node_writer:
/// ```
/// some_list:
///   0: "superfoo"
///   1: "superbar"
///   2: "superbaz"
/// ```
pub struct InspectListClosure<'a, T, F>(pub &'a [T], pub F)
where
    F: Fn(&mut NodeWriter<'_>, &str, &T);

impl<'a, T, F> WriteInspect for InspectListClosure<'a, T, F>
where
    F: Fn(&mut NodeWriter<'_>, &str, &T),
{
    fn write_inspect(&self, writer: &mut NodeWriter<'_>, key: &str) {
        let mut child = writer.create_child(key);
        for (i, val) in self.0.iter().enumerate() {
            self.1(&mut child, &i.to_string(), val);
        }
    }
}
