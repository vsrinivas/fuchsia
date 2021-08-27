// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    arbitrary::Arbitrary,
    std::{
        borrow::Cow,
        fmt,
        ops::{Deref, Index},
        slice::SliceIndex,
        str,
    },
};

/// A newtype wrapping a boxed slice with bytes representing an SSID
/// element as defined by IEEE 802.11-2016 9.4.2.2.
///
/// Bytes representing an SSID read from a FIDL struct or persistent
/// storage should be immediately wrapped by this type to ensure proper
/// type-checking for their use as an SSID elsewhere in WLAN components.
///
/// The wrapped bytes are kept private to enforce the use of the [`from`]
/// method to create a value of type [`Ssid`]. This feature will eventually
/// be used to enforce an assertion that [`Ssid`] never contains a
/// boxed slice with more than 32 bytes.
///
/// [`from`]: crate::ssid::Ssid::from
/// [`Ssid`]: crate::ssid::Ssid
///
/// # Examples
///
/// Usage:
/// ```
/// let ssid: Ssid = Ssid::from("foo");
/// assert_eq!(ssid, Ssid([0x66, 0x6F, 0x6F].into()));
///
/// let ssid: Ssid = Ssid::from([1, 2, 3, 4, 5]);
/// assert_eq!(ssid, Ssid([0x01, 0x02, 0x03, 0x04, 0x05].into()));
///
/// let ssid: Ssid = Ssid::from(vec![1, 2, 3, 4, 5]);
/// assert_eq!(ssid, Ssid([0x01, 0x02, 0x03, 0x04, 0x05].into()));
///
/// assert_eq!(ssid, [0x01, 0x02, 0x03, 0x04, 0x05]);
/// assert_eq!(ssid, vec![0x01, 0x02, 0x03, 0x04, 0x05]);
/// ```
#[derive(Arbitrary)] // Derive Arbitrary for fuzzer
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct Ssid(Box<[u8]>);

impl AsRef<[u8]> for Ssid {
    fn as_ref(&self) -> &[u8] {
        &self.0[..]
    }
}

impl Deref for Ssid {
    type Target = [u8];

    fn deref(&self) -> &[u8] {
        &self.0
    }
}

impl fmt::Display for Ssid {
    /// Return an SSID formatted as <ssid-BYTES> where BYTES are the bytes of the
    /// SSID encoded as uppercase hexadecimal characters.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "<ssid-{}>", hex::encode(self.0.clone()))
    }
}

impl From<Ssid> for String {
    fn from(ssid: Ssid) -> String {
        ssid.to_string()
    }
}

impl From<Ssid> for Vec<u8> {
    fn from(ssid: Ssid) -> Vec<u8> {
        ssid.0.into_vec()
    }
}

impl From<&String> for Ssid {
    fn from(s: &String) -> Ssid {
        s.as_bytes().into()
    }
}

impl From<String> for Ssid {
    fn from(s: String) -> Ssid {
        s.into_bytes().into()
    }
}

impl From<&Vec<u8>> for Ssid {
    fn from(vec: &Vec<u8>) -> Ssid {
        vec.clone().into()
    }
}

impl From<Vec<u8>> for Ssid {
    fn from(vec: Vec<u8>) -> Ssid {
        Ssid(vec.into_boxed_slice())
    }
}

impl From<&str> for Ssid {
    fn from(s: &str) -> Ssid {
        s.as_bytes().into()
    }
}

impl<const N: usize> From<[u8; N]> for Ssid {
    fn from(array: [u8; N]) -> Ssid {
        Ssid(array.into())
    }
}

impl From<&[u8]> for Ssid {
    fn from(slice: &[u8]) -> Ssid {
        Ssid(slice.into())
    }
}

impl<Idx> Index<Idx> for Ssid
where
    Idx: SliceIndex<[u8]>,
{
    type Output = Idx::Output;

    fn index(&self, index: Idx) -> &Self::Output {
        &self.0[index]
    }
}

