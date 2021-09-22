// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    arbitrary::Arbitrary,
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211,
    std::{
        borrow::Cow,
        convert::{TryFrom, TryInto},
        fmt,
        ops::{Deref, Index},
        slice::SliceIndex,
        str,
    },
    thiserror::Error,
};

#[derive(Debug, Error)]
#[cfg_attr(test, derive(PartialEq, Eq))]
#[non_exhaustive]
pub enum SsidError {
    #[error("Invalid SSID length: {0} bytes (maximum is {})", fidl_ieee80211::MAX_SSID_BYTE_LEN)]
    Size(usize),
}

/// A newtype wrapping a boxed slice with bytes representing an SSID
/// element as defined by IEEE 802.11-2016 9.4.2.2.
///
/// Bytes representing an SSID read from a FIDL struct or persistent
/// storage should be immediately wrapped by this type to ensure proper
/// type-checking for their use as an SSID elsewhere in WLAN components.
///
/// The wrapped bytes are kept private to enforce the use of defined
/// methods to create a value of type [`Ssid`]. This ensures an [`Ssid`]
/// never contains more than the limit of 32 bytes.
///
/// [`Ssid`]: crate::ssid::Ssid
///
/// # Examples
///
/// Usage:
/// ```
/// let ssid: Ssid = Ssid::try_from("foo").unwrap();
/// assert_eq!(ssid, Ssid([0x66, 0x6F, 0x6F].into()));
///
/// let ssid: Ssid = Ssid::try_from([1, 2, 3, 4, 5]).unwrap();
/// assert_eq!(ssid, Ssid([0x01, 0x02, 0x03, 0x04, 0x05].into()));
///
/// let ssid: Ssid = Ssid::try_from(vec![1, 2, 3, 4, 5]).unwrap();
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

impl TryFrom<Box<[u8]>> for Ssid {
    type Error = SsidError;

    fn try_from(bytes: Box<[u8]>) -> Result<Self, Self::Error> {
        Ssid::check(&bytes)?;
        Ok(Ssid(bytes))
    }
}

impl<const N: usize> TryFrom<[u8; N]> for Ssid {
    type Error = SsidError;

    fn try_from(bytes: [u8; N]) -> Result<Self, Self::Error> {
        Ssid::check(&bytes)?;
        Ok(Ssid(bytes.into()))
    }
}

impl TryFrom<Box<str>> for Ssid {
    type Error = SsidError;

    fn try_from(s: Box<str>) -> Result<Self, Self::Error> {
        s.into_boxed_bytes().try_into()
    }
}

impl TryFrom<String> for Ssid {
    type Error = SsidError;

    fn try_from(s: String) -> Result<Self, Self::Error> {
        s.into_boxed_str().try_into()
    }
}

impl TryFrom<Vec<u8>> for Ssid {
    type Error = SsidError;

    fn try_from(bytes: Vec<u8>) -> Result<Self, Self::Error> {
        bytes.into_boxed_slice().try_into()
    }
}

impl TryFrom<&str> for Ssid {
    type Error = SsidError;

    fn try_from(s: &str) -> Result<Self, Self::Error> {
        Ssid::check(s.as_bytes())?;
        Ok(Ssid(String::from(s).into_boxed_str().into_boxed_bytes()))
    }
}

impl TryFrom<&[u8]> for Ssid {
    type Error = SsidError;

    fn try_from(s: &[u8]) -> Result<Self, Self::Error> {
        Ssid::check(s)?;
        Ok(Ssid(s.to_vec().into_boxed_slice()))
    }
}

impl Ssid {
    /// Returns an `Ssid` for `ssid_bytes` from a source that already enforces the size of
    /// bytes does not exceed fidl_ieee80211::MAX_SSID_BYTE_LEN. This function should only
    /// be used when the caller is certain the limit is not exceeded since the check is
    /// intentionally skipped.
    ///
    /// For example, fuchsia.wlan.* FIDL messages use the fuchsia.wlan.ieee80211/Ssid alias
    /// for all messages that accept or return an SSID. This alias imposes the required
    /// maximum limit on SSID in the FIDL message itself and is therefore impossible to
    /// exceed.
    pub fn from_bytes_unchecked(ssid_bytes: Vec<u8>) -> Ssid {
        Ssid(ssid_bytes.into_boxed_slice())
    }

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

