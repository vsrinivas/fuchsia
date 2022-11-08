// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A trie data structure used for matching inspect properties.

use std::{cmp::Eq, collections::HashMap, fmt::Debug, hash::Hash};

//TODO(fxbug.dev/38342): Move this mod to its own lib to avoid leaking its existance in the
//             fuchsia_inspect documentation.

/// Trie mapping a sequence of key fragments to nodes that
/// are able to store a value which is identifiable by the same key sequence.
pub struct Trie<K, V>
where
    K: Hash + Eq,
{
    root: TrieNode<K, V>,
}

impl<K, V> Trie<K, V>
where
    K: Hash + Eq,
{
    /// Creates a new empty Trie
    pub fn new() -> Self
    where
        K: Hash + Eq,
    {
        Trie { root: TrieNode::new() }
    }

    /// Creates an iterator of the Trie which
    /// will return an Item which is a tuple of the vector
    /// of key fragments that define the sequence up to
    /// a node containing a value, and a single value in that node.
    ///
    /// The iterator performs a stack based DFS through the trie to
    /// allow reconstruction of the key sequence defining each node.
    pub fn iter(&self) -> TrieIterator<'_, K, V> {
        TrieIterator::new(self)
    }

    /// Takes a key fragment sequence in vector form and removes the node identified
    /// by that key fragment sequence if it exists. Removes all values that exist on the
    /// node.
    pub fn remove(&mut self, key: &[K])
    where
        K: Hash + Eq + Debug,
    {
        if key.is_empty() {
            return;
        }

        Trie::remove_helper(&mut self.root, key, 0);
    }

    /// Retrieves a node identified by the key fragment vector `key` if it
    /// exists in the prefix trie, else None.
    pub fn get(&self, key: &[K]) -> Option<&V>
    where
        K: Hash + Eq + Debug,
    {
        key.iter()
            .try_fold(&self.root, |curr_node, key_fragment| curr_node.children.get(key_fragment))
            .and_then(|node| node.get_value())
    }

    /// Retrieves a mutable node identified by the key fragment vector `key` if it
    /// exists in the prefix trie, else None.
    pub fn get_mut(&mut self, key: &[K]) -> Option<&mut V>
    where
        K: Hash + Eq + Debug,
    {
        key.iter()
            .try_fold(&mut self.root, |curr_node, key_fragment| {
                curr_node.children.get_mut(key_fragment)
            })
            .and_then(|node| node.get_value_mut())
    }

    /// Takes a key fragment sequence in vector form, and a value defined by the
    /// key sequence, and populates the trie creating new nodes where needed, before
    /// inserting the value into the vector of values defined by the provided sequence.
    pub fn set(&mut self, key: impl IntoIterator<Item = K>, value: V)
    where
        K: Hash + Eq + Debug,
    {
        let key_trie_node = key.into_iter().fold(&mut self.root, |curr_node, key_fragment| {
            curr_node.children.entry(key_fragment).or_insert_with(|| TrieNode::new())
        });

        key_trie_node.value = Some(value);
    }

    fn remove_helper(curr_node: &mut TrieNode<K, V>, key: &[K], level: usize) -> bool
    where
        K: Hash + Eq + Debug,
    {
        if level == key.len() {
            curr_node.value = None;
            return curr_node.children.is_empty();
        }
        let mut delete_self: bool = false;
        if let Some(child_node) = curr_node.children.get_mut(&key[level]) {
            // TODO(lukenicholson): Consider implementing as stack and provide
            // limits to stack size?
            if Trie::remove_helper(child_node, key, level + 1) {
                curr_node.children.remove(&key[level]);
            }

            if curr_node.children.is_empty() && curr_node.value.is_none() {
                delete_self = true;
            }
        }
        delete_self
    }

    fn get_root(&self) -> &TrieNode<K, V> {
        &self.root
    }
}

/// A node of a `Trie`.
struct TrieNode<K, V>
where
    K: Hash + Eq,
{
    value: Option<V>,
    children: HashMap<K, TrieNode<K, V>>,
}

impl<K, V> TrieNode<K, V>
where
    K: Hash + Eq,
{
    /// Creates a new empty `Node`.
    pub fn new() -> Self
    where
        K: Hash + Eq,
    {
        TrieNode { value: None, children: HashMap::new() }
    }

    /// Returns a mutable reference to the value stored in this node.
    pub fn get_value_mut(&mut self) -> Option<&mut V> {
        self.value.as_mut()
    }

    /// Returns a reference to the value stored in this node.
    pub fn get_value(&self) -> Option<&V> {
        self.value.as_ref()
    }
}