impl PartialEq<Ssid> for Vec<u8> {
    fn eq(&self, other: &Ssid) -> bool {
        Ssid::deref(other)[..] == self[..]
    }
}

impl PartialEq<Vec<u8>> for Ssid {
    fn eq(&self, other: &Vec<u8>) -> bool {
        Ssid::deref(self)[..] == other[..]
    }
}

impl<const N: usize> PartialEq<[u8; N]> for Ssid {
    fn eq(&self, other: &[u8; N]) -> bool {
        Ssid::deref(self)[..] == other[..]
    }
}

impl<const N: usize> PartialEq<Ssid> for [u8; N] {
    fn eq(&self, other: &Ssid) -> bool {
        Ssid::deref(other)[..] == self[..]
    }
}

impl PartialEq<[u8]> for Ssid {
    fn eq(&self, other: &[u8]) -> bool {
        Ssid::deref(self)[..] == other[..]
    }
}

impl PartialEq<Ssid> for [u8] {
    fn eq(&self, other: &Ssid) -> bool {
        Ssid::deref(other)[..] == self[..]
    }
}

impl Ssid {
    /// Return an SSID formatted as a UTF-8 string, or <ssid-BYTES> if a UTF-8 error
    /// is encountered.
    pub fn to_string_not_redactable(&self) -> Cow<'_, str> {
        str::from_utf8(self.as_ref()).map(From::from).unwrap_or_else(|_| self.to_string().into())
    }

    pub fn empty() -> Ssid {
        Ssid([].into())
    }

    pub fn len(&self) -> usize {
        self.0.len()
    }

    pub fn to_vec(&self) -> Vec<u8> {
        self.0.clone().into()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use lazy_static::lazy_static;

    lazy_static! {
        static ref SSID_12345: Ssid = Ssid([0x01, 0x02, 0x03, 0x04, 0x05].into());
        static ref SSID_FOO: Ssid = Ssid([0x66, 0x6F, 0x6F].into());
    }

    #[test]
    fn ssid_as_ref() {
        let array_12345: [u8; 5] = [0x01, 0x02, 0x03, 0x04, 0x05];
        assert_eq!((*SSID_12345).as_ref(), array_12345.as_ref());
    }

    #[test]
    fn ssid_deref() {
        let array_12345: [u8; 5] = [0x01, 0x02, 0x03, 0x04, 0x05];
        assert_eq!(**SSID_12345, array_12345);
    }

    #[test]
    fn ssid_to_string() {
        assert_eq!(Ssid::empty().to_string(), "<ssid->");
        assert_eq!(SSID_FOO.to_string(), "<ssid-666f6f>");
        assert_eq!(SSID_12345.to_string(), "<ssid-0102030405>");
    }

    #[test]
    fn format_ssid() {
        assert_eq!(format!("{}", *SSID_12345), "<ssid-0102030405>");
    }

    #[test]
    fn ssid_into_vec() {
        assert_eq!(
            <Ssid as Into<Vec<u8>>>::into(SSID_12345.clone()),
            vec![0x01, 0x02, 0x03, 0x04, 0x05]
        );
    }

    #[test]
    fn ssid_from_borrowed_string() {
        let ssid: Ssid = <Ssid as From<&String>>::from(&String::from("foo"));
        assert_eq!(ssid, *SSID_FOO);
    }

    #[test]
    fn ssid_from_string() {
        let ssid: Ssid = <Ssid as From<String>>::from(String::from("foo"));
        assert_eq!(ssid, *SSID_FOO);
    }

    #[test]
    fn ssid_from_borrowed_vec() {
        let ssid: Ssid = <Ssid as From<&Vec<u8>>>::from(&vec![0x01, 0x02, 0x03, 0x04, 0x05]);
        assert_eq!(ssid, *SSID_12345);
    }

    #[test]
    fn ssid_from_vec() {
        let ssid: Ssid = <Ssid as From<Vec<u8>>>::from(vec![0x01, 0x02, 0x03, 0x04, 0x05]);
        assert_eq!(ssid, *SSID_12345);
    }

    #[test]
    fn ssid_from_str() {
        let ssid: Ssid = <Ssid as From<&str>>::from("foo");
        assert_eq!(ssid, *SSID_FOO);
    }

    #[test]
    fn ssid_from_array() {
        let ssid: Ssid = <Ssid as From<[u8; 5]>>::from([0x01, 0x02, 0x03, 0x04, 0x05]);
        assert_eq!(ssid, *SSID_12345);
    }

    #[test]
    fn ssid_from_slice() {
        let ssid: Ssid = <Ssid as From<&[u8]>>::from(&[0x01, 0x02, 0x03, 0x04, 0x05]);
        assert_eq!(ssid, *SSID_12345);
    }

    #[test]
    fn ssid_index() {
        assert_eq!(SSID_12345[0], 0x01);
        assert_eq!(SSID_12345[1], 0x02);
        assert_eq!(SSID_12345[2], 0x03);
        assert_eq!(SSID_12345[3], 0x04);
        assert_eq!(SSID_12345[4], 0x05);
    }

    #[test]
    fn ssid_partial_eq_vec() {
        assert_eq!(vec![], Ssid::empty());
        assert_eq!(Ssid::empty(), vec![]);

        assert_eq!(vec![1, 2, 3], Ssid([1, 2, 3].into()));
        assert_eq!(Ssid([1, 2, 3].into()), vec![1, 2, 3]);

        assert_ne!(vec![1, 2], Ssid([1, 2, 3].into()));
        assert_ne!(Ssid([1, 2, 3].into()), vec![1, 2]);
    }

    #[test]
    fn ssid_partial_eq_array() {
        assert_eq!([], Ssid::empty());
        assert_eq!(Ssid::empty(), []);

        assert_eq!([1, 2, 3], Ssid([1, 2, 3].into()));
        assert_eq!(Ssid([1, 2, 3].into()), [1, 2, 3]);

        assert_ne!([1, 2], Ssid([1, 2, 3].into()));
        assert_ne!(Ssid([1, 2, 3].into()), [1, 2]);
    }

    #[test]
    fn ssid_partial_eq_slice() {
        assert_eq!(&b""[..], &Ssid::empty());
        assert_eq!(&Ssid::empty(), &b""[..]);

        assert_eq!(&[1, 2, 3][..], &Ssid([1, 2, 3].into()));
        assert_eq!(&Ssid([1, 2, 3].into()), &[1, 2, 3][..]);

        assert_ne!(&[1, 2][..], &Ssid([1, 2, 3].into()));
        assert_ne!(&Ssid([1, 2, 3].into()), &[1, 2][..]);
    }

    #[test]
    fn ssid_to_string_not_redactable() {
        assert_eq!(Ssid::empty().to_string_not_redactable(), "");

        let sparkle_heart_ssid: Ssid = "💖".into();
        assert_eq!(sparkle_heart_ssid.to_string_not_redactable(), "💖");

        let invalid_utf8_ssid: Ssid = [0x00, 0x9f, 0x92, 0x96].as_ref().into();
        assert_eq!(invalid_utf8_ssid.to_string_not_redactable(), "<ssid-009f9296>");
    }

    #[test]
    fn ssid_empty() {
        assert_eq!(Ssid::empty(), Ssid([].into()));
        assert_eq!(vec![], Ssid::empty());
        assert_eq!(Ssid::empty().to_string(), "<ssid->");
        assert_eq!(Ssid::empty().to_string_not_redactable(), "");
    }

    #[test]
    fn ssid_len() {
        assert_eq!(Ssid::empty().len(), 0);
        assert_eq!(SSID_FOO.len(), 3);
        assert_eq!(SSID_12345.len(), 5);
    }

    #[test]
    fn ssid_to_vec() {
        let ssid = SSID_12345.clone();
        assert_eq!(ssid.to_vec(), vec![0x01, 0x02, 0x03, 0x04, 0x05]);
        // ssid not dropped
        assert_eq!(<Ssid as Into<Vec<u8>>>::into(ssid), vec![0x01, 0x02, 0x03, 0x04, 0x05]);
    }
}
