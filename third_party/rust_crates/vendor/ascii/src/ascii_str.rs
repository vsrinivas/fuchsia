// #[allow(deprecated)] doesn't silence warnings on the method invocations,
// which would call the inherent methods if AsciiExt wasn't in scope.
#![cfg_attr(feature = "std", allow(deprecated))]

use core::{fmt, mem};
use core::ops::{Index, IndexMut, Range, RangeTo, RangeFrom, RangeFull};
use core::slice::{Iter, IterMut};
#[cfg(feature = "std")]
use std::error::Error;
#[cfg(feature = "std")]
use std::ascii::AsciiExt;

use ascii_char::AsciiChar;
#[cfg(feature = "std")]
use ascii_string::AsciiString;

/// AsciiStr represents a byte or string slice that only contains ASCII characters.
///
/// It wraps an `[AsciiChar]` and implements many of `str`s methods and traits.
///
/// It can be created by a checked conversion from a `str` or `[u8]`, or borrowed from an
/// `AsciiString`.
#[derive(PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct AsciiStr {
    slice: [AsciiChar],
}

impl AsciiStr {
    /// Coerces into an `AsciiStr` slice.
    pub fn new<S: AsRef<AsciiStr> + ?Sized>(s: &S) -> &AsciiStr {
        s.as_ref()
    }

    /// Converts `&self` to a `&str` slice.
    #[inline]
    pub fn as_str(&self) -> &str {
        let ptr = self as *const AsciiStr as *const str;
        unsafe { &*ptr }
    }

    /// Converts `&self` into a byte slice.
    #[inline]
    pub fn as_bytes(&self) -> &[u8] {
        let ptr = self as *const AsciiStr as *const [u8];
        unsafe { &*ptr }
    }

    /// Returns the entire string as slice of `AsciiChar`s.
    #[inline]
    pub fn as_slice(&self) -> &[AsciiChar] {
        &self.slice
    }

    /// Returns the entire string as mutable slice of `AsciiChar`s.
    #[inline]
    pub fn as_mut_slice(&mut self) -> &mut [AsciiChar] {
        &mut self.slice
    }

    /// Returns a raw pointer to the `AsciiStr`'s buffer.
    ///
    /// The caller must ensure that the slice outlives the pointer this function returns, or else it
    /// will end up pointing to garbage. Modifying the `AsciiStr` may cause it's buffer to be
    /// reallocated, which would also make any pointers to it invalid.
    #[inline]
    pub fn as_ptr(&self) -> *const AsciiChar {
        self.as_slice().as_ptr()
    }

    /// Returns an unsafe mutable pointer to the `AsciiStr`'s buffer.
    ///
    /// The caller must ensure that the slice outlives the pointer this function returns, or else it
    /// will end up pointing to garbage. Modifying the `AsciiStr` may cause it's buffer to be
    /// reallocated, which would also make any pointers to it invalid.
    #[inline]
    pub fn as_mut_ptr(&mut self) -> *mut AsciiChar {
        self.as_mut_slice().as_mut_ptr()
    }

    /// Copies the content of this `AsciiStr` into an owned `AsciiString`.
    #[cfg(feature = "std")]
    pub fn to_ascii_string(&self) -> AsciiString {
        AsciiString::from(self.slice.to_vec())
    }

    /// Converts anything that can represent a byte slice into an `AsciiStr`.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiStr;
    /// let foo = AsciiStr::from_ascii("foo");
    /// let err = AsciiStr::from_ascii("Ŋ");
    /// assert_eq!(foo.unwrap().as_str(), "foo");
    /// assert_eq!(err.unwrap_err().valid_up_to(), 0);
    /// ```
    #[inline]
    pub fn from_ascii<B: ?Sized>(bytes: &B) -> Result<&AsciiStr, AsAsciiStrError>
    where
        B: AsRef<[u8]>,
    {
        bytes.as_ref().as_ascii_str()
    }

