// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![allow(dead_code)]

use {
    crate::label_generator::{BitstringLabelGenerator, Label},
    anyhow::{anyhow, Error},
    sha2::{Digest, Sha256},
    thiserror::Error,
};

/// An alias for hashes stored in the nodes. For leaf nodes, this will be the
/// hash of the credential metadata. For inner nodes, this will be the hash of
/// all child node hashes.
type Hash = [u8; 32];
/// An alias for a vector of sibling hashes for a particular node, the node
/// itself will be None, whereas its siblings will be Some(Hash).
type SiblingHashes = Vec<Option<Hash>>;

/// By definition the hash of an unused leaf node used when generating a parent
/// hash is [0u8; 32].
const DEFAULT_EMPTY_HASH: &[u8; 32] = &[0; 32];

#[derive(Error, Debug)]
pub enum HashTreeError {
    #[error("no available leaf nodes")]
    NoLeafNodes,
    #[error("unknown leaf label")]
    UnknownLeafLabel,
    #[error("found label but is for a non-leaf node")]
    NonLeafLabel,
    #[error("invalid tree")]
    InvalidTree { leaf_label: Label, tree_height: u8, children_per_node: u8 },
    #[error(transparent)]
    InvalidInput(#[from] anyhow::Error),
}

/// A HashTree is a representation of a Merkle Tree where each of the leaf
/// nodes contain a hash, and each parent node's hash is constructed from the
/// concatenation of its child hashes.
/// This allows the HashTree to have the property that the hash of the root can
/// be used to verify the integrity of any of the inner or leaf nodes.
#[derive(Debug)]
struct HashTree {
    height: u8,
    children_per_node: u8,
    root: Node,
    label_gen: BitstringLabelGenerator,
}

impl HashTree {
    /// Given a `height` and number of `children_per_node`, construct a full
    /// Hash Tree in which leaves will hold hash values. There will be
    /// `children_per_node^(height-1)` total leaves in which it is possible to
    /// store hashes.
    fn new(height: u8, children_per_node: u8) -> Result<Self, HashTreeError> {
        if height < 2 || children_per_node < 1 {
            return Err(HashTreeError::InvalidInput(anyhow!(
                "invalid height or children_per_node"
            )));
        }
        let label_gen = BitstringLabelGenerator::new(height, children_per_node)?;
        let root = Node::new(label_gen.root(), children_per_node, height - 1, &label_gen)?;
        let hash_tree = Self { height, children_per_node, root, label_gen };
        Ok(hash_tree)
    }

