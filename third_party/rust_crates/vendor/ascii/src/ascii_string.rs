use std::{fmt, mem};
use std::borrow::Borrow;
use std::error::Error;
use std::any::Any;
use std::str::FromStr;
use std::ops::{Deref, DerefMut, Add, Index, IndexMut};
use std::iter::FromIterator;

#[cfg(feature = "quickcheck")]
use quickcheck::{Arbitrary, Gen};

use ascii_char::AsciiChar;
use ascii_str::{AsciiStr, AsAsciiStr, AsAsciiStrError};

/// A growable string stored as an ASCII encoded buffer.
#[derive(Clone, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct AsciiString {
    vec: Vec<AsciiChar>,
}

impl AsciiString {
    /// Creates a new, empty ASCII string buffer without allocating.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiString;
    /// let mut s = AsciiString::new();
    /// ```
    #[inline]
    pub fn new() -> Self {
        AsciiString { vec: Vec::new() }
    }

    /// Creates a new ASCII string buffer with the given capacity.
    /// The string will be able to hold exactly `capacity` bytes without reallocating.
    /// If `capacity` is 0, the ASCII string will not allocate.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiString;
    /// let mut s = AsciiString::with_capacity(10);
    /// ```
    #[inline]
    pub fn with_capacity(capacity: usize) -> Self {
        AsciiString { vec: Vec::with_capacity(capacity) }
    }

    /// Creates a new `AsciiString` from a length, capacity and pointer.
    ///
    /// # Safety
    ///
    /// This is highly unsafe, due to the number of invariants that aren't checked:
    ///
    /// * The memory at `ptr` need to have been previously allocated by the same allocator this
    ///   library uses.
    /// * `length` needs to be less than or equal to `capacity`.
    /// * `capacity` needs to be the correct value.
    ///
    /// Violating these may cause problems like corrupting the allocator's internal datastructures.
    ///
    /// # Examples
    ///
    /// Basic usage:
    ///
    /// ```
    /// # use ascii::AsciiString;
    /// use std::mem;
    ///
    /// unsafe {
    ///    let s = AsciiString::from_ascii("hello").unwrap();
    ///    let ptr = s.as_ptr();
    ///    let len = s.len();
    ///    let capacity = s.capacity();
    ///
    ///    mem::forget(s);
    ///
    ///    let s = AsciiString::from_raw_parts(ptr as *mut _, len, capacity);
    ///
    ///    assert_eq!(AsciiString::from_ascii("hello").unwrap(), s);
    /// }
    /// ```
    #[inline]
    pub unsafe fn from_raw_parts(buf: *mut AsciiChar, length: usize, capacity: usize) -> Self {
        AsciiString { vec: Vec::from_raw_parts(buf, length, capacity) }
    }

    /// Converts a vector of bytes to an `AsciiString` without checking for non-ASCII characters.
    ///
    /// # Safety
    /// This function is unsafe because it does not check that the bytes passed to it are valid
    /// ASCII characters. If this constraint is violated, it may cause memory unsafety issues with
    /// future of the `AsciiString`, as the rest of this library assumes that `AsciiString`s are
    /// ASCII encoded.
    #[inline]
    pub unsafe fn from_ascii_unchecked<B>(bytes: B) -> Self
    where
        B: Into<Vec<u8>>,
    {
        let mut bytes = bytes.into();
        let vec = Vec::from_raw_parts(
            bytes.as_mut_ptr() as *mut AsciiChar,
            bytes.len(),
            bytes.capacity(),
        );
        mem::forget(bytes);
        AsciiString { vec: vec }
    }