    /// Converts anything that can be represented as a byte slice to an `AsciiStr` without checking
    /// for non-ASCII characters..
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiStr;
    /// let foo = unsafe{ AsciiStr::from_ascii_unchecked("foo") };
    /// assert_eq!(foo.as_str(), "foo");
    /// ```
    #[inline]
    pub unsafe fn from_ascii_unchecked<B: ?Sized>(bytes: &B) -> &AsciiStr
    where
        B: AsRef<[u8]>,
    {
        bytes.as_ref().as_ascii_str_unchecked()
    }

    /// Returns the number of characters / bytes in this ASCII sequence.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiStr;
    /// let s = AsciiStr::from_ascii("foo").unwrap();
    /// assert_eq!(s.len(), 3);
    /// ```
    #[inline]
    pub fn len(&self) -> usize {
        self.slice.len()
    }

    /// Returns true if the ASCII slice contains zero bytes.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiStr;
    /// let mut empty = AsciiStr::from_ascii("").unwrap();
    /// let mut full = AsciiStr::from_ascii("foo").unwrap();
    /// assert!(empty.is_empty());
    /// assert!(!full.is_empty());
    /// ```
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Returns an iterator over the characters of the `AsciiStr`.
    #[inline]
    pub fn chars(&self) -> Chars {
        self.slice.iter()
    }

    /// Returns an iterator over the characters of the `AsciiStr` which allows you to modify the
    /// value of each `AsciiChar`.
    #[inline]
    pub fn chars_mut(&mut self) -> CharsMut {
        self.slice.iter_mut()
    }

    /// Returns an iterator over the lines of the `AsciiStr`, which are themselves `AsciiStr`s.
    ///
    /// Lines are ended with either `LineFeed` (`\n`), or `CarriageReturn` then `LineFeed` (`\r\n`).
    ///
    /// The final line ending is optional.
    #[inline]
    pub fn lines(&self) -> Lines {
        Lines {
            current_index: 0,
            string: self,
        }
    }

    /// Returns an ASCII string slice with leading and trailing whitespace removed.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiStr;
    /// let example = AsciiStr::from_ascii("  \twhite \tspace  \t").unwrap();
    /// assert_eq!("white \tspace", example.trim());
    /// ```
    pub fn trim(&self) -> &Self {
        self.trim_right().trim_left()
    }

    /// Returns an ASCII string slice with leading whitespace removed.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiStr;
    /// let example = AsciiStr::from_ascii("  \twhite \tspace  \t").unwrap();
    /// assert_eq!("white \tspace  \t", example.trim_left());
    /// ```
    pub fn trim_left(&self) -> &Self {
        &self[self.chars().take_while(|a| a.is_whitespace()).count()..]
    }

    /// Returns an ASCII string slice with trailing whitespace removed.
    ///
    /// # Examples
    /// ```
    /// # use ascii::AsciiStr;
    /// let example = AsciiStr::from_ascii("  \twhite \tspace  \t").unwrap();
    /// assert_eq!("  \twhite \tspace", example.trim_right());
    /// ```
    pub fn trim_right(&self) -> &Self {
        let trimmed = self.chars()
            .rev()
            .take_while(|a| a.is_whitespace())
            .count();
        &self[..self.len() - trimmed]
    }

    /// Compares two strings case-insensitively.
    pub fn eq_ignore_ascii_case(&self, other: &Self) -> bool {
        self.len() == other.len() &&
            self.chars().zip(other.chars()).all(|(a, b)| {
                a.eq_ignore_ascii_case(b)
            })
    }

    /// Replaces lowercase letters with their uppercase equivalent.
    pub fn make_ascii_uppercase(&mut self) {
        for a in self.chars_mut() {
            *a = a.to_ascii_uppercase();
        }
    }

    /// Replaces uppercase letters with their lowercase equivalent.
    pub fn make_ascii_lowercase(&mut self) {
        for a in self.chars_mut() {
            *a = a.to_ascii_lowercase();
        }
    }

