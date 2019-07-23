use std::cmp::Ordering;

use super::snowflake::ProcessUniqueId;
use super::*;

///
/// A `Tree` builder that provides more control over how a `Tree` is created.
///
pub struct TreeBuilder<T> {
    root: Option<Node<T>>,
    node_capacity: usize,
    swap_capacity: usize,
}

impl<T> TreeBuilder<T> {
    ///
    /// Creates a new `TreeBuilder` with the default settings.
    ///
    /// ```
    /// use id_tree::TreeBuilder;
    ///
    /// let _tree_builder: TreeBuilder<i32> = TreeBuilder::new();
    /// ```
    ///
    pub fn new() -> TreeBuilder<T> {
        TreeBuilder {
            root: None,
            node_capacity: 0,
            swap_capacity: 0,
        }
    }

    ///
    /// Sets the root `Node` of the `TreeBuilder`.
    ///
    /// ```
    /// use id_tree::TreeBuilder;
    /// use id_tree::Node;
    ///
    /// let _tree_builder = TreeBuilder::new().with_root(Node::new(1));
    /// ```
    ///
    pub fn with_root(mut self, root: Node<T>) -> TreeBuilder<T> {
        self.root = Some(root);
        self
    }

    ///
    /// Sets the node_capacity of the `TreeBuilder`.
    ///
    /// Since `Tree`s own their `Node`s, they must allocate storage space as `Node`s are inserted.
    /// Using this setting allows the `Tree` to pre-allocate space for `Node`s ahead of time, so
    /// that the space allocations don't happen as the `Node`s are inserted.
    ///
    /// _Use of this setting is recommended if you know the **maximum number** of `Node`s that your
    /// `Tree` will **contain** at **any given time**._
    ///
    /// ```
    /// use id_tree::TreeBuilder;
    ///
    /// let _tree_builder: TreeBuilder<i32> = TreeBuilder::new().with_node_capacity(3);
    /// ```
    ///
    pub fn with_node_capacity(mut self, node_capacity: usize) -> TreeBuilder<T> {
        self.node_capacity = node_capacity;
        self
    }

    ///
    /// Sets the swap_capacity of the `TreeBuilder`.
    ///
    /// This is important because `Tree`s attempt to save time by re-using storage space when
    /// `Node`s are removed (instead of shuffling `Node`s around internally).  To do this, the
    /// `Tree` must store information about the space left behind when a `Node` is removed. Using
    /// this setting allows the `Tree` to pre-allocate this storage space instead of doing so as
    /// `Node`s are removed from the `Tree`.
    ///
    /// _Use of this setting is recommended if you know the **maximum "net number of
    /// removals"** that have occurred **at any given time**._
    ///
    /// For example:
    /// ---
    /// In **Scenario 1**:
    ///
    /// * Add 3 `Node`s, Remove 2 `Node`s, Add 1 `Node`.
    ///
    /// The most amount of nodes that have been removed at any given time is **2**.
    ///
    /// But in **Scenario 2**:
    ///
    /// * Add 3 `Node`s, Remove 2 `Node`s, Add 1 `Node`, Remove 2 `Node`s.
    ///
    /// The most amount of nodes that have been removed at any given time is **3**.
    ///
    /// ```
    /// use id_tree::TreeBuilder;
    ///
    /// let _tree_builder: TreeBuilder<i32> = TreeBuilder::new().with_swap_capacity(3);
    /// ```
    ///
    pub fn with_swap_capacity(mut self, swap_capacity: usize) -> TreeBuilder<T> {
        self.swap_capacity = swap_capacity;
        self
    }

    ///
    /// Build a `Tree` based upon the current settings in the `TreeBuilder`.
    ///
    /// ```
    /// use id_tree::TreeBuilder;
    /// use id_tree::Tree;
    /// use id_tree::Node;
    ///
    /// let _tree: Tree<i32> = TreeBuilder::new()
    ///         .with_root(Node::new(5))
    ///         .with_node_capacity(3)
    ///         .with_swap_capacity(2)
    ///         .build();
    /// ```
    ///
    pub fn build(mut self) -> Tree<T> {
        let tree_id = ProcessUniqueId::new();

        let mut tree = Tree {
            id: tree_id,
            root: None,
            nodes: Vec::with_capacity(self.node_capacity),
            free_ids: Vec::with_capacity(self.swap_capacity),
        };

        if self.root.is_some() {
            let node_id = NodeId {
                tree_id: tree_id,
                index: 0,
            };

            tree.nodes.push(self.root.take());
            tree.root = Some(node_id);
        }

        tree
    }
}

///
/// A tree structure consisting of `Node`s.
///
/// # Panics
/// While it is highly unlikely, any function that takes a `NodeId` _can_ `panic`.  This, however,
/// should only happen due to improper `NodeId` management within `id_tree` and should have nothing
/// to do with the library user's code.
///
/// **If this ever happens please report the issue.** `Panic`s are not expected behavior for this
/// library, but they can happen due to bugs.
///
#[derive(Debug)]
pub struct Tree<T> {
    id: ProcessUniqueId,
    root: Option<NodeId>,
    pub(crate) nodes: Vec<Option<Node<T>>>,
    free_ids: Vec<NodeId>,
}

impl<T> Tree<T> {
    ///
    /// Creates a new `Tree` with default settings (no root `Node` and no space pre-allocation).
    ///
    /// ```
    /// use id_tree::Tree;
    ///
    /// let _tree: Tree<i32> = Tree::new();
    /// ```
    ///
    pub fn new() -> Tree<T> {
        TreeBuilder::new().build()
    }

    ///
    /// Returns the number of elements the tree can hold without reallocating.
    ///
    pub fn capacity(&self) -> usize {
        self.nodes.capacity()
    }

    ///
    /// Returns the maximum height of the `Tree`.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// assert_eq!(0, tree.height());
    ///
    /// let root_id = tree.insert(Node::new(1), AsRoot).unwrap();
    /// assert_eq!(1, tree.height());
    ///
    /// tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
    /// assert_eq!(2, tree.height());
    /// ```
    ///
    pub fn height(&self) -> usize {
        match self.root {
            Some(ref id) => self.height_of_node(id),
            _ => 0,
        }
    }

    fn height_of_node(&self, node: &NodeId) -> usize {
        let mut h = 0;
        for n in self.children_ids(node).unwrap() {
            h = std::cmp::max(h, self.height_of_node(n));
        }

        h + 1
    }

    /// Inserts a new `Node` into the `Tree`.  The `InsertBehavior` provided will determine where
    /// the `Node` is inserted.
    ///
    /// Returns a `Result` containing the `NodeId` of the `Node` that was inserted or a
    /// `NodeIdError` if one occurred.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let root_node = Node::new(1);
    /// let child_node = Node::new(2);
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// let root_id = tree.insert(root_node, AsRoot).unwrap();
    ///
    /// tree.insert(child_node, UnderNode(&root_id)).unwrap();
    /// ```
    ///
    pub fn insert(
        &mut self,
        node: Node<T>,
        behavior: InsertBehavior,
    ) -> Result<NodeId, NodeIdError> {
        match behavior {
            InsertBehavior::UnderNode(parent_id) => {
                let (is_valid, error) = self.is_valid_node_id(parent_id);
                if !is_valid {
                    return Err(error.expect(
                        "Tree::insert: Missing an error value but found an \
                         invalid NodeId.",
                    ));
                }
                self.insert_with_parent(node, parent_id)
            }
            InsertBehavior::AsRoot => Ok(self.set_root(node)),
        }
    }

    ///
    /// Sets the root of the `Tree`.
    ///
    fn set_root(&mut self, new_root: Node<T>) -> NodeId {
        let new_root_id = self.insert_new_node(new_root);

        if let Some(current_root_node_id) = self.root.clone() {
            self.set_as_parent_and_child(&new_root_id, &current_root_node_id);
        }

        self.root = Some(new_root_id.clone());
        new_root_id
    }

    /// Add a new `Node` to the tree as the child of a `Node` specified by the given `NodeId`.
    ///
    fn insert_with_parent(
        &mut self,
        child: Node<T>,
        parent_id: &NodeId,
    ) -> Result<NodeId, NodeIdError> {
        let new_child_id = self.insert_new_node(child);
        self.set_as_parent_and_child(parent_id, &new_child_id);
        Ok(new_child_id)
    }

    ///
    /// Get an immutable reference to a `Node`.
    ///
    /// Returns a `Result` containing the immutable reference or a `NodeIdError` if one occurred.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// let root_id = tree.insert(Node::new(5), AsRoot).unwrap();
    ///
    /// let root_node: &Node<i32> = tree.get(&root_id).unwrap();
    ///
    /// # assert_eq!(root_node.data(), &5);
    /// ```
    ///
    pub fn get(&self, node_id: &NodeId) -> Result<&Node<T>, NodeIdError> {
        let (is_valid, error) = self.is_valid_node_id(node_id);
        if !is_valid {
            Err(error.expect("Tree::get: Missing an error value on finding an invalid NodeId."))
        } else {
            Ok(self.get_unsafe(node_id))
        }
    }

    ///
    /// Get a mutable reference to a `Node`.
    ///
    /// Returns a `Result` containing the mutable reference or a `NodeIdError` if one occurred.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// let root_id = tree.insert(Node::new(5), AsRoot).unwrap();
    ///
    /// let root_node: &mut Node<i32> = tree.get_mut(&root_id).unwrap();
    ///
    /// # assert_eq!(root_node.data(), &5);
    /// ```
    ///
    pub fn get_mut(&mut self, node_id: &NodeId) -> Result<&mut Node<T>, NodeIdError> {
        let (is_valid, error) = self.is_valid_node_id(node_id);
        if !is_valid {
            Err(error.expect("Tree::get_mut: Missing an error value on finding an invalid NodeId."))
        } else {
            Ok(self.get_mut_unsafe(node_id))
        }
    }