    /// Converts anything that can represent a byte buffer into an `AsciiString`.
    ///
    /// # Failure
    /// Returns the byte buffer if not all of the bytes are ASCII characters.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiString;
    /// let foo = AsciiString::from_ascii("foo".to_string()).unwrap();
    /// let err = AsciiString::from_ascii("Ŋ".to_string()).unwrap_err();
    /// assert_eq!(foo.as_str(), "foo");
    /// assert_eq!(err.into_source(), "Ŋ");
    /// ```
    pub fn from_ascii<B>(bytes: B) -> Result<AsciiString, FromAsciiError<B>>
    where
        B: Into<Vec<u8>> + AsRef<[u8]>,
    {
        unsafe {
            match bytes.as_ref().as_ascii_str() {
                Ok(_) => Ok(AsciiString::from_ascii_unchecked(bytes)),
                Err(e) => Err(FromAsciiError {
                    error: e,
                    owner: bytes,
                }),
            }
        }
    }

    /// Pushes the given ASCII string onto this ASCII string buffer.
    ///
    /// # Examples
    /// ```
    /// # use ascii::{AsciiString, AsAsciiStr};
    /// use std::str::FromStr;
    /// let mut s = AsciiString::from_str("foo").unwrap();
    /// s.push_str("bar".as_ascii_str().unwrap());
    /// assert_eq!(s, "foobar".as_ascii_str().unwrap());
    /// ```
    #[inline]
    pub fn push_str(&mut self, string: &AsciiStr) {
        self.vec.extend(string.chars())
    }

    /// Returns the number of bytes that this ASCII string buffer can hold without reallocating.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiString;
    /// let s = String::with_capacity(10);
    /// assert!(s.capacity() >= 10);
    /// ```
    #[inline]
    pub fn capacity(&self) -> usize {
        self.vec.capacity()
    }

    /// Reserves capacity for at least `additional` more bytes to be inserted in the given
    /// `AsciiString`. The collection may reserve more space to avoid frequent reallocations.
    ///
    /// # Panics
    /// Panics if the new capacity overflows `usize`.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiString;
    /// let mut s = AsciiString::new();
    /// s.reserve(10);
    /// assert!(s.capacity() >= 10);
    /// ```
    #[inline]
    pub fn reserve(&mut self, additional: usize) {
        self.vec.reserve(additional)
    }

    /// Reserves the minimum capacity for exactly `additional` more bytes to be inserted in the
    /// given `AsciiString`. Does nothing if the capacity is already sufficient.
    ///
    /// Note that the allocator may give the collection more space than it requests. Therefore
    /// capacity can not be relied upon to be precisely minimal. Prefer `reserve` if future
    /// insertions are expected.
    ///
    /// # Panics
    /// Panics if the new capacity overflows `usize`.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiString;
    /// let mut s = AsciiString::new();
    /// s.reserve_exact(10);
    /// assert!(s.capacity() >= 10);
    /// ```
    #[inline]
    pub fn reserve_exact(&mut self, additional: usize) {
        self.vec.reserve_exact(additional)
    }

    /// Shrinks the capacity of this ASCII string buffer to match it's length.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiString;
    /// use std::str::FromStr;
    /// let mut s = AsciiString::from_str("foo").unwrap();
    /// s.reserve(100);
    /// assert!(s.capacity() >= 100);
    /// s.shrink_to_fit();
    /// assert_eq!(s.capacity(), 3);
    /// ```
    #[inline]
    pub fn shrink_to_fit(&mut self) {
        self.vec.shrink_to_fit()
    }

    /// Adds the given ASCII character to the end of the ASCII string.
    ///
    /// # Examples
    /// ```
    /// # use ascii::{ AsciiChar, AsciiString};
    /// let mut s = AsciiString::from_ascii("abc").unwrap();
    /// s.push(AsciiChar::from('1').unwrap());
    /// s.push(AsciiChar::from('2').unwrap());
    /// s.push(AsciiChar::from('3').unwrap());
    /// assert_eq!(s, "abc123");
    /// ```
    #[inline]
    pub fn push(&mut self, ch: AsciiChar) {
        self.vec.push(ch)
    }

    /// Shortens a ASCII string to the specified length.
    ///
    /// # Panics
    /// Panics if `new_len` > current length.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiString;
    /// let mut s = AsciiString::from_ascii("hello").unwrap();
    /// s.truncate(2);
    /// assert_eq!(s, "he");
    /// ```
    #[inline]
    pub fn truncate(&mut self, new_len: usize) {
        self.vec.truncate(new_len)
    }