    /// Returns a copy of this string where letters 'a' to 'z' are mapped to 'A' to 'Z'.
    #[cfg(feature="std")]
    pub fn to_ascii_uppercase(&self) -> AsciiString {
        let mut ascii_string = self.to_ascii_string();
        ascii_string.make_ascii_uppercase();
        ascii_string
    }

    /// Returns a copy of this string where letters 'A' to 'Z' are mapped to 'a' to 'z'.
    #[cfg(feature="std")]
    pub fn to_ascii_lowercase(&self) -> AsciiString {
        let mut ascii_string = self.to_ascii_string();
        ascii_string.make_ascii_lowercase();
        ascii_string
    }
}

impl PartialEq<str> for AsciiStr {
    #[inline]
    fn eq(&self, other: &str) -> bool {
        self.as_str() == other
    }
}

impl PartialEq<AsciiStr> for str {
    #[inline]
    fn eq(&self, other: &AsciiStr) -> bool {
        other.as_str() == self
    }
}

#[cfg(feature = "std")]
impl ToOwned for AsciiStr {
    type Owned = AsciiString;

    #[inline]
    fn to_owned(&self) -> AsciiString {
        self.to_ascii_string()
    }
}

impl AsRef<[u8]> for AsciiStr {
    #[inline]
    fn as_ref(&self) -> &[u8] {
        self.as_bytes()
    }
}
impl AsRef<str> for AsciiStr {
    #[inline]
    fn as_ref(&self) -> &str {
        self.as_str()
    }
}
impl AsRef<[AsciiChar]> for AsciiStr {
    #[inline]
    fn as_ref(&self) -> &[AsciiChar] {
        &self.slice
    }
}
impl AsMut<[AsciiChar]> for AsciiStr {
    #[inline]
    fn as_mut(&mut self) -> &mut [AsciiChar] {
        &mut self.slice
    }
}

impl Default for &'static AsciiStr {
    #[inline]
    fn default() -> &'static AsciiStr {
        unsafe { "".as_ascii_str_unchecked() }
    }
}
impl<'a> From<&'a [AsciiChar]> for &'a AsciiStr {
    #[inline]
    fn from(slice: &[AsciiChar]) -> &AsciiStr {
        let ptr = slice as *const [AsciiChar] as *const AsciiStr;
        unsafe { &*ptr }
    }
}
impl<'a> From<&'a mut [AsciiChar]> for &'a mut AsciiStr {
    #[inline]
    fn from(slice: &mut [AsciiChar]) -> &mut AsciiStr {
        let ptr = slice as *mut [AsciiChar] as *mut AsciiStr;
        unsafe { &mut *ptr }
    }
}
#[cfg(feature = "std")]
impl From<Box<[AsciiChar]>> for Box<AsciiStr> {
    #[inline]
    fn from(owned: Box<[AsciiChar]>) -> Box<AsciiStr> {
        let ptr = Box::into_raw(owned) as *mut AsciiStr;
        unsafe { Box::from_raw(ptr) }
    }
}

macro_rules! impl_into {
    ($wider: ty) => {
        impl<'a> From<&'a AsciiStr> for &'a$wider {
            #[inline]
            fn from(slice: &AsciiStr) -> &$wider {
                let ptr = slice as *const AsciiStr as *const $wider;
                unsafe { &*ptr }
            }
        }
        impl<'a> From<&'a mut AsciiStr> for &'a mut $wider {
            #[inline]
            fn from(slice: &mut AsciiStr) -> &mut $wider {
                let ptr = slice as *mut AsciiStr as *mut $wider;
                unsafe { &mut *ptr }
            }
        }
        #[cfg(feature = "std")]
        impl From<Box<AsciiStr>> for Box<$wider> {
            #[inline]
            fn from(owned: Box<AsciiStr>) -> Box<$wider> {
                let ptr = Box::into_raw(owned) as *mut $wider;
                unsafe { Box::from_raw(ptr) }
            }
        }
    }
}
impl_into! {[AsciiChar]}
impl_into! {[u8]}
impl_into! {str}

