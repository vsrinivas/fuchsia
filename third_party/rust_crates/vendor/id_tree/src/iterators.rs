use std::collections::VecDeque;
use std::slice::Iter;
use std::vec::IntoIter;

use Node;
use NodeId;
use Tree;

// Note: The Clone trait is implemented manually throughout this file because a #[derive(Clone)]
// forces the type parameters of the iterator to also implement Clone, even though
// the iterator only ever holds a reference to the data of that type. E.g. cloning
// AncestorIds<'a, T> requires T: Clone, but AncestorIds only holds a reference to some data &T.
// By implementing the trait manually, we circumvent that requirement.

///
/// An Iterator over the ancestors of a `Node`.
///
/// Iterates over the ancestor `Node`s of a given `Node` in the `Tree`.  Each call to `next` will
/// return an immutable reference to the next `Node` up the `Tree`.
///
pub struct Ancestors<'a, T: 'a> {
    tree: &'a Tree<T>,
    node_id: Option<NodeId>,
}

impl<'a, T> Ancestors<'a, T> {
    pub(crate) fn new(tree: &'a Tree<T>, node_id: NodeId) -> Ancestors<'a, T> {
        Ancestors {
            tree: tree,
            node_id: Some(node_id),
        }
    }
}

impl<'a, T> Iterator for Ancestors<'a, T> {
    type Item = &'a Node<T>;

    fn next(&mut self) -> Option<&'a Node<T>> {
        self.node_id
            .take()
            .and_then(|current_id| self.tree.get(&current_id).ok())
            .and_then(|node_ref| node_ref.parent())
            .and_then(|parent_id| {
                self.node_id = Some(parent_id.clone());

                self.tree.get(parent_id).ok()
            })
    }
}

impl<'a, T> Clone for Ancestors<'a, T> {
    fn clone(&self) -> Self {
        Ancestors {
            tree: &self.tree,
            node_id: self.node_id.clone(),
        }
    }
}

///
/// An Iterator over the ancestors of a `Node`.
///
/// Iterates over `NodeId`s instead of over the `Node`s themselves.
///
pub struct AncestorIds<'a, T: 'a> {
    tree: &'a Tree<T>,
    node_id: Option<NodeId>,
}

impl<'a, T> AncestorIds<'a, T> {
    pub(crate) fn new(tree: &'a Tree<T>, node_id: NodeId) -> AncestorIds<'a, T> {
        AncestorIds {
            tree: tree,
            node_id: Some(node_id),
        }
    }
}

impl<'a, T> Iterator for AncestorIds<'a, T> {
    type Item = &'a NodeId;

    fn next(&mut self) -> Option<&'a NodeId> {
        self.node_id
            .take()
            .and_then(|current_id| self.tree.get(&current_id).ok())
            .and_then(|node_ref| node_ref.parent())
            .and_then(|parent_id| {
                self.node_id = Some(parent_id.clone());

                Some(parent_id)
            })
    }
}

impl<'a, T> Clone for AncestorIds<'a, T> {
    fn clone(&self) -> Self {
        AncestorIds {
            tree: &self.tree,
            node_id: self.node_id.clone(),
        }
    }
}

///
/// An Iterator over the children of a `Node`.
///
/// Iterates over the child `Node`s of a given `Node` in the `Tree`.  Each call to `next` will
/// return an immutable reference to the next child `Node`.
///
pub struct Children<'a, T: 'a> {
    tree: &'a Tree<T>,
    child_ids: Iter<'a, NodeId>,
}

impl<'a, T> Children<'a, T> {
    pub(crate) fn new(tree: &'a Tree<T>, node_id: NodeId) -> Children<'a, T> {
        Children {
            tree: tree,
            child_ids: tree.get_unsafe(&node_id).children().as_slice().iter(),
        }
    }
}

impl<'a, T> Iterator for Children<'a, T> {
    type Item = &'a Node<T>;

    fn next(&mut self) -> Option<&'a Node<T>> {
        self.child_ids
            .next()
            .and_then(|child_id| self.tree.get(child_id).ok())
    }
}

