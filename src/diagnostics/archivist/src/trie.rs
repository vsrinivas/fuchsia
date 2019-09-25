// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::collections::HashMap;

/// Trie mapping a sequence of key fragments to nodes that
/// are able to store a vector of multiple values which
/// are all identifiable by the same key sequence.
pub struct Trie<K, V> {
    root: TrieNode<K, V>,
}

impl<K, V> Trie<K, V> {
    pub fn new() -> Self
    where
        K: std::hash::Hash + std::cmp::Eq,
    {
        Trie { root: TrieNode::new() }
    }

    /// Creates an iterator of the Trie which
    /// will return an Item which is a tuple of the vector
    /// of key fragments that define the sequence up to
    /// a node containing values, and a single value in that node.
    ///
    /// The iterator will visit all values in a given node, one at a time,
    /// before moving on to a new node.
    ///
    /// The iterator performs a stack based DFS through the trie to
    /// allow reconstruction of the key sequence defining each node.
    pub fn iter(&self) -> TrieIterator<K, V> {
        TrieIterator {
            trie: self,
            iterator_initialized: false,
            work_stack: vec![],
            curr_key: vec![],
            curr_node: None,
            curr_val_index: 0,
        }
    }

    /// Takes a key fragment sequence in vector form, and a value defined by the
    /// key sequence, and populates the trie creating new nodes where needed, before
    /// inserting the value into the vector of values defined by the provided sequence.
    pub fn insert(&mut self, key: Vec<K>, value: V)
    where
        K: std::hash::Hash + std::cmp::Eq + std::fmt::Debug,
    {
        let key_trie_node = key.into_iter().fold(&mut self.root, |curr_node, key_fragment| {
            curr_node.children.entry(key_fragment).or_insert_with(|| TrieNode::new())
        });

        key_trie_node.values.push(value);
    }

    pub fn get_root(&self) -> &TrieNode<K, V> {
        return &self.root;
    }
}

pub struct TrieNode<K, V> {
    values: Vec<V>,
    children: HashMap<K, TrieNode<K, V>>,
}

impl<K, V> TrieNode<K, V> {
    pub fn new() -> Self
    where
        K: std::hash::Hash + std::cmp::Eq,
    {
        TrieNode { values: Vec::new(), children: HashMap::new() }
    }

    pub fn get_values(&self) -> &Vec<V> {
        return &self.values;
    }

    pub fn get_children(&self) -> &HashMap<K, TrieNode<K, V>> {
        return &self.children;
    }
}

enum TrieIteratorKeyState<'a, K> {
    AddKeyFragment(&'a K),
    PopKeyFragment,
}

struct TrieIteratorWorkEvent<'a, K, V> {
    key_state: TrieIteratorKeyState<'a, K>,
    potential_child: Option<&'a TrieNode<K, V>>,
}

pub struct TrieIterator<'a, K, V> {
    trie: &'a Trie<K, V>,
    iterator_initialized: bool,
    work_stack: Vec<TrieIteratorWorkEvent<'a, K, V>>,
    curr_key: Vec<&'a K>,
    curr_node: Option<&'a TrieNode<K, V>>,
    curr_val_index: usize,
}

impl<'a, K, V> Iterator for TrieIterator<'a, K, V> {
    type Item = (Vec<&'a K>, &'a V);

