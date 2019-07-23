//! A library for creating and modifying Tree structures.
//!
//! ## Overview
//! In this implementation, the `Tree` owns all of the `Node`s and all inter-`Node` relationships
//! are managed with `NodeId`s.
//!
//! `Tree`s in this library are "just" trees.  They do not allow cycles.  They do not allow
//! the creation of arbitrary Graph structures.  There is no weight associated with edges between
//! `Node`s.  In addition, each `Node` can have an arbitrary number of child `Node`s.
//!
//! It is important to note that this library does not support comparison-based `Node` insertion.
//! In other words, this is not a Binary Search Tree (or any other kind of search tree) library.
//! It is purely a library for storing data in a hierarchical manner.  The caller must know the
//! structure that they wish to build and then use this library to do so;  this library will not
//! make those structural decisions for you.
//!
//! ## Example Usage
//! ```
//! use id_tree::*;
//!
//! fn main() {
//!     use id_tree::InsertBehavior::*;
//!
//!     //      0
//!     //     / \
//!     //    1   2
//!     //   / \
//!     //  3   4
//!     let mut tree: Tree<i32> = TreeBuilder::new()
//!         .with_node_capacity(5)
//!         .build();
//!
//!     let root_id: NodeId = tree.insert(Node::new(0), AsRoot).unwrap();
//!     let child_id: NodeId = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
//!     tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
//!     tree.insert(Node::new(3), UnderNode(&child_id)).unwrap();
//!     tree.insert(Node::new(4), UnderNode(&child_id)).unwrap();
//!
//!     println!("Pre-order:");
//!     for node in tree.traverse_pre_order(&root_id).unwrap() {
//!         print!("{}, ", node.data());
//!     }
//!     // results in the output "0, 1, 3, 4, 2, "
//! }
//! ```
//!
//! ## Project Goals
//! * Allow caller control of as many allocations as possible (through pre-allocation)
//! * Fast `Node` insertion and removal
//! * Arbitrary _Tree_ structure creation and manipulation
//!
//! ## Non-Goals
//! * Arbitrary _Graph_ structure creation and manipulation
//! * Comparison-based node insertion of any kind
//!
//! #### Drawbacks of this Library
//! Sadly, Rust's ownership/reference system is sidestepped a bit by this implementation and this
//! can cause issues if the caller doesn't pay attention to what they are doing with `NodeId`s.
//!
//! We try to solve these issues with very careful usage of `NodeId`s so that the caller doesn't
//! have to be bothered (too much) with these concerns.
//!
//! Please see the [Potential `NodeId` Issues](struct.NodeId.html#potential-nodeid-issues) section
//! of the `NodeId` documentation for more info on what these issues are and how this library
//! attempts to solve them.
//!

#[cfg(feature = "serde_support")]
extern crate serde;

#[cfg(feature = "serde_support")]
#[macro_use]
extern crate serde_derive;

extern crate snowflake;
use self::snowflake::ProcessUniqueId;

mod behaviors;
mod error;
mod iterators;
mod node;
mod tree;

pub use behaviors::InsertBehavior;
pub use behaviors::MoveBehavior;
pub use behaviors::RemoveBehavior;
pub use behaviors::SwapBehavior;
pub use error::NodeIdError;
pub use iterators::AncestorIds;
pub use iterators::Ancestors;
pub use iterators::Children;
pub use iterators::ChildrenIds;
pub use iterators::LevelOrderTraversal;
pub use iterators::LevelOrderTraversalIds;
pub use iterators::PostOrderTraversal;
pub use iterators::PostOrderTraversalIds;
pub use iterators::PreOrderTraversal;
pub use iterators::PreOrderTraversalIds;
pub use node::Node;
pub use node::NodeBuilder;
pub use tree::Tree;
pub use tree::TreeBuilder;

///
/// An identifier used to differentiate between `Node`s within a `Tree`.
///
/// `NodeId`s are not something that the calling context will ever have to worry about generating.
/// `Tree`s generate `NodeId`s as `Node`s are inserted into them.
///
/// In addition, each `NodeId` is specific to the `Tree` that generated it.  This means that if
/// there are two `Tree`s - `A` and `B` - there's no worry of trying to access a `Node` in `A` with
/// an identifier that came from `B`.  Doing so will return a `Result::Err` value instead of
/// returning the wrong `Node`.
///
/// #### Potential `NodeId` Issues
///
/// Because `Tree`s pass out `NodeId`s as `Node`s are inserted, several issues can occur:
///
/// 1. If a `Node` is removed, the `NodeId` that previously identified it now points to nothing
/// (technically a `None` value in this case).
/// 2. If a `Node` is removed and then another is inserted later, the "new" `NodeId` that is
/// returned can (and will) be the same `NodeId` that was used to identify a different `Node`
/// previously.
///
/// The above issues may seem like deal-breakers, but our situation isn't as bad as it seems:
///
/// The first issue can be easily detected by the library itself.  In this situation, a
/// `Result::Err` will be returned with the appropriate `NodeIdError`. The second issue, however, is
/// not something that the library can detect. To mitigate this problem, this library ensures the
/// following:
///
/// 1. All `Node` methods that provide `NodeId`s will **return** `&NodeId`s instead of `NodeId`s.
/// 2. All `Tree` methods that **read** or **insert** data accept `&NodeId`s instead of taking
/// `NodeId`s.
/// 3. All `Tree` methods that **remove** data take `NodeId`s instead of accepting `&NodeId`s.
/// 4. All `Node`s that have been removed from a `Tree` will have their parent and child references
/// cleared (to avoid leaking extra `NodeId` copies).
/// 5. `NodeId`s themselves are `Clone`, but not `Copy`.
///
/// This means that no methods will ever take ownership of a `NodeId` except for methods that remove
/// a `Node` from a `Tree`. The resulting behavior is that unless the caller **explicitly `Clone`s a
/// `NodeId`** they should never be in a situation where they accidentally hold onto a `NodeId` too
/// long.  This means that we have "almost safe references" that the caller can clone if they choose
/// to.  Doing so, however, will open up the possibility of confusing which `NodeId`s refer to which
/// `Node`s in the calling context.
///
/// This _does_ transfer some of the burden to the caller, but any errors should be fairly easy to
/// sort out because an explicit `Clone` is required for such an error to occur.
///
#[derive(Clone, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]
#[cfg_attr(feature = "serde_support", derive(Serialize))]
pub struct NodeId {
    tree_id: ProcessUniqueId,
    index: usize,
}
