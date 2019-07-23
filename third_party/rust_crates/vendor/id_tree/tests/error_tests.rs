extern crate id_tree;

use id_tree::InsertBehavior::*;
use id_tree::MoveBehavior::*;
use id_tree::Node;
use id_tree::NodeIdError::*;
use id_tree::RemoveBehavior::*;
use id_tree::SwapBehavior::*;
use id_tree::Tree;
use id_tree::TreeBuilder;

#[test]
fn test_old_node_id() {
    let mut tree: Tree<i32> = TreeBuilder::new().build();

    let root_node = Node::new(1);

    let root_id = tree.insert(root_node, AsRoot).ok().unwrap();
    let root_id_copy = root_id.clone(); // this is essential to getting the Result::Err()

    let root_node = tree.remove_node(root_id, OrphanChildren);
    assert!(root_node.is_ok());

    let root_node_again = tree.remove_node(root_id_copy, OrphanChildren);
    assert!(root_node_again.is_err());

    let error = root_node_again.err().unwrap();
    assert_eq!(error, NodeIdNoLongerValid);
}

#[test]
fn test_get_node_from_other_tree() {
    let mut tree_a: Tree<i32> = TreeBuilder::new().build();
    let tree_b: Tree<i32> = TreeBuilder::new().build();

    let root_node_a = Node::new(1);
    let root_node_id_a = tree_a.insert(root_node_a, AsRoot).unwrap();

    let root_node_a = tree_a.get(&root_node_id_a);
    let root_node_b = tree_b.get(&root_node_id_a); //note use of wrong tree

    assert!(root_node_a.is_ok());
    assert!(root_node_b.is_err());
    assert_eq!(root_node_b.err().unwrap(), InvalidNodeIdForTree);
}

#[test]
fn test_get_mut_node_from_other_tree() {
    let mut tree_a: Tree<i32> = TreeBuilder::new().build();
    let mut tree_b: Tree<i32> = TreeBuilder::new().build();

    let root_node_a = Node::new(1);
    let root_node_id_a = tree_a.insert(root_node_a, AsRoot).unwrap();

    let root_node_a = tree_a.get_mut(&root_node_id_a);
    let root_node_b = tree_b.get_mut(&root_node_id_a); //note use of wrong tree

    assert!(root_node_a.is_ok());
    assert!(root_node_b.is_err());
    assert_eq!(root_node_b.err().unwrap(), InvalidNodeIdForTree);
}

#[test]
fn test_remove_node_lift_children_from_other_tree() {
    let mut tree_a: Tree<i32> = TreeBuilder::new().build();
    let mut tree_b: Tree<i32> = TreeBuilder::new().build();

    let root_node_id_a = tree_a.insert(Node::new(1), AsRoot).unwrap();

    // note use of wrong tree
    let root_node_b = tree_b.remove_node(root_node_id_a, LiftChildren);
    assert!(root_node_b.is_err());

    let error = root_node_b.err().unwrap();
    assert_eq!(error, InvalidNodeIdForTree);
}

#[test]
fn test_remove_node_orphan_children_from_other_tree() {
    let mut tree_a: Tree<i32> = TreeBuilder::new().build();
    let mut tree_b: Tree<i32> = TreeBuilder::new().build();

    let root_node_id_a = tree_a.insert(Node::new(1), AsRoot).unwrap();

    // note use of wrong tree
    let root_node_b = tree_b.remove_node(root_node_id_a, OrphanChildren);
    assert!(root_node_b.is_err());

    let error = root_node_b.err().unwrap();
    assert_eq!(error, InvalidNodeIdForTree);
}

#[test]
fn test_remove_node_remove_children_from_other_tree() {
    let mut tree_a: Tree<i32> = TreeBuilder::new().build();
    let mut tree_b: Tree<i32> = TreeBuilder::new().build();

    let root_node_id_a = tree_a.insert(Node::new(1), AsRoot).unwrap();

    // note use of wrong tree
    let root_node_b = tree_b.remove_node(root_node_id_a, DropChildren);
    assert!(root_node_b.is_err());

    let error = root_node_b.err().unwrap();
    assert_eq!(error, InvalidNodeIdForTree);
}