impl<'a, T> Clone for Children<'a, T> {
    fn clone(&self) -> Self {
        Children {
            tree: &self.tree,
            child_ids: self.child_ids.clone(),
        }
    }
}

///
/// An Iterator over the children of a `Node`.
///
/// Iterates over `NodeId`s instead of over the `Node`s themselves.
///
#[derive(Clone)]
pub struct ChildrenIds<'a> {
    child_ids: Iter<'a, NodeId>,
}

impl<'a> ChildrenIds<'a> {
    pub(crate) fn new<T>(tree: &'a Tree<T>, node_id: NodeId) -> ChildrenIds<'a> {
        ChildrenIds {
            child_ids: tree.get_unsafe(&node_id).children().as_slice().iter(),
        }
    }
}

impl<'a> Iterator for ChildrenIds<'a> {
    type Item = &'a NodeId;

    fn next(&mut self) -> Option<&'a NodeId> {
        self.child_ids.next()
    }
}

///
/// An Iterator over the sub-tree relative to a given `Node`.
///
/// Iterates over all of the `Node`s in the sub-tree of a given `Node` in the `Tree`.  Each call to
/// `next` will return an immutable reference to the next `Node` in Pre-Order Traversal order.
///
pub struct PreOrderTraversal<'a, T: 'a> {
    tree: &'a Tree<T>,
    data: VecDeque<NodeId>,
}

impl<'a, T> PreOrderTraversal<'a, T> {
    pub(crate) fn new(tree: &'a Tree<T>, node_id: NodeId) -> PreOrderTraversal<T> {
        // over allocating, but all at once instead of re-sizing and re-allocating as we go
        let mut data = VecDeque::with_capacity(tree.capacity());

        data.push_front(node_id);

        PreOrderTraversal {
            tree: tree,
            data: data,
        }
    }
}

impl<'a, T> Iterator for PreOrderTraversal<'a, T> {
    type Item = &'a Node<T>;

    fn next(&mut self) -> Option<&'a Node<T>> {
        self.data
            .pop_front()
            .and_then(|node_id| self.tree.get(&node_id).ok())
            .and_then(|node_ref| {
                // prepend child_ids
                for child_id in node_ref.children().iter().rev() {
                    self.data.push_front(child_id.clone());
                }

                Some(node_ref)
            })
    }
}

impl<'a, T> Clone for PreOrderTraversal<'a, T> {
    fn clone(&self) -> Self {
        PreOrderTraversal {
            tree: &self.tree,
            data: self.data.clone(),
        }
    }
}

///
/// An Iterator over the sub-tree relative to a given `Node`.
///
/// Iterates over all of the `NodeIds`s in the sub-tree of a given `NodeId` in the `Tree`.  Each call to
/// `next` will return the next `NodeId` in Pre-Order Traversal order.
///
pub struct PreOrderTraversalIds<'a, T: 'a> {
    tree: &'a Tree<T>,
    data: VecDeque<NodeId>,
}

impl<'a, T> PreOrderTraversalIds<'a, T> {
    pub(crate) fn new(tree: &'a Tree<T>, node_id: NodeId) -> PreOrderTraversalIds<'a, T> {
        // over allocating, but all at once instead of re-sizing and re-allocating as we go
        let mut data = VecDeque::with_capacity(tree.capacity());

        data.push_front(node_id);

        PreOrderTraversalIds {
            tree: tree,
            data: data,
        }
    }
}

impl<'a, T> Iterator for PreOrderTraversalIds<'a, T> {
    type Item = NodeId;

