use std::collections::hash_map::{HashMap, Entry};
use std::fmt;
use std::hash::Hash;
use std::iter::{FromIterator, IntoIterator};
use std::default::Default;

use Commute;

/// A commutative data structure for exact frequency counts.
#[derive(Clone)]
pub struct Frequencies<T> {
    data: HashMap<T, u64>,
}

impl<T: fmt::Debug + Eq + Hash> fmt::Debug for Frequencies<T> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{:?}", self.data)
    }
}

impl<T: Eq + Hash> Frequencies<T> {
    /// Create a new frequency table with no samples.
    pub fn new() -> Frequencies<T> {
        Default::default()
    }

    /// Add a sample to the frequency table.
    pub fn add(&mut self, v: T) {
        match self.data.entry(v) {
            Entry::Vacant(count) => { count.insert(1); },
            Entry::Occupied(mut count) => { *count.get_mut() += 1; },
        }
    }

    /// Return the number of occurrences of `v` in the data.
    pub fn count(&self, v: &T) -> u64 {
        self.data.get(v).map(|&v| v).unwrap_or(0)
    }

    /// Return the cardinality (number of unique elements) in the data.
    pub fn cardinality(&self) -> u64 {
        self.len() as u64
    }

    /// Returns the mode if one exists.
    pub fn mode(&self) -> Option<&T> {
        let counts = self.most_frequent();
        if counts.is_empty() {
            None
        } else if counts.len() >= 2 && counts[0].1 == counts[1].1 {
            None
        } else {
            Some(counts[0].0)
        }
    }

    /// Return a `Vec` of elements and their corresponding counts in
    /// descending order.
    pub fn most_frequent(&self) -> Vec<(&T, u64)> {
        let mut counts: Vec<_> = self.data.iter()
                                          .map(|(k, &v)| (k, v))
                                          .collect();
        counts.sort_by(|&(_, c1), &(_, c2)| c2.cmp(&c1));
        counts
    }

    /// Return a `Vec` of elements and their corresponding counts in
    /// ascending order.
    pub fn least_frequent(&self) -> Vec<(&T, u64)> {
        let mut counts: Vec<_> = self.data.iter()
                                          .map(|(k, &v)| (k, v))
                                          .collect();
        counts.sort_by(|&(_, c1), &(_, c2)| c1.cmp(&c2));
        counts
    }

    /// Returns the cardinality of the data.
    pub fn len(&self) -> usize {
        self.data.len()
    }
}

impl<T: Eq + Hash> Commute for Frequencies<T> {
    fn merge(&mut self, v: Frequencies<T>) {
        for (k, v2) in v.data.into_iter() {
            match self.data.entry(k) {
                Entry::Vacant(v1) => { v1.insert(v2); }
                Entry::Occupied(mut v1) => { *v1.get_mut() += v2; }
            }
        }
    }
}

impl<T: Eq + Hash> Default for Frequencies<T> {
    fn default() -> Frequencies<T> {
        Frequencies { data: HashMap::with_capacity(100000) }
    }
}

impl<T: Eq + Hash> FromIterator<T> for Frequencies<T> {
    fn from_iter<I: IntoIterator<Item=T>>(it: I) -> Frequencies<T> {
        let mut v = Frequencies::new();
        v.extend(it);
        v
    }
}

impl<T: Eq + Hash> Extend<T> for Frequencies<T> {
    fn extend<I: IntoIterator<Item=T>>(&mut self, it: I) {
        for sample in it {
            self.add(sample);
        }
    }
}

#[cfg(test)]
mod test {
    use super::Frequencies;

    #[test]
    fn ranked() {
        let mut counts = Frequencies::new();
        counts.extend(vec![1usize, 1, 2, 2, 2, 2, 2, 3, 4, 4, 4].into_iter());
        assert_eq!(counts.most_frequent()[0], (&2, 5));
        assert_eq!(counts.least_frequent()[0], (&3, 1));
    }
}