    /// Removes the last character from the ASCII string buffer and returns it.
    /// Returns `None` if this string buffer is empty.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiString;
    /// let mut s = AsciiString::from_ascii("foo").unwrap();
    /// assert_eq!(s.pop().map(|c| c.as_char()), Some('o'));
    /// assert_eq!(s.pop().map(|c| c.as_char()), Some('o'));
    /// assert_eq!(s.pop().map(|c| c.as_char()), Some('f'));
    /// assert_eq!(s.pop(), None);
    /// ```
    #[inline]
    pub fn pop(&mut self) -> Option<AsciiChar> {
        self.vec.pop()
    }

    /// Removes the ASCII character at position `idx` from the buffer and returns it.
    ///
    /// # Warning
    /// This is an O(n) operation as it requires copying every element in the buffer.
    ///
    /// # Panics
    /// If `idx` is out of bounds this function will panic.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiString;
    /// let mut s = AsciiString::from_ascii("foo").unwrap();
    /// assert_eq!(s.remove(0).as_char(), 'f');
    /// assert_eq!(s.remove(1).as_char(), 'o');
    /// assert_eq!(s.remove(0).as_char(), 'o');
    /// ```
    #[inline]
    pub fn remove(&mut self, idx: usize) -> AsciiChar {
        self.vec.remove(idx)
    }

    /// Inserts an ASCII character into the buffer at position `idx`.
    ///
    /// # Warning
    /// This is an O(n) operation as it requires copying every element in the buffer.
    ///
    /// # Panics
    /// If `idx` is out of bounds this function will panic.
    ///
    /// # Examples
    /// ```
    /// # use ascii::{AsciiString,AsciiChar};
    /// let mut s = AsciiString::from_ascii("foo").unwrap();
    /// s.insert(2, AsciiChar::b);
    /// assert_eq!(s, "fobo");
    /// ```
    #[inline]
    pub fn insert(&mut self, idx: usize, ch: AsciiChar) {
        self.vec.insert(idx, ch)
    }

    /// Returns the number of bytes in this ASCII string.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiString;
    /// let s = AsciiString::from_ascii("foo").unwrap();
    /// assert_eq!(s.len(), 3);
    /// ```
    #[inline]
    pub fn len(&self) -> usize {
        self.vec.len()
    }

    /// Returns true if the ASCII string contains zero bytes.
    ///
    /// # Examples
    /// ```
    /// # use ascii::{AsciiChar, AsciiString};
    /// let mut s = AsciiString::new();
    /// assert!(s.is_empty());
    /// s.push(AsciiChar::from('a').unwrap());
    /// assert!(!s.is_empty());
    /// ```
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Truncates the ASCII string, setting length (but not capacity) to zero.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiString;
    /// let mut s = AsciiString::from_ascii("foo").unwrap();
    /// s.clear();
    /// assert!(s.is_empty());
    /// ```
    #[inline]
    pub fn clear(&mut self) {
        self.vec.clear()
    }
}

impl Deref for AsciiString {
    type Target = AsciiStr;

    #[inline]
    fn deref(&self) -> &AsciiStr {
        let ptr = &*self.vec as *const [AsciiChar] as *const AsciiStr;
        unsafe { &*ptr }
    }
}

impl DerefMut for AsciiString {
    #[inline]
    fn deref_mut(&mut self) -> &mut AsciiStr {
        let ptr = &mut *self.vec as *mut [AsciiChar] as *mut AsciiStr;
        unsafe { &mut *ptr }
    }
}

impl PartialEq<str> for AsciiString {
    #[inline]
    fn eq(&self, other: &str) -> bool {
        **self == *other
    }
}

impl PartialEq<AsciiString> for str {
    #[inline]
    fn eq(&self, other: &AsciiString) -> bool {
        **other == *self
    }
}

macro_rules! impl_eq {
    ($lhs:ty, $rhs:ty) => {
        impl<'a> PartialEq<$rhs> for $lhs {
            #[inline]
            fn eq(&self, other: &$rhs) -> bool {
                PartialEq::eq(&**self, &**other)
            }
            #[inline]
            fn ne(&self, other: &$rhs) -> bool {
                PartialEq::ne(&**self, &**other)
            }
        }
    }
}