    fn check(bytes: &[u8]) -> Result<(), SsidError> {
        if bytes.len() > (fidl_ieee80211::MAX_SSID_BYTE_LEN as usize) {
            return Err(SsidError::Size(bytes.len()));
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use lazy_static::lazy_static;

    lazy_static! {
        static ref SSID_12345: Ssid = Ssid::try_from([0x01, 0x02, 0x03, 0x04, 0x05]).unwrap();
        static ref SSID_FOO: Ssid = Ssid::try_from([0x66, 0x6F, 0x6F]).unwrap();
    }

    #[test]
    fn ssid_check() {
        assert_eq!(Ok(()), Ssid::check(&[0x2; 0]));
        assert_eq!(Ok(()), Ssid::check(&[0x2; 20]));
        assert_eq!(Ok(()), Ssid::check(&[0x2; 32]));
        assert_eq!(Err(SsidError::Size(33)), Ssid::check(&[0x2; 33]));
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
    fn ssid_try_from_boxed_slice_ok() {
        let ssid: Ssid =
            <Ssid as TryFrom<Box<[u8]>>>::try_from(Box::from([0x01, 0x02, 0x03, 0x04, 0x05]))
                .expect("Failed to convert Box<[u8]> to Ssid");
        assert_eq!(ssid, *SSID_12345);
    }

    #[test]
    fn ssid_try_from_array_ok() {
        let array = [0x01, 0x02, 0x03, 0x04, 0x05];
        let ssid: Ssid =
            <Ssid as TryFrom<[u8; 5]>>::try_from(array).expect("Failed to convert [u8; 5] to Ssid");
        assert_eq!(ssid, *SSID_12345);
    }

    #[test]
    fn ssid_try_from_boxed_str_ok() {
        let ssid: Ssid =
            <Ssid as TryFrom<Box<str>>>::try_from(String::from("foo").into_boxed_str())
                .expect("Failed to convert Box<str> to Ssid");
        assert_eq!(ssid, *SSID_FOO);
    }

    #[test]
    fn ssid_try_from_string_ok() {
        let ssid: Ssid = <Ssid as TryFrom<String>>::try_from(String::from("foo"))
            .expect("Failed to convert String to Ssid");
        assert_eq!(ssid, *SSID_FOO);
    }

    #[test]
    fn ssid_try_from_vec_ok() {
        let ssid: Ssid = <Ssid as TryFrom<Vec<u8>>>::try_from(vec![0x01, 0x02, 0x03, 0x04, 0x05])
            .expect("Failed to convert Vec<u8> to Ssid");
        assert_eq!(ssid, *SSID_12345);
    }

    #[test]
    fn ssid_try_from_str_ok() {
        let ssid: Ssid =
            <Ssid as TryFrom<&str>>::try_from("foo").expect("Failed to convert &str to Ssid");
        assert_eq!(ssid, *SSID_FOO);
    }

    #[test]
    fn ssid_try_from_slice_ok() {
        let ssid: Ssid = <Ssid as TryFrom<&[u8]>>::try_from(&[0x01, 0x02, 0x03, 0x04, 0x05])
            .expect("Failed to convert &[u8] to Ssid");
        assert_eq!(ssid, *SSID_12345);
    }

    #[test]
    fn ssid_try_from_array_err() {
        let ssid: Result<Ssid, SsidError> = <Ssid as TryFrom<[u8; 32]>>::try_from([0x03; 32]);
        assert!(matches!(ssid, Ok(_)));
        let ssid: Result<Ssid, SsidError> = <Ssid as TryFrom<[u8; 50]>>::try_from([0x03; 50]);
        assert!(matches!(ssid, Err(SsidError::Size(50))));
    }

    #[test]
    fn ssid_try_from_string_err() {
        let ssid: Result<Ssid, SsidError> =
            <Ssid as TryFrom<String>>::try_from(String::from("12345678901234567890123456789012"));
        assert!(matches!(ssid, Ok(_)));
        let ssid: Result<Ssid, SsidError> =
            <Ssid as TryFrom<String>>::try_from(String::from("123456789012345678901234567890123"));
        assert!(matches!(ssid, Err(SsidError::Size(33))));
    }

    #[test]
    fn ssid_try_from_vec_err() {
        let ssid: Result<Ssid, SsidError> = <Ssid as TryFrom<Vec<u8>>>::try_from(vec![0x07; 32]);
        assert!(matches!(ssid, Ok(_)));
        let ssid: Result<Ssid, SsidError> = <Ssid as TryFrom<Vec<u8>>>::try_from(vec![0x07; 100]);
        assert!(matches!(ssid, Err(SsidError::Size(100))));
    }

    #[test]
    fn ssid_try_from_str_err() {
        let ssid: Result<Ssid, SsidError> =
            <Ssid as TryFrom<&str>>::try_from("12345678901234567890123456789012");
        assert!(matches!(ssid, Ok(_)));
        let ssid: Result<Ssid, SsidError> =
            <Ssid as TryFrom<&str>>::try_from("123456789012345678901234567890123");
        assert!(matches!(ssid, Err(SsidError::Size(33))));
    }

    #[test]
    fn ssid_try_from_slice_err() {
        let ssid: Result<Ssid, SsidError> = <Ssid as TryFrom<&[u8]>>::try_from(&[0x01; 32]);
        assert!(matches!(ssid, Ok(_)));
        let ssid: Result<Ssid, SsidError> = <Ssid as TryFrom<&[u8]>>::try_from(&[0x01; 33]);
        assert!(matches!(ssid, Err(SsidError::Size(33))));
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

        assert_eq!(vec![1, 2, 3], Ssid::try_from([1, 2, 3]).unwrap());
        assert_eq!(Ssid::try_from([1, 2, 3]).unwrap(), vec![1, 2, 3]);

        assert_ne!(vec![1, 2], Ssid::try_from([1, 2, 3]).unwrap());
        assert_ne!(Ssid::try_from([1, 2, 3]).unwrap(), vec![1, 2]);
    }

    #[test]
    fn ssid_partial_eq_array() {
        assert_eq!([], Ssid::empty());
        assert_eq!(Ssid::empty(), []);

        assert_eq!([1, 2, 3], Ssid::try_from([1, 2, 3]).unwrap());
        assert_eq!(Ssid::try_from([1, 2, 3]).unwrap(), [1, 2, 3]);

        assert_ne!([1, 2], Ssid::try_from([1, 2, 3]).unwrap());
        assert_ne!(Ssid::try_from([1, 2, 3]).unwrap(), [1, 2]);
    }

    #[test]
    fn ssid_partial_eq_slice() {
        assert_eq!(&b""[..], &Ssid::empty());
        assert_eq!(&Ssid::empty(), &b""[..]);

        assert_eq!(&[1, 2, 3][..], &Ssid::try_from([1, 2, 3]).unwrap());
        assert_eq!(&Ssid::try_from([1, 2, 3]).unwrap(), &[1, 2, 3][..]);

        assert_ne!(&[1, 2][..], &Ssid::try_from([1, 2, 3]).unwrap());
        assert_ne!(&Ssid::try_from([1, 2, 3]).unwrap(), &[1, 2][..]);
    }

    #[test]
    fn ssid_to_string_not_redactable() {
        assert_eq!(Ssid::empty().to_string_not_redactable(), "");

        let sparkle_heart_ssid: Ssid = Ssid::try_from("💖").unwrap();
        assert_eq!(sparkle_heart_ssid.to_string_not_redactable(), "💖");

        let invalid_utf8_ssid: Ssid = Ssid::try_from([0x00, 0x9f, 0x92, 0x96]).unwrap();
        assert_eq!(invalid_utf8_ssid.to_string_not_redactable(), "<ssid-009f9296>");
    }

    #[test]
    fn ssid_empty() {
        assert_eq!(Ssid::empty(), Ssid::try_from([]).unwrap());
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
