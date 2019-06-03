// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::vmo::{block::PropertyFormat, heap::Heap, state::State},
    failure::{format_err, Error},
    fuchsia_component::server::{ServiceFs, ServiceObjTrait},
    fuchsia_syslog::macros::*,
    fuchsia_zircon::{self as zx, HandleBased},
    mapped_vmo::Mapping,
    parking_lot::Mutex,
    paste,
    std::{cmp::max, sync::Arc},
};

mod bitfields;
mod block;
mod block_type;
mod constants;
mod heap;
pub mod reader;
mod state;
mod utils;

/// Root of the Inspect API
pub struct Inspector {
    /// The root node.
    root_node: Node,

    /// The VMO backing the inspector
    pub(super) vmo: zx::Vmo,
}

/// Trait implemented by properties.
pub trait Property<'t> {
    type Type;

    /// Set the property value to |value|.
    fn set(&'t self, value: Self::Type);
}

/// Trait implemented by metrics.
pub trait Metric {
    /// The type the metric is handling.
    type Type;

    /// Set the metric current value to |value|.
    fn set(&self, value: Self::Type);

    /// Add the given |value| to the metric current value.
    fn add(&self, value: Self::Type);

    /// Subtract the given |value| from the metric current value.
    fn subtract(&self, value: Self::Type);

    /// Return the current value of the metric for testing.
    /// NOTE: This is a temporary feature to aid unit test of Inspect clients.
    /// It will be replaced by a more comprehensive Read API implementation.
    fn get(&self) -> Result<Self::Type, Error>;
}

/// Inspect API Node data type.
/// NOTE: do not rely on PartialEq implementation for true comparison. Instead
/// leverage the reader.
#[derive(Debug)]
pub struct Node {
    /// Index of the block in the VMO.
    block_index: Option<u32>,

    /// Reference to the VMO heap.
    state: Option<Arc<Mutex<State>>>,
}

/// Root API for inspect. Used to create the VMO, export to ServiceFs, and get
/// the root node.
impl Inspector {
    /// Create a new Inspect VMO object with the default maximum size.
    pub fn new() -> Result<Self, Error> {
        Inspector::new_with_size(constants::DEFAULT_VMO_SIZE_BYTES)
    }

    /// Create a new Inspect VMO object with the given maximum size. If the
    /// given size is less than 4K, it will be made 4K which is the minimum size
    /// the VMO should have.
    pub fn new_with_size(max_size: usize) -> Result<Self, Error> {
        let (vmo, root_node) = Inspector::new_root(max_size)?;
        Ok(Inspector { vmo, root_node })
    }

    /// Exports the VMO backing this Inspector at the standard location in the
    /// supplied ServiceFs.
    pub fn export<ServiceObjTy: ServiceObjTrait>(
        &self,
        service_fs: &mut ServiceFs<ServiceObjTy>,
    ) -> Result<(), Error> {
        service_fs.dir("objects").add_vmo_file_at(
            "root.inspect",
            self.vmo.duplicate_handle(zx::Rights::BASIC | zx::Rights::READ | zx::Rights::MAP)?,
            0, /* vmo offset */
            self.vmo.get_size()?,
        );
        Ok(())
    }

    /// Get the root of the VMO object.
    pub fn root(&self) -> &Node {
        &self.root_node
    }

    fn new_root(max_size: usize) -> Result<(zx::Vmo, Node), Error> {
        let mut size = max(constants::MINIMUM_VMO_SIZE_BYTES, max_size);
        // If the size is not a multiple of 4096, round up.
        if size % constants::MINIMUM_VMO_SIZE_BYTES != 0 {
            size =
                (1 + size / constants::MINIMUM_VMO_SIZE_BYTES) * constants::MINIMUM_VMO_SIZE_BYTES;
        }
        let (mapping, vmo) = Mapping::allocate(size)
            .map_err(|e| format_err!("failed to allocate vmo zx status={}", e))?;
        let heap = Heap::new(Arc::new(mapping))?;
        let state = State::create(heap)?;
        let root_node = Node::allocate(
            Arc::new(Mutex::new(state)),
            constants::ROOT_NAME,
            constants::ROOT_PARENT_INDEX,
        );
        Ok((vmo, root_node))
    }
}