    /// Remove a `Node` from the `Tree`.  The `RemoveBehavior` provided determines what happens to
    /// the removed `Node`'s children.
    ///
    /// Returns a `Result` containing the removed `Node` or a `NodeIdError` if one occurred.
    ///
    /// **NOTE:** The `Node` that is returned will have its parent and child values cleared to avoid
    /// providing the caller with extra copies of `NodeId`s should the corresponding `Node`s be
    /// removed from the `Tree` at a later time.
    ///
    /// If the caller needs a copy of the parent or child `NodeId`s, they must `Clone` them before
    /// this `Node` is removed from the `Tree`.  Please see the
    /// [Potential `NodeId` Issues](struct.NodeId.html#potential-nodeid-issues) section
    /// of the `NodeId` documentation for more information on the implications of calling `Clone` on
    /// a `NodeId`.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    /// use id_tree::RemoveBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
    ///
    /// let child_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    /// let grandchild_id = tree.insert(Node::new(2), UnderNode(&child_id)).unwrap();
    ///
    /// let child = tree.remove_node(child_id, DropChildren).unwrap();
    ///
    /// # assert!(tree.get(&grandchild_id).is_err());
    /// # assert_eq!(tree.get(&root_id).unwrap().children().len(), 0);
    /// # assert_eq!(child.children().len(), 0);
    /// # assert_eq!(child.parent(), None);
    /// ```
    ///
    pub fn remove_node(
        &mut self,
        node_id: NodeId,
        behavior: RemoveBehavior,
    ) -> Result<Node<T>, NodeIdError> {
        let (is_valid, error) = self.is_valid_node_id(&node_id);
        if !is_valid {
            return Err(error.expect(
                "Tree::remove_node: Missing an error value but found an \
                 invalid NodeId.",
            ));
        }

        match behavior {
            RemoveBehavior::DropChildren => self.remove_node_drop_children(node_id),
            RemoveBehavior::LiftChildren => self.remove_node_lift_children(node_id),
            RemoveBehavior::OrphanChildren => self.remove_node_orphan_children(node_id),
        }
    }

    ///
    /// Remove a `Node` from the `Tree` and move its children up one "level" in the `Tree` if
    /// possible.
    ///
    /// In other words, this `Node`'s children will point to its parent as their parent instead of
    /// this `Node`.  In addition, this `Node`'s parent will have this `Node`'s children added as
    /// its own children.  If this `Node` has no parent, then calling this function is the
    /// equivalent of calling `remove_node_orphan_children`.
    ///
    fn remove_node_lift_children(&mut self, node_id: NodeId) -> Result<Node<T>, NodeIdError> {
        if let Some(parent_id) = self.get_unsafe(&node_id).parent().cloned() {
            // attach children to parent
            for child_id in self.get_unsafe(&node_id).children().clone() {
                self.set_as_parent_and_child(&parent_id, &child_id);
            }
        } else {
            self.clear_parent_of_children(&node_id);
        }

        Ok(self.remove_node_internal(node_id))
    }

    ///
    /// Remove a `Node` from the `Tree` and leave all of its children in the `Tree`.
    ///
    fn remove_node_orphan_children(&mut self, node_id: NodeId) -> Result<Node<T>, NodeIdError> {
        self.clear_parent_of_children(&node_id);
        Ok(self.remove_node_internal(node_id))
    }

    ///
    /// Remove a `Node` from the `Tree` including all its children recursively.
    ///
    fn remove_node_drop_children(&mut self, node_id: NodeId) -> Result<Node<T>, NodeIdError> {
        let mut children = self.get_mut_unsafe(&node_id).take_children();
        for child in children.drain(..) {
            try!(self.remove_node_drop_children(child));
        }
        Ok(self.remove_node_internal(node_id))
    }

    /// Moves a `Node` in the `Tree` to a new location based upon the `MoveBehavior` provided.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    /// use id_tree::MoveBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    ///
    /// let root_id = tree.insert(Node::new(1), AsRoot).unwrap();
    /// let child_id = tree.insert(Node::new(2),  UnderNode(&root_id)).unwrap();
    /// let grandchild_id = tree.insert(Node::new(3), UnderNode(&child_id)).unwrap();
    ///
    /// tree.move_node(&grandchild_id, ToRoot).unwrap();
    ///
    /// assert_eq!(tree.root_node_id(), Some(&grandchild_id));
    /// # assert!(tree.get(&grandchild_id).unwrap().children().contains(&root_id));
    /// # assert!(!tree.get(&child_id).unwrap().children().contains(&grandchild_id));
    /// ```
    ///
    pub fn move_node(
        &mut self,
        node_id: &NodeId,
        behavior: MoveBehavior,
    ) -> Result<(), NodeIdError> {
        let (is_valid, error) = self.is_valid_node_id(node_id);
        if !is_valid {
            return Err(error.expect(
                "Tree::move_node: Missing an error value on finding an \
                 invalid NodeId.",
            ));
        }

        match behavior {
            MoveBehavior::ToRoot => self.move_node_to_root(node_id),
            MoveBehavior::ToParent(parent_id) => {
                let (is_valid, error) = self.is_valid_node_id(parent_id);
                if !is_valid {
                    return Err(error.expect(
                        "Tree::move_node: Missing an error value on finding \
                         an invalid NodeId.",
                    ));
                }
                self.move_node_to_parent(node_id, parent_id)
            }
        }
    }

    /// Moves a `Node` inside a `Tree` to a new parent leaving all children in their place.
    ///
    fn move_node_to_parent(
        &mut self,
        node_id: &NodeId,
        parent_id: &NodeId,
    ) -> Result<(), NodeIdError> {
        if let Some(subtree_root_id) = self
            .find_subtree_root_between_ids(parent_id, node_id)
            .cloned()
        {
            // node_id is above parent_id, this is a move "down" the tree.

            let root = self.root.clone();

            if root.as_ref() == Some(node_id) {
                // we're moving the root down the tree.
                // also we know the root exists

                // detach subtree_root from node
                self.detach_from_parent(node_id, &subtree_root_id);

                // set subtree_root as Tree root.
                self.clear_parent(&subtree_root_id);
                self.root = Some(subtree_root_id);

                self.set_as_parent_and_child(parent_id, node_id);
            } else {
                // we're moving some other node down the tree.

                if let Some(old_parent) = self.get_unsafe(node_id).parent().cloned() {
                    // detach from old parent
                    self.detach_from_parent(&old_parent, node_id);
                    // connect old parent and subtree root
                    self.set_as_parent_and_child(&old_parent, &subtree_root_id);
                } else {
                    // node is orphaned, need to set subtree_root's parent to None (same as node's)
                    self.clear_parent(&subtree_root_id);
                }
                // detach subtree_root from node
                self.detach_from_parent(node_id, &subtree_root_id);

                self.set_as_parent_and_child(parent_id, node_id);
            }
        } else {
            // this is a move "across" or "up" the tree.

            // detach from old parent
            if let Some(old_parent) = self.get_unsafe(node_id).parent().cloned() {
                self.detach_from_parent(&old_parent, node_id);
            }

            self.set_as_parent_and_child(parent_id, node_id);
        }

        Ok(())
    }

    ///
    /// Sets a `Node` inside a `Tree` as the new root `Node`, leaving all children in their place.
    ///
    fn move_node_to_root(&mut self, node_id: &NodeId) -> Result<(), NodeIdError> {
        let old_root = self.root.clone();

        if let Some(parent_id) = self.get_unsafe(node_id).parent().cloned() {
            self.detach_from_parent(&parent_id, node_id);
        }
        self.clear_parent(node_id);
        self.root = Some(node_id.clone());

        if let Some(old_root) = old_root {
            try!(self.move_node_to_parent(&old_root, node_id));
        }

        Ok(())
    }

    ///
    /// Sorts the children of one node, in-place, using compare to compare the nodes
    ///
    /// This sort is stable and O(n log n) worst-case but allocates approximately 2 * n where n is
    /// the length of children
    ///
    /// Returns an empty `Result` containing a `NodeIdError` if one occurred.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    ///
    /// let root_id = tree.insert(Node::new(100), AsRoot).unwrap();
    /// tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    /// tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
    /// tree.insert(Node::new(0), UnderNode(&root_id)).unwrap();
    ///
    /// tree.sort_children_by(&root_id, |a, b| a.data().cmp(b.data())).unwrap();
    ///
    /// # for (i, id) in tree.get(&root_id).unwrap().children().iter().enumerate() {
    /// #   assert_eq!(*tree.get(&id).unwrap().data(), i as i32);
    /// # }
    /// ```
    ///
    pub fn sort_children_by<F>(
        &mut self,
        node_id: &NodeId,
        mut compare: F,
    ) -> Result<(), NodeIdError>
    where
        F: FnMut(&Node<T>, &Node<T>) -> Ordering,
    {
        let (is_valid, error) = self.is_valid_node_id(node_id);
        if !is_valid {
            return Err(error.expect(
                "Tree::sort_children_by: Missing an error value but found an invalid NodeId.",
            ));
        }

        let mut children = self.get_mut_unsafe(node_id).take_children();
        children.sort_by(|a, b| compare(self.get_unsafe(a), self.get_unsafe(b)));
        self.get_mut_unsafe(node_id).set_children(children);

        Ok(())
    }

    ///
    /// Sorts the children of one node, in-place, comparing their data
    ///
    /// This sort is stable and O(n log n) worst-case but allocates approximately 2 * n where n is
    /// the length of children
    ///
    /// Returns an empty `Result` containing a `NodeIdError` if one occurred.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    ///
    /// let root_id = tree.insert(Node::new(100), AsRoot).unwrap();
    /// tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    /// tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
    /// tree.insert(Node::new(0), UnderNode(&root_id)).unwrap();
    ///
    /// tree.sort_children_by_data(&root_id).unwrap();
    ///
    /// # for (i, id) in tree.get(&root_id).unwrap().children().iter().enumerate() {
    /// #   assert_eq!(*tree.get(&id).unwrap().data(), i as i32);
    /// # }
    /// ```
    ///
    pub fn sort_children_by_data(&mut self, node_id: &NodeId) -> Result<(), NodeIdError>
    where
        T: Ord,
    {
        let (is_valid, error) = self.is_valid_node_id(node_id);
        if !is_valid {
            return Err(error.expect(
                "Tree::sort_children: Missing an error value but found an invalid NodeId.",
            ));
        }

        let mut children = self.get_mut_unsafe(node_id).take_children();
        children.sort_by_key(|a| self.get_unsafe(a).data());
        self.get_mut_unsafe(node_id).set_children(children);

        Ok(())
    }

