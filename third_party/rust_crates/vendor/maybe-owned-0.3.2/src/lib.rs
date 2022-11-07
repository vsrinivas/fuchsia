//! This crate only provides the `MaybeOwned` enum
//!
//! Take a look at it's documentation for more information.
//!
#![warn(missing_docs)]
#[cfg(feature = "serde")]
extern crate serde;

#[cfg(feature = "serde")]
mod serde_impls;

mod transitive_impl;

use std::ops::Deref;
use std::cmp::Ordering;
use std::hash::{Hash, Hasher};
use std::fmt;
use std::borrow::{Cow, Borrow};
use std::str::FromStr;


use self::MaybeOwned::*;

/// This type provides a way to store data to which you either have a
/// reference to or which you do own.
///
/// It provides `From<T>`, `From<&'a T>` implementations and, in difference
/// to `Cow` does _not_ require `ToOwned` to be implemented which makes it
/// compatible with non cloneable data, as a draw back of this it does not
/// know about `ToOwned`. As a consequence of it can't know that `&str` should
/// be the borrowed version of `String` and not `&String` this is especially
/// bad wrt. `Box` as the borrowed version of `Box<T>` would be `&Box<T>`.
///
/// While this crate has some drawbacks compared to `Cow` is has the benefit,
/// that it works with Types which neither implement `Clone` nor `ToOwned`.
/// Another benefit lies in the ability to write API functions which accept
/// a generic parameter `E: Into<MaybeOwned<'a, T>>` as the API consumer can
/// pass `T`, `&'a T` and `MaybeOwned<'a, T>` as argument, without requiring
/// a explicit `Cow::Onwed` or a split into two functions one accepting
/// owed and the other borrowed values.
///
/// # Alternatives
///
/// If you mainly have values implementing `ToOwned` like `&str`/`String`, `Path`/`PathBuf` or
/// `&[T]`/`Vec<T>` using `std::borrow::Cow` might be preferable.
///
/// If you want to be able to treat `&T`, `&mut T`, `Box<T>` and `Arc<T>` the same
/// consider using [`reffers::rbma::RBMA`](https://docs.rs/reffers)
/// (through not all types/platforms are supported because
/// as it relies on the fact that for many pointers the lowest two bits are 0, and stores
/// the discriminant in them, nevertheless this is can only be used with 32bit-aligned data,
/// e.g. using a &u8 _might_ fail). RBMA also allows you to recover a `&mut T` if it was created
/// from `Box<T>`, `&mut T` or a unique `Arc`.
///
///
/// # Examples
///
/// ```
/// # use maybe_owned::MaybeOwned;
/// struct PseudoBigData(u8);
/// fn pseudo_register_fn<'a, E>(_val: E) where E: Into<MaybeOwned<'a, PseudoBigData>> { }
///
/// let data = PseudoBigData(12);
/// let data2 = PseudoBigData(13);
///
/// pseudo_register_fn(&data);
/// pseudo_register_fn(&data);
/// pseudo_register_fn(data2);
/// pseudo_register_fn(MaybeOwned::Owned(PseudoBigData(111)));
/// ```
///
/// ```
/// # use maybe_owned::MaybeOwned;
/// #[repr(C)]
/// struct OpaqueFFI {
///     ref1:  * const u8
///     //we also might want to have PhantomData etc.
/// }
///
/// // does not work as it does not implement `ToOwned`
/// // let _ = Cow::Owned(OpaqueFFI { ref1: 0 as *const u8});
///
/// // ok, MaybeOwned can do this (but can't do &str<->String as tread of)
/// let _ = MaybeOwned::Owned(OpaqueFFI { ref1: 0 as *const u8 });
/// ```
///
/// ```
/// # #[macro_use]
/// # extern crate serde_derive;
/// # extern crate serde_json;
/// # extern crate maybe_owned;
/// # #[cfg(feature = "serde")]
/// # fn main() {
/// # use maybe_owned::MaybeOwned;
/// use std::collections::HashMap;
///
/// #[derive(Serialize, Deserialize)]
/// struct SerializedData<'a> {
///     data: MaybeOwned<'a, HashMap<String, i32>>,
/// }
///
/// let mut map = HashMap::new();
/// map.insert("answer".to_owned(), 42);
///
/// // serializing can use borrowed data to avoid unnecessary copying
/// let bytes = serde_json::to_vec(&SerializedData { data: (&map).into() }).unwrap();
///
/// // deserializing creates owned data
/// let deserialized: SerializedData = serde_json::from_slice(&bytes).unwrap();
/// assert_eq!(deserialized.data["answer"], 42);
/// # }
/// # #[cfg(not(feature = "serde"))] fn main() {}
/// ```
///
/// # Transitive `std::ops` implementations
///
/// There are transitive implementations for most operator in `std::ops`.
///
/// A Op between a `MaybeOwned<L>` and `MaybeOwned<R>` is implemented if:
///
/// - L impl the Op with R
/// - L impl the Op with &R
/// - &L impl the Op with R
/// - &L impl the Op with &R
/// - the `Output` of all aboves implementations is
///   the same type
///
///
/// The `Neg` (`-` prefix) op is implemented for `V` if:
///
/// - `V` impl `Neg`
/// - `&V` impl `Neg`
/// - both have the same `Output`
///
///
/// The `Not` (`!` prefix) op is implemented for `V` if:
///
/// - `V` impl `Not`
/// - `&V` impl `Not`
/// - both have the same `Output`
///
/// Adding implementations for Ops which add a `MaybeOwned` to
/// a non `MaybeOwned` value (like `MaybeOwned<T> + T`) requires
/// far reaching specialization in rust and is therefore not done
/// for now.
#[derive(Debug)]
pub enum MaybeOwned<'a, T: 'a> {
    /// owns T
    Owned(T),
    /// has a reference to T
    Borrowed(&'a T)
}

impl<'a, T> MaybeOwned<'a, T> {