impl fmt::Display for AsciiStr {
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        fmt::Display::fmt(self.as_str(), f)
    }
}

impl fmt::Debug for AsciiStr {
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        fmt::Debug::fmt(self.as_str(), f)
    }
}

macro_rules! impl_index {
    ($idx:ty) => {
        impl Index<$idx> for AsciiStr {
            type Output = AsciiStr;

            #[inline]
            fn index(&self, index: $idx) -> &AsciiStr {
                let ptr = &self.slice[index] as *const [AsciiChar] as *const AsciiStr;
                unsafe { &* ptr }
            }
        }

        impl IndexMut<$idx> for AsciiStr {
            #[inline]
            fn index_mut(&mut self, index: $idx) -> &mut AsciiStr {
                let ptr = &mut self.slice[index] as *mut [AsciiChar] as *mut AsciiStr;
                unsafe { &mut *ptr }
            }
        }
    }
}

impl_index! { Range<usize> }
impl_index! { RangeTo<usize> }
impl_index! { RangeFrom<usize> }
impl_index! { RangeFull }

impl Index<usize> for AsciiStr {
    type Output = AsciiChar;

    #[inline]
    fn index(&self, index: usize) -> &AsciiChar {
        unsafe { mem::transmute(&self.slice[index]) }
    }
}

impl IndexMut<usize> for AsciiStr {
    #[inline]
    fn index_mut(&mut self, index: usize) -> &mut AsciiChar {
        unsafe { mem::transmute(&mut self.slice[index]) }
    }
}

#[cfg(feature = "std")]
impl AsciiExt for AsciiStr {
    type Owned = AsciiString;

    #[inline]
    fn is_ascii(&self) -> bool {
        true
    }

    fn to_ascii_uppercase(&self) -> AsciiString {
        let mut ascii_string = self.to_ascii_string();
        ascii_string.make_ascii_uppercase();
        ascii_string
    }

    fn to_ascii_lowercase(&self) -> AsciiString {
        let mut ascii_string = self.to_ascii_string();
        ascii_string.make_ascii_lowercase();
        ascii_string
    }

    fn eq_ignore_ascii_case(&self, other: &Self) -> bool {
        self.len() == other.len() &&
            self.chars().zip(other.chars()).all(|(a, b)| {
                a.eq_ignore_ascii_case(b)
            })
    }

    fn make_ascii_uppercase(&mut self) {
        for ascii in self.chars_mut() {
            ascii.make_ascii_uppercase();
        }
    }

    fn make_ascii_lowercase(&mut self) {
        for ascii in self.chars_mut() {
            ascii.make_ascii_lowercase();
        }
    }
}

impl<'a> IntoIterator for &'a AsciiStr {
    type Item = &'a AsciiChar;
    type IntoIter = Chars<'a>;
    #[inline]
    fn into_iter(self) -> Self::IntoIter {
        self.chars()
    }
}

impl<'a> IntoIterator for &'a mut AsciiStr {
    type Item = &'a mut AsciiChar;
    type IntoIter = CharsMut<'a>;
    #[inline]
    fn into_iter(self) -> Self::IntoIter {
        self.chars_mut()
    }
}

/// An immutable iterator over the characters of an `AsciiStr`.
pub type Chars<'a> = Iter<'a, AsciiChar>;

/// A mutable iterator over the characters of an `AsciiStr`.
pub type CharsMut<'a> = IterMut<'a, AsciiChar>;

/// An iterator over the lines of the internal character array.
#[derive(Clone, Debug)]
pub struct Lines<'a> {
    current_index: usize,
    string: &'a AsciiStr,
}

impl<'a> Iterator for Lines<'a> {
    type Item = &'a AsciiStr;

