# id_tree

[![Build Status](https://travis-ci.org/iwburns/id-tree.svg?branch=master)](https://travis-ci.org/iwburns/id-tree)
[![Build status](https://ci.appveyor.com/api/projects/status/rw42btsa1i7bqcx9/branch/master?svg=true)](https://ci.appveyor.com/project/iwburns/id-tree/branch/master)
[![Coverage Status](https://coveralls.io/repos/github/iwburns/id-tree/badge.svg?branch=master)](https://coveralls.io/github/iwburns/id-tree?branch=master)
[![Docs.rs](https://docs.rs/id_tree/badge.svg)](https://docs.rs/id_tree)
[![Crates.io](https://img.shields.io/crates/v/id_tree.svg)](https://crates.io/crates/id_tree)
[![](https://tokei.rs/b1/github/iwburns/id-tree)](https://github.com/iwburns/id-tree)

A library for creating and modifying Tree structures.

## Overview
In this implementation, the `Tree` owns all of the `Node`s and all inter-`Node` relationships are
managed with `NodeId`s.

`Tree`s in this library are "just" trees.  They do not allow cycles.  They do not allow
the creation of arbitrary Graph structures.  There is no weight associated with edges between
`Node`s.  In addition, each `Node` can have an arbitrary number of child `Node`s.

It is important to note that this library does not support comparison-based `Node` insertion.
In other words, this is not a Binary Search Tree (or any other kind of search tree) library.
It is purely a library for storing data in a hierarchical manner.  The caller must know the
structure that they wish to build and then use this library to do so;  this library will not
make those structural decisions for you.

## Example Usage
```
use id_tree::*;

fn main() {
    use id_tree::InsertBehavior::*;

    //      0
    //     / \
    //    1   2
    //   / \
    //  3   4
    let mut tree: Tree<i32> = TreeBuilder::new()
        .with_node_capacity(5)
        .build();

    let root_id: NodeId = tree.insert(Node::new(0), AsRoot).unwrap();
    let child_id: NodeId = tree.insert(Node::new(1), UnderNode(&root_id)).unwrap();
    tree.insert(Node::new(2), UnderNode(&root_id)).unwrap();
    tree.insert(Node::new(3), UnderNode(&child_id)).unwrap();
    tree.insert(Node::new(4), UnderNode(&child_id)).unwrap();

    println!("Pre-order:");
    for node in tree.traverse_pre_order(&root_id).unwrap() {
        print!("{}, ", node.data());
    }
    // results in the output "0, 1, 3, 4, 2, "
}
```

## Project Goals
* Allow caller control of as many allocations as possible (through pre-allocation)
* Fast `Node` insertion and removal
* Arbitrary _Tree_ structure creation and manipulation

## Non-Goals
* Arbitrary _Graph_ structure creation and manipulation
* Comparison-based node insertion of any kind

## Contributors
* [Cecile Tonglet](https://github.com/cecton)
* [Drakulix](https://github.com/Drakulix)
* [石博文](https://github.com/sbwtw)