    /// returns true if the data is owned else false
    pub fn is_owned(&self) -> bool {
        match *self {
            Owned(_) => true,
            Borrowed(_) => false
        }
    }
}

impl<'a, T> Deref for MaybeOwned<'a, T> {
    type Target = T;

    fn deref(&self) -> &T {
        match *self {
            Owned(ref v) => v,
            Borrowed(v) => v
        }
    }
}

impl<'a, T> AsRef<T> for MaybeOwned<'a, T> {
    fn as_ref(&self) -> &T {
        self
    }
}

impl<'a, T> Borrow<T> for MaybeOwned<'a, T> {
    fn borrow(&self) -> &T {
        self
    }
}

impl<'a, T> From<&'a T> for MaybeOwned<'a, T> {
    fn from(v: &'a T) -> MaybeOwned<'a, T> {
        Borrowed(v)
    }
}


impl<'a, T> From<T> for MaybeOwned<'a, T> {
    fn from(v: T) -> MaybeOwned<'a, T> {
        Owned(v)
    }
}

impl<'a, T> From<Cow<'a, T>> for MaybeOwned<'a, T>
    where T: ToOwned<Owned=T>,
{
    fn from(cow: Cow<'a, T>) -> MaybeOwned<'a, T> {
        match cow {
            Cow::Owned(v) => MaybeOwned::Owned(v),
            Cow::Borrowed(v) => MaybeOwned::Borrowed(v),
        }
    }
}

impl<'a, T> Into<Cow<'a, T>> for MaybeOwned<'a, T>
    where T: ToOwned<Owned=T>,
{
    fn into(self) -> Cow<'a, T> {
        match self {
            MaybeOwned::Owned(v) => Cow::Owned(v),
            MaybeOwned::Borrowed(v) => Cow::Borrowed(v),
        }
    }
}

impl<'a, T> Default for MaybeOwned<'a, T> where T: Default {
    fn default() -> Self {
        Owned(T::default())
    }
}


impl<'a, T> Clone for MaybeOwned<'a, T> where T: Clone {
    fn clone(&self) -> MaybeOwned<'a, T> {
        match *self {
            Owned(ref v) => Owned(v.clone()),
            Borrowed(v) => Borrowed(v)
        }
    }
}

impl<'a, 'b, A, B> PartialEq<MaybeOwned<'b, B>> for MaybeOwned<'a, A>
    where A: PartialEq<B>
{
    #[inline]
    fn eq(&self, other: &MaybeOwned<'b, B>) -> bool {
        PartialEq::eq(self.deref(), other.deref())
    }
}