    fn next(&mut self) -> Option<(Vec<&'a K>, &'a V)> {
        if !self.iterator_initialized {
            debug_assert_eq!(self.work_stack.len(), 0);
            debug_assert_eq!(self.curr_val_index, 0);
            debug_assert_eq!(self.curr_key.len(), 0);

            self.iterator_initialized = true;
            self.work_stack.push(TrieIteratorWorkEvent {
                key_state: TrieIteratorKeyState::PopKeyFragment,
                potential_child: None,
            });
            for (key_fragment, child_node) in self.trie.get_root().get_children().iter() {
                self.work_stack.push(TrieIteratorWorkEvent {
                    key_state: TrieIteratorKeyState::AddKeyFragment(key_fragment),
                    potential_child: Some(child_node),
                });
            }

            self.curr_node = Some(self.trie.get_root());
        }

        match self.curr_node {
            Some(_) => {}
            None => {
                assert_eq!(
                    self.work_stack.len(),
                    0,
                    "This state is only reachable once all work is complete!"
                );
                return None;
            }
        };

        while (self.curr_node?.get_values().is_empty()
            || self.curr_node?.get_values().len() <= self.curr_val_index)
            && !self.work_stack.is_empty()
        {
            let next_work_item = self.work_stack.pop()?;
            match next_work_item.key_state {
                TrieIteratorKeyState::PopKeyFragment => {
                    self.curr_key.pop();
                }
                TrieIteratorKeyState::AddKeyFragment(key_fragment) => {
                    match next_work_item.potential_child {
                        Some(child) => {
                            self.curr_key.push(key_fragment);
                            self.curr_node = Some(child);
                            self.curr_val_index = 0;
                        }
                        None => unreachable!(
                        "Work events that extend key fragments must have an associated child node."
                    ),
                    }

                    self.work_stack.push(TrieIteratorWorkEvent {
                        key_state: TrieIteratorKeyState::PopKeyFragment,
                        potential_child: None,
                    });

                    for (key_fragment, child_node) in self.curr_node.unwrap().get_children().iter()
                    {
                        self.work_stack.push(TrieIteratorWorkEvent {
                            key_state: TrieIteratorKeyState::AddKeyFragment(key_fragment),
                            potential_child: Some(child_node),
                        });
                    }
                }
            }
        }

        let refreshed_node_values: &Vec<V> = match self.curr_node {
            Some(node) => node.get_values(),
            None => unreachable!("We've definitely processed a new node, so this cannot be None."),
        };

        if refreshed_node_values.is_empty() || refreshed_node_values.len() <= self.curr_val_index {
            None
        } else {
            self.curr_val_index = self.curr_val_index + 1;
            Some((self.curr_key.clone(), &refreshed_node_values[self.curr_val_index - 1]))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_sequence_trie() {
        type TestTrie = Trie<char, String>;
        let mut test_trie: TestTrie = TestTrie::new();
        test_trie.insert("test".to_string().chars().collect(), "a".to_string());
        test_trie.insert("test1".to_string().chars().collect(), "b".to_string());
        let mut curr_node = test_trie.get_root();
        assert!(curr_node.get_values().is_empty());
        curr_node = curr_node.get_children().get(&'t').unwrap();
        assert!(curr_node.get_values().is_empty());
        curr_node = curr_node.get_children().get(&'e').unwrap();
        assert!(curr_node.get_values().is_empty());
        curr_node = curr_node.get_children().get(&'s').unwrap();
        assert!(curr_node.get_values().is_empty());
        curr_node = curr_node.get_children().get(&'t').unwrap();
        assert_eq!(curr_node.get_values().len(), 1);
        curr_node = curr_node.get_children().get(&'1').unwrap();
        assert_eq!(curr_node.get_values().len(), 1);
    }

    #[test]
    fn test_sequence_trie_iter() {
        type TestTrie = Trie<char, String>;
        let mut test_trie: TestTrie = TestTrie::new();
        test_trie.insert("test".to_string().chars().collect(), "a".to_string());
        test_trie.insert("test".to_string().chars().collect(), "a2".to_string());
        test_trie.insert("test1".to_string().chars().collect(), "b".to_string());
        let mut results_vec = vec![
            (vec!['t', 'e', 's', 't', '1'], "b".to_string()),
            (vec!['t', 'e', 's', 't'], "a2".to_string()),
            (vec!['t', 'e', 's', 't'], "a".to_string()),
        ];
        for (key, val) in test_trie.iter() {
            let (expected_key, expected_val) = results_vec.pop().unwrap();
            assert_eq!(*val, expected_val);
            assert_eq!(key.into_iter().collect::<String>(), expected_key.iter().collect::<String>())
        }
    }
}