    fn next(&mut self) -> Option<Self::Item> {
        let curr_idx = self.current_index;
        let len = self.string.len();
        if curr_idx >= len {
            return None;
        }

        let mut next_idx = None;
        let mut linebreak_skip = 0;

        for i in curr_idx..(len - 1) {
            match (self.string[i], self.string[i + 1]) {
                (AsciiChar::CarriageReturn, AsciiChar::LineFeed) => {
                    next_idx = Some(i);
                    linebreak_skip = 2;
                    break;
                }
                (AsciiChar::LineFeed, _) => {
                    next_idx = Some(i);
                    linebreak_skip = 1;
                    break;
                }
                _ => {}
            }
        }

        let next_idx = match next_idx {
            Some(i) => i,
            None => return None,
        };
        let line = &self.string[curr_idx..next_idx];

        self.current_index = next_idx + linebreak_skip;

        if line.is_empty() && self.current_index == self.string.len() {
            // This is a trailing line break
            None
        } else {
            Some(line)
        }
    }
}

/// Error that is returned when a sequence of `u8` are not all ASCII.
///
/// Is used by `As[Mut]AsciiStr` and the `from_ascii` method on `AsciiStr` and `AsciiString`.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct AsAsciiStrError(usize);

const ERRORMSG_STR: &'static str = "one or more bytes are not ASCII";

impl AsAsciiStrError {
    /// Returns the index of the first non-ASCII byte.
    ///
    /// It is the maximum index such that `from_ascii(input[..index])` would return `Ok(_)`.
    #[inline]
    pub fn valid_up_to(self) -> usize {
        self.0
    }
    #[cfg(not(feature = "std"))]
    /// Returns a description for this error, like `std::error::Error::description`.
    #[inline]
    pub fn description(&self) -> &'static str {
        ERRORMSG_STR
    }
}
impl fmt::Display for AsAsciiStrError {
    fn fmt(&self, fmtr: &mut fmt::Formatter) -> fmt::Result {
        write!(fmtr, "the byte at index {} is not ASCII", self.0)
    }
}
#[cfg(feature = "std")]
impl Error for AsAsciiStrError {
    #[inline]
    fn description(&self) -> &'static str {
        ERRORMSG_STR
    }
}


/// Convert slices of bytes to `AsciiStr`.
pub trait AsAsciiStr {
    /// Convert to an ASCII slice without checking for non-ASCII characters.
    unsafe fn as_ascii_str_unchecked(&self) -> &AsciiStr;
    /// Convert to an ASCII slice.
    fn as_ascii_str(&self) -> Result<&AsciiStr, AsAsciiStrError>;
}

/// Convert mutable slices of bytes to `AsciiStr`.
pub trait AsMutAsciiStr {
    /// Convert to a mutable ASCII slice without checking for non-ASCII characters.
    unsafe fn as_mut_ascii_str_unchecked(&mut self) -> &mut AsciiStr;
    /// Convert to a mutable ASCII slice.
    fn as_mut_ascii_str(&mut self) -> Result<&mut AsciiStr, AsAsciiStrError>;
}

// These generic implementations mirror the generic implementations for AsRef<T> in core.
impl<'a, T: ?Sized> AsAsciiStr for &'a T where T: AsAsciiStr {
    #[inline]
    fn as_ascii_str(&self) -> Result<&AsciiStr, AsAsciiStrError> {
        <T as AsAsciiStr>::as_ascii_str(*self)
    }

    #[inline]
    unsafe fn as_ascii_str_unchecked(&self) -> &AsciiStr {
        <T as AsAsciiStr>::as_ascii_str_unchecked(*self)
    }
}

impl<'a, T: ?Sized> AsAsciiStr for &'a mut T where T: AsAsciiStr {
    #[inline]
    fn as_ascii_str(&self) -> Result<&AsciiStr, AsAsciiStrError> {
        <T as AsAsciiStr>::as_ascii_str(*self)
    }

    #[inline]
    unsafe fn as_ascii_str_unchecked(&self) -> &AsciiStr {
        <T as AsAsciiStr>::as_ascii_str_unchecked(*self)
    }
}

