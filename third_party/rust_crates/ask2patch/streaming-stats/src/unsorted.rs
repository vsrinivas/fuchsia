use std::default::Default;
use std::iter::{FromIterator, IntoIterator};
use num_traits::ToPrimitive;

use {Commute, Partial};

/// Compute the exact median on a stream of data.
///
/// (This has time complexity `O(nlogn)` and space complexity `O(n)`.)
pub fn median<I>(it: I) -> Option<f64>
        where I: Iterator, <I as Iterator>::Item: PartialOrd + ToPrimitive {
    it.collect::<Unsorted<_>>().median()
}

/// Compute the exact mode on a stream of data.
///
/// (This has time complexity `O(nlogn)` and space complexity `O(n)`.)
///
/// If the data does not have a mode, then `None` is returned.
pub fn mode<T, I>(it: I) -> Option<T>
       where T: PartialOrd + Clone, I: Iterator<Item=T> {
    it.collect::<Unsorted<T>>().mode()
}

/// Compute the modes on a stream of data.
/// 
/// If there is a single mode, then only that value is returned in the `Vec`
/// however, if there multiple values tied for occuring the most amount of times
/// those values are returned.
/// 
/// ## Example
/// ```
/// use stats;
/// 
/// let vals = vec![1, 1, 2, 2, 3];
/// 
/// assert_eq!(stats::modes(vals.into_iter()), vec![1, 2]);
/// ```
/// This has time complexity `O(n)`
///
/// If the data does not have a mode, then an empty `Vec` is returned.
pub fn modes<T, I>(it: I) -> Vec<T>
       where T: PartialOrd + Clone, I: Iterator<Item=T> {
    it.collect::<Unsorted<T>>().modes()
}

fn median_on_sorted<T>(data: &[T]) -> Option<f64>
        where T: PartialOrd + ToPrimitive {
    Some(match data.len() {
        0 => return None,
        1 => data[0].to_f64().unwrap(),
        len if len % 2 == 0 => {
            let v1 = data[(len / 2) - 1].to_f64().unwrap();
            let v2 = data[len / 2].to_f64().unwrap();
            (v1 + v2) / 2.0
        }
        len => {
            data[len / 2].to_f64().unwrap()
        }
    })
}

fn mode_on_sorted<T, I>(it: I) -> Option<T>
        where T: PartialOrd, I: Iterator<Item=T> {
    // This approach to computing the mode works very nicely when the
    // number of samples is large and is close to its cardinality.
    // In other cases, a hashmap would be much better.
    // But really, how can we know this when given an arbitrary stream?
    // Might just switch to a hashmap to track frequencies. That would also
    // be generally useful for discovering the cardinality of a sample.
    let (mut mode, mut next) = (None, None);
    let (mut mode_count, mut next_count) = (0usize, 0usize);
    for x in it {
        if mode.as_ref().map(|y| y == &x).unwrap_or(false) {
            mode_count += 1;
        } else if next.as_ref().map(|y| y == &x).unwrap_or(false) {
            next_count += 1;
        } else {
            next = Some(x);
            next_count = 0;
        }

        if next_count > mode_count {
            mode = next;
            mode_count = next_count;
            next = None;
            next_count = 0;
        } else if next_count == mode_count {
            mode = None;
            mode_count = 0usize;
        }
    }
    mode
}

fn modes_on_sorted<T, I>(it: I) -> Vec<T>
        where T: PartialOrd, I: Iterator<Item=T> {

    let mut highest_mode = 1_u32;
    let mut modes: Vec<u32> = vec![];
    let mut values = vec![];
    let mut count = 0;
    for x in it {
        if values.len() == 0 {
            values.push(x);
            modes.push(1);
            continue
        }
        if x == values[count] {
            modes[count] += 1;
            if highest_mode < modes[count] {
                highest_mode = modes[count];
            }
        } else {
            values.push(x);
            modes.push(1);
            count += 1;
        }
    }
    modes.into_iter()
        .zip(values)
        .filter(|(cnt, _val)| *cnt == highest_mode && highest_mode > 1)
        .map(|(_, val)| val)
        .collect()
}

/// A commutative data structure for lazily sorted sequences of data.
///
/// The sort does not occur until statistics need to be computed.
///
/// Note that this works on types that do not define a total ordering like
/// `f32` and `f64`. When an ordering is not defined, an arbitrary order
/// is returned.
#[derive(Clone)]
pub struct Unsorted<T> {
    data: Vec<Partial<T>>,
    sorted: bool,
}

impl<T: PartialOrd> Unsorted<T> {
    /// Create initial empty state.
    pub fn new() -> Unsorted<T> {
        Default::default()
    }

    /// Add a new element to the set.
    pub fn add(&mut self, v: T) {
        self.dirtied();
        self.data.push(Partial(v))
    }

    /// Return the number of data points.
    pub fn len(&self) -> usize {
        self.data.len()
    }

    fn sort(&mut self) {
        if !self.sorted {
            self.data.sort();
        }
    }

    fn dirtied(&mut self) {
        self.sorted = false;
    }
}

