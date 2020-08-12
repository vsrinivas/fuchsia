// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO Follow 2018 idioms
#![allow(elided_lifetimes_in_paths)]

//! The Fuchsia Inspect format for structured metrics trees.

use {
    crate::{
        format::{
            block::{ArrayFormat, LinkNodeDisposition, PropertyFormat},
            constants,
        },
        heap::Heap,
        state::State,
    },
    anyhow::{format_err, Error},
    derivative::Derivative,
    fidl::endpoints::DiscoverableService,
    fidl_fuchsia_inspect::TreeMarker,
    fidl_fuchsia_io::{DirectoryMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceObjTrait},
    fuchsia_syslog::macros::*,
    fuchsia_zircon::{self as zx, HandleBased},
    futures::{future::BoxFuture, prelude::*},
    lazy_static::lazy_static,
    mapped_vmo::Mapping,
    parking_lot::Mutex,
    paste,
    std::{
        cmp::max,
        default::Default,
        fmt::Debug,
        sync::{
            atomic::{AtomicUsize, Ordering},
            Arc, Weak,
        },
    },
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path,
        pseudo_directory, service as pseudo_fs_service,
    },
};

#[cfg(test)]
use crate::format::block::Block;

pub mod component;
pub mod format;
pub mod health;
pub mod heap;
pub mod reader;
pub mod trie;
#[macro_use]
pub mod testing;
pub mod service;
mod state;
mod utils;

/// Directory where the diagnostics service should be added.
pub const SERVICE_DIR: &str = "diagnostics";

lazy_static! {
  // Suffix used for unique names.
  static ref UNIQUE_NAME_SUFFIX: AtomicUsize = AtomicUsize::new(0);
}

/// Root of the Inspect API
#[derive(Clone)]
pub struct Inspector {
    /// The root node.
    root_node: Arc<Node>,

    /// The VMO backing the inspector
    pub(in crate) vmo: Option<Arc<zx::Vmo>>,
}

/// Holds a list of inspect types that won't change.
#[derive(Derivative)]
#[derivative(Debug, PartialEq, Eq)]
struct ValueList {
    #[derivative(PartialEq = "ignore")]
    #[derivative(Debug = "ignore")]
    values: Mutex<Option<InspectTypeList>>,
}

impl Default for ValueList {
    fn default() -> Self {
        ValueList::new()
    }
}

type InspectTypeList = Vec<Box<dyn InspectType>>;

impl ValueList {
    /// Creates a new empty value list.
    pub fn new() -> Self {
        Self { values: Mutex::new(None) }
    }

    /// Stores an inspect type that won't change.
    pub fn record(&self, value: impl InspectType + 'static) {
        let boxed_value = Box::new(value);
        let mut values_lock = self.values.lock();
        if let Some(ref mut values) = *values_lock {
            values.push(boxed_value);
        } else {
            *values_lock = Some(vec![boxed_value]);
        }
    }
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
            Ok((vmo, root_node)) => {
                Inspector { vmo: Some(Arc::new(vmo)), root_node: Arc::new(root_node) }
            }
            Err(e) => {
                fx_log_err!("Failed to create root node. Error: {}", e);
                Inspector::new_no_op()
            }
        }
    }

    /// Returns a duplicate of the underlying VMO for this inspector.
    ///
    /// The duplicated VMO will be read-only, and is suitable to send to clients over FIDL.
    pub fn duplicate_vmo(&self) -> Option<zx::Vmo> {
        self.vmo
            .as_ref()
            .map(|vmo| {
                vmo.duplicate_handle(zx::Rights::BASIC | zx::Rights::READ | zx::Rights::MAP).ok()
            })
            .unwrap_or(None)
    }

    /// Returns a VMO holding a copy of the data in this inspector.
    ///
    /// The copied VMO will be read-only.
    pub fn copy_vmo(&self) -> Option<zx::Vmo> {
        self.copy_vmo_data().and_then(|data| {
            if let Ok(vmo) = zx::Vmo::create(data.len() as u64) {
                vmo.write(&data, 0).ok().map(|_| vmo)
            } else {
                None
            }
        })
    }

    /// Returns a copy of the bytes stored in the VMO for this inspector.
    ///
    /// The output will be truncated to only those bytes that are needed to accurately read the
    /// stored data.
    pub fn copy_vmo_data(&self) -> Option<Vec<u8>> {
        self.root_node
            .inner
            .inner_ref()
            .map(|inner_ref| {
                let state = inner_ref.state.lock();
                Some(state.heap.bytes())
            })
            .unwrap_or(None)
    }

    /// Spawns a server for handling inspect `Tree` requests in the diagnostics directory.
    pub fn serve<'a, ServiceObjTy: ServiceObjTrait>(
        &self,
        service_fs: &mut ServiceFs<ServiceObjTy>,
    ) -> Result<(), Error> {
        let (proxy, server) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;
        let inspector_clone = self.clone();
        let dir = pseudo_directory! {
            TreeMarker::SERVICE_NAME => pseudo_fs_service::host(move |stream| {
                let inspector_clone_clone = inspector_clone.clone();
                async move {
                    service::handle_request_stream(inspector_clone_clone, stream).await
                        .unwrap_or_else(|e| fx_log_err!("failed to run server: {:?}", e));
                }
                .boxed()
            }),
        };

        let server_end = server.into_channel().into();
        let scope = ExecutionScope::from_executor(Box::new(fasync::EHandle::local()));
        dir.open(scope, OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, 0, Path::empty(), server_end);
        service_fs.add_remote(SERVICE_DIR, proxy);

        Ok(())
    }

    /// Get the root of the VMO object.
    pub fn root(&self) -> &Node {
        &self.root_node
    }

    fn state(&self) -> Option<Arc<Mutex<State>>> {
        self.root().inner.inner_ref().map(|inner_ref| inner_ref.state.clone())
    }

    /// Creates a new No-Op inspector
    fn new_no_op() -> Self {
        Inspector { vmo: None, root_node: Arc::new(Node::new_no_op()) }
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
        Ok((vmo, Node::new_root(Arc::new(Mutex::new(state)))))
    }

    /// Creates an no-op inspector from the given Vmo. If the VMO is corrupted, reading can fail.
    fn no_op_from_vmo(vmo: Arc<zx::Vmo>) -> Inspector {
        Inspector { vmo: Some(vmo), root_node: Arc::new(Node::new_no_op()) }
    }
}