impl_eq! { AsciiString, String }
impl_eq! { String, AsciiString }
impl_eq! { &'a AsciiStr, String }
impl_eq! { String, &'a AsciiStr }
impl_eq! { &'a AsciiStr, AsciiString }
impl_eq! { AsciiString, &'a AsciiStr }
impl_eq! { &'a str, AsciiString }
impl_eq! { AsciiString, &'a str }

impl Borrow<AsciiStr> for AsciiString {
    #[inline]
    fn borrow(&self) -> &AsciiStr {
        &*self
    }
}

impl From<Vec<AsciiChar>> for AsciiString {
    #[inline]
    fn from(vec: Vec<AsciiChar>) -> Self {
        AsciiString { vec: vec }
    }
}

impl Into<Vec<u8>> for AsciiString {
    fn into(self) -> Vec<u8> {
        unsafe {
            let v = Vec::from_raw_parts(
                self.vec.as_ptr() as *mut u8,
                self.vec.len(),
                self.vec.capacity(),
            );

            // We forget `self` to avoid freeing it at the end of the scope.
            // Otherwise, the returned `Vec` would point to freed memory.
            mem::forget(self);
            v
        }
    }
}

impl Into<String> for AsciiString {
    #[inline]
    fn into(self) -> String {
        unsafe { String::from_utf8_unchecked(self.into()) }
    }
}

impl AsRef<AsciiStr> for AsciiString {
    #[inline]
    fn as_ref(&self) -> &AsciiStr {
        &*self
    }
}

impl AsRef<[u8]> for AsciiString {
    #[inline]
    fn as_ref(&self) -> &[u8] {
        self.as_bytes()
    }
}

impl AsMut<AsciiStr> for AsciiString {
    #[inline]
    fn as_mut(&mut self) -> &mut AsciiStr {
        &mut *self
    }
}

impl FromStr for AsciiString {
    type Err = AsAsciiStrError;

    fn from_str(s: &str) -> Result<AsciiString, AsAsciiStrError> {
        s.as_ascii_str().map(AsciiStr::to_ascii_string)
    }
}

impl fmt::Display for AsciiString {
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        fmt::Display::fmt(&**self, f)
    }
}

impl fmt::Debug for AsciiString {
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        fmt::Debug::fmt(&**self, f)
    }
}

/// Please note that the `std::fmt::Result` returned by these methods does not support
/// transmission of an error other than that an error occurred.
impl fmt::Write for AsciiString {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        let astr = try!(AsciiStr::from_ascii(s).map_err(|_| fmt::Error));
        self.push_str(astr);
        Ok(())
    }

    fn write_char(&mut self, c: char) -> fmt::Result {
        let achar = try!(AsciiChar::from(c).map_err(|_| fmt::Error));
        self.push(achar);
        Ok(())
    }
}

impl FromIterator<AsciiChar> for AsciiString {
    fn from_iter<I: IntoIterator<Item = AsciiChar>>(iter: I) -> AsciiString {
        let mut buf = AsciiString::new();
        buf.extend(iter);
        buf
    }
}

impl<'a> FromIterator<&'a AsciiStr> for AsciiString {
    fn from_iter<I: IntoIterator<Item = &'a AsciiStr>>(iter: I) -> AsciiString {
        let mut buf = AsciiString::new();
        buf.extend(iter);
        buf
    }
}

impl Extend<AsciiChar> for AsciiString {
    fn extend<I: IntoIterator<Item = AsciiChar>>(&mut self, iterable: I) {
        let iterator = iterable.into_iter();
        let (lower_bound, _) = iterator.size_hint();
        self.reserve(lower_bound);
        for ch in iterator {
            self.push(ch)
        }
    }
}

impl<'a> Extend<&'a AsciiChar> for AsciiString {
    fn extend<I: IntoIterator<Item = &'a AsciiChar>>(&mut self, iter: I) {
        self.extend(iter.into_iter().cloned())
    }
}

impl<'a> Extend<&'a AsciiStr> for AsciiString {
    fn extend<I: IntoIterator<Item = &'a AsciiStr>>(&mut self, iterable: I) {
        let iterator = iterable.into_iter();
        let (lower_bound, _) = iterator.size_hint();
        self.reserve(lower_bound);
        for s in iterator {
            self.push_str(s)
        }
    }
}