impl<'a, T: ?Sized> AsMutAsciiStr for &'a mut T where T: AsMutAsciiStr {
    #[inline]
    fn as_mut_ascii_str(&mut self) -> Result<&mut AsciiStr, AsAsciiStrError> {
        <T as AsMutAsciiStr>::as_mut_ascii_str(*self)
    }

    #[inline]
    unsafe fn as_mut_ascii_str_unchecked(&mut self) -> &mut AsciiStr {
        <T as AsMutAsciiStr>::as_mut_ascii_str_unchecked(*self)
    }
}

impl AsAsciiStr for AsciiStr {
    #[inline]
    fn as_ascii_str(&self) -> Result<&AsciiStr, AsAsciiStrError> {
        Ok(self)
    }
    #[inline]
    unsafe fn as_ascii_str_unchecked(&self) -> &AsciiStr {
        self
    }
}
impl AsMutAsciiStr for AsciiStr {
    #[inline]
    fn as_mut_ascii_str(&mut self) -> Result<&mut AsciiStr, AsAsciiStrError> {
        Ok(self)
    }
    #[inline]
    unsafe fn as_mut_ascii_str_unchecked(&mut self) -> &mut AsciiStr {
        self
    }
}

impl AsAsciiStr for [AsciiChar] {
    #[inline]
    fn as_ascii_str(&self) -> Result<&AsciiStr, AsAsciiStrError> {
        Ok(self.into())
    }
    #[inline]
    unsafe fn as_ascii_str_unchecked(&self) -> &AsciiStr {
        self.into()
    }
}
impl AsMutAsciiStr for [AsciiChar] {
    #[inline]
    fn as_mut_ascii_str(&mut self) -> Result<&mut AsciiStr, AsAsciiStrError> {
        Ok(self.into())
    }
    #[inline]
    unsafe fn as_mut_ascii_str_unchecked(&mut self) -> &mut AsciiStr {
        self.into()
    }
}

impl AsAsciiStr for [u8] {
    fn as_ascii_str(&self) -> Result<&AsciiStr, AsAsciiStrError> {
        match self.iter().position(|&b| b > 127) {
            Some(index) => Err(AsAsciiStrError(index)),
            None => unsafe { Ok(self.as_ascii_str_unchecked()) },
        }
    }
    #[inline]
    unsafe fn as_ascii_str_unchecked(&self) -> &AsciiStr {
        let ptr = self as *const [u8] as *const AsciiStr;
        &*ptr
    }
}
impl AsMutAsciiStr for [u8] {
    fn as_mut_ascii_str(&mut self) -> Result<&mut AsciiStr, AsAsciiStrError> {
        match self.iter().position(|&b| b > 127) {
            Some(index) => Err(AsAsciiStrError(index)),
            None => unsafe { Ok(self.as_mut_ascii_str_unchecked()) },
        }
    }
    #[inline]
    unsafe fn as_mut_ascii_str_unchecked(&mut self) -> &mut AsciiStr {
        let ptr = self as *mut [u8] as *mut AsciiStr;
        &mut *ptr
    }
}

impl AsAsciiStr for str {
    fn as_ascii_str(&self) -> Result<&AsciiStr, AsAsciiStrError> {
        self.as_bytes().as_ascii_str()
    }
    #[inline]
    unsafe fn as_ascii_str_unchecked(&self) -> &AsciiStr {
        self.as_bytes().as_ascii_str_unchecked()
    }
}
impl AsMutAsciiStr for str {
    fn as_mut_ascii_str(&mut self) -> Result<&mut AsciiStr, AsAsciiStrError> {
        match self.bytes().position(|b| b > 127) {
            Some(index) => Err(AsAsciiStrError(index)),
            None => unsafe { Ok(self.as_mut_ascii_str_unchecked()) },
        }
    }
    #[inline]
    unsafe fn as_mut_ascii_str_unchecked(&mut self) -> &mut AsciiStr {
        let ptr = self as *mut str as *mut AsciiStr;
        &mut *ptr
    }
}


