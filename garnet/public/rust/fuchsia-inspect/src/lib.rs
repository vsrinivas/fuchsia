// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    crate::{
        block::{ArrayFormat, PropertyFormat},
        heap::Heap,
        state::State,
    },
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
pub mod component;
mod constants;
pub mod health;
mod heap;
pub mod reader;
#[macro_use]
pub mod testing;
mod state;
mod utils;

/// Root of the Inspect API
pub struct Inspector {
    /// The root node.
    root_node: Node,

    /// The VMO backing the inspector
    pub(in crate) vmo: Option<zx::Vmo>,
}

/// Root API for inspect. Used to create the VMO, export to ServiceFs, and get
/// the root node.
impl Inspector {
    /// Create a new Inspect VMO object with the default maximum size.
    pub fn new() -> Self {
        Inspector::new_with_size(constants::DEFAULT_VMO_SIZE_BYTES)
    }

    /// True if the Inspector was created successfully (it's not No-Op)
    pub fn is_valid(&self) -> bool {
        self.vmo.is_some() && self.root_node.is_valid()
    }

    /// Create a new Inspect VMO object with the given maximum size. If the
    /// given size is less than 4K, it will be made 4K which is the minimum size
    /// the VMO should have.
    pub fn new_with_size(max_size: usize) -> Self {
        match Inspector::new_root(max_size) {
            Ok((vmo, root_node)) => Inspector { vmo: Some(vmo), root_node },
            Err(e) => {
                fx_log_err!("Failed to create root node. Error: {}", e);
                Inspector::new_no_op()
            }
        }
    }

    /// Exports the VMO backing this Inspector at the standard location in the
    /// supplied ServiceFs.
    pub fn export<ServiceObjTy: ServiceObjTrait>(&self, service_fs: &mut ServiceFs<ServiceObjTy>) {
        self.vmo
            .as_ref()
            .ok_or(format_err!("Cannot expose No-Op Inspector"))
            .and_then(|vmo| {
                service_fs.dir("objects").add_vmo_file_at(
                    "root.inspect",
                    vmo.duplicate_handle(zx::Rights::BASIC | zx::Rights::READ | zx::Rights::MAP)?,
                    0, /* vmo offset */
                    vmo.get_size()?,
                );
                Ok(())
            })
            .unwrap_or_else(|e| {
                fx_log_err!("Failed to expose vmo. Error: {:?}", e);
            });
    }

    /// Get the root of the VMO object.
    pub fn root(&self) -> &Node {
        &self.root_node
    }

    /// Creates a new No-Op inspector
    fn new_no_op() -> Self {
        Inspector { vmo: None, root_node: Node::new_no_op() }
    }

    /// Allocates a new VMO and initializes it.
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

/// Trait implemented by all inspect types. It provides constructor functions that are not
/// intended for use outside the crate.
trait InspectType {
    type Inner;