/// An iterator for a `Trie`.
pub struct TrieIterator<'a, K, V>
where
    K: Hash + Eq,
{
    work_stack: Vec<WorkStackEntry<'a, K, V>>,
}

struct WorkStackEntry<'a, K, V>
where
    K: Hash + Eq,
{
    current_node: &'a TrieNode<K, V>,
    current_key: Vec<&'a K>,
}

impl<'a, K, V> TrieIterator<'a, K, V>
where
    K: Hash + Eq,
{
    fn new(trie: &'a Trie<K, V>) -> Self {
        let root = trie.get_root();
        Self { work_stack: vec![WorkStackEntry { current_node: root, current_key: vec![] }] }
    }
}

impl<'a, K, V> Iterator for TrieIterator<'a, K, V>
where
    K: Hash + Eq,
{
    /// Each item is a path to the node plus the value in that node.
    type Item = (Vec<&'a K>, Option<&'a V>);

    fn next(&mut self) -> Option<Self::Item> {
        let WorkStackEntry { current_node, current_key } = match self.work_stack.pop() {
            None => return None,
            Some(item) => item,
        };
        for (key, child) in current_node.children.iter() {
            let mut child_key = current_key.clone();
            child_key.push(key);
            self.work_stack.push(WorkStackEntry { current_node: child, current_key: child_key });
        }
        let value = current_node.get_value();
        Some((current_key, value))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia::test]
    fn test_sequence_trie() {
        type TestTrie = Trie<char, String>;
        let mut test_trie: TestTrie = TestTrie::new();
        test_trie.set("test".to_string().chars(), "a".to_string());
        test_trie.set("test1".to_string().chars(), "b".to_string());

        let mut curr_node = test_trie.get_root();
        assert!(curr_node.get_value().is_none());
        curr_node = curr_node.children.get(&'t').unwrap();
        assert!(curr_node.get_value().is_none());
        curr_node = curr_node.children.get(&'e').unwrap();
        assert!(curr_node.get_value().is_none());
        curr_node = curr_node.children.get(&'s').unwrap();
        assert!(curr_node.get_value().is_none());
        curr_node = curr_node.children.get(&'t').unwrap();
        assert!(curr_node.get_value().is_some());
        curr_node = curr_node.children.get(&'1').unwrap();
        assert!(curr_node.get_value().is_some());

        let key: Vec<char> = "test".to_string().chars().collect();
        assert_eq!(test_trie.get(&key), Some(&"a".to_string()));

        let key: Vec<char> = "test1".to_string().chars().collect();
        assert_eq!(test_trie.get(&key), Some(&"b".to_string()));
    }

    #[fuchsia::test]
    fn test_sequence_trie_removal() {
        type TestTrie = Trie<char, String>;
        let mut test_trie: TestTrie = TestTrie::new();
        test_trie.set("test".to_string().chars(), "a".to_string());
        test_trie.set("test1".to_string().chars(), "b".to_string());
        test_trie.set("text".to_string().chars(), "c".to_string());
        test_trie.set("text12".to_string().chars(), "d".to_string());

        test_trie.remove("test".to_string().chars().collect::<Vec<_>>().as_ref());
        test_trie.remove(&[]);
        test_trie.remove("text12".to_string().chars().collect::<Vec<_>>().as_ref());

        let key: Vec<char> = "test".to_string().chars().collect();
        assert!(test_trie.get(&key).is_none());

        let key: Vec<char> = "test1".to_string().chars().collect();
        assert_eq!(test_trie.get(&key), Some(&"b".to_string()));

        let key: Vec<char> = "text".to_string().chars().collect();
        assert_eq!(test_trie.get(&key), Some(&"c".to_string()));

        let key: Vec<char> = "text1".to_string().chars().collect();
        assert!(test_trie.get(&key).is_none());
    }

    #[fuchsia::test]
    fn test_sequence_trie_iter() {
        type TestTrie = Trie<char, String>;
        let mut test_trie: TestTrie = TestTrie::new();
        test_trie.set("test".to_string().chars(), "a".to_string());
        test_trie.set("test1".to_string().chars(), "b".to_string());
        let mut results_vec = vec![
            (vec!['t', 'e', 's', 't', '1'], Some("b".to_string())),
            (vec!['t', 'e', 's', 't'], Some("a".to_string())),
            (vec!['t', 'e', 's'], None),
            (vec!['t', 'e'], None),
            (vec!['t'], None),
            (vec![], None),
        ];
        for (key, val) in test_trie.iter() {
            let (expected_key, expected_val) = results_vec.pop().unwrap();
            assert_eq!(val, expected_val.as_ref());
            assert_eq!(key.into_iter().collect::<String>(), expected_key.iter().collect::<String>())
        }

        assert!(results_vec.is_empty());
    }

    #[fuchsia::test]
    fn test_empty_trie_iters() {
        type TestTrie = Trie<char, String>;
        let empty_trie: TestTrie = TestTrie::new();
        let mut results_vec: Vec<(Vec<char>, Option<String>)> = vec![(vec![], None)];
        for (key, val) in empty_trie.iter() {
            let (expected_key, expected_val) = results_vec.pop().unwrap();
            assert_eq!(val, expected_val.as_ref());
            assert_eq!(key.into_iter().collect::<String>(), expected_key.iter().collect::<String>())
        }
        assert!(results_vec.is_empty());

        let mut one_entry_trie: TestTrie = TestTrie::new();
        one_entry_trie.set(vec!['t'], "a".to_string());
        let mut results_vec = vec![(vec!['t'], Some("a".to_string())), (vec![], None)];
        for (key, val) in one_entry_trie.iter() {
            let (expected_key, expected_val) = results_vec.pop().unwrap();
            assert_eq!(val, expected_val.as_ref());
            assert_eq!(key.into_iter().collect::<String>(), expected_key.iter().collect::<String>())
        }

        assert!(results_vec.is_empty());
    }

    #[fuchsia::test]
    fn test_component_store_trie() {
        #[derive(Debug, Eq, PartialEq, Clone, Ord, PartialOrd)]
        struct Data(usize);
        type TestTrie = Trie<String, Data>;

        let mut trie = TestTrie::new();

        // Returns a key that emulates what the archivist stores.
        let key = |vals: Vec<&str>| vals.into_iter().map(|s| s.to_string()).collect::<Vec<_>>();
        // Inserts into the trie.
        let mut insert = |key: Vec<String>, x: usize| trie.set(key, Data(x));

        let a: Vec<String> = key(vec!["core", "foo", "bar"]);
        let b: Vec<String> = key(vec!["core", "foo", "baz"]);
        let c: Vec<String> = key(vec!["core", "quux"]);
        let d: Vec<String> = key(vec!["foo.cmx", "3"]);
        let e: Vec<String> = key(vec!["foo.cmx", "2"]);

        insert(a.clone(), 1);
        insert(b.clone(), 2);
        insert(c.clone(), 3);
        insert(d.clone(), 4);
        insert(e.clone(), 5);

        // Validates that the given expected key-value pairs match the ones in the trie.
        macro_rules! check {
            ($expected:expr) => {
                let mut expected = $expected;
                expected.sort();
                let mut result = trie
                    .iter()
                    .map(|(k, v)| (k.iter().map(|x| x.to_string()).collect(), v.cloned()))
                    .collect::<Vec<(_, _)>>();
                result.sort();
                assert_eq!(expected, result);
            };
        }

        check!(vec![
            (key(vec![]), None),
            (key(vec!["core"]), None),
            (key(vec!["core", "foo"]), None),
            (key(vec!["foo.cmx"]), None),
            (c.clone(), Some(Data(3))),
            (a.clone(), Some(Data(1))),
            (b.clone(), Some(Data(2))),
            (d.clone(), Some(Data(4))),
            (e.clone(), Some(Data(5))),
        ]);

        trie.remove(&a);
        trie.remove(&b);
        trie.remove(&c);
        trie.remove(&d);

        check!(
            vec![(e.clone(), Some(Data(5))), (key(vec!["foo.cmx"]), None), (key(vec![]), None),]
        );

        trie.remove(&e);
        check!(vec![(key(vec![]), None::<Data>)]);

        assert!(trie.root.value.is_none());
        assert!(trie.root.children.is_empty());
    }
}