impl<'a> Add<&'a AsciiStr> for AsciiString {
    type Output = AsciiString;

    #[inline]
    fn add(mut self, other: &AsciiStr) -> AsciiString {
        self.push_str(other);
        self
    }
}

impl<T> Index<T> for AsciiString
where
    AsciiStr: Index<T>,
{
    type Output = <AsciiStr as Index<T>>::Output;

    #[inline]
    fn index(&self, index: T) -> &<AsciiStr as Index<T>>::Output {
        &(**self)[index]
    }
}

impl<T> IndexMut<T> for AsciiString
where
    AsciiStr: IndexMut<T>,
{
    #[inline]
    fn index_mut(&mut self, index: T) -> &mut <AsciiStr as Index<T>>::Output {
        &mut (**self)[index]
    }
}


/// A possible error value when converting an `AsciiString` from a byte vector or string.
/// It wraps an `AsAsciiStrError` which you can get through the `ascii_error()` method.
///
/// This is the error type for `AsciiString::from_ascii()` and
/// `IntoAsciiString::into_ascii_string()`. They will never clone or touch the content of the
/// original type; It can be extracted by the `into_source` method.
///
/// #Examples
/// ```
/// # use ascii::IntoAsciiString;
/// let err = "bø!".to_string().into_ascii_string().unwrap_err();
/// assert_eq!(err.ascii_error().valid_up_to(), 1);
/// assert_eq!(err.into_source(), "bø!".to_string());
/// ```
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct FromAsciiError<O> {
    error: AsAsciiStrError,
    owner: O,
}
impl<O> FromAsciiError<O> {
    /// Get the position of the first non-ASCII byte or character.
    #[inline]
    pub fn ascii_error(&self) -> AsAsciiStrError {
        self.error
    }
    /// Get back the original, unmodified type.
    #[inline]
    pub fn into_source(self) -> O {
        self.owner
    }
}
impl<O> fmt::Debug for FromAsciiError<O> {
    #[inline]
    fn fmt(&self, fmtr: &mut fmt::Formatter) -> fmt::Result {
        fmt::Debug::fmt(&self.error, fmtr)
    }
}
impl<O> fmt::Display for FromAsciiError<O> {
    #[inline]
    fn fmt(&self, fmtr: &mut fmt::Formatter) -> fmt::Result {
        fmt::Display::fmt(&self.error, fmtr)
    }
}
impl<O: Any> Error for FromAsciiError<O> {
    #[inline]
    fn description(&self) -> &str {
        self.error.description()
    }
    /// Always returns an `AsAsciiStrError`
    fn cause(&self) -> Option<&Error> {
        Some(&self.error as &Error)
    }
}


/// Convert vectors into `AsciiString`.
pub trait IntoAsciiString: Sized {
    /// Convert to `AsciiString` without checking for non-ASCII characters.
    unsafe fn into_ascii_string_unchecked(self) -> AsciiString;
    /// Convert to `AsciiString`.
    fn into_ascii_string(self) -> Result<AsciiString, FromAsciiError<Self>>;
}

impl IntoAsciiString for AsciiString {
    #[inline]
    unsafe fn into_ascii_string_unchecked(self) -> AsciiString {
        self
    }
    #[inline]
    fn into_ascii_string(self) -> Result<Self, FromAsciiError<Self>> {
        Ok(self)
    }
}

impl IntoAsciiString for Vec<AsciiChar> {
    #[inline]
    unsafe fn into_ascii_string_unchecked(self) -> AsciiString {
        AsciiString::from(self)
    }
    #[inline]
    fn into_ascii_string(self) -> Result<AsciiString, FromAsciiError<Self>> {
        Ok(AsciiString::from(self))
    }
}