/// Trait implemented by all inspect types.
pub trait InspectType: Send + Sync {}

/// Trait implemented by all inspect types. It provides constructor functions that are not
/// intended for use outside the crate.
trait InspectTypeInternal {
    fn new(state: Arc<Mutex<State>>, block_index: u32) -> Self;
    fn new_no_op() -> Self;
    fn is_valid(&self) -> bool;
}

/// An inner type of all inspect nodes and properties. Each variant implies a
/// different relationship with the underlying inspect VMO.
#[derive(Debug, Derivative)]
#[derivative(Default)]
enum Inner<T: InnerType> {
    /// The node or property is not attached to the inspect VMO.
    #[derivative(Default)]
    None,

    /// The node or property is attached to the inspect VMO, iff its strong
    /// reference is still alive.
    Weak(Weak<InnerRef<T>>),

    /// The node or property is attached to the inspect VMO.
    Strong(Arc<InnerRef<T>>),
}

impl<T: InnerType> Inner<T> {
    /// Create a new Inner with the desired block index within the inspect VMO
    fn new(state: Arc<Mutex<State>>, block_index: u32) -> Self {
        Self::Strong(Arc::new(InnerRef { state, block_index, data: T::Data::default() }))
    }

    /// Returns true if the number of strong references to this node or property
    /// is greater than 0.
    fn is_valid(&self) -> bool {
        match self {
            Self::None => false,
            Self::Weak(weak_ref) => weak_ref.strong_count() > 0,
            Self::Strong(_) => true,
        }
    }

    /// Returns a `Some(Arc<InnerRef>)` iff the node or property is currently
    /// attached to inspect, or `None` otherwise. Weak pointers are upgraded
    /// if possible, but their lifetime as strong references are expected to be
    /// short.
    fn inner_ref(&self) -> Option<Arc<InnerRef<T>>> {
        match self {
            Self::None => None,
            Self::Weak(weak_ref) => weak_ref.upgrade(),
            Self::Strong(inner_ref) => Some(Arc::clone(inner_ref)),
        }
    }

    /// Make a weak reference.
    fn clone_weak(&self) -> Self {
        match self {
            Self::None => Self::None,
            Self::Weak(weak_ref) => Self::Weak(weak_ref.clone()),
            Self::Strong(inner_ref) => Self::Weak(Arc::downgrade(inner_ref)),
        }
    }
}

/// Inspect API types implement Eq,PartialEq returning true all the time so that
/// structs embedding inspect types can derive these traits as well.
/// IMPORTANT: Do not rely on these traits implementations for real comparisons
/// or validation tests, instead leverage the reader.
impl<T: InnerType> PartialEq for Inner<T> {
    fn eq(&self, _other: &Self) -> bool {
        true
    }
}

impl<T: InnerType> Eq for Inner<T> {}

/// A type that is owned by inspect nodes and properties, sharing ownership of
/// the inspect VMO heap, and with numerical pointers to the location in the
/// heap in which it resides.
#[derive(Debug)]
struct InnerRef<T: InnerType> {
    /// Index of the block in the VMO.
    block_index: u32,

    /// Reference to the VMO heap.
    state: Arc<Mutex<State>>,

    /// Associated data for this type.
    data: T::Data,
}

impl<T: InnerType> Drop for InnerRef<T> {
    /// InnerRef has a manual drop impl, to guarantee a single deallocation in
    /// the case of multiple strong references.
    fn drop(&mut self) {
        T::free(&mut *self.state.lock(), self.block_index).unwrap();
    }
}

/// De-allocation behavior and associated data for an inner type.
trait InnerType {
    /// Associated data stored on the InnerRef
    type Data: Default + Debug;

    /// De-allocation behavior for when the InnerRef gets dropped
    fn free(state: &mut State, block_index: u32) -> Result<(), Error>;
}

#[derive(Default, Debug)]
struct InnerNodeType;

impl InnerType for InnerNodeType {
    // Each node has a list of recorded values.
    type Data = ValueList;

    fn free(state: &mut State, block_index: u32) -> Result<(), Error> {
        if block_index == 0 {
            return Ok(());
        }
        state
            .free_value(block_index)
            .map_err(|err| err.context(format!("Failed to free node index={}", block_index)))
    }
}

#[derive(Default, Debug)]
struct InnerValueType;

impl InnerType for InnerValueType {
    type Data = ();
    fn free(state: &mut State, block_index: u32) -> Result<(), Error> {
        state
            .free_value(block_index)
            .map_err(|err| err.context(format!("Failed to free value index={}", block_index)))
    }
}

#[derive(Default, Debug)]
struct InnerPropertyType;

impl InnerType for InnerPropertyType {
    type Data = ();
    fn free(state: &mut State, block_index: u32) -> Result<(), Error> {
        state
            .free_property(block_index)
            .map_err(|err| err.context(format!("Failed to free property index={}", block_index)))
    }
}

#[derive(Default, Debug)]
struct InnerLazyNodeType;

impl InnerType for InnerLazyNodeType {
    type Data = ();
    fn free(state: &mut State, block_index: u32) -> Result<(), Error> {
        state
            .free_lazy_node(block_index)
            .map_err(|err| err.context(format!("Failed to free lazy node index={}", block_index)))
    }
}