impl<'a, T> Eq for MaybeOwned<'a, T> where T: Eq {}

impl<'a, T> PartialOrd for MaybeOwned<'a, T>
    where T: PartialOrd
{
    #[inline]
    fn partial_cmp(&self, other: &MaybeOwned<'a, T>) -> Option<Ordering> {
        PartialOrd::partial_cmp(self.deref(), other.deref())
    }
}

impl<'a, T> Ord for MaybeOwned<'a, T>
    where T: Ord
{
    #[inline]
    fn cmp(&self, other: &MaybeOwned<'a, T>) -> Ordering {
        Ord::cmp(self.deref(), other.deref())
    }
}

impl<'a, T> Hash for MaybeOwned<'a, T>
    where T: Hash
{
    #[inline]
    fn hash<H: Hasher>(&self, state: &mut H) {
        Hash::hash(self.deref(), state)
    }
}

impl<'a, T> fmt::Display for MaybeOwned<'a, T>
    where T: fmt::Display
{
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            Owned(ref o) => fmt::Display::fmt(o, f),
            Borrowed(b) => fmt::Display::fmt(b, f),
        }
    }
}

impl<'a, T> FromStr for MaybeOwned<'a, T>
    where T: FromStr
{
    type Err = T::Err;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Ok(MaybeOwned::Owned(T::from_str(s)?))
    }
}

impl<'a, T> MaybeOwned<'a, T> where T: Clone {

    /// Aquires a mutable reference to owned data.
    ///
    /// Clones data if it is not already owned.
    ///
    /// ## Example
    ///
    /// ```
    /// use maybe_owned::MaybeOwned;
    ///
    /// #[derive(Clone, Debug, PartialEq, Eq)]
    /// struct PseudoBigData(u8);
    ///
    /// let data = PseudoBigData(12);
    ///
    /// let mut maybe: MaybeOwned<PseudoBigData> = (&data).into();
    /// assert_eq!(false, maybe.is_owned());
    ///
    /// {
    ///     let reference = maybe.to_mut();
    ///     assert_eq!(&mut PseudoBigData(12), reference);
    /// }
    /// assert!(maybe.is_owned());
    /// ```
    ///
    pub fn to_mut(&mut self) -> &mut T {
        match *self {
            Owned(ref mut v) => v,
            Borrowed(v) => {
                *self = Owned(v.clone());
                match *self {
                    Owned(ref mut v) => v,
                    Borrowed(..) => unreachable!()
                }
            }

        }
    }

    /// Extracts the owned data.
    ///
    /// If the data is borrowed it is cloned before being extracted.
    pub fn into_owned(self) -> T {
        match self {
            Owned(v) => v,
            Borrowed(v) => v.clone()
        }
    }
}


#[cfg(test)]
mod tests {
    use super::MaybeOwned;

    type TestType = Vec<()>;