/// Utility for generating functions to create a metric.
///   `name`: identifier for the name (example: double)
///   `type`: the type of the metric (example: f64)
macro_rules! create_metric_fn {
    ($name:ident, $name_cap:ident, $type:ident) => {
        paste::item! {
            pub fn [<create_ $name _metric>](&self, name: &str, value: $type)
                -> [<$name_cap Metric>] {
                self.state
                    .as_ref()
                    .ok_or(format_err!("No-Op node"))
                    .and_then(|state| {
                        state
                            .lock()
                            .[<create_ $name _metric>](name, value, self.block_index.unwrap())
                    })
                    .map(|block| {
                        [<$name_cap Metric>] {state: self.state.clone(), block_index: Some(block.index()) }
                    })
                    .unwrap_or([<$name_cap Metric>] { state: None, block_index: None })
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
    ) -> Node {
        state
            .lock()
            .create_node(name, parent_index)
            .map(|block| Node { state: Some(state.clone()), block_index: Some(block.index()) })
            .unwrap_or(Node { state: None, block_index: None })
    }

    /// Add a child to this node.
    pub fn create_child(&self, name: &str) -> Node {
        self.state
            .as_ref()
            .ok_or(format_err!("No-Op node"))
            .and_then(|state| state.lock().create_node(name, self.block_index.unwrap()))
            .map(|block| Node { state: self.state.clone(), block_index: Some(block.index()) })
            .unwrap_or(Node { state: None, block_index: None })
    }

    /// Add a metric to this node: create_int_metric, create_double_metric,
    /// create_uint_metric.
    create_metric_fn!(int, Int, i64);
    create_metric_fn!(uint, Uint, u64);
    create_metric_fn!(double, Double, f64);

    /// Add a string property to this node.
    pub fn create_string_property(&self, name: &str, value: &str) -> StringProperty {
        self.state
            .as_ref()
            .ok_or(format_err!("No-Op node"))
            .and_then(|state| {
                state.lock().create_property(
                    name,
                    value.as_bytes(),
                    PropertyFormat::String,
                    self.block_index.unwrap(),
                )
            })
            .map(|block| StringProperty {
                state: self.state.clone(),
                block_index: Some(block.index()),
            })
            .unwrap_or(StringProperty { state: None, block_index: None })
    }

    /// Add a byte vector property to this node.
    pub fn create_bytes_property(&self, name: &str, value: &[u8]) -> BytesProperty {
        self.state
            .as_ref()
            .ok_or(format_err!("No-Op node"))
            .and_then(|state| {
                state.lock().create_property(
                    name,
                    value,
                    PropertyFormat::Bytes,
                    self.block_index.unwrap(),
                )
            })
            .map(|block| BytesProperty {
                state: self.state.clone(),
                block_index: Some(block.index()),
            })
            .unwrap_or(BytesProperty { state: None, block_index: None })
    }
}

impl Drop for Node {
    fn drop(&mut self) {
        self.state.as_ref().map(|state| {
            state
                .lock()
                .free_value(self.block_index.unwrap())
                .expect(&format!("Failed to free node index={}", self.block_index.unwrap()));
        });
    }
}

macro_rules! dummy_trait_impls {
    ($type:ident) => {
        /// Inspect API types implement Eq,PartialEq returning true all the time so that
        /// structs embedding inspect types can derive these traits as well.
        /// IMPORTANT: Do not rely on these traits implementations for real comparisons
        /// or validation tests, instead leverage the reader.
        impl PartialEq for $type {
            fn eq(&self, _other: &$type) -> bool {
                true
            }
        }

        impl Eq for $type {}
    };
}

dummy_trait_impls!(Node);

/// Utility for generating metric functions (example: set, add, subtract)
///   `fn_name`: the name of the function to generate (example: set)
///   `type`: the type of the argument of the function to generate (example: f64)
///   `name`: the readble name of the type of the function (example: double)
macro_rules! metric_fn {
    ($fn_name:ident, $type:ident, $name:ident) => {
        paste::item! {
            fn $fn_name(&self, value: $type) {
                if let Some(ref state) = self.state {
                    state.lock().[<$fn_name _ $name _metric>](self.block_index.unwrap(), value)
                        .unwrap_or_else(|e| {
                            fx_log_err!("Failed to {} metric. Error: {:?}", stringify!($fn_name), e);
                        });
                }
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
            /// NOTE: do not rely on PartialEq implementation for true comparison.
            /// Instead leverage the reader.
            #[derive(Debug)]
            pub struct [<$name_cap Metric>] {
                /// Index of the block in the VMO.
                block_index: Option<u32>,

                /// Reference to the VMO heap.
                state: Option<Arc<Mutex<State>>>,
            }

            impl Metric for [<$name_cap Metric>] {
                type Type = $type;
                metric_fn!(set, $type, $name);
                metric_fn!(add, $type, $name);
                metric_fn!(subtract, $type, $name);

                fn get(&self) -> Result<$type, Error> {
                    if let Some(ref state) = self.state {
                        state.lock().[<get_ $name _metric>](self.block_index.unwrap())
                    } else {
                        Err(format_err!("Metric is No-Op"))
                    }
                }
            }

            dummy_trait_impls!([<$name_cap Metric>]);

            impl Drop for [<$name_cap Metric>] {
                fn drop(&mut self) {
                    self.state.as_ref().map(|state| {
                        state
                            .lock()
                            .free_value(self.block_index.unwrap())
                            .expect(&format!("Failed to free metric index={}", self.block_index.unwrap()));
                    });
                }
            }
        }
    };
}

macro_rules! property {
    ($name:ident, $type:expr, $bytes:expr) => {
        paste::item! {
            /// Inspect API Property data type.
            /// NOTE: do not rely on PartialEq implementation for true comparison.
            /// Instead leverage the reader.
            #[derive(Debug)]
            pub struct [<$name Property>] {
                /// Index of the block in the VMO.
                block_index: Option<u32>,

                /// Reference to the VMO heap.
                state: Option<Arc<Mutex<State>>>,
            }

            impl<'t> Property<'t> for [<$name Property>] {
                type Type = &'t $type;

                fn set(&'t self, value: &'t $type) {
                    if let Some(ref state) = self.state {
                        state.lock().set_property(self.block_index.unwrap(), $bytes)
                            .unwrap_or_else(|e| fx_log_err!("Failed to set property. Error: {:?}", e));
                    }
                }
            }

            dummy_trait_impls!([<$name Property>]);

            impl Drop for [<$name Property>] {
                fn drop(&mut self) {
                    self.state.as_ref().map(|state| {
                        state
                            .lock()
                            .free_property(self.block_index.unwrap())
                            .expect(&format!("Failed to free property index={}", self.block_index.unwrap()));
                    });
                }
            }
        }
    };
}

metric!(int, Int, i64);
metric!(uint, Uint, u64);
metric!(double, Double, f64);

property!(String, str, value.as_bytes());
property!(Bytes, [u8], value);

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::vmo::{block_type::BlockType, constants, heap::Heap},
        mapped_vmo::Mapping,
    };

    #[test]
    fn inspector_new() {
        let test_object = Inspector::new().unwrap();
        assert_eq!(test_object.vmo.get_size().unwrap(), constants::DEFAULT_VMO_SIZE_BYTES as u64);
        let root_name_block =
            test_object.root().state.as_ref().unwrap().lock().heap.get_block(2).unwrap();
        assert_eq!(root_name_block.name_contents().unwrap(), constants::ROOT_NAME);
    }

    #[test]
    fn inspector_new_with_size() {
        let test_object = Inspector::new_with_size(8192).unwrap();
        assert_eq!(test_object.vmo.get_size().unwrap(), 8192);
        let root_name_block =
            test_object.root().state.as_ref().unwrap().lock().heap.get_block(2).unwrap();
        assert_eq!(root_name_block.name_contents().unwrap(), constants::ROOT_NAME);

        // If size is not a multiple of 4096, it'll be rounded up.
        let test_object = Inspector::new_with_size(10000).unwrap();
        assert_eq!(test_object.vmo.get_size().unwrap(), 12288);

        // If size is less than the minimum size, the minimum will be set.
        let test_object = Inspector::new_with_size(2000).unwrap();
        assert_eq!(test_object.vmo.get_size().unwrap(), 4096);
    }

    #[test]
    fn inspector_new_root() {
        // Note, the small size we request should be rounded up to a full 4kB page.
        let (vmo, root_node) = Inspector::new_root(100).unwrap();
        assert_eq!(vmo.get_size().unwrap(), 4096);
        let root_name_block = root_node.state.as_ref().unwrap().lock().heap.get_block(2).unwrap();
        assert_eq!(root_name_block.name_contents().unwrap(), constants::ROOT_NAME);
    }

    #[test]
    fn node() {
        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX);
        let node_block =
            node.state.as_ref().unwrap().lock().heap.get_block(node.block_index.unwrap()).unwrap();
        assert_eq!(node_block.block_type(), BlockType::NodeValue);
        assert_eq!(node_block.child_count().unwrap(), 0);
        {
            let child = node.create_child("child");
            let child_block = node
                .state
                .as_ref()
                .unwrap()
                .lock()
                .heap
                .get_block(child.block_index.unwrap())
                .unwrap();
            assert_eq!(child_block.block_type(), BlockType::NodeValue);
            assert_eq!(child_block.child_count().unwrap(), 0);
            assert_eq!(node_block.child_count().unwrap(), 1);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn double_metric() {
        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX);
        let node_block =
            node.state.as_ref().unwrap().lock().heap.get_block(node.block_index.unwrap()).unwrap();
        {
            let metric = node.create_double_metric("metric", 1.0);
            let metric_block = node
                .state
                .as_ref()
                .unwrap()
                .lock()
                .heap
                .get_block(metric.block_index.unwrap())
                .unwrap();
            assert_eq!(metric_block.block_type(), BlockType::DoubleValue);
            assert_eq!(metric_block.double_value().unwrap(), 1.0);
            assert_eq!(node_block.child_count().unwrap(), 1);

            metric.set(2.0);
            assert_eq!(metric_block.double_value().unwrap(), 2.0);
            assert_eq!(metric.get().unwrap(), 2.0);

            metric.subtract(5.5);
            assert_eq!(metric_block.double_value().unwrap(), -3.5);

            metric.add(8.1);
            assert_eq!(metric_block.double_value().unwrap(), 4.6);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn int_metric() {
        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX);
        let node_block =
            node.state.as_ref().unwrap().lock().heap.get_block(node.block_index.unwrap()).unwrap();
        {
            let metric = node.create_int_metric("metric", 1);
            let metric_block = node
                .state
                .as_ref()
                .unwrap()
                .lock()
                .heap
                .get_block(metric.block_index.unwrap())
                .unwrap();
            assert_eq!(metric_block.block_type(), BlockType::IntValue);
            assert_eq!(metric_block.int_value().unwrap(), 1);
            assert_eq!(node_block.child_count().unwrap(), 1);

            metric.set(2);
            assert_eq!(metric_block.int_value().unwrap(), 2);
            assert_eq!(metric.get().unwrap(), 2);

            metric.subtract(5);
            assert_eq!(metric_block.int_value().unwrap(), -3);

            metric.add(8);
            assert_eq!(metric_block.int_value().unwrap(), 5);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn uint_metric() {
        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX);
        let node_block =
            node.state.as_ref().unwrap().lock().heap.get_block(node.block_index.unwrap()).unwrap();
        {
            let metric = node.create_uint_metric("metric", 1);
            let metric_block = node
                .state
                .as_ref()
                .unwrap()
                .lock()
                .heap
                .get_block(metric.block_index.unwrap())
                .unwrap();
            assert_eq!(metric_block.block_type(), BlockType::UintValue);
            assert_eq!(metric_block.uint_value().unwrap(), 1);
            assert_eq!(node_block.child_count().unwrap(), 1);

            metric.set(5);
            assert_eq!(metric_block.uint_value().unwrap(), 5);
            assert_eq!(metric.get().unwrap(), 5);

            metric.subtract(3);
            assert_eq!(metric_block.uint_value().unwrap(), 2);

            metric.add(8);
            assert_eq!(metric_block.uint_value().unwrap(), 10);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn string_property() {
        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX);
        let node_block =
            node.state.as_ref().unwrap().lock().heap.get_block(node.block_index.unwrap()).unwrap();
        {
            let property = node.create_string_property("property", "test");
            let property_block = node
                .state
                .as_ref()
                .unwrap()
                .lock()
                .heap
                .get_block(property.block_index.unwrap())
                .unwrap();
            assert_eq!(property_block.block_type(), BlockType::PropertyValue);
            assert_eq!(property_block.property_total_length().unwrap(), 4);
            assert_eq!(property_block.property_format().unwrap(), PropertyFormat::String);
            assert_eq!(node_block.child_count().unwrap(), 1);

            property.set("test-set");
            assert_eq!(property_block.property_total_length().unwrap(), 8);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn bytes_property() {
        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX);
        let node_block =
            node.state.as_ref().unwrap().lock().heap.get_block(node.block_index.unwrap()).unwrap();
        {
            let property = node.create_bytes_property("property", b"test");
            let property_block = node
                .state
                .as_ref()
                .unwrap()
                .lock()
                .heap
                .get_block(property.block_index.unwrap())
                .unwrap();
            assert_eq!(property_block.block_type(), BlockType::PropertyValue);
            assert_eq!(property_block.property_total_length().unwrap(), 4);
            assert_eq!(property_block.property_format().unwrap(), PropertyFormat::Bytes);
            assert_eq!(node_block.child_count().unwrap(), 1);

            property.set(b"test-set");
            assert_eq!(property_block.property_total_length().unwrap(), 8);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn dummy_partialeq() -> Result<(), Error> {
        let inspector = Inspector::new()?;
        let root = inspector.root();

        // Types should all be equal to another type. This is to enable clients
        // with inspect types in their structs be able to derive PartialEq and
        // Eq smoothly.
        assert_eq!(root, &root.create_child("child1"));
        assert_eq!(root.create_int_metric("metric1", 1), root.create_int_metric("metric2", 2));
        assert_eq!(
            root.create_double_metric("metric1", 1.0),
            root.create_double_metric("metric2", 2.0)
        );
        assert_eq!(root.create_uint_metric("metric1", 1), root.create_uint_metric("metric2", 2));
        assert_eq!(
            root.create_string_property("metric1", "value1"),
            root.create_string_property("metric2", "value2")
        );
        assert_eq!(
            root.create_bytes_property("metric1", b"value1"),
            root.create_bytes_property("metric2", b"value2")
        );

        Ok(())
    }

    fn get_state(mapping: Arc<Mapping>) -> Arc<Mutex<State>> {
        let heap = Heap::new(mapping).unwrap();
        Arc::new(Mutex::new(State::create(heap).unwrap()))
    }

}