    ///
    /// Sorts the children of one node, in-place, using f to extract a key by which to order the
    /// sort by.
    ///
    /// This sort is stable and O(n log n) worst-case but allocates approximately 2 * n where n is
    /// the length of children
    ///
    /// Returns an empty `Result` containing a `NodeIdError` if one occurred.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    ///
    /// let root_id = tree.insert(Node::new(100), AsRoot).unwrap();
    /// tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    /// tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
    /// tree.insert(Node::new(0), UnderNode(&root_id)).unwrap();
    ///
    /// tree.sort_children_by_key(&root_id, |x| x.data().clone()).unwrap();
    ///
    /// # for (i, id) in tree.get(&root_id).unwrap().children().iter().enumerate() {
    /// #   assert_eq!(*tree.get(&id).unwrap().data(), i as i32);
    /// # }
    /// ```
    ///
    pub fn sort_children_by_key<B, F>(
        &mut self,
        node_id: &NodeId,
        mut f: F,
    ) -> Result<(), NodeIdError>
    where
        B: Ord,
        F: FnMut(&Node<T>) -> B,
    {
        let (is_valid, error) = self.is_valid_node_id(node_id);
        if !is_valid {
            return Err(error.expect(
                "Tree::sort_children_by_key: Missing an error value but found an invalid NodeId.",
            ));
        }

        let mut children = self.get_mut_unsafe(node_id).take_children();
        children.sort_by_key(|a| f(self.get_unsafe(a)));
        self.get_mut_unsafe(node_id).set_children(children);

        Result::Ok(())
    }

    /// Swap `Node`s in the `Tree` based upon the `SwapBehavior` provided.
    ///
    /// Both `NodeId`s are still valid after this process and are not swapped.
    ///
    /// This keeps the positions of the `Node`s in their parents' children collection.
    ///
    /// Returns an empty `Result` containing a `NodeIdError` if one occurred on either provided
    /// `NodeId`.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    /// use id_tree::SwapBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    ///
    /// let root_id = tree.insert(Node::new(1), AsRoot).unwrap();
    ///
    /// let first_child_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
    /// let second_child_id = tree.insert(Node::new(3), UnderNode(&root_id)).unwrap();
    /// let grandchild_id = tree.insert(Node::new(4), UnderNode(&second_child_id)).unwrap();
    ///
    /// tree.swap_nodes(&first_child_id, &grandchild_id, TakeChildren).unwrap();
    ///
    /// assert!(tree.get(&second_child_id).unwrap().children().contains(&first_child_id));
    /// assert!(tree.get(&root_id).unwrap().children().contains(&grandchild_id));
    /// ```
    ///
    pub fn swap_nodes(
        &mut self,
        first_id: &NodeId,
        second_id: &NodeId,
        behavior: SwapBehavior,
    ) -> Result<(), NodeIdError> {
        let (is_valid, error) = self.is_valid_node_id(first_id);
        if !is_valid {
            return Err(error
                .expect("Tree::swap_nodes: Missing an error value but found an invalid NodeId."));
        }

        let (is_valid, error) = self.is_valid_node_id(second_id);
        if !is_valid {
            return Err(error
                .expect("Tree::swap_nodes: Missing an error value but found an invalid NodeId."));
        }

        match behavior {
            SwapBehavior::TakeChildren => self.swap_nodes_take_children(first_id, second_id),
            SwapBehavior::LeaveChildren => self.swap_nodes_leave_children(first_id, second_id),
            SwapBehavior::ChildrenOnly => self.swap_nodes_children_only(first_id, second_id),
        }
    }

    /// Swaps two `Node`s including their children given their `NodeId`s.
    ///
    fn swap_nodes_take_children(
        &mut self,
        first_id: &NodeId,
        second_id: &NodeId,
    ) -> Result<(), NodeIdError> {
        let lower_upper_test = self
            .find_subtree_root_between_ids(first_id, second_id)
            .map(|_| (first_id, second_id))
            .or_else(|| {
                self.find_subtree_root_between_ids(second_id, first_id)
                    .map(|_| (second_id, first_id))
            });

        if let Some((lower_id, upper_id)) = lower_upper_test {
            let upper_parent_id = self.get_unsafe(upper_id).parent().cloned();

            let lower_parent_id = {
                let lower = self.get_mut_unsafe(lower_id);
                // lower is lower, so it has a parent for sure
                let lower_parent_id = lower.parent().unwrap().clone();

                if upper_parent_id.is_some() {
                    lower.set_parent(upper_parent_id.clone());
                } else {
                    lower.set_parent(None);
                }

                lower_parent_id
            };

            self.detach_from_parent(&lower_parent_id, lower_id);

            if upper_parent_id.is_some() {
                self.get_mut_unsafe(upper_parent_id.as_ref().unwrap())
                    .replace_child(upper_id.clone(), lower_id.clone());
            } else if self.root.as_ref() == Some(upper_id) {
                self.root = Some(lower_id.clone());
            }

            self.get_mut_unsafe(upper_id)
                .set_parent(Some(lower_id.clone()));
            self.get_mut_unsafe(lower_id).add_child(upper_id.clone());
        } else {
            // just across

            let is_same_parent =
                self.get_unsafe(first_id).parent() == self.get_unsafe(second_id).parent();

            if is_same_parent {
                let parent_id = self.get_unsafe(first_id).parent().cloned();
                if let Some(parent_id) = parent_id {
                    // same parent
                    // get indices
                    let parent = self.get_mut_unsafe(&parent_id);
                    let first_index = parent
                        .children()
                        .iter()
                        .enumerate()
                        .find(|&(_, id)| id == first_id)
                        .unwrap()
                        .0;
                    let second_index = parent
                        .children()
                        .iter()
                        .enumerate()
                        .find(|&(_, id)| id == second_id)
                        .unwrap()
                        .0;

                    parent.children_mut().swap(first_index, second_index);
                } else {
                    // swapping the root with itself??
                }
            } else {
                let first_parent_id = self.get_unsafe(first_id).parent().cloned().unwrap();
                let second_parent_id = self.get_unsafe(second_id).parent().cloned().unwrap();

                // replace parents
                self.get_mut_unsafe(first_id)
                    .set_parent(Some(second_parent_id.clone()));
                self.get_mut_unsafe(second_id)
                    .set_parent(Some(first_parent_id.clone()));

                // change children
                self.get_mut_unsafe(&first_parent_id)
                    .replace_child(first_id.clone(), second_id.clone());
                self.get_mut_unsafe(&second_parent_id)
                    .replace_child(second_id.clone(), first_id.clone());
            }
        }

        Ok(())
    }

    fn swap_nodes_leave_children(
        &mut self,
        first_id: &NodeId,
        second_id: &NodeId,
    ) -> Result<(), NodeIdError> {
        //take care of these nodes' children's parent values
        self.set_parent_of_children(first_id, Some(second_id.clone()));
        self.set_parent_of_children(second_id, Some(first_id.clone()));

        //swap children of these nodes
        let first_children = self.get_unsafe(first_id).children().clone();
        let second_children = self.get_unsafe(second_id).children().clone();
        self.get_mut_unsafe(first_id).set_children(second_children);
        self.get_mut_unsafe(second_id).set_children(first_children);

        let first_parent = self.get_unsafe(first_id).parent().cloned();
        let second_parent = self.get_unsafe(second_id).parent().cloned();

        //todo: some of this could probably be abstracted out into a method or two
        match (first_parent, second_parent) {
            (Some(ref first_parent_id), Some(ref second_parent_id)) => {
                let first_index = self
                    .get_unsafe(first_parent_id)
                    .children()
                    .iter()
                    .position(|id| id == first_id)
                    .unwrap();
                let second_index = self
                    .get_unsafe(second_parent_id)
                    .children()
                    .iter()
                    .position(|id| id == second_id)
                    .unwrap();

                unsafe {
                    let temp = self
                        .get_mut_unsafe(first_parent_id)
                        .children_mut()
                        .get_unchecked_mut(first_index);
                    *temp = second_id.clone();
                }
                unsafe {
                    let temp = self
                        .get_mut_unsafe(second_parent_id)
                        .children_mut()
                        .get_unchecked_mut(second_index);
                    *temp = first_id.clone();
                }

                self.get_mut_unsafe(first_id)
                    .set_parent(Some(second_parent_id.clone()));
                self.get_mut_unsafe(second_id)
                    .set_parent(Some(first_parent_id.clone()));
            }
            (Some(ref first_parent_id), None) => {
                let first_index = self
                    .get_unsafe(first_parent_id)
                    .children()
                    .iter()
                    .position(|id| id == first_id)
                    .unwrap();

                unsafe {
                    let temp = self
                        .get_mut_unsafe(first_parent_id)
                        .children_mut()
                        .get_unchecked_mut(first_index);
                    *temp = second_id.clone();
                }

                self.get_mut_unsafe(first_id).set_parent(None);
                self.get_mut_unsafe(second_id)
                    .set_parent(Some(first_parent_id.clone()));

                if let Some(root_id) = self.root_node_id().cloned() {
                    if root_id == second_id.clone() {
                        self.root = Some(first_id.clone());
                    }
                }
            }
            (None, Some(ref second_parent_id)) => {
                let second_index = self
                    .get_unsafe(second_parent_id)
                    .children()
                    .iter()
                    .position(|id| id == second_id)
                    .unwrap();

                unsafe {
                    let temp = self
                        .get_mut_unsafe(second_parent_id)
                        .children_mut()
                        .get_unchecked_mut(second_index);
                    *temp = first_id.clone();
                }

                self.get_mut_unsafe(first_id)
                    .set_parent(Some(second_parent_id.clone()));
                self.get_mut_unsafe(second_id).set_parent(None);

                if let Some(root_id) = self.root_node_id().cloned() {
                    if root_id == first_id.clone() {
                        self.root = Some(second_id.clone());
                    }
                }
            }
            (None, None) => {
                if let Some(root_id) = self.root_node_id().cloned() {
                    if root_id == first_id.clone() {
                        self.root = Some(second_id.clone());
                    } else if root_id == second_id.clone() {
                        self.root = Some(first_id.clone());
                    }
                }
            }
        }

        Ok(())
    }

    fn swap_nodes_children_only(
        &mut self,
        first_id: &NodeId,
        second_id: &NodeId,
    ) -> Result<(), NodeIdError> {
        let lower_upper_test = self
            .find_subtree_root_between_ids(first_id, second_id)
            .map(|_| (first_id, second_id))
            .or_else(|| {
                self.find_subtree_root_between_ids(second_id, first_id)
                    .map(|_| (second_id, first_id))
            });

        // todo: lots of repetition in here

        let first_children = self.get_unsafe(first_id).children().clone();
        let second_children = self.get_unsafe(second_id).children().clone();

        if let Some((lower_id, upper_id)) = lower_upper_test {
            let lower_parent = self.get_unsafe(lower_id).parent().cloned().unwrap();

            let (mut upper_children, lower_children) = if upper_id == first_id {
                (first_children, second_children)
            } else {
                (second_children, first_children)
            };

            for child in &upper_children {
                self.get_mut_unsafe(child)
                    .set_parent(Some(lower_id.clone()));
            }
            for child in &lower_children {
                self.get_mut_unsafe(child)
                    .set_parent(Some(upper_id.clone()));
            }

            if upper_id == &lower_parent {
                // direct child
                upper_children.retain(|id| id != lower_id);
            }

            //swap children of these nodes
            self.get_mut_unsafe(upper_id).set_children(lower_children);
            self.get_mut_unsafe(lower_id).set_children(upper_children);

            //add lower to upper
            self.set_as_parent_and_child(upper_id, lower_id);
        } else {
            //just across

            //take care of these nodes' children's parent values
            for child in &first_children {
                self.get_mut_unsafe(child)
                    .set_parent(Some(second_id.clone()));
            }
            for child in &second_children {
                self.get_mut_unsafe(child)
                    .set_parent(Some(first_id.clone()));
            }

            //swap children of these nodes
            self.get_mut_unsafe(first_id).set_children(second_children);
            self.get_mut_unsafe(second_id).set_children(first_children);
        }

        Ok(())
    }