/// Utility for generating the implementation of all inspect types (including the struct):
///  - All Inspect Types (*Property, Node) can be No-Op. This macro generates the
///    appropiate internal constructors.
///  - All Inspect Types derive PartialEq, Eq. This generates the dummy implementation
///    for the wrapped type.
macro_rules! inspect_type_impl {
    ($(#[$attr:meta])* struct $name:ident, $type:ident) => {
        paste::item! {
            $(#[$attr])*
            /// NOTE: do not rely on PartialEq implementation for true comparison.
            /// Instead leverage the reader.
            ///
            /// NOTE: Operations on a Default value are no-ops.
            #[derive(Debug, PartialEq, Eq, Default)]
            pub struct $name {
                inner: Inner<$type>,
            }

            #[cfg(test)]
            impl $name {
                #[allow(missing_docs)]
                pub fn get_block(&self) -> Option<Block<Arc<Mapping>>> {
                    self.inner.inner_ref().and_then(|inner_ref| {
                        inner_ref.state.lock().heap.get_block(inner_ref.block_index).ok()
                    })
                }

                #[allow(missing_docs)]
                pub fn block_index(&self) -> u32 {
                    self.inner.inner_ref().unwrap().block_index
                }
            }

            impl InspectType for $name {}

            impl InspectTypeInternal for $name {
                fn new(state: Arc<Mutex<State>>, block_index: u32) -> Self {
                    Self {
                        inner: Inner::new(state, block_index),
                    }
                }

                fn is_valid(&self) -> bool {
                    self.inner.is_valid()
                }

                fn new_no_op() -> Self {
                    Self { inner: Inner::None }
                }
            }
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
            #[must_use]
            #[allow(missing_docs)]
            pub fn [<create_ $name >](&self, name: impl AsRef<str>, value: $type)
                -> [<$name_cap Property>] {
                    self.inner.inner_ref().and_then(|inner_ref| {
                        inner_ref.state
                            .lock()
                            .[<create_ $name _metric>](name.as_ref(), value, inner_ref.block_index)
                            .map(|block| {
                                [<$name_cap Property>]::new(inner_ref.state.clone(), block.index())
                            })
                            .ok()
                    })
                    .unwrap_or([<$name_cap Property>]::new_no_op())
            }

            pub fn [<record_ $name >](&self, name: impl AsRef<str>, value: $type) {
                let property = self.[<create_ $name>](name, value);
                self.record(property);
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
            #[must_use]
            #[allow(missing_docs)]
            pub fn [<create_ $name _array>](&self, name: impl AsRef<str>, slots: usize)
                -> [<$name_cap ArrayProperty>] {
                    self.[<create_ $name _array_internal>](name, slots, ArrayFormat::Default)
            }

            fn [<create_ $name _array_internal>](&self, name: impl AsRef<str>, slots: usize, format: ArrayFormat)
                -> [<$name_cap ArrayProperty>] {
                    self.inner.inner_ref().and_then(|inner_ref| {
                        inner_ref.state
                            .lock()
                            .[<create_ $name _array>](name.as_ref(), slots, format, inner_ref.block_index)
                            .map(|block| {
                                [<$name_cap ArrayProperty>]::new(inner_ref.state.clone(), block.index())
                            })
                            .ok()
                    })
                    .unwrap_or([<$name_cap ArrayProperty>]::new_no_op())
            }
        }
    };
}

#[allow(missing_docs)]
pub struct LinearHistogramParams<T> {
    pub floor: T,
    pub step_size: T,
    pub buckets: usize,
}

/// Utility for generating functions to create a linear histogram property.
///   `name`: identifier for the name (example: double)
///   `name_cap`: identifier for the name capitalized (example: Double)
///   `type`: the type of the numeric property (example: f64)
macro_rules! create_linear_histogram_property_fn {
    ($name:ident, $name_cap:ident, $type:ident) => {
        paste::item! {
            #[must_use]
            #[allow(missing_docs)]
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

#[allow(missing_docs)]
pub struct ExponentialHistogramParams<T> {
    pub floor: T,
    pub initial_step: T,
    pub step_multiplier: T,
    pub buckets: usize,
}

/// Utility for generating functions to create an exponential histogram property.
///   `name`: identifier for the name (example: double)
///   `name_cap`: identifier for the name capitalized (example: Double)
///   `type`: the type of the numeric property (example: f64)
macro_rules! create_exponential_histogram_property_fn {
    ($name:ident, $name_cap:ident, $type:ident) => {
        paste::item! {
            #[must_use]
            #[allow(missing_docs)]
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

/// Utility for generating functions to create lazy nodes.
///   `fn_suffix`: identifier for the fn name.
///   `disposition`: identifier for the type of LinkNodeDisposition.
macro_rules! create_lazy_property_fn {
    ($fn_suffix:ident, $disposition:ident) => {
        paste::item! {
            #[must_use]
            pub fn [<create_lazy_ $fn_suffix>]<F>(&self, name: impl AsRef<str>, callback: F) -> LazyNode
            where F: Fn() -> BoxFuture<'static, Result<Inspector, Error>> + Sync + Send + 'static {
                self.inner.inner_ref().and_then(|inner_ref| {
                    inner_ref
                        .state
                        .lock()
                        .create_lazy_node(
                            name.as_ref(),
                            inner_ref.block_index,
                            LinkNodeDisposition::$disposition,
                            callback,
                        )
                        .map(|block| LazyNode::new(inner_ref.state.clone(), block.index()))
                        .ok()
                })
                .unwrap_or(LazyNode::new_no_op())
            }

            pub fn [<record_lazy_ $fn_suffix>]<F>(
                &self, name: impl AsRef<str>, callback: F)
            where F: Fn() -> BoxFuture<'static, Result<Inspector, Error>> + Sync + Send + 'static {
                let property = self.[<create_lazy_ $fn_suffix>](name, callback);
                self.record(property);
            }
        }
    }
}

inspect_type_impl!(
    /// Inspect API Node data type.
    struct Node,
    InnerNodeType
);

inspect_type_impl!(
    /// Inspect API Lazy Node data type.
    struct LazyNode,
    InnerLazyNodeType
);

impl Node {
    pub(in crate) fn new_root(state: Arc<Mutex<State>>) -> Node {
        Node::new(state, 0)
    }

    /// Create a weak reference to the original node. All operations on a weak
    /// reference have identical semantics to the original node for as long
    /// as the original node is live. After that, all operations are no-ops.
    pub fn clone_weak(&self) -> Node {
        Self { inner: self.inner.clone_weak() }
    }

    /// Creates and keeps track of a child with the given `name`.
    pub fn record_child<F>(&self, name: impl AsRef<str>, initialize: F)
    where
        F: FnOnce(&mut Node),
    {
        let mut child = self.create_child(name);
        initialize(&mut child);
        self.record(child);
    }

    /// Add a child to this node.
    #[must_use]
    pub fn create_child(&self, name: impl AsRef<str>) -> Node {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .lock()
                    .create_node(name.as_ref(), inner_ref.block_index)
                    .map(|block| Node::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(Node::new_no_op())
    }

    /// Keeps track of the given property for the lifetime of the node.
    pub fn record(&self, property: impl InspectType + 'static) {
        self.inner.inner_ref().map(|inner_ref| inner_ref.data.record(property));
    }

    // Add a lazy node property to this node:
    // - create_lazy_node: adds a lazy child to this node. This node will be
    //   populated by the given callback on demand.
    // - create_lazy_values: adds a lazy child to this node. The lazy node
    //   children and properties are added to this node on demand. Name is only
    //   used in the event that a reader does not obtain the values.
    create_lazy_property_fn!(child, Child);
    create_lazy_property_fn!(values, Inline);

    // Add a numeric property to this node: create_int, create_double,
    // create_uint.
    create_numeric_property_fn!(int, Int, i64);
    create_numeric_property_fn!(uint, Uint, u64);
    create_numeric_property_fn!(double, Double, f64);

    // Add an array property to this node: create_int_array, create_double_array,
    // create_uint_array.
    create_array_property_fn!(int, Int, i64);
    create_array_property_fn!(uint, Uint, u64);
    create_array_property_fn!(double, Double, f64);

    // Add a linear histogram property to this node: create_int_linear_histogram,
    // create_uint_linear_histogram, create_double_linear_histogram.
    create_linear_histogram_property_fn!(int, Int, i64);
    create_linear_histogram_property_fn!(uint, Uint, u64);
    create_linear_histogram_property_fn!(double, Double, f64);

    // Add an exponential histogram property to this node: create_int_exponential_histogram,
    // create_uint_exponential_histogram, create_double_exponential_histogram.
    create_exponential_histogram_property_fn!(int, Int, i64);
    create_exponential_histogram_property_fn!(uint, Uint, u64);
    create_exponential_histogram_property_fn!(double, Double, f64);

    /// Creates a lazy node from the given VMO.
    #[must_use]
    pub fn create_lazy_child_from_vmo(&self, name: impl AsRef<str>, vmo: Arc<zx::Vmo>) -> LazyNode {
        self.create_lazy_child(name.as_ref(), move || {
            let vmo_clone = vmo.clone();
            async move { Ok(Inspector::no_op_from_vmo(vmo_clone)) }.boxed()
        })
    }

    /// Records a lazy node from the given VMO.
    pub fn record_lazy_child_from_vmo(&self, name: impl AsRef<str>, vmo: Arc<zx::Vmo>) {
        self.record_lazy_child(name.as_ref(), move || {
            let vmo_clone = vmo.clone();
            async move { Ok(Inspector::no_op_from_vmo(vmo_clone)) }.boxed()
        });
    }

    /// Add a string property to this node.
    #[must_use]
    pub fn create_string(&self, name: impl AsRef<str>, value: impl AsRef<str>) -> StringProperty {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .lock()
                    .create_property(
                        name.as_ref(),
                        value.as_ref().as_bytes(),
                        PropertyFormat::String,
                        inner_ref.block_index,
                    )
                    .map(|block| StringProperty::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(StringProperty::new_no_op())
    }

    /// Creates and saves a string property for the lifetime of the node.
    pub fn record_string(&self, name: impl AsRef<str>, value: impl AsRef<str>) {
        let property = self.create_string(name, value);
        self.record(property);
    }

    /// Add a byte vector property to this node.
    #[must_use]
    pub fn create_bytes(&self, name: impl AsRef<str>, value: impl AsRef<[u8]>) -> BytesProperty {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .lock()
                    .create_property(
                        name.as_ref(),
                        value.as_ref(),
                        PropertyFormat::Bytes,
                        inner_ref.block_index,
                    )
                    .map(|block| BytesProperty::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(BytesProperty::new_no_op())
    }

    /// Creates and saves a bytes property for the lifetime of the node.
    pub fn record_bytes(&self, name: impl AsRef<str>, value: impl AsRef<[u8]>) {
        let property = self.create_bytes(name, value);
        self.record(property);
    }

    /// Add a bool property to this node.
    #[must_use]
    pub fn create_bool(&self, name: impl AsRef<str>, value: bool) -> BoolProperty {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .lock()
                    .create_bool(name.as_ref(), value, inner_ref.block_index)
                    .map(|block| BoolProperty::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(BoolProperty::new_no_op())
    }

    /// Creates and saves a bool property for the lifetime of the node.
    pub fn record_bool(&self, name: impl AsRef<str>, value: bool) {
        let property = self.create_bool(name, value);
        self.record(property);
    }
}

/// Trait implemented by properties.
pub trait Property<'t> {
    #[allow(missing_docs)]
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
                if let Some(ref inner_ref) = self.inner.inner_ref() {
                    inner_ref.state.lock().[<$fn_name _ $name _metric>](inner_ref.block_index, value)
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
///   `name_cap`: the capitalized readble name of the type of the function (example: Double)
///   `type`: the type of the argument of the function to generate (example: f64)
macro_rules! numeric_property {
    ($name:ident, $name_cap:ident, $type:ident) => {
        paste::item! {
            inspect_type_impl!(
                /// Inspect API Numeric Property data type.
                struct [<$name_cap Property>],
                InnerValueType
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
                    if let Some(ref inner_ref) = self.inner.inner_ref() {
                        inner_ref.state.lock().[<get_ $name _metric>](inner_ref.block_index)
                    } else {
                        Err(format_err!("Property is No-Op"))
                    }
                }
            }
        }
    };
}

numeric_property!(int, Int, i64);
numeric_property!(uint, Uint, u64);
numeric_property!(double, Double, f64);

/// Utility for generating a byte/string property datatype impl
///   `name`: the readable name of the type of the function (example: String)
///   `type`: the type of the argument of the function to generate (example: str)
///   `bytes`: an optional method to get the bytes of the property
macro_rules! property {
    ($name:ident, $type:expr $(, $bytes:ident)?) => {
        paste::item! {
            inspect_type_impl!(
                /// Inspect API Property data type.
                struct [<$name Property>],
                InnerPropertyType
            );

            impl<'t> Property<'t> for [<$name Property>] {
                type Type = &'t $type;

                fn set(&'t self, value: &'t $type) {
                    if let Some(ref inner_ref) = self.inner.inner_ref() {
                        inner_ref.state.lock().set_property(inner_ref.block_index, value$(.$bytes())?)
                            .unwrap_or_else(|e| fx_log_err!("Failed to set property. Error: {:?}", e));
                    }
                }

            }
        }
    };
}

property!(String, str, as_bytes);
property!(Bytes, [u8]);

inspect_type_impl!(
    /// Inspect API Bool Property data type.
    struct BoolProperty,
    InnerValueType
);

impl<'t> Property<'t> for BoolProperty {
    type Type = bool;

    fn set(&self, value: bool) {
        if let Some(ref inner_ref) = self.inner.inner_ref() {
            inner_ref.state.lock().set_bool(inner_ref.block_index, value).unwrap_or_else(|e| {
                fx_log_err!("Failed to set property. Error: {:?}", e);
            });
        }
    }
}

#[allow(missing_docs)]
pub trait ArrayProperty {
    #[allow(missing_docs)]
    type Type;

    /// Set the array value to |value| at the given |index|.
    fn set(&self, index: usize, value: Self::Type);

    /// Add the given |value| to the property current value at the given |index|.
    fn add(&self, index: usize, value: Self::Type);

    /// Subtract the given |value| to the property current value at the given |index|.
    fn subtract(&self, index: usize, value: Self::Type);

    /// Sets all slots of the array to 0.
    fn clear(&self);
}

/// Utility for generating array property functions (example: set, add, subtract)
///   `fn_name`: the name of the function to generate (example: set)
///   `type`: the type of the argument of the function to generate (example: f64)
///   `name`: the readble name of the type of the function (example: double)
macro_rules! array_property_fn {
    ($fn_name:ident, $type:ident, $name:ident) => {
        paste::item! {
            fn $fn_name(&self, index: usize, value: $type) {
                if let Some(ref inner_ref) = self.inner.inner_ref() {
                    inner_ref.state.lock().[<$fn_name _array_ $name _slot>](inner_ref.block_index, index, value)
                        .unwrap_or_else(|e| {
                            fx_log_err!("Failed to {} property. Error: {:?}", stringify!($fn_name), e);
                        });
                }
            }
        }
    };
}

/// Utility for generating a numeric array datatype impl
///   `name`: the readble name of the type of the function (example: double)
///   `type`: the type of the argument of the function to generate (example: f64)
macro_rules! array_property {
    ($name:ident, $name_cap:ident, $type:ident) => {
        paste::item! {
            inspect_type_impl!(
                /// Inspect API Array Property data type.
                struct [<$name_cap ArrayProperty>],
                InnerValueType
            );

            impl [<$name_cap ArrayProperty>] {
            }

            impl ArrayProperty for [<$name_cap ArrayProperty>] {
                type Type = $type;

                array_property_fn!(set, $type, $name);
                array_property_fn!(add, $type, $name);
                array_property_fn!(subtract, $type, $name);

                fn clear(&self) {
                    if let Some(ref inner_ref) = self.inner.inner_ref() {
                        inner_ref.state.lock().clear_array(inner_ref.block_index, 0)
                            .unwrap_or_else(|e| {
                                fx_log_err!("Failed to clear property. Error: {:?}", e);
                            });
                    }
                }
            }
        }
    };
}

array_property!(int, Int, i64);
array_property!(uint, Uint, u64);
array_property!(double, Double, f64);

#[allow(missing_docs)]
pub trait HistogramProperty {
    #[allow(missing_docs)]
    type Type;

    /// Inserts a new value in the histogram.
    fn insert(&self, value: Self::Type);

    /// Inserts the given value in the histogram |count| times.
    fn insert_multiple(&self, value: Self::Type, count: usize);

    /// Clears all buckets of the histogram.
    fn clear(&self);
}

macro_rules! histogram_property {
    ($histogram_type:ident, $name_cap:ident, $type:ident, $clear_start_index:expr) => {
        paste::item! {
            impl HistogramProperty for [<$name_cap $histogram_type HistogramProperty>] {
                type Type = $type;

                fn insert(&self, value: $type) {
                    self.insert_multiple(value, 1);
                }

                fn insert_multiple(&self, value: $type, count: usize) {
                    self.array.add(self.get_index(value), count as $type);
                }

                fn clear(&self) {
                    if let Some(ref inner_ref) = self.array.inner.inner_ref() {
                        // Ensure we don't delete the array slots that contain histogram metadata.
                        inner_ref.state.lock().clear_array(inner_ref.block_index, $clear_start_index)
                            .unwrap_or_else(|e| {
                                fx_log_err!("Failed to {} property. Error: {:?}", stringify!($fn_name), e);
                            });
                    }
                }
            }
        }
    };
}

macro_rules! linear_histogram_property {
    ($name_cap:ident, $type:ident) => {
        paste::item! {
            #[derive(Debug)]
            pub struct [<$name_cap LinearHistogramProperty>] {
                array: [<$name_cap ArrayProperty>],
                floor: $type,
                slots: usize,
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
                fn get_block(&self) -> Option<Block<Arc<Mapping>>> {
                    self.array.get_block()
                }
            }

            histogram_property!(
                Linear, $name_cap, $type,
                // -2 = the overflow and underflow slots which still need to be cleared.
                constants::LINEAR_HISTOGRAM_EXTRA_SLOTS - 2);
        }
    };
}

macro_rules! exponential_histogram_property {
    ($name_cap:ident, $type:ident) => {
        paste::item! {
            #[derive(Debug)]
            pub struct [<$name_cap ExponentialHistogramProperty>] {
                array: [<$name_cap ArrayProperty>],
                floor: $type,
                initial_step: $type,
                step_multiplier: $type,
                slots: usize,
            }

            impl [<$name_cap ExponentialHistogramProperty>] {
                fn get_index(&self, value: $type) -> usize {
                    let mut current_floor = self.floor;
                    let mut offset = self.initial_step;
                    // Start in the underflow index.
                    let mut index = constants::EXPONENTIAL_HISTOGRAM_EXTRA_SLOTS - 2;
                    while value >= current_floor && index < self.slots - 1 {
                        current_floor = self.floor + offset;
                        offset *= self.step_multiplier;
                        index += 1;
                    }
                    index as usize
                }

                #[cfg(test)]
                fn get_block(&self) -> Option<Block<Arc<Mapping>>> {
                    self.array.get_block()
                }
            }

            histogram_property!(
                Exponential, $name_cap, $type,
                // -2 = the overflow and underflow slots which still need to be cleared.
                constants::EXPONENTIAL_HISTOGRAM_EXTRA_SLOTS - 2);
        }
    };
}

linear_histogram_property!(Double, f64);
linear_histogram_property!(Int, i64);
linear_histogram_property!(Uint, u64);
exponential_histogram_property!(Double, f64);
exponential_histogram_property!(Int, i64);
exponential_histogram_property!(Uint, u64);

/// Generates a unique name that can be used in inspect nodes and properties.
pub fn unique_name(prefix: &str) -> String {
    let suffix = UNIQUE_NAME_SUFFIX.fetch_add(1, Ordering::Relaxed);
    format!("{}{}", prefix, suffix)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            assert_inspect_tree,
            format::{block::LinkNodeDisposition, block_type::BlockType, constants},
            heap::Heap,
            reader,
        },
        anyhow::bail,
        fdio,
        fidl::endpoints::DiscoverableService,
        fidl_fuchsia_sys::ComponentControllerEvent,
        fuchsia_async as fasync,
        fuchsia_component::client,
        fuchsia_component::server::ServiceObj,
        glob::glob,
        mapped_vmo::Mapping,
    };

    const TEST_COMPONENT_CMX: &str = "inspect_test_component.cmx";
    const TEST_COMPONENT_URL: &str =
        "fuchsia-pkg://fuchsia.com/fuchsia-inspect-tests#meta/inspect_test_component.cmx";

    #[test]
    fn inspector_new() {
        let test_object = Inspector::new();
        assert_eq!(
            test_object.vmo.as_ref().unwrap().get_size().unwrap(),
            constants::DEFAULT_VMO_SIZE_BYTES as u64
        );
    }

    #[test]
    fn inspector_duplicate_vmo() {
        let test_object = Inspector::new();
        assert_eq!(
            test_object.vmo.as_ref().unwrap().get_size().unwrap(),
            constants::DEFAULT_VMO_SIZE_BYTES as u64
        );
        assert_eq!(
            test_object.duplicate_vmo().unwrap().get_size().unwrap(),
            constants::DEFAULT_VMO_SIZE_BYTES as u64
        );
    }

    #[test]
    fn inspector_copy_data() {
        let test_object = Inspector::new();

        assert_eq!(
            test_object.vmo.as_ref().unwrap().get_size().unwrap(),
            constants::DEFAULT_VMO_SIZE_BYTES as u64
        );
        // The copy will be a single page, since that is all that is used.
        assert_eq!(test_object.copy_vmo_data().unwrap().len(), 4096);
    }

    #[test]
    fn no_op() {
        let inspector = Inspector::new_with_size(4096);
        // Make the VMO full.
        let nodes = (0..127).map(|_| inspector.root().create_child("test")).collect::<Vec<Node>>();

        assert!(nodes.iter().all(|node| node.is_valid()));
        let no_op_node = inspector.root().create_child("no-op-child");
        assert!(!no_op_node.is_valid());
    }

    #[fasync::run_singlethreaded(test)]
    async fn new_no_op() -> Result<(), Error> {
        let mut fs: ServiceFs<ServiceObj<'_, ()>> = ServiceFs::new();

        let inspector = Inspector::new_no_op();
        assert!(!inspector.is_valid());
        assert!(!inspector.root().is_valid());

        // Ensure serve doesn't crash on a No-Op inspector
        inspector.serve(&mut fs)
    }

    #[test]
    fn inspector_new_with_size() {
        let test_object = Inspector::new_with_size(8192);
        assert_eq!(test_object.vmo.as_ref().unwrap().get_size().unwrap(), 8192);

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
        let inner = root_node.inner.inner_ref().unwrap();
        assert_eq!(inner.block_index, 0);
    }

    #[test]
    fn node() {
        // Create and use a default value.
        let default = Node::default();
        default.record_int("a", 0);

        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let root = Node::new_root(state);
        let node = root.create_child("node");
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
    fn node_no_op_clone_weak() {
        let default = Node::default();
        assert!(!default.is_valid());
        let weak = default.clone_weak();
        assert!(!weak.is_valid());
        let _ = weak.create_child("child");
        std::mem::drop(default);
        let _ = weak.create_uint("age", 1337);
        assert!(!weak.is_valid());
    }

    #[test]
    fn node_clone_weak() {
        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let root = Node::new_root(state);
        let node = root.create_child("node");
        let node_weak = node.clone_weak();
        let node_weak_2 = node_weak.clone_weak(); // Weak from another weak

        let node_block = node.get_block().unwrap();
        assert_eq!(node_block.block_type(), BlockType::NodeValue);
        assert_eq!(node_block.child_count().unwrap(), 0);
        let node_weak_block = node.get_block().unwrap();
        assert_eq!(node_weak_block.block_type(), BlockType::NodeValue);
        assert_eq!(node_weak_block.child_count().unwrap(), 0);
        let node_weak_2_block = node.get_block().unwrap();
        assert_eq!(node_weak_2_block.block_type(), BlockType::NodeValue);
        assert_eq!(node_weak_2_block.child_count().unwrap(), 0);

        let child_from_strong = node.create_child("child");
        let child = node_weak.create_child("child_1");
        let child_2 = node_weak_2.create_child("child_2");
        std::mem::drop(node_weak_2);
        assert_eq!(node_weak_block.child_count().unwrap(), 3);
        std::mem::drop(child_from_strong);
        assert_eq!(node_weak_block.child_count().unwrap(), 2);
        std::mem::drop(child);
        assert_eq!(node_weak_block.child_count().unwrap(), 1);
        assert!(node_weak.is_valid());
        assert!(child_2.is_valid());
        std::mem::drop(node);
        assert!(!node_weak.is_valid());
        let _ = node_weak.create_child("orphan");
        let _ = child_2.create_child("orphan");
    }

    #[test]
    fn double_property() {
        // Create and use a default value.
        let default = DoubleProperty::default();
        default.add(1.0);

        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let root = Node::new_root(state);
        let node = root.create_child("node");
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
        // Create and use a default value.
        let default = IntProperty::default();
        default.add(1);

        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let root = Node::new_root(state);
        let node = root.create_child("node");
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
        // Create and use a default value.
        let default = UintProperty::default();
        default.add(1);

        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let root = Node::new_root(state);
        let node = root.create_child("node");
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
    fn bool_property() {
        // Create and use a default value.
        let default = BoolProperty::default();
        default.set(true);

        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let root = Node::new_root(state);
        let node = root.create_child("node");
        let node_block = node.get_block().unwrap();
        {
            let property = node.create_bool("property", true);
            let property_block = property.get_block().unwrap();
            assert_eq!(property_block.block_type(), BlockType::BoolValue);
            assert_eq!(property_block.bool_value().unwrap(), true);
            assert_eq!(node_block.child_count().unwrap(), 1);

            property.set(false);
            assert_eq!(property_block.bool_value().unwrap(), false);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn string_property() {
        // Create and use a default value.
        let default = StringProperty::default();
        default.set("test");

        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let root = Node::new_root(state);
        let node = root.create_child("node");
        let node_block = node.get_block().unwrap();
        {
            let property = node.create_string("property", "test");
            let property_block = property.get_block().unwrap();
            assert_eq!(property_block.block_type(), BlockType::BufferValue);
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
        // Create and use a default value.
        let default = BytesProperty::default();
        default.set(&[0u8, 3u8]);

        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
        let root = Node::new_root(state);
        let node = root.create_child("node");
        let node_block = node.get_block().unwrap();
        {
            let property = node.create_bytes("property", b"test");
            let property_block = property.get_block().unwrap();
            assert_eq!(property_block.block_type(), BlockType::BufferValue);
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
        // Create and use a default value.
        let default = DoubleArrayProperty::default();
        default.add(1, 1.0);
        let default = IntArrayProperty::default();
        default.add(1, 1);
        let default = UintArrayProperty::default();
        default.add(1, 1);

        let inspector = Inspector::new();
        let root = inspector.root();
        let node = root.create_child("node");
        let node_block = node.get_block().unwrap();
        {
            let array = node.create_double_array("array_property", 5);
            let array_block = array.get_block().unwrap();

            array.set(0, 5.0);
            assert_eq!(array_block.array_get_double_slot(0).unwrap(), 5.0);

            array.add(0, 5.3);
            assert_eq!(array_block.array_get_double_slot(0).unwrap(), 10.3);

            array.subtract(0, 3.4);
            assert_eq!(array_block.array_get_double_slot(0).unwrap(), 6.9);

            array.set(1, 2.5);
            array.set(3, -3.1);

            for (i, value) in [6.9, 2.5, 0.0, -3.1, 0.0].iter().enumerate() {
                assert_eq!(array_block.array_get_double_slot(i).unwrap(), *value);
            }

            array.clear();
            for i in 0..5 {
                assert_eq!(0.0, array_block.array_get_double_slot(i).unwrap());
            }

            assert_eq!(node_block.child_count().unwrap(), 1);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn linear_histograms() {
        let inspector = Inspector::new();
        let root = inspector.root();
        let node = root.create_child("node");
        let node_block = node.get_block().unwrap();
        {
            let int_histogram = node.create_int_linear_histogram(
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

            let uint_histogram = node.create_uint_linear_histogram(
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

            uint_histogram.clear();
            for (i, value) in [10, 5, 0, 0, 0, 0, 0, 0, 0].iter().enumerate() {
                assert_eq!(*value, block.array_get_uint_slot(i).unwrap());
            }

            let double_histogram = node.create_double_linear_histogram(
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

            assert_eq!(node_block.child_count().unwrap(), 3);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn exponential_histograms() {
        let inspector = Inspector::new();
        let root = inspector.root();
        let node = root.create_child("node");
        let node_block = node.get_block().unwrap();
        {
            let int_histogram = node.create_int_exponential_histogram(
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

            let uint_histogram = node.create_uint_exponential_histogram(
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

            uint_histogram.clear();
            for (i, value) in [1, 1, 2, 0, 0, 0, 0, 0, 0].iter().enumerate() {
                assert_eq!(*value, block.array_get_uint_slot(i).unwrap());
            }

            let double_histogram = node.create_double_exponential_histogram(
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

            assert_eq!(node_block.child_count().unwrap(), 3);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn owned_method_argument_properties() {
        let mapping = Arc::new(Mapping::allocate(4096).unwrap().0);
        let state = get_state(mapping.clone());
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

    #[test]
    fn exp_histogram_insert() {
        let inspector = Inspector::new();
        let root = inspector.root();
        let hist = root.create_int_exponential_histogram(
            "test",
            ExponentialHistogramParams {
                floor: 0,
                initial_step: 2,
                step_multiplier: 4,
                buckets: 4,
            },
        );
        for i in -200..200 {
            hist.insert(i);
        }
        let block = hist.get_block().unwrap();
        assert_eq!(block.array_get_int_slot(0).unwrap(), 0);
        assert_eq!(block.array_get_int_slot(1).unwrap(), 2);
        assert_eq!(block.array_get_int_slot(2).unwrap(), 4);

        // Buckets
        let i = 3;
        assert_eq!(block.array_get_int_slot(i).unwrap(), 200);
        assert_eq!(block.array_get_int_slot(i + 1).unwrap(), 2);
        assert_eq!(block.array_get_int_slot(i + 2).unwrap(), 6);
        assert_eq!(block.array_get_int_slot(i + 3).unwrap(), 24);
        assert_eq!(block.array_get_int_slot(i + 4).unwrap(), 96);
        assert_eq!(block.array_get_int_slot(i + 5).unwrap(), 72);
    }

    #[test]
    fn lazy_values() {
        let inspector = Inspector::new();
        let node = inspector.root().create_child("node");
        let node_block = node.get_block().unwrap();
        {
            let lazy_node =
                node.create_lazy_values("lazy", || async move { Ok(Inspector::new()) }.boxed());
            let lazy_node_block = lazy_node.get_block().unwrap();
            assert_eq!(lazy_node_block.block_type(), BlockType::LinkValue);
            assert_eq!(
                lazy_node_block.link_node_disposition().unwrap(),
                LinkNodeDisposition::Inline
            );
            assert_eq!(lazy_node_block.link_content_index().unwrap(), 5);
            assert_eq!(node_block.child_count().unwrap(), 1);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn lazy_node() {
        let inspector = Inspector::new();
        let node = inspector.root().create_child("node");
        let node_block = node.get_block().unwrap();
        {
            let lazy_node =
                node.create_lazy_child("lazy", || async move { Ok(Inspector::new()) }.boxed());
            let lazy_node_block = lazy_node.get_block().unwrap();
            assert_eq!(lazy_node_block.block_type(), BlockType::LinkValue);
            assert_eq!(
                lazy_node_block.link_node_disposition().unwrap(),
                LinkNodeDisposition::Child
            );
            assert_eq!(lazy_node_block.link_content_index().unwrap(), 5);
            assert_eq!(node_block.child_count().unwrap(), 1);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn inspector_lazy_from_vmo() {
        let inspector = Inspector::new();
        inspector.root().record_uint("test", 3);

        let embedded_inspector = Inspector::new();
        embedded_inspector.root().record_uint("test2", 4);
        let vmo = embedded_inspector.duplicate_vmo().unwrap();

        inspector.root().record_lazy_child_from_vmo("lazy", Arc::new(vmo));
        assert_inspect_tree!(inspector, root: {
            test: 3u64,
            lazy: {
                test2: 4u64,
            }
        });
    }

    #[test]
    fn value_list_record() {
        let inspector = Inspector::new();
        let child = inspector.root().create_child("test");
        let value_list = ValueList::new();
        assert!(value_list.values.lock().is_none());
        value_list.record(child);
        assert_eq!(value_list.values.lock().as_ref().unwrap().len(), 1);
    }

    #[test]
    fn record() {
        let inspector = Inspector::new();
        let property = inspector.root().create_uint("a", 1);
        inspector.root().record_uint("b", 2);
        {
            let child = inspector.root().create_child("child");
            child.record(property);
            child.record_double("c", 3.14);
            assert_inspect_tree!(inspector, root: {
                a: 1u64,
                b: 2u64,
                child: {
                    c: 3.14,
                }
            });
        }
        // `child` went out of scope, meaning it was deleted.
        // Property `a` should be gone as well, given that it was being tracked by `child`.
        assert_inspect_tree!(inspector, root: {
            b: 2u64,
        });
    }

    #[test]
    fn record_child() {
        let inspector = Inspector::new();
        inspector.root().record_child("test", |node| {
            node.record_int("a", 1);
        });
        assert_inspect_tree!(inspector, root: {
            test: {
                a: 1i64,
            }
        })
    }

    #[test]
    fn record_weak() {
        let inspector = Inspector::new();
        let main = inspector.root().create_child("main");
        let main_weak = main.clone_weak();
        let property = main_weak.create_uint("a", 1);

        // Ensure either the weak or strong reference can be used for recording
        main_weak.record_uint("b", 2);
        main.record_uint("c", 3);
        {
            let child = main_weak.create_child("child");
            child.record(property);
            child.record_double("c", 3.14);
            assert_inspect_tree!(inspector, root: { main: {
                a: 1u64,
                b: 2u64,
                c: 3u64,
                child: {
                    c: 3.14,
                }
            }});
        }
        // `child` went out of scope, meaning it was deleted.
        // Property `a` should be gone as well, given that it was being tracked by `child`.
        assert_inspect_tree!(inspector, root: { main: {
            b: 2u64,
            c: 3u64
        }});
        std::mem::drop(main);
        // Recording after dropping a strong reference is a no-op
        main_weak.record_double("d", 1.0);
        // Verify that dropping a strong reference cleans up the state
        assert_inspect_tree!(inspector, root: { });
    }

    #[fasync::run_singlethreaded(test)]
    async fn connect_to_service() -> Result<(), Error> {
        let mut service_fs = ServiceFs::new();
        let env = service_fs.create_nested_environment("test")?;
        let mut app = client::launch(&env.launcher(), TEST_COMPONENT_URL.to_string(), None)?;

        fasync::Task::spawn(service_fs.collect()).detach();

        let mut component_stream = app.controller().take_event_stream();
        match component_stream
            .next()
            .await
            .expect("component event stream ended before termination event")?
        {
            ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
                bail!(
                    "Component terminated unexpectedly. Code: {}. Reason: {:?}",
                    return_code,
                    termination_reason
                );
            }
            ComponentControllerEvent::OnDirectoryReady {} => {
                let pattern = format!(
                    "/hub/r/test/*/c/{}/*/out/diagnostics/{}",
                    TEST_COMPONENT_CMX,
                    TreeMarker::SERVICE_NAME
                );
                let path = glob(&pattern)?.next().unwrap().expect("failed to parse glob");
                let (tree, server_end) =
                    fidl::endpoints::create_proxy::<TreeMarker>().expect("failed to create proxy");
                fdio::service_connect(
                    &path.to_string_lossy().to_string(),
                    server_end.into_channel(),
                )
                .expect("failed to connect to service");

                let hierarchy = reader::read_from_tree(&tree).await?;
                assert_inspect_tree!(hierarchy, root: {
                    int: 3i64,
                    "lazy-node": {
                        a: "test",
                        child: {
                            double: 3.14,
                        },
                    }
                });
                app.kill().map_err(|e| format_err!("failed to kill component: {}", e))
            }
        }
    }

    #[test]
    fn unique_name() {
        let inspector = Inspector::new();

        let name_1 = super::unique_name("a");
        assert_eq!(name_1, "a0");
        inspector.root().record_uint(name_1, 1);

        let name_2 = super::unique_name("a");
        assert_eq!(name_2, "a1");
        inspector.root().record_uint(name_2, 1);

        assert_inspect_tree!(inspector, root: {
            a0: 1u64,
            a1: 1u64,
        });
    }
}
