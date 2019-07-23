use NodeId;

///
/// Describes the possible behaviors of the `Tree::remove_node` method.
///
pub enum RemoveBehavior {
    ///
    /// All children will be dropped recursively.  In other words, the entire sub-tree of the `Node`
    /// being removed will be dropped from the tree.  Those `Node`s will no longer exist and
    /// cannot be accessed even if you have the `NodeId` the previously pointed to them.
    ///
    /// This means even without using `Clone` you might end up with copies of invalid `NodeId`s.
    /// Use this behavior with caution.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    /// use id_tree::RemoveBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    ///
    /// let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
    /// let child_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    /// let grandchild_id = tree.insert(Node::new(2), UnderNode(&child_id)).unwrap();
    ///
    /// let child = tree.remove_node(child_id, DropChildren).ok().unwrap();
    ///
    /// assert!(tree.get(&grandchild_id).is_err());
    /// assert_eq!(tree.get(&root_id).unwrap().children().len(), 0);
    /// assert_eq!(child.children().len(), 0);
    /// assert_eq!(child.parent(), None);
    /// ```
    ///
    DropChildren,

    ///
    /// If the removed `Node` (let's call it `A`) has a parent, `A`'s parent will become the
    /// parent of `A`'s children.  This effectively just shifts them up one level in the `Tree`.
    ///
    /// If `A` doesn't have a parent, then this behaves exactly like
    /// `RemoveBehavior::OrphanChildren`.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    /// use id_tree::RemoveBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    ///
    /// let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
    /// let child_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    /// let grandchild_id = tree.insert(Node::new(2), UnderNode(&child_id)).unwrap();
    ///
    /// let child = tree.remove_node(child_id, LiftChildren).ok().unwrap();
    ///
    /// assert!(tree.get(&grandchild_id).is_ok());
    /// assert!(tree.get(&root_id).unwrap().children().contains(&grandchild_id));
    /// assert_eq!(child.children().len(), 0);
    /// assert_eq!(child.parent(), None);
    /// ```
    ///
    LiftChildren,

    ///
    /// All children will have their parent references cleared.  This means nothing will point to
    /// them, but they will still exist in the tree.  Those `Node`s can still be accessed provided
    /// that you have the `NodeId` that points to them.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    /// use id_tree::RemoveBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    ///
    /// let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
    /// let child_id = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    /// let grandchild_id = tree.insert(Node::new(2), UnderNode(&child_id)).unwrap();
    ///
    /// let child = tree.remove_node(child_id, OrphanChildren).ok().unwrap();
    ///
    /// assert!(tree.get(&grandchild_id).is_ok());
    /// assert_eq!(tree.get(&root_id).unwrap().children().len(), 0);
    /// assert_eq!(child.children().len(), 0);
    /// assert_eq!(child.parent(), None);
    /// ```
    ///
    OrphanChildren,
}

///
/// Describes the possible behaviors of the `Tree::move_node` method.
///
pub enum MoveBehavior<'a> {
    ///
    /// Sets the `Node` in question as the new root `Node`, leaving all children in their place (in
    /// other words, they will travel with the `Node` being moved).
    ///
    /// If there is already a root `Node` in place, it will be attached as the last child of the new
    /// root.
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
    /// assert!(tree.get(&grandchild_id).unwrap().children().contains(&root_id));
    /// assert!(!tree.get(&child_id).unwrap().children().contains(&grandchild_id));
    /// ```
    ///
    ToRoot,

    ///
    /// Moves a `Node` inside the `Tree` to a new parent leaving all children in their place.
    ///
    /// If the new parent (let's call it `B`) is a descendant of the `Node` being moved (`A`), then
    /// the direct child of `A` on the path from `A` to `B` will be shifted upwards to take the
    /// place of its parent (`A`).  All other children of `A` will be left alone, meaning they will
    /// travel with it down the `Tree`.
    ///
    /// Please note that during the "shift-up" part of the above scenario, the `Node` being shifted
    /// up will always be added as the last child of its new parent.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    /// use id_tree::MoveBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    ///
    /// let root_id = tree.insert(Node::new(1), AsRoot).ok().unwrap();
    /// let first_child_id = tree.insert(Node::new(2),  UnderNode(&root_id)).unwrap();
    /// let second_child_id = tree.insert(Node::new(3), UnderNode(&root_id)).unwrap();
    /// let grandchild_id = tree.insert(Node::new(4), UnderNode(&first_child_id)).unwrap();
    ///
    /// tree.move_node(&grandchild_id, ToParent(&second_child_id)).unwrap();
    ///
    /// assert!(!tree.get(&first_child_id).unwrap().children().contains(&grandchild_id));
    /// assert!(tree.get(&second_child_id).unwrap().children().contains(&grandchild_id));
    /// ```
    ///
    ToParent(&'a NodeId),
}