impl IntoAsciiString for Vec<u8> {
    #[inline]
    unsafe fn into_ascii_string_unchecked(self) -> AsciiString {
        AsciiString::from_ascii_unchecked(self)
    }
    #[inline]
    fn into_ascii_string(self) -> Result<AsciiString, FromAsciiError<Self>> {
        AsciiString::from_ascii(self)
    }
}

impl<'a> IntoAsciiString for &'a [u8] {
    #[inline]
    unsafe fn into_ascii_string_unchecked(self) -> AsciiString {
        AsciiString::from_ascii_unchecked(self)
    }
    #[inline]
    fn into_ascii_string(self) -> Result<AsciiString, FromAsciiError<Self>> {
        AsciiString::from_ascii(self)
    }
}

impl IntoAsciiString for String {
    #[inline]
    unsafe fn into_ascii_string_unchecked(self) -> AsciiString {
        AsciiString::from_ascii_unchecked(self)
    }
    #[inline]
    fn into_ascii_string(self) -> Result<AsciiString, FromAsciiError<Self>> {
        AsciiString::from_ascii(self)
    }
}

impl<'a> IntoAsciiString for &'a str {
    #[inline]
    unsafe fn into_ascii_string_unchecked(self) -> AsciiString {
        AsciiString::from_ascii_unchecked(self)
    }
    #[inline]
    fn into_ascii_string(self) -> Result<AsciiString, FromAsciiError<Self>> {
        AsciiString::from_ascii(self)
    }
}

#[cfg(feature = "quickcheck")]
impl Arbitrary for AsciiString {
    fn arbitrary<G: Gen>(g: &mut G) -> Self {
        let size = {
            let s = g.size();
            g.gen_range(0, s)
        };
        let mut s = AsciiString::with_capacity(size);
        for _ in 0..size {
            s.push(AsciiChar::arbitrary(g));
        }
        s
    }

    fn shrink(&self) -> Box<Iterator<Item = Self>> {
        let chars: Vec<AsciiChar> = self.as_slice().to_vec();
        Box::new(chars.shrink().map(
            |x| x.into_iter().collect::<AsciiString>(),
        ))
    }
}

#[cfg(test)]
mod tests {
    use std::str::FromStr;
    use AsciiChar;
    use super::{AsciiString, IntoAsciiString};

    #[test]
    fn into_string() {
        let v = AsciiString::from_ascii(&[40_u8, 32, 59][..]).unwrap();
        assert_eq!(Into::<String>::into(v), "( ;".to_string());
    }

    #[test]
    fn into_bytes() {
        let v = AsciiString::from_ascii(&[40_u8, 32, 59][..]).unwrap();
        assert_eq!(Into::<Vec<u8>>::into(v), vec![40_u8, 32, 59])
    }

    #[test]
    fn from_ascii_vec() {
        let vec = vec![AsciiChar::from('A').unwrap(), AsciiChar::from('B').unwrap()];
        assert_eq!(AsciiString::from(vec), AsciiString::from_str("AB").unwrap());
    }

    #[test]
    fn fmt_ascii_string() {
        let s = "abc".to_string().into_ascii_string().unwrap();
        assert_eq!(format!("{}", s), "abc".to_string());
        assert_eq!(format!("{:?}", s), "\"abc\"".to_string());
    }

    #[test]
    fn write_fmt() {
        use std::{fmt, str};

        let mut s0 = AsciiString::new();
        fmt::write(&mut s0, format_args!("Hello World")).unwrap();
        assert_eq!(s0, "Hello World");

        let mut s1 = AsciiString::new();
        fmt::write(&mut s1, format_args!("{}", 9)).unwrap();
        assert_eq!(s1, "9");

        let mut s2 = AsciiString::new();
        let sparkle_heart_bytes = [240, 159, 146, 150];
        let sparkle_heart = str::from_utf8(&sparkle_heart_bytes).unwrap();
        assert!(fmt::write(&mut s2, format_args!("{}", sparkle_heart)).is_err());
    }
}
