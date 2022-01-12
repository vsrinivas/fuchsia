// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::marker::PhantomData;

use crate::writer::{
    private::InspectTypeInternal, ArrayProperty, Inner, InnerValueType, InspectType, State,
    StringReference,
};

#[derive(Debug, PartialEq, Eq, Default)]
pub struct StringArrayProperty<'a> {
    inner: Inner<InnerValueType>,
    phantom: PhantomData<&'a ()>,
}

impl InspectType for StringArrayProperty<'_> {}

impl InspectTypeInternal for StringArrayProperty<'_> {
    fn new(state: State, block_index: u32) -> Self {
        Self { inner: Inner::new(state, block_index), phantom: PhantomData }
    }

    fn is_valid(&self) -> bool {
        self.inner.is_valid()
    }

    fn new_no_op() -> Self {
        Self { inner: Inner::None, phantom: PhantomData }
    }

    fn state(&self) -> Option<State> {
        Some(self.inner.inner_ref()?.state.clone())
    }

    fn block_index(&self) -> Option<u32> {
        Some(self.inner.inner_ref()?.block_index)
    }
}

impl<'a> ArrayProperty for StringArrayProperty<'a> {
    type Type = StringReference<'a>;

    fn set(&self, index: usize, value: impl Into<Self::Type>) {
        if let Some(ref inner_ref) = self.inner.inner_ref() {
            inner_ref
                .state
                .try_lock()
                .and_then(|mut state| {
                    state.set_array_string_slot(inner_ref.block_index, index, value)
                })
                .ok();
        }
    }

    fn clear(&self) {
        if let Some(ref inner_ref) = self.inner.inner_ref() {
            inner_ref
                .state
                .try_lock()
                .and_then(|mut state| state.clear_array(inner_ref.block_index, 0))
                .ok();
        }
    }
}

impl Drop for StringArrayProperty<'_> {
    fn drop(&mut self) {
        self.clear();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::assert_json_diff;
    use crate::hierarchy::DiagnosticsHierarchy;
    use crate::Inspector;

    impl StringArrayProperty<'_> {
        pub fn load_string_slot(&self, slot: usize) -> Option<String> {
            self.inner.inner_ref().and_then(|inner_ref| {
                inner_ref
                    .state
                    .try_lock()
                    .and_then(|mut state| {
                        state.load_string(
                            state
                                .heap()
                                .get_block(self.block_index().unwrap())
                                .unwrap()
                                .array_get_string_index_slot(slot)?,
                        )
                    })
                    .ok()
            })
        }
    }

    #[fuchsia::test]
    fn string_array_property() {
        let inspector = Inspector::new();
        let root = inspector.root();
        let node = root.create_child("node");
        let node_block = node.get_block().unwrap();

        {
            let array = node.create_string_array("string_array", 5);
            assert_eq!(node_block.child_count().unwrap(), 1);

            array.set(0, "0");
            array.set(1, "1");
            array.set(2, "2");
            array.set(3, "3");
            array.set(4, "4");

            // this should fail silently
            array.set(5, "5");
            assert!(array.load_string_slot(5).is_none());

            let expected: Vec<String> =
                vec!["0".into(), "1".into(), "2".into(), "3".into(), "4".into()];

            assert_json_diff!(inspector, root: {
                node: {
                    string_array: expected,
                },
            });

            array.clear();

            let expected: Vec<String> = vec![String::new(); 5];

            assert_json_diff!(inspector, root: {
                node: {
                    string_array: expected,
                },
            });

            assert!(array.load_string_slot(5).is_none());
        }

        assert_eq!(node_block.child_count().unwrap(), 0);
    }
}