    ///
    /// Returns a `Some` value containing the `NodeId` of the root `Node` if it exists.  Otherwise a
    /// `None` value is returned.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// let root_id = tree.insert(Node::new(5), AsRoot).unwrap();
    ///
    /// assert_eq!(&root_id, tree.root_node_id().unwrap());
    /// ```
    ///
    pub fn root_node_id(&self) -> Option<&NodeId> {
        self.root.as_ref()
    }

    ///
    /// Returns an `Ancestors` iterator (or a `NodeIdError` if one occurred).
    ///
    /// Allows iteration over the ancestor `Node`s of a given `NodeId` directly instead of having
    /// to call `tree.get(...)` with a `NodeId` each time.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
    /// let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    ///
    /// let mut ancestors = tree.ancestors(&node_1).unwrap();
    ///
    /// assert_eq!(ancestors.next().unwrap().data(), &0);
    /// assert!(ancestors.next().is_none());
    /// ```
    ///
    pub fn ancestors(&self, node_id: &NodeId) -> Result<Ancestors<T>, NodeIdError> {
        let (is_valid, error) = self.is_valid_node_id(node_id);
        if !is_valid {
            return Err(error
                .expect("Tree::ancestors: Missing an error value but found an invalid NodeId."));
        }

        Ok(Ancestors::new(self, node_id.clone()))
    }

    ///
    /// Returns an `AncestorIds` iterator (or a `NodeIdError` if one occurred).
    ///
    /// Allows iteration over the ancestor `NodeId`s of a given `NodeId`.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
    /// let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    ///
    /// let mut ancestor_ids = tree.ancestor_ids(&node_1).unwrap();
    ///
    /// assert_eq!(ancestor_ids.next().unwrap(), &root_id);
    /// assert!(ancestor_ids.next().is_none());
    /// ```
    ///
    pub fn ancestor_ids(&self, node_id: &NodeId) -> Result<AncestorIds<T>, NodeIdError> {
        let (is_valid, error) = self.is_valid_node_id(node_id);
        if !is_valid {
            return Err(error
                .expect("Tree::ancestor_ids: Missing an error value but found an invalid NodeId."));
        }

        Ok(AncestorIds::new(self, node_id.clone()))
    }

    ///
    /// Returns a `Children` iterator (or a `NodeIdError` if one occurred).
    ///
    /// Allows iteration over the child `Node`s of a given `NodeId` directly instead of having
    /// to call `tree.get(...)` with a `NodeId` each time.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
    /// tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    ///
    /// let mut children = tree.children(&root_id).unwrap();
    ///
    /// assert_eq!(children.next().unwrap().data(), &1);
    /// assert!(children.next().is_none());
    /// ```
    ///
    pub fn children(&self, node_id: &NodeId) -> Result<Children<T>, NodeIdError> {
        let (is_valid, error) = self.is_valid_node_id(node_id);
        if !is_valid {
            return Err(
                error.expect("Tree::children: Missing an error value but found an invalid NodeId.")
            );
        }

        Ok(Children::new(self, node_id.clone()))
    }

    ///
    /// Returns a `ChildrenIds` iterator (or a `NodeIdError` if one occurred).
    ///
    /// Allows iteration over the child `NodeId`s of a given `NodeId`.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
    /// let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    ///
    /// let mut children_ids = tree.children_ids(&root_id).unwrap();
    ///
    /// assert_eq!(children_ids.next().unwrap(), &node_1);
    /// assert!(children_ids.next().is_none());
    /// ```
    ///
    pub fn children_ids(&self, node_id: &NodeId) -> Result<ChildrenIds, NodeIdError> {
        let (is_valid, error) = self.is_valid_node_id(node_id);
        if !is_valid {
            return Err(error
                .expect("Tree::children_ids: Missing an error value but found an invalid NodeId."));
        }

        Ok(ChildrenIds::new(self, node_id.clone()))
    }

    /// Returns a `PreOrderTraversal` iterator (or a `NodeIdError` if one occurred).
    ///
    /// Allows iteration over all of the `Node`s in the sub-tree below a given `Node`.  This
    /// iterator will always include that sub-tree "root" specified by the `NodeId` given.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
    /// tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    ///
    /// let mut nodes = tree.traverse_pre_order(&root_id).unwrap();
    ///
    /// assert_eq!(nodes.next().unwrap().data(), &0);
    /// assert_eq!(nodes.next().unwrap().data(), &1);
    /// assert!(nodes.next().is_none());
    /// ```
    ///
    pub fn traverse_pre_order(
        &self,
        node_id: &NodeId,
    ) -> Result<PreOrderTraversal<T>, NodeIdError> {
        let (is_valid, error) = self.is_valid_node_id(node_id);
        if !is_valid {
            return Err(error.expect(
                "Tree::traverse_pre_order: Missing an error value but found an invalid NodeId.",
            ));
        }

        Ok(PreOrderTraversal::new(self, node_id.clone()))
    }

    /// Returns a `PreOrderTraversalIds` iterator (or a `NodeIdError` if one occurred).
    ///
    /// Allows iteration over all of the `NodeId`s in the sub-tree below a given `NodeId`.  This
    /// iterator will always include that sub-tree "root" specified by the `NodeId` given.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
    /// tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    ///
    /// let mut nodes = tree.traverse_pre_order_ids(&root_id).unwrap();
    ///
    /// assert_eq!(tree.get(&nodes.next().unwrap()).unwrap().data(), &0);
    /// assert_eq!(tree.get(&nodes.next().unwrap()).unwrap().data(), &1);
    /// assert!(nodes.next().is_none());
    /// ```
    ///
    pub fn traverse_pre_order_ids(
        &self,
        node_id: &NodeId,
    ) -> Result<PreOrderTraversalIds<T>, NodeIdError> {
        let (is_valid, error) = self.is_valid_node_id(&node_id);
        if !is_valid {
            return Err(error.expect(
                "Tree::traverse_pre_order_ids: Missing an error value but found an invalid NodeId.",
            ));
        }

        Ok(PreOrderTraversalIds::new(self, node_id.clone()))
    }

    /// Returns a `PostOrderTraversal` iterator (or a `NodeIdError` if one occurred).
    ///
    /// Allows iteration over all of the `Node`s in the sub-tree below a given `Node`.  This
    /// iterator will always include that sub-tree "root" specified by the `NodeId` given.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
    /// tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    ///
    /// let mut nodes = tree.traverse_post_order(&root_id).unwrap();
    ///
    /// assert_eq!(nodes.next().unwrap().data(), &1);
    /// assert_eq!(nodes.next().unwrap().data(), &0);
    /// assert!(nodes.next().is_none());
    /// ```
    ///
    pub fn traverse_post_order(
        &self,
        node_id: &NodeId,
    ) -> Result<PostOrderTraversal<T>, NodeIdError> {
        let (is_valid, error) = self.is_valid_node_id(node_id);
        if !is_valid {
            return Err(error.expect(
                "Tree::traverse_post_order: Missing an error value but found an invalid NodeId.",
            ));
        }

        Ok(PostOrderTraversal::new(self, node_id.clone()))
    }

    /// Returns a `PostOrderTraversalIds` iterator (or a `NodeIdError` if one occurred).
    ///
    /// Allows iteration over all of the `NodeId`s in the sub-tree below a given `NodeId`.  This
    /// iterator will always include that sub-tree "root" specified by the `NodeId` given.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
    /// tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    ///
    /// let mut nodes = tree.traverse_post_order_ids(&root_id).unwrap();
    ///
    /// assert_eq!(tree.get(&nodes.next().unwrap()).unwrap().data(), &1);
    /// assert_eq!(tree.get(&nodes.next().unwrap()).unwrap().data(), &0);
    /// assert!(nodes.next().is_none());
    /// ```
    ///
    pub fn traverse_post_order_ids(
        &self,
        node_id: &NodeId,
    ) -> Result<PostOrderTraversalIds, NodeIdError> {
        let (is_valid, error) = self.is_valid_node_id(node_id);
        if !is_valid {
            return Err(error.expect(
                "Tree::traverse_post_order_ids: Missing an error value but found an invalid NodeId.",
            ));
        }

        Ok(PostOrderTraversalIds::new(self, node_id.clone()))
    }

    /// Returns a `LevelOrderTraversal` iterator (or a `NodeIdError` if one occurred).
    ///
    /// Allows iteration over all of the `Node`s in the sub-tree below a given `Node`.  This
    /// iterator will always include that sub-tree "root" specified by the `NodeId` given.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
    /// tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    ///
    /// let mut nodes = tree.traverse_level_order(&root_id).unwrap();
    ///
    /// assert_eq!(nodes.next().unwrap().data(), &0);
    /// assert_eq!(nodes.next().unwrap().data(), &1);
    /// assert!(nodes.next().is_none());
    /// ```
    ///
    pub fn traverse_level_order(
        &self,
        node_id: &NodeId,
    ) -> Result<LevelOrderTraversal<T>, NodeIdError> {
        let (is_valid, error) = self.is_valid_node_id(node_id);
        if !is_valid {
            return Err(error.expect(
                "Tree::traverse_level_order: Missing an error value but found an invalid NodeId.",
            ));
        }

        Ok(LevelOrderTraversal::new(self, node_id.clone()))
    }