    fn with_into<'a, I: Into<MaybeOwned<'a, TestType>>>(v: I) -> MaybeOwned<'a, TestType> {
        v.into()
    }

    #[test]
    fn is_owned() {
        let data = TestType::default();
        assert!(MaybeOwned::Owned(data).is_owned());
    }

    #[test]
    fn into_with_owned() {
        //ty check if it accepts references
        let data = TestType::default();
        assert!(with_into(data).is_owned())

    }
    #[test]
    fn into_with_borrow() {
        //ty check if it accepts references
        let data = TestType::default();
        assert!(!with_into(&data).is_owned());
    }

    #[test]
    fn clone_owned() {
        let maybe = MaybeOwned::<TestType>::default();
        assert!(maybe.clone().is_owned());
    }

    #[test]
    fn clone_borrow() {
        let data = TestType::default();
        let maybe: MaybeOwned<TestType> = (&data).into();
        assert!(!maybe.clone().is_owned());
    }

    #[test]
    fn to_mut() {
        let data = TestType::default();
        let mut maybe: MaybeOwned<TestType> = (&data).into();
        assert!(!maybe.is_owned());
        {
            let _mut_ref = maybe.to_mut();
        }
        assert!(maybe.is_owned());
    }

    #[test]
    fn into_inner() {
        let data = vec![1u32,2];
        let maybe: MaybeOwned<Vec<u32>> = (&data).into();
        assert_eq!(data, maybe.into_owned());
    }


    #[test]
    fn has_default() {
        #[derive(Default)]
        struct TestType(u8);
        let _x: MaybeOwned<TestType> = Default::default();
    }

    #[test]
    fn has_clone() {
        #[derive(Clone)]
        struct TestType(u8);
        let _x  = TestType(12).clone();
    }

    #[test]
    fn has_partial_eq() {
        #[derive(PartialEq)]
        struct TestType(f32);

        let n = TestType(33.0);
        let a = MaybeOwned::Owned(TestType(42.0));
        let b = MaybeOwned::Borrowed(&n);
        let c = MaybeOwned::Owned(TestType(33.0));

        assert_eq!(a == b, false);
        assert_eq!(b == c, true);
        assert_eq!(c == a, false);
    }

    #[test]
    fn has_eq() {
        #[derive(PartialEq, Eq)]
        struct TestType(i32);

        let n = TestType(33);
        let a = MaybeOwned::Owned(TestType(42));
        let b = MaybeOwned::Borrowed(&n);
        let c = MaybeOwned::Owned(TestType(33));

        assert_eq!(a == b, false);
        assert_eq!(b == c, true);
        assert_eq!(c == a, false);
    }

    #[test]
    fn has_partial_ord() {
        #[derive(PartialEq, PartialOrd)]
        struct TestType(f32);

        let n = TestType(33.0);
        let a = MaybeOwned::Owned(TestType(42.0));
        let b = MaybeOwned::Borrowed(&n);
        let c = MaybeOwned::Owned(TestType(33.0));

        assert_eq!(a > b, true);
        assert_eq!(b > c, false);
        assert_eq!(a < c, false);
    }

    #[test]
    fn has_ord() {
        #[derive(PartialEq, Eq, PartialOrd, Ord)]
        struct TestType(i32);

        let n = TestType(33);
        let a = MaybeOwned::Owned(TestType(42));
        let b = MaybeOwned::Borrowed(&n);
        let c = MaybeOwned::Owned(TestType(33));

        assert_eq!(a > b, true);
        assert_eq!(b > c, false);
        assert_eq!(a < c, false);
    }

    #[test]
    fn has_hash() {
        use std::collections::HashMap;

        let mut map = HashMap::new();
        map.insert(MaybeOwned::Owned(42), 33);

        assert_eq!(map.get(&MaybeOwned::Borrowed(&42)), Some(&33));
    }

    #[test]
    fn has_display() {
        let n = 33;
        let a = MaybeOwned::Owned(42);
        let b = MaybeOwned::Borrowed(&n);

        let s = format!("{} {}", a, b);

        assert_eq!(s, "42 33");
    }

    #[test]
    fn from_cow() {
        use std::borrow::Cow;

        fn test<'a, V: Into<MaybeOwned<'a, i32>>>(v: V, n: i32) { assert_eq!(*v.into(), n) }

        let n = 33;
        test(Cow::Owned(42), 42);
        test(Cow::Borrowed(&n), n);
    }

    #[test]
    fn into_cow() {
        use std::borrow::Cow;

        fn test<'a, V: Into<Cow<'a, i32>>>(v: V, n: i32) { assert_eq!(*v.into(), n) }

        let n = 33;
        test(MaybeOwned::Owned(42), 42);
        test(MaybeOwned::Borrowed(&n), n);
    }

    #[test]
    fn from_str() {
        let as_string = "12";
        //assumption as_string is convertable to u32
        assert_eq!(12u32, as_string.parse().unwrap());
        assert_eq!(MaybeOwned::Owned(12u32), as_string.parse().unwrap());
    }

    #[test]
    fn as_ref() {
        let data  = TestType::default();
        let maybe_owned = MaybeOwned::Borrowed(&data);
        let _ref: &TestType = maybe_owned.as_ref();
        assert_eq!(
            &data as *const _ as usize,
            _ref as *const _ as usize
        );
    }

    #[test]
    fn borrow() {
        use std::borrow::Borrow;

        let data = TestType::default();
        let maybe_owned = MaybeOwned::Borrowed(&data);
        let _ref: &TestType = maybe_owned.borrow();
        assert_eq!(
            &data as *const _ as usize,
            _ref as *const _ as usize
        );
    }
}
