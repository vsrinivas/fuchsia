// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error};
use mapped_vmo::Mapping;
use num_traits::ToPrimitive;
use parking_lot::Mutex;
use paste;
use std::marker::PhantomData;
use std::rc::Rc;
use std::sync::Arc;

use crate::vmo::heap::Heap;
use crate::vmo::state::State;

mod bitfields;
mod block;
mod block_type;
mod constants;
mod heap;
mod reader;
mod state;
mod utils;

/// Root of the Inspect API
pub struct Inspector {
    /// The root node.
    root_node: Node,
}

/// Provides functions required to implement formats for a property.
pub trait PropertyFormat {
    fn bytes(&self) -> &[u8];
    fn flag(&self) -> u8;
    fn length_in_bytes(&self) -> u32;
}

/// Inspect API Node data type.
pub struct Node {
    /// Index of the block in the VMO.
    block_index: u32,

    /// Reference to the VMO heap.
    state: Arc<Mutex<State>>,
}

/// Inspect API Property data type.
pub struct Property<T: PropertyFormat> {
    /// Index of the block in the VMO.
    block_index: u32,

    /// Reference to the VMO heap.
    state: Arc<Mutex<State>>,
    phantom: PhantomData<T>,
}

/// Root API for inspect. Used to create the VMO and get the root node.
impl Inspector {
    /// Create a new Inspect VMO object with the given maximum size.
    pub fn new(max_size: usize, name: &str) -> Result<Self, Error> {
        let (mapping, _) = Mapping::allocate(max_size)
            .map_err(|e| format_err!("failed to allocate vmo zx status={}", e))?;
        let heap = Heap::new(Rc::new(mapping))?;
        let state = State::create(heap)?;
        let root_node =
            Node::allocate(Arc::new(Mutex::new(state)), name, constants::ROOT_PARENT_INDEX)?;
        Ok(Inspector { root_node })
    }

    /// Create the root of the VMO object with the given |name|.
    pub fn root(&self) -> &Node {
        &self.root_node
    }
}

/// Implementation of property for a byte vector.
impl PropertyFormat for &[u8] {
    fn bytes(&self) -> &[u8] {
        &self
    }

    fn flag(&self) -> u8 {
        constants::PROPERTY_FLAG_BYTE_VECTOR
    }

    fn length_in_bytes(&self) -> u32 {
        self.len().to_u32().unwrap()
    }
}

/// Implementation of property for a string.
impl PropertyFormat for &str {
    fn bytes(&self) -> &[u8] {
        self.as_bytes()
    }

    fn flag(&self) -> u8 {
        constants::PROPERTY_FLAG_STRING
    }

    fn length_in_bytes(&self) -> u32 {
        self.bytes().len().to_u32().unwrap()
    }
}

/// Implementation of property for a string.
impl PropertyFormat for String {
    fn bytes(&self) -> &[u8] {
        self.as_bytes()
    }

    fn flag(&self) -> u8 {
        constants::PROPERTY_FLAG_STRING
    }

    fn length_in_bytes(&self) -> u32 {
        self.bytes().len().to_u32().unwrap()
    }
}

/// Utility for generating functions to create a metric.
///   `name`: identifier for the name (example: double)
///   `type`: the type of the metric (example: f64)
macro_rules! create_metric_fn {
    ($name:ident, $name_cap:ident, $type:ident) => {
        paste::item! {
            pub fn [<create_ $name _metric>](&self, name: &str, value: $type)
                -> Result<[<$name_cap Metric>], Error> {
                let block = self.state.lock().[<create_ $name _metric>](
                    name, value, self.block_index)?;
                Ok([<$name_cap Metric>] {state: self.state.clone(), block_index: block.index() })
            }
        }
    };
}

impl Node {
    /// Allocate a new NODE object. Called through Inspector.
    pub(in crate::vmo) fn allocate(
        state: Arc<Mutex<State>>,
        name: &str,
        parent_index: u32,
    ) -> Result<Self, Error> {
        let block = state.lock().create_node(name, parent_index)?;
        Ok(Node { state: state.clone(), block_index: block.index() })
    }