    /// Returns a `LevelOrderTraversalIds` iterator (or a `NodeIdError` if one occurred).
    ///
    /// Allows iteration over all of the `NodeIds`s in the sub-tree below a given `NodeId`.  This
    /// iterator will always include that sub-tree "root" specified by the `NodeId` given.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    /// let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
    /// tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    ///
    /// let mut nodes = tree.traverse_level_order_ids(&root_id).unwrap();
    ///
    /// assert_eq!(tree.get(&nodes.next().unwrap()).unwrap().data(), &0);
    /// assert_eq!(tree.get(&nodes.next().unwrap()).unwrap().data(), &1);
    /// assert!(nodes.next().is_none());
    /// ```
    ///
    pub fn traverse_level_order_ids(
        &self,
        node_id: &NodeId,
    ) -> Result<LevelOrderTraversalIds<T>, NodeIdError> {
        let (is_valid, error) = self.is_valid_node_id(node_id);
        if !is_valid {
            return Err(error.expect(
                "Tree::traverse_level_order: Missing an error value but found an invalid NodeId.",
            ));
        }

        Ok(LevelOrderTraversalIds::new(self, node_id.clone()))
    }

    // Nothing should make it past this function.
    // If there is a way for a NodeId to be invalid, it should be caught here.
    fn is_valid_node_id(&self, node_id: &NodeId) -> (bool, Option<NodeIdError>) {
        if node_id.tree_id != self.id {
            return (false, Some(NodeIdError::InvalidNodeIdForTree));
        }

        if node_id.index >= self.nodes.len() {
            panic!(
                "NodeId: {:?} is out of bounds. This is most likely a bug in id_tree. Please \
                 report this issue!",
                node_id
            );
        }

        unsafe {
            if self.nodes.get_unchecked(node_id.index).is_none() {
                return (false, Some(NodeIdError::NodeIdNoLongerValid));
            }
        }

        (true, None)
    }

    fn find_subtree_root_between_ids<'a>(
        &'a self,
        lower_id: &'a NodeId,
        upper_id: &'a NodeId,
    ) -> Option<&'a NodeId> {
        if let Some(lower_parent) = self.get_unsafe(lower_id).parent() {
            if lower_parent == upper_id {
                return Some(lower_id);
            } else {
                return self.find_subtree_root_between_ids(lower_parent, upper_id);
            }
        }

        // lower_id has no parent, it can't be below upper_id
        None
    }

    fn set_as_parent_and_child(&mut self, parent_id: &NodeId, child_id: &NodeId) {
        self.get_mut_unsafe(parent_id).add_child(child_id.clone());

        self.get_mut_unsafe(child_id)
            .set_parent(Some(parent_id.clone()));
    }

    fn detach_from_parent(&mut self, parent_id: &NodeId, node_id: &NodeId) {
        self.get_mut_unsafe(parent_id)
            .children_mut()
            .retain(|child_id| child_id != node_id);
    }

    fn insert_new_node(&mut self, new_node: Node<T>) -> NodeId {
        if !self.free_ids.is_empty() {
            let new_node_id: NodeId = self
                .free_ids
                .pop()
                .expect("Tree::insert_new_node: Couldn't pop from Vec with len() > 0.");

            self.nodes.push(Some(new_node));
            self.nodes.swap_remove(new_node_id.index);

            new_node_id
        } else {
            let new_node_index = self.nodes.len();
            self.nodes.push(Some(new_node));

            self.new_node_id(new_node_index)
        }
    }

    fn remove_node_internal(&mut self, node_id: NodeId) -> Node<T> {
        if let Some(root_id) = self.root.clone() {
            if node_id == root_id {
                self.root = None;
            }
        }

        let mut node = self.take_node(node_id.clone());

        // The only thing we care about here is dealing with "this" Node's parent's children
        // This Node's children's parent will be handled in different ways depending upon how this
        // method is called.
        if let Some(parent_id) = node.parent() {
            self.get_mut_unsafe(parent_id)
                .children_mut()
                .retain(|child_id| child_id != &node_id);
        }

        // avoid providing the caller with extra copies of NodeIds
        node.children_mut().clear();
        node.set_parent(None);

        node
    }

    fn take_node(&mut self, node_id: NodeId) -> Node<T> {
        self.nodes.push(None);
        let node = self.nodes.swap_remove(node_id.index).expect(
            "Tree::take_node: An invalid NodeId made it past id_tree's internal checks. \
             Please report this issue!",
        );
        self.free_ids.push(node_id);

        node
    }

    fn new_node_id(&self, node_index: usize) -> NodeId {
        NodeId {
            tree_id: self.id,
            index: node_index,
        }
    }

    fn clear_parent(&mut self, node_id: &NodeId) {
        self.set_parent(node_id, None);
    }

    fn set_parent(&mut self, node_id: &NodeId, new_parent: Option<NodeId>) {
        self.get_mut_unsafe(node_id).set_parent(new_parent);
    }

    fn clear_parent_of_children(&mut self, node_id: &NodeId) {
        self.set_parent_of_children(node_id, None);
    }

    fn set_parent_of_children(&mut self, node_id: &NodeId, new_parent: Option<NodeId>) {
        for child_id in self.get_unsafe(node_id).children().clone() {
            self.set_parent(&child_id, new_parent.clone());
        }
    }

    pub(crate) fn get_unsafe(&self, node_id: &NodeId) -> &Node<T> {
        unsafe {
            self.nodes.get_unchecked(node_id.index).as_ref().expect(
                "Tree::get_unsafe: An invalid NodeId made it past id_tree's internal \
                 checks.  Please report this issue!",
            )
        }
    }

    fn get_mut_unsafe(&mut self, node_id: &NodeId) -> &mut Node<T> {
        unsafe {
            self.nodes.get_unchecked_mut(node_id.index).as_mut().expect(
                "Tree::get_mut_unsafe: An invalid NodeId made it past id_tree's internal \
                 checks.  Please report this issue!",
            )
        }
    }
}
impl<T> Default for Tree<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> PartialEq for Tree<T>
where
    T: PartialEq,
{
    fn eq(&self, other: &Tree<T>) -> bool {
        if self.nodes.iter().filter(|x| x.is_some()).count()
            != other.nodes.iter().filter(|x| x.is_some()).count()
        {
            return false;
        }

        for ((i, node1), (j, node2)) in self
            .nodes
            .iter()
            .enumerate()
            .filter_map(|(i, x)| (*x).as_ref().map(|x| (i, x)))
            .zip(
                other
                    .nodes
                    .iter()
                    .enumerate()
                    .filter_map(|(i, x)| (*x).as_ref().map(|x| (i, x))),
            )
        {
            let parent1_node = node1.parent.as_ref().and_then(|x| self.get(x).ok());
            let parent2_node = node2.parent.as_ref().and_then(|x| other.get(x).ok());

            if i != j || node1 != node2 || parent1_node != parent2_node {
                return false;
            }
        }

        true
    }
}

impl<T> Clone for Tree<T>
where
    T: Clone,
{
    fn clone(&self) -> Self {
        let tree_id = ProcessUniqueId::new();

        Tree {
            id: tree_id,
            root: self.root.as_ref().map(|x| NodeId {
                tree_id,
                index: x.index,
            }),
            nodes: self
                .nodes
                .iter()
                .map(|x| {
                    x.as_ref().map(|y| Node {
                        data: y.data.clone(),
                        parent: y.parent.as_ref().map(|z| NodeId {
                            tree_id,
                            index: z.index,
                        }),
                        children: y
                            .children
                            .iter()
                            .map(|z| NodeId {
                                tree_id,
                                index: z.index,
                            })
                            .collect(),
                    })
                })
                .collect(),
            free_ids: self
                .free_ids
                .iter()
                .map(|x| NodeId {
                    tree_id,
                    index: x.index,
                })
                .collect(),
        }
    }
}

#[cfg(test)]
mod tree_builder_tests {
    use super::super::Node;
    use super::TreeBuilder;

    #[test]
    fn test_new() {
        let tb: TreeBuilder<i32> = TreeBuilder::new();
        assert!(tb.root.is_none());
        assert_eq!(tb.node_capacity, 0);
        assert_eq!(tb.swap_capacity, 0);
    }

    #[test]
    fn test_with_root() {
        let tb: TreeBuilder<i32> = TreeBuilder::new().with_root(Node::new(5));

        assert_eq!(tb.root.unwrap().data(), &5);
        assert_eq!(tb.node_capacity, 0);
        assert_eq!(tb.swap_capacity, 0);
    }

    #[test]
    fn test_with_node_capacity() {
        let tb: TreeBuilder<i32> = TreeBuilder::new().with_node_capacity(10);

        assert!(tb.root.is_none());
        assert_eq!(tb.node_capacity, 10);
        assert_eq!(tb.swap_capacity, 0);
    }

    #[test]
    fn test_with_swap_capacity() {
        let tb: TreeBuilder<i32> = TreeBuilder::new().with_swap_capacity(10);

        assert!(tb.root.is_none());
        assert_eq!(tb.node_capacity, 0);
        assert_eq!(tb.swap_capacity, 10);
    }

    #[test]
    fn test_with_all_settings() {
        let tb: TreeBuilder<i32> = TreeBuilder::new()
            .with_root(Node::new(5))
            .with_node_capacity(10)
            .with_swap_capacity(3);

        assert_eq!(tb.root.unwrap().data(), &5);
        assert_eq!(tb.node_capacity, 10);
        assert_eq!(tb.swap_capacity, 3);
    }

    #[test]
    fn test_build() {
        let tree = TreeBuilder::new()
            .with_root(Node::new(5))
            .with_node_capacity(10)
            .with_swap_capacity(3)
            .build();

        let root = tree.get(tree.root_node_id().unwrap()).unwrap();

        assert_eq!(root.data(), &5);
        assert_eq!(tree.capacity(), 10);
        assert_eq!(tree.free_ids.capacity(), 3);
    }
}

#[cfg(test)]
mod tree_tests {
    use super::super::Node;
    use super::super::NodeId;
    use super::Tree;
    use super::TreeBuilder;

    #[test]
    fn test_new() {
        let tree: Tree<i32> = Tree::new();

        assert_eq!(tree.root, None);
        assert_eq!(tree.nodes.len(), 0);
        assert_eq!(tree.free_ids.len(), 0);
    }

    #[test]
    fn test_get() {
        let tree = TreeBuilder::new().with_root(Node::new(5)).build();

        let root_id = tree.root.clone().unwrap();
        let root = tree.get(&root_id).unwrap();

        assert_eq!(root.data(), &5);
    }

    #[test]
    fn test_get_mut() {
        let mut tree = TreeBuilder::new().with_root(Node::new(5)).build();

        let root_id = tree.root.clone().unwrap();

        {
            let root = tree.get(&root_id).unwrap();
            assert_eq!(root.data(), &5);
        }

        {
            let root = tree.get_mut(&root_id).unwrap();
            *root.data_mut() = 6;
        }

        let root = tree.get(&root_id).unwrap();
        assert_eq!(root.data(), &6);
    }