#[test]
fn test_move_node_into_other_tree() {
    let mut tree_a: Tree<i32> = TreeBuilder::new().build();
    let mut tree_b: Tree<i32> = TreeBuilder::new().build();

    let root_node_a = Node::new(1);
    let root_node_id_a = tree_a.insert(root_node_a, AsRoot).unwrap();

    let root_node_b = Node::new(1);
    let root_node_id_b = tree_b.insert(root_node_b, AsRoot).unwrap();

    // note use of invalid parent
    let result = tree_a.move_node(&root_node_id_a, ToParent(&root_node_id_b));
    assert!(result.is_err());

    let error = result.err().unwrap();
    assert_eq!(error, InvalidNodeIdForTree);
}

#[test]
fn test_move_node_from_other_tree() {
    let mut tree_a: Tree<i32> = TreeBuilder::new().build();
    let mut tree_b: Tree<i32> = TreeBuilder::new().build();

    let root_node_a = Node::new(1);
    let root_node_id_a = tree_a.insert(root_node_a, AsRoot).unwrap();

    let root_node_b = Node::new(1);
    let root_node_id_b = tree_b.insert(root_node_b, AsRoot).unwrap();

    // note use of invalid child
    let result = tree_a.move_node(&root_node_id_b, ToParent(&root_node_id_a));
    assert!(result.is_err());

    let error = result.err().unwrap();
    assert_eq!(error, InvalidNodeIdForTree);
}

#[test]
fn test_move_node_to_root_by_invalid_id() {
    let mut tree_a: Tree<i32> = TreeBuilder::new().build();
    let mut tree_b: Tree<i32> = TreeBuilder::new().build();

    let root_node_a = Node::new(1);
    let _ = tree_a.insert(root_node_a, AsRoot).unwrap();

    let root_node_b = Node::new(1);
    let root_node_id_b = tree_b.insert(root_node_b, AsRoot).unwrap();

    let result = tree_a.move_node(&root_node_id_b, ToRoot);
    assert!(result.is_err());

    let error = result.err().unwrap();
    assert_eq!(error, InvalidNodeIdForTree);
}

#[test]
fn test_sort_by_invalid_id() {
    let mut tree_a: Tree<i32> = TreeBuilder::new().build();
    let mut tree_b: Tree<i32> = TreeBuilder::new().build();

    let root_node_a = Node::new(1);
    let _ = tree_a.insert(root_node_a, AsRoot).unwrap();

    let root_node_b = Node::new(1);
    let root_node_id_b = tree_b.insert(root_node_b, AsRoot).unwrap();

    let result = tree_a.sort_children_by(&root_node_id_b, |a, b| a.data().cmp(b.data()));
    assert!(result.is_err());

    let error = result.err().unwrap();
    assert_eq!(error, InvalidNodeIdForTree);
}

#[test]
fn test_sort_by_data_invalid_id() {
    let mut tree_a: Tree<i32> = TreeBuilder::new().build();
    let mut tree_b: Tree<i32> = TreeBuilder::new().build();

    let root_node_a = Node::new(1);
    let _ = tree_a.insert(root_node_a, AsRoot).unwrap();

    let root_node_b = Node::new(1);
    let root_node_id_b = tree_b.insert(root_node_b, AsRoot).unwrap();

    let result = tree_a.sort_children_by_data(&root_node_id_b);
    assert!(result.is_err());

    let error = result.err().unwrap();
    assert_eq!(error, InvalidNodeIdForTree);
}

#[test]
fn test_sort_by_key_invalid_id() {
    let mut tree_a: Tree<i32> = TreeBuilder::new().build();
    let mut tree_b: Tree<i32> = TreeBuilder::new().build();

    let root_node_a = Node::new(1);
    let _ = tree_a.insert(root_node_a, AsRoot).unwrap();

    let root_node_b = Node::new(1);
    let root_node_id_b = tree_b.insert(root_node_b, AsRoot).unwrap();

    let result = tree_a.sort_children_by_key(&root_node_id_b, |x| *x.data());
    assert!(result.is_err());

    let error = result.err().unwrap();
    assert_eq!(error, InvalidNodeIdForTree);
}