    /// Add a child to this node.
    pub fn create_child(&self, name: &str) -> Result<Node, Error> {
        let block = self.state.lock().create_node(name, self.block_index)?;
        Ok(Node { state: self.state.clone(), block_index: block.index() })
    }

    /// Add a metric to this node: create_int_metric, create_double_metric,
    /// create_uint_metric.
    create_metric_fn!(int, Int, i64);
    create_metric_fn!(uint, Uint, u64);
    create_metric_fn!(double, Double, f64);

    /// Add a property to this node.
    pub fn create_property<T: PropertyFormat>(
        &self,
        name: &str,
        value: T,
    ) -> Result<Property<T>, Error> {
        let block = self.state.lock().create_property(name, value, self.block_index)?;
        Ok(Property::<T> {
            state: self.state.clone(),
            block_index: block.index(),
            phantom: PhantomData,
        })
    }
}

impl Drop for Node {
    fn drop(&mut self) {
        self.state
            .lock()
            .free_value(self.block_index)
            .expect(&format!("Failed to free node index={}", self.block_index));
    }
}

/// Utility for generating metric functions (example: set, add, subtract)
///   `fn_name`: the name of the function to generate (example: set)
///   `type`: the type of the argument of the function to generate (example: f64)
///   `name`: the readble name of the type of the function (example: double)
macro_rules! metric_fn {
    ($fn_name:ident, $type:ident, $name:ident) => {
        paste::item! {
            pub fn $fn_name(&self, value: $type) -> Result<(), Error> {
                self.state.lock().[<$fn_name _ $name _metric>](self.block_index, value)
            }
        }
    };
}

/// Utility for generating a metric datatype impl
///   `name`: the readble name of the type of the function (example: double)
///   `type`: the type of the argument of the function to generate (example: f64)
macro_rules! metric {
    ($name:ident, $name_cap:ident, $type:ident) => {
        paste::item! {
            /// Inspect API Metric data type.
            pub struct [<$name_cap Metric>] {
                /// Index of the block in the VMO.
                block_index: u32,

                /// Reference to the VMO heap.
                state: Arc<Mutex<State>>,
            }

            impl [<$name_cap Metric>] {
                metric_fn!(set, $type, $name);
                metric_fn!(add, $type, $name);
                metric_fn!(subtract, $type, $name);
            }

            impl Drop for [<$name_cap Metric>] {
                fn drop(&mut self) {
                    self.state
                        .lock()
                        .free_value(self.block_index)
                        .expect(&format!("Failed to free metric index={}", self.block_index));
                }
            }
        }
    };
}

metric!(int, Int, i64);
metric!(uint, Uint, u64);
metric!(double, Double, f64);

impl<T: PropertyFormat> Property<T> {
    /// Set a property value.
    pub fn set(&self, value: T) -> Result<(), Error> {
        self.state.lock().set_property(self.block_index, value)
    }
}