    #[test]
    fn test_set_root() {
        use InsertBehavior::*;

        let a = 5;
        let b = 6;
        let node_a = Node::new(a);
        let node_b = Node::new(b);

        let mut tree = TreeBuilder::new().build();

        let node_a_id = tree.insert(node_a, AsRoot).unwrap();
        let root_id = tree.root.clone().unwrap();
        assert_eq!(node_a_id, root_id);

        {
            let node_a_ref = tree.get(&node_a_id).unwrap();
            let root_ref = tree.get(&root_id).unwrap();
            assert_eq!(node_a_ref.data(), &a);
            assert_eq!(root_ref.data(), &a);
        }

        let node_b_id = tree.insert(node_b, AsRoot).unwrap();
        let root_id = tree.root.clone().unwrap();
        assert_eq!(node_b_id, root_id);

        {
            let node_b_ref = tree.get(&node_b_id).unwrap();
            let root_ref = tree.get(&root_id).unwrap();
            assert_eq!(node_b_ref.data(), &b);
            assert_eq!(root_ref.data(), &b);

            let node_b_child_id = node_b_ref.children().get(0).unwrap();
            let node_b_child_ref = tree.get(&node_b_child_id).unwrap();
            assert_eq!(node_b_child_ref.data(), &a);
        }
    }

    #[test]
    fn test_root_node_id() {
        let tree = TreeBuilder::new().with_root(Node::new(5)).build();

        let root_id = tree.root.clone().unwrap();
        let root_node_id = tree.root_node_id().unwrap();

        assert_eq!(&root_id, root_node_id);
    }

    #[test]
    fn test_insert_with_parent() {
        use InsertBehavior::*;

        let a = 1;
        let b = 2;
        let r = 5;

        let mut tree = TreeBuilder::new().with_root(Node::new(r)).build();

        let node_a = Node::new(a);
        let node_b = Node::new(b);

        let root_id = tree.root.clone().unwrap();
        let node_a_id = tree.insert(node_a, UnderNode(&root_id)).unwrap();
        let node_b_id = tree.insert(node_b, UnderNode(&root_id)).unwrap();

        let node_a_ref = tree.get(&node_a_id).unwrap();
        let node_b_ref = tree.get(&node_b_id).unwrap();
        assert_eq!(node_a_ref.data(), &a);
        assert_eq!(node_b_ref.data(), &b);

        assert_eq!(node_a_ref.parent().unwrap().clone(), root_id);
        assert_eq!(node_b_ref.parent().unwrap().clone(), root_id);

        let root_node_ref = tree.get(&root_id).unwrap();
        let root_children: &Vec<NodeId> = root_node_ref.children();

        let child_1_id = root_children.get(0).unwrap();
        let child_2_id = root_children.get(1).unwrap();

        let child_1_ref = tree.get(&child_1_id).unwrap();
        let child_2_ref = tree.get(&child_2_id).unwrap();

        assert_eq!(child_1_ref.data(), &a);
        assert_eq!(child_2_ref.data(), &b);
    }

    #[test]
    fn test_remove_node_lift_children() {
        use InsertBehavior::*;
        use RemoveBehavior::*;

        let mut tree = TreeBuilder::new().with_root(Node::new(5)).build();

        let root_id = tree.root.clone().unwrap();

        let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2_id = tree.insert(Node::new(2), UnderNode(&node_1_id)).unwrap();
        let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();

        let node_1 = tree.remove_node(node_1_id.clone(), LiftChildren).unwrap();

        assert_eq!(Some(&root_id), tree.root_node_id());

        assert_eq!(node_1.data(), &1);
        assert_eq!(node_1.children().len(), 0);
        assert!(node_1.parent().is_none());
        assert!(tree.get(&node_1_id).is_err());

        let root_ref = tree.get(&root_id).unwrap();
        let node_2_ref = tree.get(&node_2_id).unwrap();
        let node_3_ref = tree.get(&node_3_id).unwrap();

        assert_eq!(node_2_ref.data(), &2);
        assert_eq!(node_3_ref.data(), &3);

        assert_eq!(node_2_ref.parent().unwrap(), &root_id);
        assert_eq!(node_3_ref.parent().unwrap(), &root_id);

        assert!(root_ref.children().contains(&node_2_id));
        assert!(root_ref.children().contains(&node_3_id));
    }

    #[test]
    fn test_remove_node_orphan_children() {
        use InsertBehavior::*;
        use RemoveBehavior::*;

        let mut tree = TreeBuilder::new().with_root(Node::new(5)).build();

        let root_id = tree.root.clone().unwrap();

        let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2_id = tree.insert(Node::new(2), UnderNode(&node_1_id)).unwrap();
        let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();

        let node_1 = tree.remove_node(node_1_id.clone(), OrphanChildren).unwrap();

        assert_eq!(Some(&root_id), tree.root_node_id());

        assert_eq!(node_1.data(), &1);
        assert_eq!(node_1.children().len(), 0);
        assert!(node_1.parent().is_none());
        assert!(tree.get(&node_1_id).is_err());

        let node_2_ref = tree.get(&node_2_id).unwrap();
        let node_3_ref = tree.get(&node_3_id).unwrap();

        assert_eq!(node_2_ref.data(), &2);
        assert_eq!(node_3_ref.data(), &3);

        assert!(node_2_ref.parent().is_none());
        assert!(node_3_ref.parent().is_none());
    }

    #[test]
    fn test_remove_root() {
        use RemoveBehavior::*;

        let mut tree = TreeBuilder::new().with_root(Node::new(5)).build();

        let root_id = tree.root.clone().unwrap();
        tree.remove_node(root_id.clone(), OrphanChildren).unwrap();
        assert_eq!(None, tree.root_node_id());

        let mut tree = TreeBuilder::new().with_root(Node::new(5)).build();

        let root_id = tree.root.clone().unwrap();
        tree.remove_node(root_id.clone(), LiftChildren).unwrap();
        assert_eq!(None, tree.root_node_id());
    }

    #[test]
    fn test_move_node_to_parent() {
        use InsertBehavior::*;
        use MoveBehavior::*;

        let mut tree = Tree::new();

        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();

        // move 3 "across" the tree
        tree.move_node(&node_3_id, ToParent(&node_2_id)).unwrap();
        assert!(tree.get(&root_id).unwrap().children().contains(&node_1_id));
        assert!(tree.get(&root_id).unwrap().children().contains(&node_2_id));
        assert!(tree
            .get(&node_2_id,)
            .unwrap()
            .children()
            .contains(&node_3_id,));

        // move 3 "up" the tree
        tree.move_node(&node_3_id, ToParent(&root_id)).unwrap();
        assert!(tree.get(&root_id).unwrap().children().contains(&node_1_id));
        assert!(tree.get(&root_id).unwrap().children().contains(&node_2_id));
        assert!(tree.get(&root_id).unwrap().children().contains(&node_3_id));

        // move 3 "down" (really this is across though) the tree
        tree.move_node(&node_3_id, ToParent(&node_1_id)).unwrap();
        assert!(tree.get(&root_id).unwrap().children().contains(&node_1_id));
        assert!(tree.get(&root_id).unwrap().children().contains(&node_2_id));
        assert!(tree
            .get(&node_1_id,)
            .unwrap()
            .children()
            .contains(&node_3_id,));

        // move 1 "down" the tree
        tree.move_node(&node_1_id, ToParent(&node_3_id)).unwrap();
        assert!(tree.get(&root_id).unwrap().children().contains(&node_2_id));
        assert!(tree.get(&root_id).unwrap().children().contains(&node_3_id));
        assert!(tree
            .get(&node_3_id,)
            .unwrap()
            .children()
            .contains(&node_1_id,));

        // note: node_1 is at the lowest point in the tree before these insertions.
        let node_4_id = tree.insert(Node::new(4), UnderNode(&node_1_id)).unwrap();
        let node_5_id = tree.insert(Node::new(5), UnderNode(&node_4_id)).unwrap();

        // move 3 "down" the tree
        tree.move_node(&node_3_id, ToParent(&node_5_id)).unwrap();
        assert!(tree.get(&root_id).unwrap().children().contains(&node_2_id));
        assert!(tree.get(&root_id).unwrap().children().contains(&node_1_id));
        assert!(tree
            .get(&node_1_id,)
            .unwrap()
            .children()
            .contains(&node_4_id,));
        assert!(tree
            .get(&node_4_id,)
            .unwrap()
            .children()
            .contains(&node_5_id,));
        assert!(tree
            .get(&node_5_id,)
            .unwrap()
            .children()
            .contains(&node_3_id,));

        // move root "down" the tree
        tree.move_node(&root_id, ToParent(&node_2_id)).unwrap();
        assert!(tree.get(&node_2_id).unwrap().children().contains(&root_id));
        assert!(tree.get(&root_id).unwrap().children().contains(&node_1_id));
        assert!(tree
            .get(&node_1_id,)
            .unwrap()
            .children()
            .contains(&node_4_id,));
        assert!(tree
            .get(&node_4_id,)
            .unwrap()
            .children()
            .contains(&node_5_id,));
        assert!(tree
            .get(&node_5_id,)
            .unwrap()
            .children()
            .contains(&node_3_id,));
        assert_eq!(tree.root_node_id(), Some(&node_2_id));
    }