#[cfg(test)]
mod tests {
    use AsciiChar;
    use super::{AsciiStr, AsAsciiStr, AsMutAsciiStr, AsAsciiStrError};

    #[test]
    fn generic_as_ascii_str() {
        fn generic<C: AsAsciiStr + ?Sized>(c: &C) -> Result<&AsciiStr, AsAsciiStrError> {
            c.as_ascii_str()
        }
        let arr = [AsciiChar::A];
        let ascii_str: &AsciiStr = arr.as_ref().into();
        assert_eq!(generic("A"), Ok(ascii_str));
        assert_eq!(generic(&b"A"[..]), Ok(ascii_str));
        assert_eq!(generic(ascii_str), Ok(ascii_str));
        assert_eq!(generic(&"A"), Ok(ascii_str));
        assert_eq!(generic(&ascii_str), Ok(ascii_str));
        assert_eq!(generic(&mut "A"), Ok(ascii_str));
    }

    #[test]
    fn generic_as_mut_ascii_str() {
        fn generic_mut<C: AsMutAsciiStr + ?Sized>(
            c: &mut C,
        ) -> Result<&mut AsciiStr, AsAsciiStrError> {
            c.as_mut_ascii_str()
        }

        let mut arr_mut = [AsciiChar::B];
        let mut ascii_str_mut: &mut AsciiStr = arr_mut.as_mut().into();
        // Need a second reference to prevent overlapping mutable borrows
        let mut arr_mut_2 = [AsciiChar::B];
        let ascii_str_mut_2: &mut AsciiStr = arr_mut_2.as_mut().into();
        assert_eq!(generic_mut(&mut ascii_str_mut), Ok(&mut *ascii_str_mut_2));
        assert_eq!(generic_mut(ascii_str_mut), Ok(&mut *ascii_str_mut_2));
    }

    #[test]
    #[cfg(feature = "std")]
    fn as_ascii_str() {
        macro_rules! err {{$i:expr} => {Err(AsAsciiStrError($i))}}
        let mut s: String = "abčd".to_string();
        let mut b: Vec<u8> = s.clone().into();
        assert_eq!(s.as_str().as_ascii_str(), err!(2));
        assert_eq!(s.as_mut_str().as_mut_ascii_str(), err!(2));
        assert_eq!(b.as_slice().as_ascii_str(), err!(2));
        assert_eq!(b.as_mut_slice().as_mut_ascii_str(), err!(2));
        let mut a = [AsciiChar::a, AsciiChar::b];
        assert_eq!((&s[..2]).as_ascii_str(), Ok((&a[..]).into()));
        assert_eq!((&b[..2]).as_ascii_str(), Ok((&a[..]).into()));
        let a = Ok((&mut a[..]).into());
        assert_eq!((&mut s[..2]).as_mut_ascii_str(), a);
        assert_eq!((&mut b[..2]).as_mut_ascii_str(), a);
    }

    #[test]
    fn default() {
        let default: &'static AsciiStr = Default::default();
        assert!(default.is_empty());
    }

    #[test]
    fn as_str() {
        let b = b"( ;";
        let v = AsciiStr::from_ascii(b).unwrap();
        assert_eq!(v.as_str(), "( ;");
        assert_eq!(AsRef::<str>::as_ref(v), "( ;");
    }

    #[test]
    fn as_bytes() {
        let b = b"( ;";
        let v = AsciiStr::from_ascii(b).unwrap();
        assert_eq!(v.as_bytes(), b"( ;");
        assert_eq!(AsRef::<[u8]>::as_ref(v), b"( ;");
    }