    fn next(&mut self) -> Option<NodeId> {
        self.data.pop_front().and_then(|node_id| {
            self.tree.get(&node_id).ok().and_then(|node_ref| {
                // prepend child_ids
                for child_id in node_ref.children().iter().rev() {
                    self.data.push_front(child_id.clone());
                }

                Some(node_id)
            })
        })
    }
}

impl<'a, T> Clone for PreOrderTraversalIds<'a, T> {
    fn clone(&self) -> Self {
        PreOrderTraversalIds {
            tree: &self.tree,
            data: self.data.clone(),
        }
    }
}

///
/// An Iterator over the sub-tree relative to a given `Node`.
///
/// Iterates over all of the `Node`s in the sub-tree of a given `Node` in the `Tree`.  Each call to
/// `next` will return an immutable reference to the next `Node` in Post-Order Traversal order.
///
pub struct PostOrderTraversal<'a, T: 'a> {
    tree: &'a Tree<T>,
    ids: IntoIter<NodeId>,
}

impl<'a, T> PostOrderTraversal<'a, T> {
    pub(crate) fn new(tree: &'a Tree<T>, node_id: NodeId) -> PostOrderTraversal<T> {
        // over allocating, but all at once instead of re-sizing and re-allocating as we go
        let mut ids = Vec::with_capacity(tree.capacity());

        PostOrderTraversal::process_nodes(node_id, tree, &mut ids);

        PostOrderTraversal {
            tree: tree,
            ids: ids.into_iter(),
        }
    }

    fn process_nodes(starting_id: NodeId, tree: &Tree<T>, ids: &mut Vec<NodeId>) {
        let node = tree.get(&starting_id).unwrap();

        for child_id in node.children() {
            PostOrderTraversal::process_nodes(child_id.clone(), tree, ids);
        }

        ids.push(starting_id);
    }
}

impl<'a, T> Iterator for PostOrderTraversal<'a, T> {
    type Item = &'a Node<T>;

    fn next(&mut self) -> Option<&'a Node<T>> {
        self.ids
            .next()
            .and_then(|node_id| self.tree.get(&node_id).ok())
    }
}

impl<'a, T> Clone for PostOrderTraversal<'a, T> {
    fn clone(&self) -> Self {
        PostOrderTraversal {
            tree: &self.tree,
            ids: self.ids.clone(),
        }
    }
}

///
/// An Iterator over the sub-tree relative to a given `Node`.
///
/// Iterates over all of the `NodeId`s in the sub-tree of a given `NodeId` in the `Tree`.  Each call to
/// `next` will return the next `NodeId` in Post-Order Traversal order.
///
#[derive(Clone)]
pub struct PostOrderTraversalIds {
    ids: IntoIter<NodeId>,
}

impl PostOrderTraversalIds {
    pub(crate) fn new<T>(tree: &Tree<T>, node_id: NodeId) -> PostOrderTraversalIds {
        // over allocating, but all at once instead of re-sizing and re-allocating as we go
        let mut ids = Vec::with_capacity(tree.capacity());

        PostOrderTraversalIds::process_nodes(node_id, tree, &mut ids);

        PostOrderTraversalIds {
            ids: ids.into_iter(),
        }
    }

    fn process_nodes<T>(starting_id: NodeId, tree: &Tree<T>, ids: &mut Vec<NodeId>) {
        let node = tree.get(&starting_id).unwrap();

        for child_id in node.children() {
            PostOrderTraversalIds::process_nodes(child_id.clone(), tree, ids);
        }

        ids.push(starting_id);
    }
}

impl Iterator for PostOrderTraversalIds {
    type Item = NodeId;

    fn next(&mut self) -> Option<NodeId> {
        self.ids.next()
    }
}

///
/// An Iterator over the sub-tree relative to a given `Node`.
///
/// Iterates over all of the `Node`s in the sub-tree of a given `Node` in the `Tree`.  Each call to
/// `next` will return an immutable reference to the next `Node` in Level-Order Traversal order.
///
pub struct LevelOrderTraversal<'a, T: 'a> {
    tree: &'a Tree<T>,
    data: VecDeque<NodeId>,
}

impl<'a, T> LevelOrderTraversal<'a, T> {
    pub(crate) fn new(tree: &'a Tree<T>, node_id: NodeId) -> LevelOrderTraversal<T> {
        // over allocating, but all at once instead of re-sizing and re-allocating as we go
        let mut data = VecDeque::with_capacity(tree.capacity());

        data.push_back(node_id);

        LevelOrderTraversal {
            tree: tree,
            data: data,
        }
    }
}