#[test]
fn test_swap_sub_trees_of_different_trees() {
    let mut tree_a: Tree<i32> = TreeBuilder::new().build();
    let mut tree_b: Tree<i32> = TreeBuilder::new().build();

    let root_node_a = Node::new(1);
    let root_node_id_a = tree_a.insert(root_node_a, AsRoot).unwrap();

    let root_node_b = Node::new(1);
    let root_node_id_b = tree_b.insert(root_node_b, AsRoot).unwrap();

    // note use of invalid child
    let result = tree_a.swap_nodes(&root_node_id_b, &root_node_id_a, TakeChildren);
    assert!(result.is_err());

    let error = result.err().unwrap();
    assert_eq!(error, InvalidNodeIdForTree);
}

#[test]
fn test_ancestors_different_trees() {
    let mut a = Tree::new();
    let b = Tree::<i32>::new();

    let root_id = a.insert(Node::new(1), AsRoot).unwrap();

    // note usage of `b` instead of `a`
    let ancestors = b.ancestors(&root_id);

    assert!(ancestors.is_err());
    let error = ancestors.err().unwrap();
    assert_eq!(error, InvalidNodeIdForTree);
}

#[test]
fn test_ancestors_old_id() {
    let mut a = Tree::new();

    let root_id = a.insert(Node::new(1), AsRoot).unwrap();
    // `.clone()` required to get this error
    let root_id_clone = root_id.clone();
    let _ = a.remove_node(root_id, DropChildren).unwrap();

    // note usage of cloned `NodeId`
    let ancestors = a.ancestors(&root_id_clone);

    assert!(ancestors.is_err());
    let error = ancestors.err().unwrap();
    assert_eq!(error, NodeIdNoLongerValid);
}

#[test]
fn test_ancestor_ids_different_trees() {
    let mut a = Tree::new();
    let b = Tree::<i32>::new();

    let root_id = a.insert(Node::new(1), AsRoot).unwrap();

    // note usage of `b` instead of `a`
    let ancestors = b.ancestor_ids(&root_id);

    assert!(ancestors.is_err());
    let error = ancestors.err().unwrap();
    assert_eq!(error, InvalidNodeIdForTree);
}

#[test]
fn test_ancestor_ids_old_id() {
    let mut a = Tree::new();

    let root_id = a.insert(Node::new(1), AsRoot).unwrap();
    // `.clone()` required to get this error
    let root_id_clone = root_id.clone();
    let _ = a.remove_node(root_id, DropChildren).unwrap();

    // note usage of cloned `NodeId`
    let ancestors = a.ancestor_ids(&root_id_clone);

    assert!(ancestors.is_err());
    let error = ancestors.err().unwrap();
    assert_eq!(error, NodeIdNoLongerValid);
}

#[test]
fn test_children_different_trees() {
    let mut a = Tree::new();
    let b = Tree::<i32>::new();

    let root_id = a.insert(Node::new(1), AsRoot).unwrap();

    // note usage of `b` instead of `a`
    let ancestors = b.children(&root_id);

    assert!(ancestors.is_err());
    let error = ancestors.err().unwrap();
    assert_eq!(error, InvalidNodeIdForTree);
}

#[test]
fn test_children_old_id() {
    let mut a = Tree::new();

    let root_id = a.insert(Node::new(1), AsRoot).unwrap();
    // `.clone()` required to get this error
    let root_id_clone = root_id.clone();
    let _ = a.remove_node(root_id, DropChildren).unwrap();

    // note usage of cloned `NodeId`
    let ancestors = a.children(&root_id_clone);

    assert!(ancestors.is_err());
    let error = ancestors.err().unwrap();
    assert_eq!(error, NodeIdNoLongerValid);
}

#[test]
fn test_children_ids_different_trees() {
    let mut a = Tree::new();
    let b = Tree::<i32>::new();

    let root_id = a.insert(Node::new(1), AsRoot).unwrap();

    // note usage of `b` instead of `a`
    let ancestors = b.children_ids(&root_id);

    assert!(ancestors.is_err());
    let error = ancestors.err().unwrap();
    assert_eq!(error, InvalidNodeIdForTree);
}

#[test]
fn test_children_ids_old_id() {
    let mut a = Tree::new();

    let root_id = a.insert(Node::new(1), AsRoot).unwrap();
    // `.clone()` required to get this error
    let root_id_clone = root_id.clone();
    let _ = a.remove_node(root_id, DropChildren).unwrap();

    // note usage of cloned `NodeId`
    let ancestors = a.children_ids(&root_id_clone);

    assert!(ancestors.is_err());
    let error = ancestors.err().unwrap();
    assert_eq!(error, NodeIdNoLongerValid);
}
