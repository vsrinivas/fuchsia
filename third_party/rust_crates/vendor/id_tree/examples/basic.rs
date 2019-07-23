extern crate id_tree;

use id_tree::*;

fn main() {
    use id_tree::InsertBehavior::*;

    //      0
    //     / \
    //    1   2
    //   / \
    //  3   4
    let mut tree: Tree<i32> = TreeBuilder::new().with_node_capacity(5).build();

    let root_id: NodeId = tree.insert(Node::new(0), AsRoot).unwrap();
    let child_id: NodeId = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
    tree.insert(Node::new(3), UnderNode(&child_id)).unwrap();
    tree.insert(Node::new(4), UnderNode(&child_id)).unwrap();

    println!("Debug: {:?}", tree);

    println!("Post-order:");

    for node in tree.traverse_post_order(&root_id).unwrap() {
        print!("{}, ", node.data());
    }

    println!();
    println!("Pre-order:");

    for node in tree.traverse_pre_order(&root_id).unwrap() {
        print!("{}, ", node.data());
    }

    println!();
}