impl<'a, T> Iterator for LevelOrderTraversal<'a, T> {
    type Item = &'a Node<T>;

    fn next(&mut self) -> Option<&'a Node<T>> {
        self.data
            .pop_front()
            .and_then(|node_id| self.tree.get(&node_id).ok())
            .and_then(|node_ref| {
                for child_id in node_ref.children() {
                    self.data.push_back(child_id.clone());
                }

                Some(node_ref)
            })
    }
}

impl<'a, T> Clone for LevelOrderTraversal<'a, T> {
    fn clone(&self) -> Self {
        LevelOrderTraversal {
            tree: &self.tree,
            data: self.data.clone(),
        }
    }
}

///
/// An Iterator over the sub-tree relative to a given `Node`.
///
/// Iterates over all of the `NodeId`s in the sub-tree of a given `NodeId` in the `Tree`.  Each call to
/// `next` will return the next `NodeId` in Level-Order TraversalIds order.
///
pub struct LevelOrderTraversalIds<'a, T: 'a> {
    tree: &'a Tree<T>,
    data: VecDeque<NodeId>,
}

impl<'a, T> LevelOrderTraversalIds<'a, T> {
    pub(crate) fn new(tree: &'a Tree<T>, node_id: NodeId) -> LevelOrderTraversalIds<T> {
        // over allocating, but all at once instead of re-sizing and re-allocating as we go
        let mut data = VecDeque::with_capacity(tree.capacity());

        data.push_back(node_id);

        LevelOrderTraversalIds {
            tree: tree,
            data: data,
        }
    }
}

impl<'a, T> Iterator for LevelOrderTraversalIds<'a, T> {
    type Item = NodeId;

    fn next(&mut self) -> Option<NodeId> {
        self.data.pop_front().and_then(|node_id| {
            self.tree.get(&node_id).ok().and_then(|node_ref| {
                for child_id in node_ref.children() {
                    self.data.push_back(child_id.clone());
                }

                Some(node_id)
            })
        })
    }
}

impl<'a, T> Clone for LevelOrderTraversalIds<'a, T> {
    fn clone(&self) -> Self {
        LevelOrderTraversalIds {
            tree: &self.tree,
            data: self.data.clone(),
        }
    }
}

#[cfg(test)]
mod tests {

    use InsertBehavior::*;
    use Node;
    use Tree;

    #[test]
    fn test_ancestors() {
        let mut tree = Tree::new();

        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2 = tree.insert(Node::new(2), UnderNode(&node_1)).unwrap();
        let node_3 = tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let ancestors = tree.ancestors(&root_id).unwrap();
        assert_eq!(ancestors.count(), 0);

        let data = [0];
        for (index, node) in tree.ancestors(&node_1).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }

        let data = [1, 0];
        for (index, node) in tree.ancestors(&node_2).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }

        let data = [1, 0];
        for (index, node) in tree.ancestors(&node_3).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }
    }

    #[test]
    fn test_ancestors_clone() {
        let mut tree = Tree::new();

        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2 = tree.insert(Node::new(2), UnderNode(&node_1)).unwrap();

        let node_2_ancestors = tree.ancestors(&node_2).unwrap();
        let node_2_ancestors_clone = node_2_ancestors.clone();

        // Clone is a separate entity
        let data = [1, 0];
        for (index, (node, clone_it_node)) in
            node_2_ancestors.zip(node_2_ancestors_clone).enumerate()
        {
            assert_eq!(node.data(), &data[index]);
            assert_eq!(clone_it_node.data(), &data[index]);
        }

        // State is copied over to clone
        let mut node_2_ancestors = tree.ancestors(&node_2).unwrap();

        node_2_ancestors.next();

        let mut node_2_ancestors_clone = node_2_ancestors.clone();

        assert_eq!(node_2_ancestors_clone.next(), Some(&Node::new(0)));
    }

    #[test]
    fn test_ancestor_ids() {
        let mut tree = Tree::new();

        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2 = tree.insert(Node::new(2), UnderNode(&node_1)).unwrap();
        let node_3 = tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let ancestor_ids = tree.ancestor_ids(&root_id).unwrap();
        assert_eq!(ancestor_ids.count(), 0);

        let data = [0];
        for (index, node_id) in tree.ancestor_ids(&node_1).unwrap().enumerate() {
            assert_eq!(tree.get(node_id).unwrap().data(), &data[index]);
        }

        let data = [1, 0];
        for (index, node_id) in tree.ancestor_ids(&node_2).unwrap().enumerate() {
            assert_eq!(tree.get(node_id).unwrap().data(), &data[index]);
        }

        let data = [1, 0];
        for (index, node_id) in tree.ancestor_ids(&node_3).unwrap().enumerate() {
            assert_eq!(tree.get(node_id).unwrap().data(), &data[index]);
        }
    }

    #[test]
    fn test_ancestor_ids_clone() {
        let mut tree = Tree::new();

        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2 = tree.insert(Node::new(2), UnderNode(&node_1)).unwrap();

        let node_2_ancestor_ids = tree.ancestor_ids(&node_2).unwrap();
        let node_2_ancestor_ids_clone = node_2_ancestor_ids.clone();

        // Clone is a separate entity
        let data = [1, 0];
        for (index, (node_id, clone_it_node_id)) in node_2_ancestor_ids
            .zip(node_2_ancestor_ids_clone)
            .enumerate()
        {
            assert_eq!(tree.get(node_id).unwrap().data(), &data[index]);
            assert_eq!(tree.get(clone_it_node_id).unwrap().data(), &data[index]);
        }

        // State is copied over to clone
        let mut node_2_ancestor_ids = tree.ancestor_ids(&node_2).unwrap();

        node_2_ancestor_ids.next();

        let mut node_2_ancestor_ids_clone = node_2_ancestor_ids.clone();

        assert_eq!(node_2_ancestor_ids_clone.next(), Some(&root_id));
    }

    #[test]
    fn test_children() {
        let mut tree = Tree::new();

        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2 = tree.insert(Node::new(2), UnderNode(&node_1)).unwrap();
        let node_3 = tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let data = [1];
        for (index, node) in tree.children(&root_id).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }

        let data = [2, 3];
        for (index, node) in tree.children(&node_1).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }

        let children = tree.children(&node_2).unwrap();
        assert_eq!(children.count(), 0);

        let children = tree.children(&node_3).unwrap();
        assert_eq!(children.count(), 0);
    }

    #[test]
    fn test_children_clone() {
        let mut tree = Tree::new();

        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();

        let root_children = tree.children(&root_id).unwrap();
        let root_children_clone = root_children.clone();

        // Clone is a separate entity
        let data = [1, 2];
        for (index, (node, clone_it_node)) in root_children.zip(root_children_clone).enumerate() {
            assert_eq!(node.data(), &data[index]);
            assert_eq!(clone_it_node.data(), &data[index]);
        }

        // State is copied over to clone
        let mut root_children = tree.children(&root_id).unwrap();

        root_children.next();

        let mut root_children_clone = root_children.clone();

        assert_eq!(root_children_clone.next(), Some(&Node::new(2)));
    }

    #[test]
    fn test_children_ids() {
        let mut tree = Tree::new();

        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2 = tree.insert(Node::new(2), UnderNode(&node_1)).unwrap();
        let node_3 = tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let data = [1];
        for (index, node_id) in tree.children_ids(&root_id).unwrap().enumerate() {
            assert_eq!(tree.get(node_id).unwrap().data(), &data[index]);
        }

        let data = [2, 3];
        for (index, node_id) in tree.children_ids(&node_1).unwrap().enumerate() {
            assert_eq!(tree.get(node_id).unwrap().data(), &data[index]);
        }

        let children_ids = tree.children_ids(&node_2).unwrap();
        assert_eq!(children_ids.count(), 0);

        let children_ids = tree.children_ids(&node_3).unwrap();
        assert_eq!(children_ids.count(), 0);
    }

    #[test]
    fn test_children_ids_clone() {
        let mut tree = Tree::new();

        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2 = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();

        let root_children_ids = tree.children_ids(&root_id).unwrap();
        let root_children_ids_clone = root_children_ids.clone();

        // Clone is a separate entity
        let data = [1, 2];
        for (index, (node_id, clone_it_node_id)) in
            root_children_ids.zip(root_children_ids_clone).enumerate()
        {
            assert_eq!(tree.get(node_id).unwrap().data(), &data[index]);
            assert_eq!(tree.get(clone_it_node_id).unwrap().data(), &data[index]);
        }

        // State is copied over to clone
        let mut root_children_ids = tree.children_ids(&root_id).unwrap();

        root_children_ids.next();

        let mut root_children_ids_clone = root_children_ids.clone();

        assert_eq!(root_children_ids_clone.next(), Some(&node_2));
    }

    #[test]
    fn test_pre_order_traversal() {
        let mut tree = Tree::new();

        //      0
        //     / \
        //    1   2
        //   /
        //  3
        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2 = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        let node_3 = tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let data = [0, 1, 3, 2];
        for (index, node) in tree.traverse_pre_order(&root_id).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }

        let data = [1, 3];
        for (index, node) in tree.traverse_pre_order(&node_1).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }

        let data = [2];
        for (index, node) in tree.traverse_pre_order(&node_2).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }

        let data = [3];
        for (index, node) in tree.traverse_pre_order(&node_3).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }
    }

    #[test]
    fn test_pre_order_traversal_clone() {
        let mut tree = Tree::new();

        //      0
        //     / \
        //    1   2
        //   /
        //  3
        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let traversal_from_root = tree.traverse_pre_order(&root_id).unwrap();
        let traversal_from_root_clone = traversal_from_root.clone();

        // Clone is a separate entity
        let data = [0, 1, 3, 2];
        for (index, (node, clone_it_node)) in traversal_from_root
            .zip(traversal_from_root_clone)
            .enumerate()
        {
            assert_eq!(node.data(), &data[index]);
            assert_eq!(clone_it_node.data(), &data[index]);
        }

        // State is copied over to clone
        let mut traversal_from_root = tree.traverse_pre_order(&root_id).unwrap();

        traversal_from_root.next();

        let mut traversal_from_root_clone = traversal_from_root.clone();

        assert_eq!(traversal_from_root_clone.next(), Some(&Node::new(1)));
    }

    #[test]
    fn test_pre_order_traversal_ids() {
        let mut tree = Tree::new();

        //      0
        //     / \
        //    1   2
        //   /
        //  3
        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2 = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        let node_3 = tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let data = [0, 1, 3, 2];
        for (index, node_id) in tree.traverse_pre_order_ids(&root_id).unwrap().enumerate() {
            assert_eq!(tree.get(&node_id).unwrap().data(), &data[index]);
        }

        let data = [1, 3];
        for (index, node_id) in tree.traverse_pre_order_ids(&node_1).unwrap().enumerate() {
            assert_eq!(tree.get(&node_id).unwrap().data(), &data[index]);
        }

        let data = [2];
        for (index, node_id) in tree.traverse_pre_order_ids(&node_2).unwrap().enumerate() {
            assert_eq!(tree.get(&node_id).unwrap().data(), &data[index]);
        }

        let data = [3];
        for (index, node_id) in tree.traverse_pre_order_ids(&node_3).unwrap().enumerate() {
            assert_eq!(tree.get(&node_id).unwrap().data(), &data[index]);
        }
    }

    #[test]
    fn test_pre_order_traversal_ids_clone() {
        let mut tree = Tree::new();

        //      0
        //     / \
        //    1   2
        //   /
        //  3
        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let traversal_from_root_ids = tree.traverse_pre_order_ids(&root_id).unwrap();
        let traversal_from_root_ids_clone = traversal_from_root_ids.clone();

        // Clone is a separate entity
        let data = [0, 1, 3, 2];
        for (index, (node_id, clone_it_node_id)) in traversal_from_root_ids
            .zip(traversal_from_root_ids_clone)
            .enumerate()
        {
            assert_eq!(tree.get(&node_id).unwrap().data(), &data[index]);
            assert_eq!(tree.get(&clone_it_node_id).unwrap().data(), &data[index]);
        }

        // State is copied over to clone
        let mut traversal_from_root_ids = tree.traverse_pre_order_ids(&root_id).unwrap();

        traversal_from_root_ids.next();

        let mut traversal_from_root_ids_clone = traversal_from_root_ids.clone();

        assert_eq!(traversal_from_root_ids_clone.next(), Some(node_1));
    }

    #[test]
    fn test_post_order_traversal() {
        let mut tree = Tree::new();

        //      0
        //     / \
        //    1   2
        //   /
        //  3
        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2 = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        let node_3 = tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let data = [3, 1, 2, 0];
        for (index, node) in tree.traverse_post_order(&root_id).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }

        let data = [3, 1];
        for (index, node) in tree.traverse_post_order(&node_1).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }

        let data = [2];
        for (index, node) in tree.traverse_post_order(&node_2).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }

        let data = [3];
        for (index, node) in tree.traverse_post_order(&node_3).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }
    }

    #[test]
    fn test_post_order_traversal_clone() {
        let mut tree = Tree::new();

        //      0
        //     / \
        //    1   2
        //   /
        //  3
        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let traverse_from_root = tree.traverse_post_order(&root_id).unwrap();
        let traverse_from_root_clone = traverse_from_root.clone();

        // Clone is a separate entity
        let data = [3, 1, 2, 0];
        for (index, (node, clone_it_node)) in
            traverse_from_root.zip(traverse_from_root_clone).enumerate()
        {
            assert_eq!(node.data(), &data[index]);
            assert_eq!(clone_it_node.data(), &data[index]);
        }

        // State is copied over to clone
        let mut traversal_from_root = tree.traverse_post_order(&root_id).unwrap();

        traversal_from_root.next();

        let mut traversal_from_root_ids = traversal_from_root.clone();

        assert_eq!(traversal_from_root_ids.next(), Some(&Node::new(1)));
    }

    #[test]
    fn test_post_order_traversal_ids() {
        let mut tree = Tree::new();

        //      0
        //     / \
        //    1   2
        //   /
        //  3
        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2 = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        let node_3 = tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let data = [3, 1, 2, 0];
        for (index, node_id) in tree.traverse_post_order_ids(&root_id).unwrap().enumerate() {
            assert_eq!(tree.get(&node_id).unwrap().data(), &data[index]);
        }

        let data = [3, 1];
        for (index, node_id) in tree.traverse_post_order_ids(&node_1).unwrap().enumerate() {
            assert_eq!(tree.get(&node_id).unwrap().data(), &data[index]);
        }

        let data = [2];
        for (index, node_id) in tree.traverse_post_order_ids(&node_2).unwrap().enumerate() {
            assert_eq!(tree.get(&node_id).unwrap().data(), &data[index]);
        }

        let data = [3];
        for (index, node_id) in tree.traverse_post_order_ids(&node_3).unwrap().enumerate() {
            assert_eq!(tree.get(&node_id).unwrap().data(), &data[index]);
        }
    }

    #[test]
    fn test_post_order_traversal_ids_clone() {
        let mut tree = Tree::new();

        //      0
        //     / \
        //    1   2
        //   /
        //  3
        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let traverse_from_root_ids = tree.traverse_post_order_ids(&root_id).unwrap();
        let traverse_from_root_ids_clone = traverse_from_root_ids.clone();

        // Clone is a separate entity
        let data = [3, 1, 2, 0];
        for (index, (node_id, clone_it_node_id)) in traverse_from_root_ids
            .zip(traverse_from_root_ids_clone)
            .enumerate()
        {
            assert_eq!(tree.get(&node_id).unwrap().data(), &data[index]);
            assert_eq!(tree.get(&clone_it_node_id).unwrap().data(), &data[index]);
        }

        // State is copied over from clone
        let mut traversal_from_root_ids = tree.traverse_post_order_ids(&root_id).unwrap();

        traversal_from_root_ids.next();

        let mut traversal_from_root_ids_clone = traversal_from_root_ids.clone();

        assert_eq!(traversal_from_root_ids_clone.next(), Some(node_1));
    }

    #[test]
    fn test_level_order_traversal() {
        let mut tree = Tree::new();

        //      0
        //     / \
        //    1   2
        //   /
        //  3
        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2 = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        let node_3 = tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let data = [0, 1, 2, 3];
        for (index, node) in tree.traverse_level_order(&root_id).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }

        let data = [1, 3];
        for (index, node) in tree.traverse_level_order(&node_1).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }

        let data = [2];
        for (index, node) in tree.traverse_level_order(&node_2).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }

        let data = [3];
        for (index, node) in tree.traverse_level_order(&node_3).unwrap().enumerate() {
            assert_eq!(node.data(), &data[index]);
        }
    }

    #[test]
    fn test_level_order_traversal_clone() {
        let mut tree = Tree::new();

        //      0
        //     / \
        //    1   2
        //   /
        //  3
        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let traverse_from_root = tree.traverse_level_order(&root_id).unwrap();
        let traverse_from_root_clone = traverse_from_root.clone();

        // Clone is a separate entity
        let data = [0, 1, 2, 3];
        for (index, (node, clone_it_node)) in
            traverse_from_root.zip(traverse_from_root_clone).enumerate()
        {
            assert_eq!(node.data(), &data[index]);
            assert_eq!(clone_it_node.data(), &data[index]);
        }

        // State is copied over to clone
        let mut traversal_from_root = tree.traverse_level_order(&root_id).unwrap();

        traversal_from_root.next();

        let mut traversal_from_root_clone = traversal_from_root.clone();

        assert_eq!(traversal_from_root_clone.next(), Some(&Node::new(1)));
    }

    #[test]
    fn test_level_order_traversal_ids() {
        let mut tree = Tree::new();

        //      0
        //     / \
        //    1   2
        //   /
        //  3
        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        let node_2 = tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        let node_3 = tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let data = [0, 1, 2, 3];
        for (index, node_id) in tree.traverse_level_order_ids(&root_id).unwrap().enumerate() {
            assert_eq!(tree.get(&node_id).unwrap().data(), &data[index]);
        }

        let data = [1, 3];
        for (index, node_id) in tree.traverse_level_order_ids(&node_1).unwrap().enumerate() {
            assert_eq!(tree.get(&node_id).unwrap().data(), &data[index]);
        }

        let data = [2];
        for (index, node_id) in tree.traverse_level_order_ids(&node_2).unwrap().enumerate() {
            assert_eq!(tree.get(&node_id).unwrap().data(), &data[index]);
        }

        let data = [3];
        for (index, node_id) in tree.traverse_level_order_ids(&node_3).unwrap().enumerate() {
            assert_eq!(tree.get(&node_id).unwrap().data(), &data[index]);
        }
    }

    #[test]
    fn test_level_order_traversal_ids_clone() {
        let mut tree = Tree::new();

        //      0
        //     / \
        //    1   2
        //   /
        //  3
        let root_id = tree.insert(Node::new(0), AsRoot).unwrap();
        let node_1 = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
        tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
        tree.insert(Node::new(3), UnderNode(&node_1)).unwrap();

        let traverse_from_root_ids = tree.traverse_level_order_ids(&root_id).unwrap();
        let traverse_from_root_ids_clone = traverse_from_root_ids.clone();

        // Clone is a separate entity
        let data = [0, 1, 2, 3];
        for (index, (node_id, clone_it_node_id)) in traverse_from_root_ids
            .zip(traverse_from_root_ids_clone)
            .enumerate()
        {
            assert_eq!(tree.get(&node_id).unwrap().data(), &data[index]);
            assert_eq!(tree.get(&clone_it_node_id).unwrap().data(), &data[index]);
        }

        // State is copied over to clone
        let mut traversal_from_root_ids = tree.traverse_level_order_ids(&root_id).unwrap();

        traversal_from_root_ids.next();

        let mut traversal_from_root_ids_clone = traversal_from_root_ids.clone();

        assert_eq!(traversal_from_root_ids_clone.next(), Some(node_1));
    }
}