    #[test]
    fn make_ascii_case() {
        let mut bytes = ([b'a', b'@', b'A'], [b'A', b'@', b'a']);
        let a = bytes.0.as_mut_ascii_str().unwrap();
        let b = bytes.1.as_mut_ascii_str().unwrap();
        assert!(a.eq_ignore_ascii_case(b));
        assert!(b.eq_ignore_ascii_case(a));
        a.make_ascii_lowercase();
        b.make_ascii_uppercase();
        assert_eq!(a, "a@a");
        assert_eq!(b, "A@A");
    }

    #[test]
    #[cfg(feature = "std")]
    fn to_ascii_case() {
        let bytes = ([b'a', b'@', b'A'], [b'A', b'@', b'a']);
        let a = bytes.0.as_ascii_str().unwrap();
        let b = bytes.1.as_ascii_str().unwrap();
        assert_eq!(a.to_ascii_lowercase().as_str(), "a@a");
        assert_eq!(a.to_ascii_uppercase().as_str(), "A@A");
        assert_eq!(b.to_ascii_lowercase().as_str(), "a@a");
        assert_eq!(b.to_ascii_uppercase().as_str(), "A@A");
    }

    #[test]
    #[cfg(feature = "std")]
    fn ascii_ext() {
        #[allow(deprecated)]
        use std::ascii::AsciiExt;
        assert!(AsciiExt::is_ascii(<&AsciiStr>::default()));
        let mut mutable = String::from("a@AA@a");
        let parts = mutable.split_at_mut(3);
        let a = parts.0.as_mut_ascii_str().unwrap();
        let b = parts.1.as_mut_ascii_str().unwrap();
        assert!(AsciiExt::eq_ignore_ascii_case(a, b));
        assert_eq!(AsciiExt::to_ascii_lowercase(a).as_str(), "a@a");
        assert_eq!(AsciiExt::to_ascii_uppercase(b).as_str(), "A@A");
        AsciiExt::make_ascii_uppercase(a);
        AsciiExt::make_ascii_lowercase(b);
        assert_eq!(a, "A@A");
        assert_eq!(b, "a@a");
    }

    #[test]
    fn chars_iter() {
        let chars = &[b'h', b'e', b'l', b'l', b'o', b' ', b'w', b'o', b'r', b'l', b'd', b'\0'];
        let ascii = AsciiStr::from_ascii(chars).unwrap();
        for (achar, byte) in ascii.chars().zip(chars.iter()) {
            assert_eq!(achar, byte);
        }
    }

    #[test]
    fn chars_iter_mut() {
        let chars = &mut [b'h', b'e', b'l', b'l', b'o', b' ', b'w', b'o', b'r', b'l', b'd', b'\0'];
        let ascii = chars.as_mut_ascii_str().unwrap();

        *ascii.chars_mut().next().unwrap() = AsciiChar::H;

        assert_eq!(ascii[0], b'H');
    }

    #[test]
    fn lines_iter() {
        use core::iter::Iterator;
        let lines: [&str; 3] = ["great work", "cool beans", "awesome stuff"];
        let joined = "great work\ncool beans\r\nawesome stuff\n";
        let ascii = AsciiStr::from_ascii(joined.as_bytes()).unwrap();
        for (asciiline, line) in ascii.lines().zip(&lines) {
            assert_eq!(asciiline, *line);
        }

        let trailing_line_break = b"\n";
        let ascii = AsciiStr::from_ascii(&trailing_line_break).unwrap();
        for _ in ascii.lines() {
            unreachable!();
        }

        let empty_lines = b"\n\r\n\n\r\n";
        let mut ensure_iterated = false;
        let ascii = AsciiStr::from_ascii(&empty_lines).unwrap();
        for line in ascii.lines() {
            ensure_iterated = true;
            assert!(line.is_empty());
        }
        assert!(ensure_iterated);
    }

    #[test]
    #[cfg(feature = "std")]
    fn fmt_ascii_str() {
        let s = "abc".as_ascii_str().unwrap();
        assert_eq!(format!("{}", s), "abc".to_string());
        assert_eq!(format!("{:?}", s), "\"abc\"".to_string());
    }
}