    /// Returns the root hash of the tree.
    fn get_root_hash<'a>(&'a self) -> Result<&'a Hash, HashTreeError> {
        Ok(&self.root.hash.as_ref().unwrap_or(DEFAULT_EMPTY_HASH))
    }

    /// Attempts to find an unused leaf node.
    /// This should be used prior to update_leaf_hash when inserting a new leaf
    /// metadata, as the label (and its auxiliary hashes) are required for CR50
    /// to calculate the updated root.
    fn get_free_leaf_label(&self) -> Result<Label, HashTreeError> {
        match self.root.get_free_leaf_label() {
            GetFreeLeafOutcome::NoLeafNodes => Err(HashTreeError::NoLeafNodes),
            GetFreeLeafOutcome::OnLeafPath(leaf_label) => Ok(leaf_label),
        }
    }

    /// Returns all sibling hashes along the path from leaf -> root.
    /// These will be returned as a vector of vector of hashes. Each outer layer
    /// entry will correspond with a layer of the tree from bottom to top, and
    /// each inner layer entry will contain sibling nodes from left to right.
    /// Hashes for nodes along the path to the leaf (including the leaf itself)
    /// _will not_ be returned, but instead _will_ have an empty vector where
    /// the calculated hash should be. This is necessary because we don't have
    /// knowledge of _where_ the leaf-path child is, and the order of children
    /// in the calculation of the parent hash is important.
    /// TODO(arkay): Update this to output hashes as CR50 expects them.
    fn get_auxiliary_hashes(&self, label: &Label) -> Result<Vec<SiblingHashes>, HashTreeError> {
        match self.root.find_siblings(label) {
            FindSiblingsOutcome::LeafFound(aux_hashes) => Ok(aux_hashes),
            FindSiblingsOutcome::NonLeafNodeFound => Err(HashTreeError::NonLeafLabel),
            FindSiblingsOutcome::NotFound => Err(HashTreeError::UnknownLeafLabel),
        }
    }

    /// Returns the metadata hash associated with this label, or returns an
    /// error if a matching leaf node is not found.
    /// If the leaf_label is associated with an unused/empty leaf, the metadata
    /// hash returned with be all 0s, as per the default expectations for unused
    /// leaves.
    fn get_leaf_hash(&self, label: &Label) -> Result<&Hash, HashTreeError> {
        match self.root.read_leaf(label) {
            ReadLeafOutcome::LeafFound(hash) => Ok(hash),
            ReadLeafOutcome::NonLeafNodeFound => Err(HashTreeError::NonLeafLabel),
            ReadLeafOutcome::NotFound => Err(HashTreeError::UnknownLeafLabel),
        }
    }

    /// Updates the metadata hash associated with this label, or returns an
    /// error if a matching leaf node is not found.
    fn update_leaf_hash(&mut self, label: &Label, metadata: Hash) -> Result<bool, HashTreeError> {
        match self.root.write_leaf(label, &Some(metadata)) {
            WriteLeafOutcome::LeafUpdated => Ok(true),
            WriteLeafOutcome::NonLeafNodeFound => Err(HashTreeError::NonLeafLabel),
            WriteLeafOutcome::NotFound => Err(HashTreeError::UnknownLeafLabel),
        }
    }

    /// Deletes the metadata hash associated with this label, or returns an
    /// error if a matching leaf node is not found.
    fn delete_leaf(&mut self, label: &Label) -> Result<(), HashTreeError> {
        match self.root.write_leaf(label, &None) {
            WriteLeafOutcome::LeafUpdated => Ok(()),
            WriteLeafOutcome::NonLeafNodeFound => Err(HashTreeError::NonLeafLabel),
            WriteLeafOutcome::NotFound => Err(HashTreeError::UnknownLeafLabel),
        }
    }

    /// Clears all leaves from a tree and reinstantiates the tree's structure.
    fn reset(&mut self) -> Result<(), HashTreeError> {
        // Resetting a tree should be as simple as dropping the reference to
        // the root node, which will cause it and its subtree to be cleaned up.
        self.root = Node::new(
            self.label_gen.root(),
            self.children_per_node,
            self.height - 1,
            &self.label_gen,
        )?;
        Ok(())
    }

    /// Verifies the tree by ensuring that all inner nodes' hashes are
    /// equivalent to the hash of their child nodes.
    fn verify_tree(&self) -> Result<(), HashTreeError> {
        self.root.verify().map_err(|label| HashTreeError::InvalidTree {
            leaf_label: label,
            tree_height: self.height,
            children_per_node: self.children_per_node,
        })
    }
}

enum GetFreeLeafOutcome {
    NoLeafNodes,
    OnLeafPath(Label),
}

enum WriteLeafOutcome {
    NonLeafNodeFound,
    LeafUpdated,
    NotFound,
}

enum ReadLeafOutcome<'a> {
    NonLeafNodeFound,
    LeafFound(&'a Hash),
    NotFound,
}

enum FindSiblingsOutcome {
    NonLeafNodeFound,
    LeafFound(Vec<SiblingHashes>),
    NotFound,
}

#[derive(Debug, Eq, PartialEq)]
struct Node {
    label: Label,
    hash: Option<Hash>,
    children: Vec<Node>,
}

impl Node {
    /// Creates a new node and `children_per_node` children if layers > 1,
    /// also updates the internal hash to be the hash of its children.
    fn new(
        label: Label,
        children_per_node: u8,
        layers: u8,
        label_gen: &BitstringLabelGenerator,
    ) -> Result<Self, Error> {
        let mut node = Self { label: label, hash: None, children: Vec::new() };
        if layers == 0 {
            return Ok(node);
        }

        let mut hasher = Sha256::new();
        for child_index in 0..children_per_node {
            let child = Node::new(
                label_gen.child_label(&node.label, child_index)?,
                children_per_node,
                layers - 1,
                label_gen,
            )?;
            hasher.update(&child.hash.as_ref().unwrap_or(DEFAULT_EMPTY_HASH));
            node.children.push(child);
        }
        node.hash = Some(hasher.finalize().into());
        Ok(node)
    }

    fn is_leaf(&self) -> bool {
        self.children.is_empty()
    }

    fn is_unused(&self) -> bool {
        self.hash.is_none()
    }