    #[test]
    fn test_move_node_to_root() {
        use InsertBehavior::*;

        // test move with existing root
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&node_1_id)).unwrap();

            tree.move_node_to_root(&node_2_id).unwrap();

            assert_eq!(tree.root_node_id(), Some(&node_2_id));
            assert!(tree.get(&node_2_id).unwrap().children().contains(&root_id));
            assert!(!tree
                .get(&node_1_id,)
                .unwrap()
                .children()
                .contains(&node_2_id,));
        }

        // test move with existing root and with orphan
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&node_1_id)).unwrap();

            tree.remove_node_orphan_children(node_1_id).unwrap();
            tree.move_node_to_root(&node_2_id).unwrap();

            assert_eq!(tree.root_node_id(), Some(&node_2_id));
            assert!(tree.get(&node_2_id).unwrap().children().contains(&root_id));
            assert_eq!(tree.get(&root_id).unwrap().children().len(), 0);
        }

        // test move without root and with orphan
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&node_1_id)).unwrap();

            tree.remove_node_orphan_children(root_id).unwrap();
            tree.move_node_to_root(&node_1_id).unwrap();

            assert_eq!(tree.root_node_id(), Some(&node_1_id));
            assert!(tree
                .get(&node_1_id,)
                .unwrap()
                .children()
                .contains(&node_2_id,));
            assert_eq!(tree.get(&node_1_id).unwrap().children().len(), 1);
        }
    }

    #[test]
    fn test_find_subtree_root_below_upper_id() {
        use InsertBehavior::*;

        let mut tree = Tree::new();

        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2_id = tree.insert(Node::new(2), UnderNode(&node_1_id)).unwrap();
        let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();
        let node_4_id = tree.insert(Node::new(4), UnderNode(&node_2_id)).unwrap();

        let sub_root = tree.find_subtree_root_between_ids(&node_1_id, &root_id);
        assert_eq!(sub_root, Some(&node_1_id));
        let sub_root = tree.find_subtree_root_between_ids(&root_id, &node_1_id); //invert for None
        assert_eq!(sub_root, None);

        let sub_root = tree.find_subtree_root_between_ids(&node_2_id, &root_id);
        assert_eq!(sub_root, Some(&node_1_id));
        let sub_root = tree.find_subtree_root_between_ids(&root_id, &node_2_id); //invert for None
        assert_eq!(sub_root, None);

        let sub_root = tree.find_subtree_root_between_ids(&node_3_id, &node_1_id);
        assert_eq!(sub_root, Some(&node_3_id));
        let sub_root = tree.find_subtree_root_between_ids(&node_1_id, &node_3_id); //invert for None
        assert_eq!(sub_root, None);

        let sub_root = tree.find_subtree_root_between_ids(&node_4_id, &root_id);
        assert_eq!(sub_root, Some(&node_1_id));
        let sub_root = tree.find_subtree_root_between_ids(&root_id, &node_4_id); //invert for None
        assert_eq!(sub_root, None);
    }

    #[test]
    fn test_swap_nodes_take_children() {
        use InsertBehavior::*;
        use SwapBehavior::*;

        // test across swap
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();
            let node_4_id = tree.insert(Node::new(4), UnderNode(&node_2_id)).unwrap();

            tree.swap_nodes(&node_3_id, &node_4_id, TakeChildren)
                .unwrap();

            assert!(tree
                .get(&node_1_id,)
                .unwrap()
                .children()
                .contains(&node_4_id,));
            assert!(tree
                .get(&node_2_id,)
                .unwrap()
                .children()
                .contains(&node_3_id,));
        }

        // test ordering via swap
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();

            tree.swap_nodes(&node_1_id, &node_2_id, TakeChildren)
                .unwrap();

            let children = tree.get(&root_id).unwrap().children();
            assert!(children[0] == node_2_id);
            assert!(children[1] == node_1_id);
        }

        // test swap down
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();

            tree.swap_nodes(&root_id, &node_3_id, TakeChildren).unwrap();

            assert_eq!(tree.root_node_id(), Some(&node_3_id));

            assert!(tree.get(&node_3_id).unwrap().children().contains(&root_id));

            let children = tree.get(&root_id).unwrap().children();
            assert!(children[0] == node_1_id);
            assert!(children[1] == node_2_id);
        }

        // test swap down without root
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();

            tree.swap_nodes(&node_1_id, &node_3_id, TakeChildren)
                .unwrap();

            assert!(tree
                .get(&node_3_id,)
                .unwrap()
                .children()
                .contains(&node_1_id,));

            let children = tree.get(&root_id).unwrap().children();
            assert!(children[0] == node_3_id);
            assert!(children[1] == node_2_id);
        }
    }

    #[test]
    fn test_swap_nodes_leave_children() {
        use InsertBehavior::*;
        use MoveBehavior::*;
        use RemoveBehavior::*;
        use SwapBehavior::*;

        // test across swap
        // from:
        //        0
        //       / \
        //      1   2
        //      |   |
        //      3   4
        // to:
        //        0
        //       / \
        //      2   1
        //      |   |
        //      3   4
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();
            let node_4_id = tree.insert(Node::new(4), UnderNode(&node_2_id)).unwrap();

            tree.swap_nodes(&node_1_id, &node_2_id, LeaveChildren)
                .unwrap();

            let root_children = tree.get(&root_id).unwrap().children();
            assert_eq!(root_children[0], node_2_id);
            assert_eq!(root_children[1], node_1_id);

            assert_eq!(tree.get(&node_3_id).unwrap().parent(), Some(&node_2_id));
            assert_eq!(tree.get(&node_4_id).unwrap().parent(), Some(&node_1_id));

            assert!(tree
                .get(&node_1_id,)
                .unwrap()
                .children()
                .contains(&node_4_id,));
            assert!(tree
                .get(&node_2_id,)
                .unwrap()
                .children()
                .contains(&node_3_id,));
        }

        // test down swap (with no space between nodes)
        // from:
        //        0
        //       / \
        //      1   2
        //      |   |
        //      3   4
        // to:
        //        0
        //       / \
        //      3   2
        //      |   |
        //      1   4
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();
            tree.insert(Node::new(4), UnderNode(&node_2_id)).unwrap();

            tree.swap_nodes(&node_1_id, &node_3_id, LeaveChildren)
                .unwrap();

            let root_children = tree.get(&root_id).unwrap().children();
            assert_eq!(root_children[0], node_3_id);
            assert_eq!(root_children[1], node_2_id);

            assert_eq!(tree.get(&node_3_id).unwrap().parent(), Some(&root_id));
            assert_eq!(tree.get(&node_1_id).unwrap().parent(), Some(&node_3_id));

            assert!(tree
                .get(&node_3_id,)
                .unwrap()
                .children()
                .contains(&node_1_id,));
            assert_eq!(tree.get(&node_1_id).unwrap().children().len(), 0);
        }

        // test down swap (with space between nodes)
        // from:
        //        0
        //       / \
        //      1   2
        //      |   |
        //      3   4
        //      |
        //      5
        // to:
        //        0
        //       / \
        //      5   2
        //      |   |
        //      3   4
        //      |
        //      1
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();
            tree.insert(Node::new(4), UnderNode(&node_2_id)).unwrap();
            let node_5_id = tree.insert(Node::new(5), UnderNode(&node_3_id)).unwrap();

            tree.swap_nodes(&node_1_id, &node_5_id, LeaveChildren)
                .unwrap();

            let root_children = tree.get(&root_id).unwrap().children();
            assert_eq!(root_children[0], node_5_id);
            assert_eq!(root_children[1], node_2_id);

            assert_eq!(tree.get(&node_3_id).unwrap().parent(), Some(&node_5_id));
            assert_eq!(tree.get(&node_1_id).unwrap().parent(), Some(&node_3_id));
            assert_eq!(tree.get(&node_5_id).unwrap().parent(), Some(&root_id));

            assert!(tree
                .get(&node_3_id,)
                .unwrap()
                .children()
                .contains(&node_1_id,));
            assert!(tree
                .get(&node_5_id,)
                .unwrap()
                .children()
                .contains(&node_3_id,));
            assert_eq!(tree.get(&node_1_id).unwrap().children().len(), 0);
        }

        // test down swap (with root)
        // from:
        //        0
        //       / \
        //      1   2
        //      |   |
        //      3   4
        // to:
        //        4
        //       / \
        //      1   2
        //      |   |
        //      3   0
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();
            let node_4_id = tree.insert(Node::new(4), UnderNode(&node_2_id)).unwrap();

            tree.swap_nodes(&root_id, &node_4_id, LeaveChildren)
                .unwrap();

            assert_eq!(tree.root_node_id(), Some(&node_4_id));

            let node_4_children = tree.get(&node_4_id).unwrap().children();
            assert_eq!(node_4_children[0], node_1_id);
            assert_eq!(node_4_children[1], node_2_id);

            assert_eq!(tree.get(&node_1_id).unwrap().parent(), Some(&node_4_id));
            assert_eq!(tree.get(&node_2_id).unwrap().parent(), Some(&node_4_id));
            assert_eq!(tree.get(&root_id).unwrap().parent(), Some(&node_2_id));

            assert!(tree.get(&node_2_id).unwrap().children().contains(&root_id));
            assert_eq!(tree.get(&root_id).unwrap().children().len(), 0);
        }

        // test orphaned swap (no root)
        // from:
        //      1   2
        //      |   |
        //      3   4
        // to:
        //      2   1
        //      |   |
        //      3   4
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();
            let node_4_id = tree.insert(Node::new(4), UnderNode(&node_2_id)).unwrap();
            tree.remove_node(root_id, OrphanChildren).unwrap();

            tree.swap_nodes(&node_1_id, &node_2_id, LeaveChildren)
                .unwrap();

            assert_eq!(tree.root_node_id(), None);

            assert_eq!(tree.get(&node_3_id).unwrap().parent(), Some(&node_2_id));
            assert_eq!(tree.get(&node_4_id).unwrap().parent(), Some(&node_1_id));

            assert!(tree
                .get(&node_2_id,)
                .unwrap()
                .children()
                .contains(&node_3_id,));
            assert!(tree
                .get(&node_1_id,)
                .unwrap()
                .children()
                .contains(&node_4_id,));
        }

        // test orphaned swap (1 is root)
        // from:
        //      1   2
        //      |   |
        //      3   4
        // to:
        //      2   1
        //      |   |
        //      3   4
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();
            let node_4_id = tree.insert(Node::new(4), UnderNode(&node_2_id)).unwrap();
            tree.remove_node(root_id, OrphanChildren).unwrap();
            tree.move_node(&node_1_id, ToRoot).unwrap();

            tree.swap_nodes(&node_1_id, &node_2_id, LeaveChildren)
                .unwrap();

            assert_eq!(tree.root_node_id(), Some(&node_2_id));

            assert_eq!(tree.get(&node_3_id).unwrap().parent(), Some(&node_2_id));
            assert_eq!(tree.get(&node_4_id).unwrap().parent(), Some(&node_1_id));

            assert!(tree
                .get(&node_2_id,)
                .unwrap()
                .children()
                .contains(&node_3_id,));
            assert!(tree
                .get(&node_1_id,)
                .unwrap()
                .children()
                .contains(&node_4_id,));
        }
    }

    #[test]
    fn test_swap_nodes_children_only() {
        use InsertBehavior::*;
        use SwapBehavior::*;

        // test across swap
        // swap(1,2)
        // from:
        //        0
        //       / \
        //      1   2
        //     / \   \
        //    3   4   5
        // to:
        //        0
        //       / \
        //      1   2
        //     /   / \
        //    5   3   4
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();
            let node_4_id = tree.insert(Node::new(4), UnderNode(&node_1_id)).unwrap();
            let node_5_id = tree.insert(Node::new(5), UnderNode(&node_2_id)).unwrap();

            tree.swap_nodes(&node_1_id, &node_2_id, ChildrenOnly)
                .unwrap();

            let root_children = tree.get(&root_id).unwrap().children();
            assert_eq!(root_children[0], node_1_id);
            assert_eq!(root_children[1], node_2_id);

            assert_eq!(tree.get(&node_3_id).unwrap().parent(), Some(&node_2_id));
            assert_eq!(tree.get(&node_4_id).unwrap().parent(), Some(&node_2_id));
            assert_eq!(tree.get(&node_5_id).unwrap().parent(), Some(&node_1_id));

            assert!(tree
                .get(&node_1_id,)
                .unwrap()
                .children()
                .contains(&node_5_id,));
            assert!(tree
                .get(&node_2_id,)
                .unwrap()
                .children()
                .contains(&node_3_id,));
            assert!(tree
                .get(&node_2_id,)
                .unwrap()
                .children()
                .contains(&node_4_id,));
        }

        // test down swap (with no space between nodes)
        // swap(1,3)
        // from:
        //        0
        //       / \
        //      1   2
        //     / \   \
        //    3   4   5
        //    |   |
        //    6   7
        // to:
        //        0
        //       / \
        //      1   2
        //     / \   \
        //    6   3   5
        //        |
        //        4
        //        |
        //        7
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();
            let node_4_id = tree.insert(Node::new(4), UnderNode(&node_1_id)).unwrap();
            tree.insert(Node::new(5), UnderNode(&node_2_id)).unwrap();
            let node_6_id = tree.insert(Node::new(6), UnderNode(&node_3_id)).unwrap();
            tree.insert(Node::new(7), UnderNode(&node_4_id)).unwrap();

            tree.swap_nodes(&node_1_id, &node_3_id, ChildrenOnly)
                .unwrap();

            let root_children = tree.get(&root_id).unwrap().children();
            assert_eq!(root_children[0], node_1_id);
            assert_eq!(root_children[1], node_2_id);

            assert_eq!(tree.get(&node_3_id).unwrap().parent(), Some(&node_1_id));
            assert_eq!(tree.get(&node_1_id).unwrap().parent(), Some(&root_id));
            assert_eq!(tree.get(&node_4_id).unwrap().parent(), Some(&node_3_id));
            assert_eq!(tree.get(&node_6_id).unwrap().parent(), Some(&node_1_id));

            let node_1_children = tree.get(&node_1_id).unwrap().children();
            assert_eq!(node_1_children[0], node_6_id);
            assert_eq!(node_1_children[1], node_3_id);
            assert!(tree
                .get(&node_3_id,)
                .unwrap()
                .children()
                .contains(&node_4_id,));
        }

        // test down swap (with space between nodes)
        // swap(1, 6)
        // from:
        //        0
        //       / \
        //      1   2
        //     / \   \
        //    3   4   5
        //    |   |
        //    6   7
        // to:
        //        0
        //       / \
        //      1   2
        //     /     \
        //    6       5
        //   / \
        //  3   4
        //      |
        //      7
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();
            let node_4_id = tree.insert(Node::new(4), UnderNode(&node_1_id)).unwrap();
            tree.insert(Node::new(5), UnderNode(&node_2_id)).unwrap();
            let node_6_id = tree.insert(Node::new(6), UnderNode(&node_3_id)).unwrap();
            tree.insert(Node::new(7), UnderNode(&node_4_id)).unwrap();

            tree.swap_nodes(&node_1_id, &node_6_id, ChildrenOnly)
                .unwrap();

            let root_children = tree.get(&root_id).unwrap().children();
            assert_eq!(root_children[0], node_1_id);
            assert_eq!(root_children[1], node_2_id);

            assert_eq!(tree.get(&node_3_id).unwrap().parent(), Some(&node_6_id));
            assert_eq!(tree.get(&node_4_id).unwrap().parent(), Some(&node_6_id));
            assert_eq!(tree.get(&node_6_id).unwrap().parent(), Some(&node_1_id));

            assert!(tree
                .get(&node_1_id,)
                .unwrap()
                .children()
                .contains(&node_6_id,));
            assert!(!tree
                .get(&node_1_id,)
                .unwrap()
                .children()
                .contains(&node_3_id,));
            assert!(!tree
                .get(&node_1_id,)
                .unwrap()
                .children()
                .contains(&node_4_id,));
            assert!(tree
                .get(&node_6_id,)
                .unwrap()
                .children()
                .contains(&node_3_id,));
            assert!(tree
                .get(&node_6_id,)
                .unwrap()
                .children()
                .contains(&node_4_id,));
        }

        // test down swap (with root)
        // swap(0,1)
        // from:
        //        0
        //       / \
        //      1   2
        //     / \   \
        //    3   4   5
        //    |   |
        //    6   7
        // to:
        //        0
        //       /|\
        //      3 4 1
        //      | | |
        //      6 7 2
        //          |
        //          5
        {
            let mut tree = Tree::new();
            let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
            let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            let node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();
            let node_4_id = tree.insert(Node::new(4), UnderNode(&node_1_id)).unwrap();
            tree.insert(Node::new(5), UnderNode(&node_2_id)).unwrap();
            tree.insert(Node::new(6), UnderNode(&node_3_id)).unwrap();
            tree.insert(Node::new(7), UnderNode(&node_4_id)).unwrap();

            tree.swap_nodes(&root_id, &node_1_id, ChildrenOnly).unwrap();

            let root_children = tree.get(&root_id).unwrap().children();
            assert_eq!(root_children[0], node_3_id);
            assert_eq!(root_children[1], node_4_id);
            assert_eq!(root_children[2], node_1_id);

            assert_eq!(tree.get(&node_1_id).unwrap().parent(), Some(&root_id));
            assert_eq!(tree.get(&node_3_id).unwrap().parent(), Some(&root_id));
            assert_eq!(tree.get(&node_4_id).unwrap().parent(), Some(&root_id));
            assert_eq!(tree.get(&node_2_id).unwrap().parent(), Some(&node_1_id));

            let node_1_children = tree.get(&node_1_id).unwrap().children();
            assert_eq!(node_1_children[0], node_2_id);
        }
    }

    #[test]
    fn test_tree_height() {
        use InsertBehavior::*;
        use RemoveBehavior::*;

        // empty tree
        let mut tree = Tree::new();
        assert_eq!(0, tree.height());

        // the tree with single root node
        let root_id = tree.insert(Node::new(1), AsRoot).unwrap();
        assert_eq!(1, tree.height());

        // root node with single child
        let child_1_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        assert_eq!(2, tree.height());

        // root node with two children
        let child_2_id = tree.insert(Node::new(3), UnderNode(&root_id)).unwrap();
        assert_eq!(2, tree.height());

        // grandson
        tree.insert(Node::new(4), UnderNode(&child_1_id)).unwrap();
        assert_eq!(3, tree.height());

        // remove child_1 and gradson
        tree.remove_node(child_1_id, DropChildren).unwrap();
        assert_eq!(2, tree.height());

        // remove child_2
        tree.remove_node(child_2_id, LiftChildren).unwrap();
        assert_eq!(1, tree.height());
    }

    #[test]
    fn test_partial_eq() {
        use InsertBehavior::*;

        let mut tree = Tree::new();
        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();

        // ensure PartialEq doesn't work when the number of used nodes are not equal
        {
            let mut other = Tree::new();
            let root_id = other.insert(Node::new(0), AsRoot).unwrap();
            other.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            other.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            assert_ne!(tree, other);
        }

        // ensure PartialEq doesn't work when the data is not equal
        {
            let mut other = Tree::new();
            let root_id = other.insert(Node::new(0), AsRoot).unwrap();
            let id = other.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            other.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            other.insert(Node::new(4), UnderNode(&id)).unwrap();
            assert_ne!(tree, other);
        }

        // ensure PartialEq doesn't work when the parents aren't equal
        {
            let mut other = Tree::new();
            let root_id = other.insert(Node::new(0), AsRoot).unwrap();
            other.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            let id = other.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            other.insert(Node::new(3), UnderNode(&id)).unwrap();
            assert_ne!(tree, other);
        }

        // ensure PartialEq works even if the number of free spots in Tree.nodes is different
        {
            let mut other = Tree::new();
            let root_id = other.insert(Node::new(0), AsRoot).unwrap();
            let id = other.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            other.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            other.insert(Node::new(3), UnderNode(&id)).unwrap();
            let to_delete = other.insert(Node::new(42), UnderNode(&root_id)).unwrap();
            other.take_node(to_delete);
            assert_ne!(
                tree.nodes.iter().filter(|x| x.is_none()).count(),
                other.nodes.iter().filter(|x| x.is_none()).count()
            );
            assert_eq!(tree, other);
        }

        // ensure PartialEq doesn't work when the Node's index are different
        {
            let mut other = Tree::new();
            let root_id = other.insert(Node::new(0), AsRoot).unwrap();
            let to_delete = other.insert(Node::new(42), UnderNode(&root_id)).unwrap();
            let id = other.insert(Node::new(1), UnderNode(&root_id)).unwrap();
            other.insert(Node::new(2), UnderNode(&root_id)).unwrap();
            other.insert(Node::new(3), UnderNode(&id)).unwrap();
            other.take_node(to_delete);
            assert_ne!(tree, other);
        }
    }

    #[test]
    fn test_clone() {
        use InsertBehavior::*;

        let mut tree = Tree::new();
        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        let _node_3_id = tree.insert(Node::new(3), UnderNode(&node_1_id)).unwrap();
        let node_4_id = tree.insert(Node::new(4), UnderNode(&node_2_id)).unwrap();
        tree.take_node(node_4_id);

        let cloned = tree.clone();
        assert!(cloned.root.is_some());
        let tree_id = cloned.id;

        // ensure cloned tree has a new id
        assert_ne!(tree.id, tree_id);

        // ensure cloned tree's root is using the new tree id
        assert_eq!(cloned.root.as_ref().map(|x| x.tree_id), Some(tree_id));

        // ensure cloned tree's free_ids is using the new tree id
        assert_eq!(cloned.free_ids[0].tree_id, tree_id);

        // ensure nodes' parent are using the new tree id
        assert_eq!(
            cloned.nodes[1]
                .as_ref()
                .map(|x| x.parent.as_ref().map(|x| x.tree_id)),
            Some(Some(tree_id))
        );

        // ensure nodes' children are using the new tree id
        assert_eq!(
            cloned
                .children(cloned.root.as_ref().unwrap())
                .unwrap()
                .next()
                .map(|x| x.parent.as_ref().map(|x| x.tree_id)),
            Some(Some(tree_id))
        );

        // ensure the tree and the cloned tree are equal
        assert_eq!(tree, cloned);
    }
}