///
/// Describes the possible behaviors of the `Tree::insert` method.
///
pub enum InsertBehavior<'a> {
    ///
    /// Sets the root of the `Tree`.
    ///
    /// If there is already a root `Node` present in the tree, that `Node` is set as the first child
    /// of the new root.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    ///
    /// tree.insert(Node::new(5), AsRoot).unwrap();
    /// ```
    AsRoot,

    ///
    /// Returns a `Result` containing the `NodeId` of the child that was added or a `NodeIdError` if
    /// one occurred.
    ///
    /// Note: Adds the new Node to the end of its children.
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
    UnderNode(&'a NodeId),
}

///
/// Describes the possible behaviors of the `Tree::swap_nodes` method.
///
pub enum SwapBehavior {
    ///
    /// Take the children of the `Node`s being swapped with them.  In other words, this swaps the
    /// `Node`s in question along with their entire sub-tree.  This *does not* affect the
    /// relationship between the `Node`s being swapped and their children.
    ///
    /// If one `Node` is a descendant of the other getting swapped, the former *upper* `Node` is
    /// attached as the last child of the former *lower* `Node` after the swap. (The *lower* will
    /// take the *uppers* original position as usual.) The subtree of the former *upper* node is not
    /// touched except that the *lower* `Node` is moved including all its children.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    /// use id_tree::SwapBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    ///
    /// let root_id = tree.insert(Node::new(1), AsRoot).unwrap();
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
    TakeChildren,

    ///
    /// Leave the children of the `Node`s being swapped where they are.  In other words, this only
    /// swaps the `Node`s themselves.  This *does* affect the relationship between the `Node`s
    /// being swapped and their children.
    ///
    /// Please Note: Because this behavior alters the relationship between the `Node`s being
    /// swapped and their children, any calls to `children()` that have been cloned will no longer
    /// point to the children of the `Node` that you might think they do.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    /// use id_tree::SwapBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    ///
    /// let root_id = tree.insert(Node::new(1), AsRoot).unwrap();
    /// let first_child_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
    /// let second_child_id = tree.insert(Node::new(3), UnderNode(&root_id)).unwrap();
    /// let grandchild_id = tree.insert(Node::new(4), UnderNode(&second_child_id)).unwrap();
    ///
    /// tree.swap_nodes(&first_child_id, &second_child_id, LeaveChildren).unwrap();
    ///
    /// assert!(tree.get(&first_child_id).unwrap().children().contains(&grandchild_id));
    /// assert_eq!(tree.get(&second_child_id).unwrap().children().len(), 0);
    /// ```
    ///
    LeaveChildren,

    ///
    /// Swap the children of the `Node`s in question only.  This does not swap the `Node`s that are
    /// specified when calling this method.  This *does* affect the relationship between the
    /// `Node`s that are specified and their children.
    ///
    /// If one `Node` is a descendant of the other getting swapped, the child swapping step will
    /// take place and then the *lower* `Node` in the swap will be added as the last child of the
    /// *upper* `Node` in the swap.
    ///
    /// Please Note: Because this behavior alters the relationship between the `Node`s being
    /// swapped and their children, any calls to `children()` that have been cloned will no longer
    /// point to the children of the `Node` that you think they do.
    ///
    /// ```
    /// use id_tree::*;
    /// use id_tree::InsertBehavior::*;
    /// use id_tree::SwapBehavior::*;
    ///
    /// let mut tree: Tree<i32> = Tree::new();
    ///
    /// let root_id = tree.insert(Node::new(1), AsRoot).unwrap();
    /// let first_child_id = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
    /// let second_child_id = tree.insert(Node::new(3), UnderNode(&root_id)).unwrap();
    /// let grandchild_id = tree.insert(Node::new(4), UnderNode(&second_child_id)).unwrap();
    /// let grandchild_id_2 = tree.insert(Node::new(5), UnderNode(&first_child_id)).unwrap();
    ///
    /// tree.swap_nodes(&first_child_id, &second_child_id, ChildrenOnly).unwrap();
    ///
    /// assert!(tree.get(&first_child_id).unwrap().children().contains(&grandchild_id));
    /// assert!(tree.get(&second_child_id).unwrap().children().contains(&grandchild_id_2));
    /// ```
    ///
    ChildrenOnly,
}