    /// Finds an unused leaf, if it exists, and sets the Hash `value` of that leaf,
    /// recalculating the hash of the tree if the tree was changed.
    /// If no unused leaf was found, returns GetFreeLeafOutcome::NoLeafNodes.
    fn get_free_leaf_label(&self) -> GetFreeLeafOutcome {
        // If we're a leaf and unused, set our value to the passed in value.
        if self.is_leaf() && self.is_unused() {
            // TODO(arkay): Should we reserve this label in some way?
            // If not, we should add a lock.
            return GetFreeLeafOutcome::OnLeafPath(self.label);
        }

        // Recursively look for a child leaf to reserve.
        for child in self.children.iter() {
            match child.get_free_leaf_label() {
                GetFreeLeafOutcome::OnLeafPath(leaf_label) => {
                    // Found a free leaf, return it.
                    return GetFreeLeafOutcome::OnLeafPath(leaf_label);
                }
                GetFreeLeafOutcome::NoLeafNodes => continue,
            }
        }
        GetFreeLeafOutcome::NoLeafNodes
    }

    fn read_leaf<'a>(&'a self, label: &Label) -> ReadLeafOutcome<'a> {
        match (self.label, &self.hash) {
            (node_label, Some(hash)) if node_label == *label => {
                return ReadLeafOutcome::LeafFound(hash);
            }
            (node_label, None) if node_label == *label => {
                // Node labels should be unique so if we found a non-leaf node
                // of the same label, we short-circuit.
                return ReadLeafOutcome::NonLeafNodeFound;
            }
            (_, _) => {
                // Continue with checking the children.
            }
        }

        for child in self.children.iter() {
            match child.read_leaf(label) {
                ReadLeafOutcome::NotFound => {
                    // Continue to check other branches
                }
                ret => return ret,
            }
        }

        ReadLeafOutcome::NotFound
    }

    /// Find siblings of all nodes along the path to the leaf label node.
    /// This recursively attempts to find a leaf node with `label` and returns
    /// all hashes of sibling nodes from left to right, bottom to top, with
    /// None indicating the index of the node that is on the path to the leaf
    /// (or the leaf itself).
    fn find_siblings(&self, label: &Label) -> FindSiblingsOutcome {
        if self.label == *label {
            if self.is_leaf() {
                return FindSiblingsOutcome::LeafFound(vec![]);
            } else {
                // Node labels should be unique so if we found a non-leaf node
                // of the same label, we short-circuit.
                return FindSiblingsOutcome::NonLeafNodeFound;
            }
        }

        let mut on_leaf_path = false;
        let mut sibling_hashes = vec![];
        let mut auxiliary_hashes = vec![];
        for child in self.children.iter() {
            // If we already found the leaf on this path, we still need to
            // gather siblings at this level but we don't need to recurse into
            // the children.
            if on_leaf_path {
                sibling_hashes.push(Some(child.hash.as_ref().unwrap_or(DEFAULT_EMPTY_HASH)));
                continue;
            }

            match child.find_siblings(label) {
                FindSiblingsOutcome::NonLeafNodeFound => {
                    // Short circuit.
                    return FindSiblingsOutcome::NonLeafNodeFound;
                }
                FindSiblingsOutcome::LeafFound(child_aux_hashes) => {
                    // Mark that we found the leaf on this path so we should
                    // return siblings.
                    on_leaf_path = true;
                    auxiliary_hashes = child_aux_hashes;
                    // Insert a placeholder empty vector to mark where in the
                    // left -> right flow the path to the leaf was found.
                    sibling_hashes.push(None);
                }
                FindSiblingsOutcome::NotFound => {
                    // Continue to check other branches, but store all children
                    // at this level by reference, in case this is on the path
                    // to the leaf.
                    sibling_hashes.push(Some(child.hash.as_ref().unwrap_or(DEFAULT_EMPTY_HASH)));
                }
            }
        }

        if on_leaf_path {
            // Clone all the sibling hashes now that we know we need to provide
            // them as aux hashes.
            // FIXME: This is definitely wrong.
            let cloned_hashes = sibling_hashes
                .into_iter()
                .map(|sibling_hash| (sibling_hash).map(|hash| hash.clone()))
                .collect();
            auxiliary_hashes.push(cloned_hashes);
            FindSiblingsOutcome::LeafFound(auxiliary_hashes)
        } else {
            FindSiblingsOutcome::NotFound
        }
    }

    fn write_leaf(&mut self, label: &Label, value: &Option<Hash>) -> WriteLeafOutcome {
        if self.label == *label {
            if self.is_leaf() {
                self.hash = value.clone();
                return WriteLeafOutcome::LeafUpdated;
            } else {
                // Node labels should be unique so if we found a non-leaf node
                // of the same label, we short-circuit.
                return WriteLeafOutcome::NonLeafNodeFound;
            }
        }

        // First iteration, recursively looking for the correct leaf node.
        let mut iter = self.children.iter_mut();
        loop {
            match iter.next() {
                Some(child) => {
                    match child.write_leaf(label, value) {
                        WriteLeafOutcome::LeafUpdated => {
                            // Found and updated the leaf, break out.
                            break;
                        }
                        WriteLeafOutcome::NonLeafNodeFound => {
                            // Short-circuit for non-leaf node label.
                            return WriteLeafOutcome::NonLeafNodeFound;
                        }
                        WriteLeafOutcome::NotFound => continue,
                    }
                }
                // Return early if we exhausted all children and did not find
                // the leaf to update.
                None => return WriteLeafOutcome::NotFound,
            }
        }

        // If necessary, second iteration, updates the hash.
        let mut hasher = Sha256::new();
        for child in self.children.iter() {
            hasher.update(child.hash.as_ref().unwrap_or(DEFAULT_EMPTY_HASH));
        }
        self.hash = Some(hasher.finalize().into());
        WriteLeafOutcome::LeafUpdated
    }

    /// Returns Ok(()) if the tree is valid, otherwise, returns Err(label)
    /// where label is the label of the first node with an mismatched hash.
    fn verify(&self) -> Result<(), Label> {
        // Leaf nodes store a hash supplied by the user and are always valid.
        if self.is_leaf() {
            return Ok(());
        }

        let mut hasher = Sha256::new();
        for child in self.children.iter() {
            hasher.update(child.hash.as_ref().unwrap_or(DEFAULT_EMPTY_HASH));
        }
        let calculated_hash: [u8; 32] = hasher.finalize().into();
        if *self.hash.as_ref().unwrap_or(DEFAULT_EMPTY_HASH) != calculated_hash {
            return Err(self.label);
        }
        for child in self.children.iter() {
            if let Err(label) = child.verify() {
                return Err(label);
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::label_generator::BAD_LABEL;

    fn create_tree(height: u8, children_per_node: u8) -> (HashTree, u8) {
        (
            HashTree::new(height, children_per_node).unwrap(),
            children_per_node.pow((height - 1).into()),
        )
    }

    #[test]
    fn test_tree() {
        // This tree can hold 8 leaves.
        let (mut tree, _) = create_tree(4, 2);
        assert!(tree.verify_tree().is_ok());

        for i in 1..6 {
            let node_label = tree.get_free_leaf_label().unwrap();
            tree.update_leaf_hash(&node_label, [i; 32]).unwrap();
            assert_eq!(&[i; 32], tree.get_leaf_hash(&node_label).unwrap());
        }
        assert!(tree.verify_tree().is_ok());
    }

    #[test]
    fn test_tree_bad_height() {
        assert!(HashTree::new(1, 2).is_err());
    }

    #[test]
    fn test_tree_bad_children_per_node() {
        assert!(HashTree::new(4, 0).is_err());
    }

    #[test]
    fn test_large_tree() {
        // This tree can hold 64 leaves.
        let (mut tree, max_leaves) = create_tree(4, 4);
        assert!(tree.verify_tree().is_ok());

        for i in 1..=max_leaves {
            let node_label = tree.get_free_leaf_label().unwrap();
            tree.update_leaf_hash(&node_label, [i; 32]).unwrap();
            assert_eq!(&[i; 32], tree.get_leaf_hash(&node_label).unwrap());
        }
        assert!(tree.verify_tree().is_ok());
    }

    #[test]
    fn test_tree_not_enough_leaves() {
        // This tree can hold 8 leaves.
        let (mut tree, max_leaves) = create_tree(4, 2);
        assert!(tree.verify_tree().is_ok());
        assert_eq!(max_leaves, 8);

        // Fill up the tree with 8 leaves.
        for i in 1..=max_leaves {
            let node_label = tree.get_free_leaf_label().unwrap();
            tree.update_leaf_hash(&node_label, [i; 32]).unwrap();
            assert_eq!(&[i; 32], tree.get_leaf_hash(&node_label).unwrap());
        }

        // Insert a 9th leaf which should fail.
        assert!(tree.get_free_leaf_label().is_err());
    }

    #[test]
    fn test_read_leaf_bad_label() {
        let (mut tree, _) = create_tree(4, 2);
        assert!(tree.verify_tree().is_ok());

        // Insert a leaf.
        let node_label = tree.get_free_leaf_label().unwrap();
        tree.update_leaf_hash(&node_label, [1; 32]).unwrap();
        assert_eq!(&[1; 32], tree.get_leaf_hash(&node_label).unwrap());
        assert!(tree.verify_tree().is_ok());

        // Try to read a bad leaf.
        assert!(tree.get_leaf_hash(&BAD_LABEL).is_err())
    }

    #[test]
    fn test_tree_update_leaf() {
        let (mut tree, _) = create_tree(4, 2);
        assert!(tree.verify_tree().is_ok());

        // Insert a leaf.
        let node_label = tree.get_free_leaf_label().unwrap();
        tree.update_leaf_hash(&node_label, [1; 32]).unwrap();
        assert_eq!(&[1; 32], tree.get_leaf_hash(&node_label).unwrap());
        assert!(tree.verify_tree().is_ok());

        // Update the leaf.
        tree.update_leaf_hash(&node_label, [2; 32]).unwrap();
        assert_eq!(&[2; 32], tree.get_leaf_hash(&node_label).unwrap());
        assert!(tree.verify_tree().is_ok());
    }

    #[test]
    fn test_tree_update_leaf_bad_label() {
        let (mut tree, _) = create_tree(4, 2);
        assert!(tree.verify_tree().is_ok());

        // Insert a leaf.
        let node_label = tree.get_free_leaf_label().unwrap();
        tree.update_leaf_hash(&node_label, [1; 32]).unwrap();
        assert_eq!(&[1; 32], tree.get_leaf_hash(&node_label).unwrap());
        assert!(tree.verify_tree().is_ok());

        // Try to update a bad leaf and check that the value has not changed.
        assert!(tree.update_leaf_hash(&BAD_LABEL, [2; 32]).is_err());
        assert_eq!(&[1; 32], tree.get_leaf_hash(&node_label).unwrap());
        assert!(tree.verify_tree().is_ok());
    }

    #[test]
    fn test_tree_delete_leaf() {
        let (mut tree, _) = create_tree(4, 2);
        assert!(tree.verify_tree().is_ok());

        // Insert a leaf.
        let node_label = tree.get_free_leaf_label().unwrap();
        tree.update_leaf_hash(&node_label, [1; 32]).unwrap();
        assert_eq!(&[1; 32], tree.get_leaf_hash(&node_label).unwrap());
        assert!(tree.verify_tree().is_ok());

        // Delete the leaf.
        tree.delete_leaf(&node_label).unwrap();
        assert!(tree.get_leaf_hash(&node_label).is_err());
        assert!(tree.verify_tree().is_ok());
    }

    #[test]
    fn test_tree_delete_leaf_bad_leaf() {
        let (mut tree, _) = create_tree(4, 2);
        assert!(tree.verify_tree().is_ok());

        // Insert a leaf.
        let node_label = tree.get_free_leaf_label().unwrap();
        tree.update_leaf_hash(&node_label, [1; 32]).unwrap();
        assert_eq!(&[1; 32], tree.get_leaf_hash(&node_label).unwrap());
        assert!(tree.verify_tree().is_ok());

        // Try to delete a bad leaf and check that the value has not changed.
        assert!(tree.delete_leaf(&BAD_LABEL).is_err());
        assert_eq!(&[1; 32], tree.get_leaf_hash(&node_label).unwrap());
        assert!(tree.verify_tree().is_ok());
    }

    #[test]
    fn test_tree_fill_up_leaves_delete_leaf_then_can_reuse() {
        let (mut tree, max_leaves) = create_tree(4, 2);
        assert!(tree.verify_tree().is_ok());

        // Insert 2^(4-1) leaves.
        let mut node_label = None;
        for i in 1..=max_leaves {
            let leaf_label = tree.get_free_leaf_label().unwrap();
            tree.update_leaf_hash(&leaf_label, [i; 32]).unwrap();
            assert_eq!(&[i; 32], tree.get_leaf_hash(&leaf_label).unwrap());
            node_label = Some(leaf_label);
        }
        assert!(node_label.is_some());

        // Check that we cannot a 9th leaf.
        assert!(tree.get_free_leaf_label().is_err());

        // Delete the last added leaf.
        tree.delete_leaf(&node_label.unwrap()).unwrap();
        assert!(tree.verify_tree().is_ok());

        // Check that now we _can_ add a 9th leaf.
        let leaf_label = tree.get_free_leaf_label().unwrap();
        tree.update_leaf_hash(&leaf_label, [max_leaves + 1; 32]).unwrap();
        assert_eq!(&[max_leaves + 1; 32], tree.get_leaf_hash(&leaf_label).unwrap());
        assert!(tree.verify_tree().is_ok());
    }

    #[test]
    fn test_tree_fill_up_leaves_reset_then_can_fill_up_again() {
        let (mut tree, max_leaves) = create_tree(4, 2);
        assert!(tree.verify_tree().is_ok());

        // Insert 2^(4-1) leaves.
        for i in 1..=max_leaves {
            let node_label = tree.get_free_leaf_label().unwrap();
            tree.update_leaf_hash(&node_label, [i; 32]).unwrap();
            assert_eq!(&[i; 32], tree.get_leaf_hash(&node_label).unwrap());
        }

        // Check that we cannot a 9th leaf.
        assert!(tree.get_free_leaf_label().is_err());

        // Delete the last added leaf.
        tree.reset().unwrap();
        assert!(tree.verify_tree().is_ok());

        // Check that now we can add another 2^(4-1) leaves.
        for i in 1..=max_leaves {
            let node_label = tree.get_free_leaf_label().unwrap();
            tree.update_leaf_hash(&node_label, [i + 10; 32]).unwrap();
            assert_eq!(&[i + 10; 32], tree.get_leaf_hash(&node_label).unwrap());
        }
        assert!(tree.verify_tree().is_ok());
    }

    #[test]
    fn test_get_aux_hashes_all_leaves_used() {
        let (mut tree, max_leaves) = create_tree(3, 3);
        assert!(tree.verify_tree().is_ok());

        // Fill up the tree, keeping track of all node labels for leaves.
        let mut leaf_labels = vec![];
        for i in 1..=max_leaves {
            let node_label = tree.get_free_leaf_label().unwrap();
            tree.update_leaf_hash(&node_label, [i; 32]).unwrap();
            assert_eq!(&[i; 32], tree.get_leaf_hash(&node_label).unwrap());
            leaf_labels.push(node_label);
        }
        assert!(!leaf_labels.is_empty());

        // Get auxiliary hashes for an arbitrary (in this case hardcoded 3rd)
        // leaf and try to reconstruct the root hash.
        let leaf_label = &leaf_labels[3];
        let aux_hashes = tree.get_auxiliary_hashes(leaf_label).unwrap();

        let mut next_hash = *tree.get_leaf_hash(leaf_label).unwrap();
        for layer_hashes in aux_hashes {
            let mut hasher = Sha256::new();
            for sibling_hash in layer_hashes {
                // Found the index that should use the calculated hash.
                if let Some(hash) = sibling_hash {
                    hasher.update(hash);
                } else {
                    // Found the index that should use the calculated hash.
                    hasher.update(&next_hash);
                }
            }
            next_hash = hasher.finalize().into();
        }
        assert_eq!(next_hash, *tree.get_root_hash().unwrap());
    }

    #[test]
    fn test_get_aux_hashes_not_all_leaves_used() {
        let (mut tree, max_leaves) = create_tree(3, 3);
        assert!(tree.verify_tree().is_ok());

        // Fill up all but one leaf of the tree, keeping track of the labels.
        let mut leaf_label_opt = None;
        for i in 1..max_leaves {
            let node_label = tree.get_free_leaf_label().unwrap();
            tree.update_leaf_hash(&node_label, [i; 32]).unwrap();
            assert_eq!(&[i; 32], tree.get_leaf_hash(&node_label).unwrap());
            leaf_label_opt = Some(node_label);
        }
        assert!(leaf_label_opt.is_some());

        // Get auxiliary hashes for an arbitrary last leaf and try to
        // reconstruct the root hash.
        // This tests that the placeholder value for the leaf-path node
        // does not get mistaken for an empty node.
        let leaf_label = &leaf_label_opt.unwrap();
        let aux_hashes = tree.get_auxiliary_hashes(leaf_label).unwrap();

        let mut next_hash = *tree.get_leaf_hash(leaf_label).unwrap();
        for layer_hashes in aux_hashes {
            let mut hasher = Sha256::new();
            for sibling_hash in layer_hashes {
                if let Some(hash) = sibling_hash {
                    hasher.update(hash);
                } else {
                    // Found the index that should use the calculated hash.
                    hasher.update(next_hash);
                }
            }
            next_hash = hasher.finalize().into();
        }
        assert_eq!(next_hash, *tree.get_root_hash().unwrap());
    }
}