impl<T: PropertyFormat> Drop for Property<T> {
    fn drop(&mut self) {
        self.state
            .lock()
            .free_property(self.block_index)
            .expect(&format!("Failed to free property index={}", self.block_index));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::vmo::block_type::BlockType;
    use crate::vmo::constants;
    use crate::vmo::heap::Heap;
    use mapped_vmo::Mapping;
    use std::rc::Rc;

    #[test]
    fn node() {
        let mapping = Rc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX).unwrap();
        let node_block = node.state.lock().heap.get_block(node.block_index).unwrap();
        assert_eq!(node_block.block_type(), BlockType::NodeValue);
        assert_eq!(node_block.child_count().unwrap(), 0);
        {
            let child = node.create_child("child").unwrap();
            let child_block = node.state.lock().heap.get_block(child.block_index).unwrap();
            assert_eq!(child_block.block_type(), BlockType::NodeValue);
            assert_eq!(child_block.child_count().unwrap(), 0);
            assert_eq!(node_block.child_count().unwrap(), 1);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn double_metric() {
        let mapping = Rc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX).unwrap();
        let node_block = node.state.lock().heap.get_block(node.block_index).unwrap();
        {
            let metric = node.create_double_metric("metric", 1.0).unwrap();
            let metric_block = node.state.lock().heap.get_block(metric.block_index).unwrap();
            assert_eq!(metric_block.block_type(), BlockType::DoubleValue);
            assert_eq!(metric_block.double_value().unwrap(), 1.0);
            assert_eq!(node_block.child_count().unwrap(), 1);

            assert!(metric.set(2.0).is_ok());
            assert_eq!(metric_block.double_value().unwrap(), 2.0);

            assert!(metric.subtract(5.5).is_ok());
            assert_eq!(metric_block.double_value().unwrap(), -3.5);

            assert!(metric.add(8.1).is_ok());
            assert_eq!(metric_block.double_value().unwrap(), 4.6);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn int_metric() {
        let mapping = Rc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX).unwrap();
        let node_block = node.state.lock().heap.get_block(node.block_index).unwrap();
        {
            let metric = node.create_int_metric("metric", 1).unwrap();
            let metric_block = node.state.lock().heap.get_block(metric.block_index).unwrap();
            assert_eq!(metric_block.block_type(), BlockType::IntValue);
            assert_eq!(metric_block.int_value().unwrap(), 1);
            assert_eq!(node_block.child_count().unwrap(), 1);

            assert!(metric.set(2).is_ok());
            assert_eq!(metric_block.int_value().unwrap(), 2);

            assert!(metric.subtract(5).is_ok());
            assert_eq!(metric_block.int_value().unwrap(), -3);

            assert!(metric.add(8).is_ok());
            assert_eq!(metric_block.int_value().unwrap(), 5);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn uint_metric() {
        let mapping = Rc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX).unwrap();
        let node_block = node.state.lock().heap.get_block(node.block_index).unwrap();
        {
            let metric = node.create_uint_metric("metric", 1).unwrap();
            let metric_block = node.state.lock().heap.get_block(metric.block_index).unwrap();
            assert_eq!(metric_block.block_type(), BlockType::UintValue);
            assert_eq!(metric_block.uint_value().unwrap(), 1);
            assert_eq!(node_block.child_count().unwrap(), 1);

            assert!(metric.set(5).is_ok());
            assert_eq!(metric_block.uint_value().unwrap(), 5);

            assert!(metric.subtract(3).is_ok());
            assert_eq!(metric_block.uint_value().unwrap(), 2);

            assert!(metric.add(8).is_ok());
            assert_eq!(metric_block.uint_value().unwrap(), 10);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn string_property() {
        let mapping = Rc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX).unwrap();
        let node_block = node.state.lock().heap.get_block(node.block_index).unwrap();
        {
            let property = node.create_property("property", "test").unwrap();
            let property_block = node.state.lock().heap.get_block(property.block_index).unwrap();
            assert_eq!(property_block.block_type(), BlockType::PropertyValue);
            assert_eq!(property_block.property_total_length().unwrap(), 4);
            assert_eq!(property_block.property_flags().unwrap(), constants::PROPERTY_FLAG_STRING);
            assert_eq!(node_block.child_count().unwrap(), 1);

            assert!(property.set("test-set").is_ok());
            assert_eq!(property_block.property_total_length().unwrap(), 8);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn bytevector_property() {
        let mapping = Rc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX).unwrap();
        let node_block = node.state.lock().heap.get_block(node.block_index).unwrap();
        {
            let property = node.create_property("property", "test".as_bytes()).unwrap();
            let property_block = node.state.lock().heap.get_block(property.block_index).unwrap();
            assert_eq!(property_block.block_type(), BlockType::PropertyValue);
            assert_eq!(property_block.property_total_length().unwrap(), 4);
            assert_eq!(
                property_block.property_flags().unwrap(),
                constants::PROPERTY_FLAG_BYTE_VECTOR
            );
            assert_eq!(node_block.child_count().unwrap(), 1);

            assert!(property.set("test-set".as_bytes()).is_ok());
            assert_eq!(property_block.property_total_length().unwrap(), 8);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    fn get_state(mapping: Rc<Mapping>) -> Arc<Mutex<State>> {
        let heap = Heap::new(mapping).unwrap();
        Arc::new(Mutex::new(State::create(heap).unwrap()))
    }
}