impl<T: PartialOrd + Eq + Clone> Unsorted<T> {
    pub fn cardinality(&mut self) -> usize {
        self.sort();
        let mut set = self.data.clone();
        set.dedup();
        set.len()
    }
}

impl<T: PartialOrd + Clone> Unsorted<T> {
    /// Returns the mode of the data.
    pub fn mode(&mut self) -> Option<T> {
        self.sort();
        mode_on_sorted(self.data.iter()).map(|p| p.0.clone())
    }

    /// Returns the modes of the data.
    pub fn modes(&mut self) -> Vec<T> {
        self.sort();
        modes_on_sorted(self.data.iter())
            .into_iter()
            .map(|p| p.0.clone())
            .collect()
    }
}

impl<T: PartialOrd + ToPrimitive> Unsorted<T> {
    /// Returns the median of the data.
    pub fn median(&mut self) -> Option<f64> {
        self.sort();
        median_on_sorted(&*self.data)
    }
}

impl<T: PartialOrd> Commute for Unsorted<T> {
    fn merge(&mut self, v: Unsorted<T>) {
        self.dirtied();
        self.data.extend(v.data.into_iter());
    }
}

impl<T: PartialOrd> Default for Unsorted<T> {
    fn default() -> Unsorted<T> {
        Unsorted {
            data: Vec::with_capacity(1000),
            sorted: true,
        }
    }
}

impl<T: PartialOrd> FromIterator<T> for Unsorted<T> {
    fn from_iter<I: IntoIterator<Item=T>>(it: I) -> Unsorted<T> {
        let mut v = Unsorted::new();
        v.extend(it);
        v
    }
}

impl<T: PartialOrd> Extend<T> for Unsorted<T> {
    fn extend<I: IntoIterator<Item=T>>(&mut self, it: I) {
        self.dirtied();
        self.data.extend(it.into_iter().map(Partial))
    }
}

#[cfg(test)]
mod test {
    use super::{median, mode, modes};

    #[test]
    fn median_stream() {
        assert_eq!(median(vec![3usize, 5, 7, 9].into_iter()), Some(6.0));
        assert_eq!(median(vec![3usize, 5, 7].into_iter()), Some(5.0));
    }

    #[test]
    fn mode_stream() {
        assert_eq!(mode(vec![3usize, 5, 7, 9].into_iter()), None);
        assert_eq!(mode(vec![3usize, 3, 3, 3].into_iter()), Some(3));
        assert_eq!(mode(vec![3usize, 3, 3, 4].into_iter()), Some(3));
        assert_eq!(mode(vec![4usize, 3, 3, 3].into_iter()), Some(3));
        assert_eq!(mode(vec![1usize, 1, 2, 3, 3].into_iter()), None);
    }

    #[test]
    fn median_floats() {
        assert_eq!(median(vec![3.0f64, 5.0, 7.0, 9.0].into_iter()), Some(6.0));
        assert_eq!(median(vec![3.0f64, 5.0, 7.0].into_iter()), Some(5.0));
        assert_eq!(median(vec![1.0f64, 2.5, 3.0].into_iter()), Some(2.5));
    }

    #[test]
    fn mode_floats() {
        assert_eq!(mode(vec![3.0f64, 5.0, 7.0, 9.0].into_iter()), None);
        assert_eq!(mode(vec![3.0f64, 3.0, 3.0, 3.0].into_iter()), Some(3.0));
        assert_eq!(mode(vec![3.0f64, 3.0, 3.0, 4.0].into_iter()), Some(3.0));
        assert_eq!(mode(vec![4.0f64, 3.0, 3.0, 3.0].into_iter()), Some(3.0));
        assert_eq!(mode(vec![1.0f64, 1.0, 2.0, 3.0, 3.0].into_iter()), None);
    }

    #[test]
    fn modes_stream() {
        assert_eq!(modes(vec![3usize, 5, 7, 9].into_iter()), vec![]);
        assert_eq!(modes(vec![3usize, 3, 3, 3].into_iter()), vec![3]);
        assert_eq!(modes(vec![3usize, 3, 4, 4].into_iter()), vec![3, 4]);
        assert_eq!(modes(vec![4usize, 3, 3, 3].into_iter()), vec![3]);
        assert_eq!(modes(vec![1usize, 1, 2, 2].into_iter()), vec![1, 2]);
        let vec: Vec<u32> = vec![];
        assert_eq!(modes(vec.into_iter()), vec![]);
    }

    #[test]
    fn modes_floats() {
        assert_eq!(modes(vec![3_f64, 5.0, 7.0, 9.0].into_iter()), vec![]);
        assert_eq!(modes(vec![3_f64, 3.0, 3.0, 3.0].into_iter()), vec![3.0]);
        assert_eq!(modes(vec![3_f64, 3.0, 4.0, 4.0].into_iter()), vec![3.0, 4.0]);
        assert_eq!(modes(vec![1_f64, 1.0, 2.0, 3.0, 3.0].into_iter()), vec![1.0, 3.0]);
    }
}