    fn new(state: Arc<Mutex<State>>, block_index: u32) -> Self;
    fn new_no_op() -> Self;
    fn is_valid(&self) -> bool;
    fn unwrap_ref(&self) -> &Self::Inner;
}

/// Utility for generating the implementation of all inspect types:
///  - All Inspect Types (*Property, Node) can be No-Op. This macro generates the
///    appropiate internal constructors.
///  - All Inspect Types derive PartialEq, Eq. This generates the dummy implementation
///    for the wrapped type.
macro_rules! inspect_type_impl {
    ($(#[$attr:meta])* struct $name:ident) => {
        paste::item! {
            $(#[$attr])*
            /// NOTE: do not rely on PartialEq implementation for true comparison.
            /// Instead leverage the reader.
            #[derive(Debug, PartialEq, Eq)]
            pub struct $name {
                inner: Option<[<Inner $name>]>,
            }

            #[cfg(test)]
            impl $name {
                pub fn get_block(&self) -> Option<crate::block::Block<Arc<Mapping>>> {
                    self.inner.as_ref().and_then(|inner| {
                        inner.state.lock().heap.get_block(inner.block_index).ok()
                    })
                }

                pub fn block_index(&self) -> u32 {
                    self.inner.as_ref().unwrap().block_index
                }
            }

            impl InspectType for $name {
                type Inner = [<Inner $name>];

                fn new(state: Arc<Mutex<State>>, block_index: u32) -> Self {
                    Self {
                        inner: Some([<Inner $name>] {
                            state, block_index
                        })
                    }
                }

                fn is_valid(&self) -> bool {
                    self.inner.is_some()
                }

                fn new_no_op() -> Self {
                    Self { inner: None }
                }

                fn unwrap_ref(&self) -> &Self::Inner {
                    self.inner.as_ref().unwrap()
                }
            }

            #[derive(Debug)]
            struct [<Inner $name>] {
                /// Index of the block in the VMO.
                block_index: u32,

                /// Reference to the VMO heap.
                state: Arc<Mutex<State>>,
            }

            /// Inspect API types implement Eq,PartialEq returning true all the time so that
            /// structs embedding inspect types can derive these traits as well.
            /// IMPORTANT: Do not rely on these traits implementations for real comparisons
            /// or validation tests, instead leverage the reader.
            impl PartialEq for [<Inner $name>] {
                fn eq(&self, _other: &[<Inner $name>]) -> bool {
                    true
                }
            }

            impl Eq for [<Inner $name>] {}
        }
    }
}

/// Utility for generating functions to create a numeric property.
///   `name`: identifier for the name (example: double)
///   `name_cap`: identifier for the name capitalized (example: Double)
///   `type`: the type of the numeric property (example: f64)
macro_rules! create_numeric_property_fn {
    ($name:ident, $name_cap:ident, $type:ident) => {
        paste::item! {
            pub fn [<create_ $name >](&self, name: impl AsRef<str>, value: $type)
                -> [<$name_cap Property>] {
                    self.inner.as_ref().and_then(|inner| {
                        inner.state
                            .lock()
                            .[<create_ $name _metric>](name.as_ref(), value, inner.block_index)
                            .map(|block| {
                                [<$name_cap Property>]::new(inner.state.clone(), block.index())
                            })
                            .ok()
                    })
                    .unwrap_or([<$name_cap Property>]::new_no_op())
            }
        }
    };
}

/// Utility for generating functions to create an array property.
///   `name`: identifier for the name (example: double)
///   `name_cap`: identifier for the name capitalized (example: Double)
///   `type`: the type of the numeric property (example: f64)
macro_rules! create_array_property_fn {
    ($name:ident, $name_cap:ident, $type:ident) => {
        paste::item! {
            pub fn [<create_ $name _array>](&self, name: impl AsRef<str>, slots: u8)
                -> [<$name_cap ArrayProperty>] {
                    self.[<create_ $name _array_internal>](name, slots, ArrayFormat::Default)
            }

            fn [<create_ $name _array_internal>](&self, name: impl AsRef<str>, slots: u8, format: ArrayFormat)
                -> [<$name_cap ArrayProperty>] {
                    self.inner.as_ref().and_then(|inner| {
                        inner.state
                            .lock()
                            .[<create_ $name _array>](name.as_ref(), slots, format, inner.block_index)
                            .map(|block| {
                                [<$name_cap ArrayProperty>]::new(inner.state.clone(), block.index())
                            })
                            .ok()
                    })
                    .unwrap_or([<$name_cap ArrayProperty>]::new_no_op())
            }
        }
    };
}

pub struct LinearHistogramParams<T> {
    floor: T,
    step_size: T,
    buckets: u8,
}

/// Utility for generating functions to create a linear histogram property.
///   `name`: identifier for the name (example: double)
///   `name_cap`: identifier for the name capitalized (example: Double)
///   `type`: the type of the numeric property (example: f64)
macro_rules! create_linear_histogram_property_fn {
    ($name:ident, $name_cap:ident, $type:ident) => {
        paste::item! {
            pub fn [<create_ $name _linear_histogram>](
                &self, name: impl AsRef<str>, params: LinearHistogramParams<$type>)
                -> [<$name_cap LinearHistogramProperty>] {
                let slots = params.buckets + constants::LINEAR_HISTOGRAM_EXTRA_SLOTS;
                let array = self.[<create_ $name _array_internal>](name, slots, ArrayFormat::LinearHistogram);
                array.set(0, params.floor);
                array.set(1, params.step_size);
                [<$name_cap LinearHistogramProperty>] {
                    floor: params.floor,
                    step_size: params.step_size,
                    slots,
                    array
                }
            }
        }
    };
}

pub struct ExponentialHistogramParams<T> {
    floor: T,
    initial_step: T,
    step_multiplier: T,
    buckets: u8,
}

/// Utility for generating functions to create an exponential histogram property.
///   `name`: identifier for the name (example: double)
///   `name_cap`: identifier for the name capitalized (example: Double)
///   `type`: the type of the numeric property (example: f64)
macro_rules! create_exponential_histogram_property_fn {
    ($name:ident, $name_cap:ident, $type:ident) => {
        paste::item! {
            pub fn [<create_ $name _exponential_histogram>](
              &self, name: impl AsRef<str>, params: ExponentialHistogramParams<$type>)
              -> [<$name_cap ExponentialHistogramProperty>] {
                let slots = params.buckets + constants::EXPONENTIAL_HISTOGRAM_EXTRA_SLOTS;
                let array = self.[<create_ $name _array_internal>](name, slots, ArrayFormat::ExponentialHistogram);
                array.set(0, params.floor);
                array.set(1, params.initial_step);
                array.set(2, params.step_multiplier);
                [<$name_cap ExponentialHistogramProperty>] {
                    floor: params.floor,
                    initial_step: params.initial_step,
                    step_multiplier: params.step_multiplier,
                    slots,
                    array
                }
            }
        }
    };
}

inspect_type_impl!(
    /// Inspect API Node data type.
    struct Node
);

impl Node {
    /// Allocate a new NODE object. Called through Inspector.
    pub(in crate) fn allocate(state: Arc<Mutex<State>>, name: &str, parent_index: u32) -> Node {
        state
            .lock()
            .create_node(name, parent_index)
            .map(|block| Node::new(state.clone(), block.index()))
            .unwrap_or(Node::new_no_op())
    }

    /// Add a child to this node.
    pub fn create_child(&self, name: impl AsRef<str>) -> Node {
        self.inner
            .as_ref()
            .and_then(|inner| {
                inner
                    .state
                    .lock()
                    .create_node(name.as_ref(), inner.block_index)
                    .map(|block| Node::new(inner.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(Node::new_no_op())
    }

    /// Add a numeric property to this node: create_int, create_double,
    /// create_uint.
    create_numeric_property_fn!(int, Int, i64);
    create_numeric_property_fn!(uint, Uint, u64);
    create_numeric_property_fn!(double, Double, f64);

    /// Add an array property to this node: create_int_array, create_double_array,
    /// create_uint_array.
    create_array_property_fn!(int, Int, i64);
    create_array_property_fn!(uint, Uint, u64);
    create_array_property_fn!(double, Double, f64);

    /// Add a linear histogram property to this node: create_int_linear_histogram,
    /// create_uint_linear_histogram, create_double_linear_histogram.
    create_linear_histogram_property_fn!(int, Int, i64);
    create_linear_histogram_property_fn!(uint, Uint, u64);
    create_linear_histogram_property_fn!(double, Double, f64);

    /// Add an exponential histogram property to this node: create_int_linear_histogram,
    /// create_uint_linear_histogram, create_double_linear_histogram.
    create_exponential_histogram_property_fn!(int, Int, i64);
    create_exponential_histogram_property_fn!(uint, Uint, u64);
    create_exponential_histogram_property_fn!(double, Double, f64);

    /// Add a string property to this node.
    pub fn create_string(&self, name: impl AsRef<str>, value: impl AsRef<str>) -> StringProperty {
        self.inner
            .as_ref()
            .and_then(|inner| {
                inner
                    .state
                    .lock()
                    .create_property(
                        name.as_ref(),
                        value.as_ref().as_bytes(),
                        PropertyFormat::String,
                        inner.block_index,
                    )
                    .map(|block| StringProperty::new(inner.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(StringProperty::new_no_op())
    }

    /// Add a byte vector property to this node.
    pub fn create_bytes(&self, name: impl AsRef<str>, value: impl AsRef<[u8]>) -> BytesProperty {
        self.inner
            .as_ref()
            .and_then(|inner| {
                inner
                    .state
                    .lock()
                    .create_property(
                        name.as_ref(),
                        value.as_ref(),
                        PropertyFormat::Bytes,
                        inner.block_index,
                    )
                    .map(|block| BytesProperty::new(inner.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(BytesProperty::new_no_op())
    }
}

impl Drop for Node {
    fn drop(&mut self) {
        self.inner.as_ref().map(|inner| {
            inner
                .state
                .lock()
                .free_value(inner.block_index)
                .expect(&format!("Failed to free node index={}", inner.block_index));
        });
    }
}

/// Trait implemented by properties.
pub trait Property<'t> {
    type Type;

    /// Set the property value to |value|.
    fn set(&'t self, value: Self::Type);
}

/// Trait implemented by numeric properties.
pub trait NumericProperty {
    /// The type the property is handling.
    type Type;

    /// Add the given |value| to the property current value.
    fn add(&self, value: Self::Type);

    /// Subtract the given |value| from the property current value.
    fn subtract(&self, value: Self::Type);

    /// Return the current value of the property for testing.
    /// NOTE: This is a temporary feature to aid unit test of Inspect clients.
    /// It will be replaced by a more comprehensive Read API implementation.
    fn get(&self) -> Result<Self::Type, Error>;
}

/// Utility for generating numeric property functions (example: set, add, subtract)
///   `fn_name`: the name of the function to generate (example: set)
///   `type`: the type of the argument of the function to generate (example: f64)
///   `name`: the readble name of the type of the function (example: double)
macro_rules! numeric_property_fn {
    ($fn_name:ident, $type:ident, $name:ident) => {
        paste::item! {
            fn $fn_name(&self, value: $type) {
                if let Some(ref inner) = self.inner {
                    inner.state.lock().[<$fn_name _ $name _metric>](inner.block_index, value)
                        .unwrap_or_else(|e| {
                            fx_log_err!("Failed to {} property. Error: {:?}", stringify!($fn_name), e);
                        });
                }
            }
        }
    };
}

/// Utility for generting a Drop implementation for numeric and array properties.
///     `name`: the name of the struct of the property for which Drop will be implemented.
macro_rules! drop_value_impl {
    ($name:ident) => {
        impl Drop for $name {
            fn drop(&mut self) {
                self.inner.as_ref().map(|inner| {
                    inner
                        .state
                        .lock()
                        .free_value(inner.block_index)
                        .expect(&format!("Failed to free property index={}", inner.block_index));
                });
            }
        }
    };
}

/// Utility for generating a numeric property datatype impl
///   `name`: the readble name of the type of the function (example: double)
///   `type`: the type of the argument of the function to generate (example: f64)
macro_rules! numeric_property {
    ($name:ident, $name_cap:ident, $type:ident) => {
        paste::item! {
            inspect_type_impl!(
                /// Inspect API Numeric Property data type.
                struct [<$name_cap Property>]
            );

            impl<'t> Property<'t> for [<$name_cap Property>] {
                type Type = $type;

                numeric_property_fn!(set, $type, $name);
            }

            impl NumericProperty for [<$name_cap Property>] {
                type Type = $type;
                numeric_property_fn!(add, $type, $name);
                numeric_property_fn!(subtract, $type, $name);

                fn get(&self) -> Result<$type, Error> {
                    if let Some(ref inner) = self.inner {
                        inner.state.lock().[<get_ $name _metric>](inner.block_index)
                    } else {
                        Err(format_err!("Property is No-Op"))
                    }
                }
            }

            drop_value_impl!([<$name_cap Property>]);
        }
    };
}

numeric_property!(int, Int, i64);
numeric_property!(uint, Uint, u64);
numeric_property!(double, Double, f64);

/// Utility for generating a numeric property datatype impl
///   `name`: the readable name of the type of the function (example: double)
///   `type`: the type of the argument of the function to generate (example: f64)
///   `bytes`: an expression to get the bytes of the property
macro_rules! property {
    ($name:ident, $type:expr, $bytes:expr) => {
        paste::item! {
            inspect_type_impl!(
                /// Inspect API Property data type.
                struct [<$name Property>]
            );

            impl<'t> Property<'t> for [<$name Property>] {
                type Type = &'t $type;

                fn set(&'t self, value: &'t $type) {
                    if let Some(ref inner) = self.inner {
                        inner.state.lock().set_property(inner.block_index, $bytes)
                            .unwrap_or_else(|e| fx_log_err!("Failed to set property. Error: {:?}", e));
                    }
                }

            }

            impl Drop for [<$name Property>] {
                fn drop(&mut self) {
                    self.inner.as_ref().map(|inner| {
                        inner.state
                            .lock()
                            .free_property(inner.block_index)
                            .expect(&format!("Failed to free property index={}", inner.block_index));
                    });
                }
            }
        }
    };
}

property!(String, str, value.as_bytes());
property!(Bytes, [u8], value);

pub trait ArrayProperty {
    type Type;

    /// Set the array value to |value| at the given |index|.
    fn set(&self, index: usize, value: Self::Type);

    /// Add the given |value| to the property current value at the given |index|.
    fn add(&self, index: usize, value: Self::Type);

    /// Subtract the given |value| to the property current value at the given |index|.
    fn subtract(&self, index: usize, value: Self::Type);
}

/// Utility for generating array property functions (example: set, add, subtract)
///   `fn_name`: the name of the function to generate (example: set)
///   `type`: the type of the argument of the function to generate (example: f64)
///   `name`: the readble name of the type of the function (example: double)
macro_rules! array_property_fn {
    ($fn_name:ident, $type:ident, $name:ident) => {
        paste::item! {
            fn $fn_name(&self, index: usize, value: $type) {
                if let Some(ref inner) = self.inner {
                    inner.state.lock().[<$fn_name _array_ $name _slot>](inner.block_index, index, value)
                        .unwrap_or_else(|e| {
                            fx_log_err!("Failed to {} property. Error: {:?}", stringify!($fn_name), e);
                        });
                }
            }
        }
    };
}

/// Utility for generating a numeric property datatype impl
///   `name`: the readble name of the type of the function (example: double)
///   `type`: the type of the argument of the function to generate (example: f64)
macro_rules! array_property {
    ($name:ident, $name_cap:ident, $type:ident) => {
        paste::item! {
            inspect_type_impl!(
                /// Inspect API Array Property data type.
                struct [<$name_cap ArrayProperty>]
            );

            impl [<$name_cap ArrayProperty>] {
            }

            impl ArrayProperty for [<$name_cap ArrayProperty>] {
                type Type = $type;

                array_property_fn!(set, $type, $name);
                array_property_fn!(add, $type, $name);
                array_property_fn!(subtract, $type, $name);
            }

            drop_value_impl!([<$name_cap ArrayProperty>]);
        }
    };
}

array_property!(int, Int, i64);
array_property!(uint, Uint, u64);
array_property!(double, Double, f64);

pub trait HistogramProperty {
    type Type;

    fn insert(&self, value: Self::Type);
    fn insert_multiple(&self, value: Self::Type, count: usize);
}

macro_rules! histogram_property {
    ($histogram_type:ident, $name_cap:ident, $type:ident) => {
        paste::item! {
            impl HistogramProperty for [<$name_cap $histogram_type HistogramProperty>] {
                type Type = $type;

                fn insert(&self, value: $type) {
                    self.insert_multiple(value, 1);
                }

                fn insert_multiple(&self, value: $type, count: usize) {
                    self.array.add(self.get_index(value), count as $type);
                }
            }
        }
    };
}

macro_rules! linear_histogram_property {
    ($name_cap:ident, $type:ident) => {
        paste::item! {
            pub struct [<$name_cap LinearHistogramProperty>] {
                array: [<$name_cap ArrayProperty>],
                floor: $type,
                slots: u8,
                step_size: $type,
            }

            impl [<$name_cap LinearHistogramProperty>] {
                fn get_index(&self, value: $type) -> usize {
                    let mut current_floor = self.floor;
                    // Start in the underflow index.
                    let mut index = constants::LINEAR_HISTOGRAM_EXTRA_SLOTS - 2;
                    while value >= current_floor && index < self.slots - 1 {
                        current_floor += self.step_size;
                        index += 1;
                    }
                    index as usize
                }

                #[cfg(test)]
                fn get_block(&self) -> Option<crate::block::Block<Arc<Mapping>>> {
                    self.array.get_block()
                }
            }

            histogram_property!(Linear, $name_cap, $type);
        }
    };
}

macro_rules! exponential_histogram_property {
    ($name_cap:ident, $type:ident) => {
        paste::item! {
            pub struct [<$name_cap ExponentialHistogramProperty>] {
                array: [<$name_cap ArrayProperty>],
                floor: $type,
                initial_step: $type,
                step_multiplier: $type,
                slots: u8,
            }

            impl [<$name_cap ExponentialHistogramProperty>] {
                fn get_index(&self, value: $type) -> usize {
                    let mut current_floor = self.floor;
                    let mut current_step = self.initial_step;
                    // Start in the underflow index.
                    let mut index = constants::EXPONENTIAL_HISTOGRAM_EXTRA_SLOTS - 2;
                    while value >= current_floor && index < self.slots - 1 {
                        current_floor += current_step;
                        current_step *= self.step_multiplier;
                        index += 1;
                    }
                    index as usize
                }

                #[cfg(test)]
                fn get_block(&self) -> Option<crate::block::Block<Arc<Mapping>>> {
                    self.array.get_block()
                }
            }

            histogram_property!(Exponential, $name_cap, $type);
        }
    };
}

linear_histogram_property!(Double, f64);
linear_histogram_property!(Int, i64);
linear_histogram_property!(Uint, u64);
exponential_histogram_property!(Double, f64);
exponential_histogram_property!(Int, i64);
exponential_histogram_property!(Uint, u64);

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{block_type::BlockType, constants, heap::Heap},
        fuchsia_async::{self as fasync, futures::StreamExt},
        mapped_vmo::Mapping,
    };

    #[test]
    fn inspector_new() {
        let test_object = Inspector::new();
        assert_eq!(
            test_object.vmo.as_ref().unwrap().get_size().unwrap(),
            constants::DEFAULT_VMO_SIZE_BYTES as u64
        );
        let root_name_block =
            test_object.root().unwrap_ref().state.lock().heap.get_block(2).unwrap();
        assert_eq!(root_name_block.name_contents().unwrap(), constants::ROOT_NAME);
    }

    #[test]
    fn no_op() {
        let inspector = Inspector::new_with_size(4096);
        // Make the VMO full.
        let nodes = (0..126).map(|_| inspector.root().create_child("test")).collect::<Vec<Node>>();
        let root_block = inspector.root().get_block().unwrap();

        assert!(nodes.iter().all(|node| node.is_valid()));
        assert_eq!(root_block.child_count().unwrap(), 126);
        let no_op_node = inspector.root().create_child("no-op-child");
        assert!(!no_op_node.is_valid());
    }

    #[fasync::run_singlethreaded(test)]
    async fn new_no_op() -> Result<(), Error> {
        let mut fs = ServiceFs::new();

        let inspector = Inspector::new_no_op();
        assert!(!inspector.is_valid());
        assert!(!inspector.root().is_valid());

        // Ensure export doesn't crash on a No-Op inspector
        inspector.export(&mut fs);

        fs.take_and_serve_directory_handle()?;
        fasync::spawn(fs.collect());
        Ok(())
    }

    #[test]
    fn inspector_new_with_size() {
        let test_object = Inspector::new_with_size(8192);
        assert_eq!(test_object.vmo.as_ref().unwrap().get_size().unwrap(), 8192);
        let root_name_block =
            test_object.root().unwrap_ref().state.lock().heap.get_block(2).unwrap();
        assert_eq!(root_name_block.name_contents().unwrap(), constants::ROOT_NAME);

        // If size is not a multiple of 4096, it'll be rounded up.
        let test_object = Inspector::new_with_size(10000);
        assert_eq!(test_object.vmo.unwrap().get_size().unwrap(), 12288);

        // If size is less than the minimum size, the minimum will be set.
        let test_object = Inspector::new_with_size(2000);
        assert_eq!(test_object.vmo.unwrap().get_size().unwrap(), 4096);
    }

    #[test]
    fn inspector_new_root() {
        // Note, the small size we request should be rounded up to a full 4kB page.
        let (vmo, root_node) = Inspector::new_root(100).unwrap();
        assert_eq!(vmo.get_size().unwrap(), 4096);
        let root_name_block = root_node.unwrap_ref().state.lock().heap.get_block(2).unwrap();
        assert_eq!(root_name_block.name_contents().unwrap(), constants::ROOT_NAME);
    }

    #[test]
    fn node() {
        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX);
        let node_block = node.get_block().unwrap();
        assert_eq!(node_block.block_type(), BlockType::NodeValue);
        assert_eq!(node_block.child_count().unwrap(), 0);
        {
            let child = node.create_child("child");
            let child_block = child.get_block().unwrap();
            assert_eq!(child_block.block_type(), BlockType::NodeValue);
            assert_eq!(child_block.child_count().unwrap(), 0);
            assert_eq!(node_block.child_count().unwrap(), 1);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn double_property() {
        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX);
        let node_block = node.get_block().unwrap();
        {
            let property = node.create_double("property", 1.0);
            let property_block = property.get_block().unwrap();
            assert_eq!(property_block.block_type(), BlockType::DoubleValue);
            assert_eq!(property_block.double_value().unwrap(), 1.0);
            assert_eq!(node_block.child_count().unwrap(), 1);

            property.set(2.0);
            assert_eq!(property_block.double_value().unwrap(), 2.0);
            assert_eq!(property.get().unwrap(), 2.0);

            property.subtract(5.5);
            assert_eq!(property_block.double_value().unwrap(), -3.5);

            property.add(8.1);
            assert_eq!(property_block.double_value().unwrap(), 4.6);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn int_property() {
        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX);
        let node_block = node.get_block().unwrap();
        {
            let property = node.create_int("property", 1);
            let property_block = property.get_block().unwrap();
            assert_eq!(property_block.block_type(), BlockType::IntValue);
            assert_eq!(property_block.int_value().unwrap(), 1);
            assert_eq!(node_block.child_count().unwrap(), 1);

            property.set(2);
            assert_eq!(property_block.int_value().unwrap(), 2);
            assert_eq!(property.get().unwrap(), 2);

            property.subtract(5);
            assert_eq!(property_block.int_value().unwrap(), -3);

            property.add(8);
            assert_eq!(property_block.int_value().unwrap(), 5);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn uint_property() {
        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX);
        let node_block = node.get_block().unwrap();
        {
            let property = node.create_uint("property", 1);
            let property_block = property.get_block().unwrap();
            assert_eq!(property_block.block_type(), BlockType::UintValue);
            assert_eq!(property_block.uint_value().unwrap(), 1);
            assert_eq!(node_block.child_count().unwrap(), 1);

            property.set(5);
            assert_eq!(property_block.uint_value().unwrap(), 5);
            assert_eq!(property.get().unwrap(), 5);

            property.subtract(3);
            assert_eq!(property_block.uint_value().unwrap(), 2);

            property.add(8);
            assert_eq!(property_block.uint_value().unwrap(), 10);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn string_property() {
        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX);
        let node_block = node.get_block().unwrap();
        {
            let property = node.create_string("property", "test");
            let property_block = property.get_block().unwrap();
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
        let node_block = node.get_block().unwrap();
        {
            let property = node.create_bytes("property", b"test");
            let property_block = property.get_block().unwrap();
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
    fn test_array() {
        let inspector = Inspector::new();
        let root = inspector.root();
        let root_block = root.get_block().unwrap();
        {
            let array = root.create_double_array("array_property", 5);
            let array_block = array.get_block().unwrap();

            array.set(0, 5.0);
            assert_eq!(array_block.array_get_double_slot(0).unwrap(), 5.0);

            array.add(0, 5.3);
            assert_eq!(array_block.array_get_double_slot(0).unwrap(), 10.3);

            array.subtract(0, 3.4);
            assert_eq!(array_block.array_get_double_slot(0).unwrap(), 6.9);

            array.set(1, 2.5);
            array.set(3, -3.1);

            for (i, value) in [6.9, 2.5, 0.0, -3.1, 0.0].into_iter().enumerate() {
                assert_eq!(array_block.array_get_double_slot(i).unwrap(), *value);
            }

            assert_eq!(root_block.child_count().unwrap(), 1);
        }
        assert_eq!(root_block.child_count().unwrap(), 0);
    }

    #[test]
    fn linear_histograms() {
        let inspector = Inspector::new();
        let root = inspector.root();
        let root_block = root.get_block().unwrap();
        {
            let int_histogram = root.create_int_linear_histogram(
                "int-histogram",
                LinearHistogramParams { floor: 10, step_size: 5, buckets: 5 },
            );
            int_histogram.insert_multiple(-1, 2); // underflow
            int_histogram.insert(25);
            int_histogram.insert(500); // overflow
            let block = int_histogram.get_block().unwrap();
            for (i, value) in [10, 5, 2, 0, 0, 0, 1, 0, 1].iter().enumerate() {
                assert_eq!(block.array_get_int_slot(i).unwrap(), *value);
            }

            let uint_histogram = root.create_uint_linear_histogram(
                "uint-histogram",
                LinearHistogramParams { floor: 10, step_size: 5, buckets: 5 },
            );
            uint_histogram.insert_multiple(0, 2); // underflow
            uint_histogram.insert(25);
            uint_histogram.insert(500); // overflow
            let block = uint_histogram.get_block().unwrap();
            for (i, value) in [10, 5, 2, 0, 0, 0, 1, 0, 1].iter().enumerate() {
                assert_eq!(block.array_get_uint_slot(i).unwrap(), *value);
            }

            let double_histogram = root.create_double_linear_histogram(
                "double-histogram",
                LinearHistogramParams { floor: 10.0, step_size: 5.0, buckets: 5 },
            );
            double_histogram.insert_multiple(0.0, 2); // underflow
            double_histogram.insert(25.3);
            double_histogram.insert(500.0); // overflow
            let block = double_histogram.get_block().unwrap();
            for (i, value) in [10.0, 5.0, 2.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0].iter().enumerate() {
                assert_eq!(block.array_get_double_slot(i).unwrap(), *value);
            }

            assert_eq!(root_block.child_count().unwrap(), 3);
        }
        assert_eq!(root_block.child_count().unwrap(), 0);
    }

    #[test]
    fn exponential_histograms() {
        let inspector = Inspector::new();
        let root = inspector.root();
        let root_block = root.get_block().unwrap();
        {
            let int_histogram = root.create_int_exponential_histogram(
                "int-histogram",
                ExponentialHistogramParams {
                    floor: 1,
                    initial_step: 1,
                    step_multiplier: 2,
                    buckets: 4,
                },
            );
            int_histogram.insert_multiple(-1, 2); // underflow
            int_histogram.insert(8);
            int_histogram.insert(500); // overflow
            let block = int_histogram.get_block().unwrap();
            for (i, value) in [1, 1, 2, 2, 0, 0, 0, 1, 1].iter().enumerate() {
                assert_eq!(block.array_get_int_slot(i).unwrap(), *value);
            }

            let uint_histogram = root.create_uint_exponential_histogram(
                "uint-histogram",
                ExponentialHistogramParams {
                    floor: 1,
                    initial_step: 1,
                    step_multiplier: 2,
                    buckets: 4,
                },
            );
            uint_histogram.insert_multiple(0, 2); // underflow
            uint_histogram.insert(8);
            uint_histogram.insert(500); // overflow
            let block = uint_histogram.get_block().unwrap();
            for (i, value) in [1, 1, 2, 2, 0, 0, 0, 1, 1].iter().enumerate() {
                assert_eq!(block.array_get_uint_slot(i).unwrap(), *value);
            }

            let double_histogram = root.create_double_exponential_histogram(
                "double-histogram",
                ExponentialHistogramParams {
                    floor: 1.0,
                    initial_step: 1.0,
                    step_multiplier: 2.0,
                    buckets: 4,
                },
            );
            double_histogram.insert_multiple(0.0, 2); // underflow
            double_histogram.insert(8.3);
            double_histogram.insert(500.0); // overflow
            let block = double_histogram.get_block().unwrap();
            for (i, value) in [1.0, 1.0, 2.0, 2.0, 0.0, 0.0, 0.0, 1.0, 1.0].iter().enumerate() {
                assert_eq!(block.array_get_double_slot(i).unwrap(), *value);
            }

            assert_eq!(root_block.child_count().unwrap(), 3);
        }
        assert_eq!(root_block.child_count().unwrap(), 0);
    }

    #[test]
    fn owned_method_argument_properties() {
        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let node = Node::allocate(state, "root", constants::HEADER_INDEX);
        let node_block =
            node.unwrap_ref().state.lock().heap.get_block(node.unwrap_ref().block_index).unwrap();
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

    #[test]
    fn dummy_partialeq() -> Result<(), Error> {
        let inspector = Inspector::new();
        let root = inspector.root();

        // Types should all be equal to another type. This is to enable clients
        // with inspect types in their structs be able to derive PartialEq and
        // Eq smoothly.
        assert_eq!(root, &root.create_child("child1"));
        assert_eq!(root.create_int("property1", 1), root.create_int("property2", 2));
        assert_eq!(root.create_double("property1", 1.0), root.create_double("property2", 2.0));
        assert_eq!(root.create_uint("property1", 1), root.create_uint("property2", 2));
        assert_eq!(
            root.create_string("property1", "value1"),
            root.create_string("property2", "value2")
        );
        assert_eq!(
            root.create_bytes("property1", b"value1"),
            root.create_bytes("property2", b"value2")
        );

        Ok(())
    }

    fn get_state(mapping: Arc<Mapping>) -> Arc<Mutex<State>> {
        let heap = Heap::new(mapping).unwrap();
        Arc::new(Mutex::new(State::create(heap).unwrap()))
    }

}
