// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    borrow::Cow,
    sync::atomic::{AtomicUsize, Ordering},
};

/// This is the generator for StringReference ID's.
static NEXT_STRING_REFERENCE_ID: AtomicUsize = AtomicUsize::new(0);

/// StringReference is a type that can be constructed and passed into
/// the Inspect API as a name of a Node. If this is done, only one
/// reference counted instance of the string will be allocated per
/// Inspector. They can be safely used with LazyNodes.
pub struct StringReference<'a> {
    // The canonical data referred to by this instance.
    data: Cow<'a, str>,

    // The identifier used by state to locate this reference.
    reference_id: usize,
}

impl<'a> StringReference<'a> {
    /// Construct a StringReference with non-owned data.
    pub fn new(data: &'a str) -> Self {
        Self {
            data: Cow::Borrowed(data),
            reference_id: NEXT_STRING_REFERENCE_ID.fetch_add(1, Ordering::SeqCst),
        }
    }

    /// Construct a StringReference with owned data. For internal use. To construct
    /// an owning StringReference with the public API, use from(String).
    fn new_owned(data: String) -> Self {
        Self {
            data: Cow::Owned(data),
            reference_id: NEXT_STRING_REFERENCE_ID.fetch_add(1, Ordering::SeqCst),
        }
    }

    /// Access a read-only reference to the data in a StringReference.
    pub(crate) fn data(&self) -> &str {
        &self.data
    }

    /// Get the ID of this StringReference for State. Note that this is not
    /// necessarily equivalent to the block index of the StringReference in the VMO.
    pub(crate) fn id(&self) -> usize {
        self.reference_id
    }
}

impl<'a> From<&'a StringReference<'a>> for StringReference<'a> {
    fn from(sf: &'a StringReference<'a>) -> Self {
        Self { data: Cow::Borrowed(sf.data()), reference_id: sf.reference_id }
    }
}

impl<'a> From<&'a str> for StringReference<'a> {
    fn from(data: &'a str) -> Self {
        StringReference::new(data)
    }
}

impl<'a> From<&'a String> for StringReference<'a> {
    fn from(data: &'a String) -> Self {
        StringReference::new(data)
    }
}

/// This trait allows the construction of a StringReference that owns its data.
impl From<String> for StringReference<'_> {
    fn from(data: String) -> Self {
        StringReference::new_owned(data)
    }
}

impl<'a> From<Cow<'a, str>> for StringReference<'a> {
    fn from(data: Cow<'a, str>) -> StringReference<'a> {
        StringReference::new_owned(data.into_owned())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::writer::{testing_utils::get_state, Inspector, Node};
    use diagnostics_hierarchy::assert_data_tree;
    use lazy_static::lazy_static;

    #[test]
    fn string_references_as_names() {
        lazy_static! {
            static ref FOO: StringReference<'static> = "foo".into();
            static ref BAR: StringReference<'static> = "bar".into();
            static ref BAZ: StringReference<'static> = "baz".into();
        };

        let inspector = Inspector::new();
        inspector.root().record_int(&*FOO, 0);
        let child = inspector.root().create_child(&*BAR);
        child.record_double(&*FOO, 3.25);

        assert_data_tree!(inspector, root: {
            foo: 0i64,
            bar: {
                foo: 3.25,
            },
        });

        {
            let _baz_property = child.create_uint(&*BAZ, 4);
            assert_data_tree!(inspector, root: {
                foo: 0i64,
                bar: {
                    baz: 4u64,
                    foo: 3.25,
                },
            });
        }

        assert_data_tree!(inspector, root: {
            foo: 0i64,
            bar: {
                foo: 3.25,
            },
        });

        let pre_loop_allocated =
            inspector.state().unwrap().try_lock().unwrap().stats().allocated_blocks;
        let pre_loop_deallocated =
            inspector.state().unwrap().try_lock().unwrap().stats().deallocated_blocks;

        for i in 0..300 {
            child.record_int(&*BAR, i);
        }

        assert_eq!(
            inspector.state().unwrap().try_lock().unwrap().stats().allocated_blocks,
            pre_loop_allocated + 300
        );
        assert_eq!(
            inspector.state().unwrap().try_lock().unwrap().stats().deallocated_blocks,
            pre_loop_deallocated
        );

        let pre_loop_count = pre_loop_allocated + 300;

        for i in 0..300 {
            child.record_int("abcd", i);
        }

        assert_eq!(
            inspector.state().unwrap().try_lock().unwrap().stats().allocated_blocks,
            pre_loop_count + 300 /* the int blocks */ + 300 /* individual blocks for "abcd" */
        );
        assert_eq!(
            inspector.state().unwrap().try_lock().unwrap().stats().deallocated_blocks,
            pre_loop_deallocated
        );
    }

    #[test]
    fn owned_method_argument_properties() {
        let state = get_state(4096);
        let root = Node::new_root(state);
        let node = root.create_child("node");
        let node_block = node.get_block().unwrap();
        {
            let _string_property =
                node.create_string(String::from("string_property"), String::from("test"));
            let _bytes_property =
                node.create_bytes(String::from("bytes_property"), vec![0, 1, 2, 3]);
            let _double_property = node.create_double(String::from("double_property"), 1.0);
            let _int_property = node.create_int(String::from("int_property"), 1);
            let _uint_property = node.create_uint(String::from("uint_property"), 1);
            assert_eq!(node_block.child_count().unwrap(), 5);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }
}
