// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use anyhow::Context;
use byteorder::{ByteOrder, LittleEndian, WriteBytesExt};
use core::hash::Hash;
use std::collections::HashSet;
use std::convert::TryInto;

/// Private convenience macro for defining the appropriate
/// traits for primitive types with fixed encoding lengths.
macro_rules! def_fixed_len(
    ($t:ty, $len:expr, |$pack_buf:ident, $pack_var:ident| $pack_block:expr, | $unpack_buf:ident | $unpack_block:expr) => {
        def_fixed_len!($t, $len, $t, |$pack_buf, $pack_var|$pack_block, | $unpack_buf | $unpack_block);
    };
    ($t:ty, $len:expr, $pack_as:ty,  |$pack_buf:ident, $pack_var:ident| $pack_block:expr, | $unpack_buf:ident | $unpack_block:expr) => {
        impl TryPackAs<$pack_as> for $t {
            fn pack_as_len(&self) -> io::Result<usize> {
                Ok(<$t>::FIXED_LEN)
            }

            fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
                let pack = | $pack_buf: &mut T, $pack_var: $t | $pack_block;
                //let pack: dyn Fn(&mut T, $t) -> io::Result<()> = $pack_block;

                pack (buffer, *self).map(|_|<$t>::FIXED_LEN)
            }
        }

        impl SpinelFixedLen for $t {
            const FIXED_LEN: usize = $len;
        }

        impl TryPack for $t {
            fn pack_len(&self) -> io::Result<usize> {
                TryPackAs::<$pack_as>::pack_as_len(self)
            }

            fn try_pack<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
                TryPackAs::<$pack_as>::try_pack_as(self, buffer)
            }
        }

        impl<'a> TryUnpackAs<'a, $pack_as> for $t {
            fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
                let unpack_fn = | $unpack_buf: &[u8] | $unpack_block;

                if iter.len() < <$t>::FIXED_LEN {
                    Err(UnpackingError::InvalidInternalLength)?
                }

                let result: Result<$t,UnpackingError> = unpack_fn(iter.as_slice());
                *iter = iter.as_slice()[<$t>::FIXED_LEN..].iter();
                Ok(result?)
            }
        }

        impl_try_unpack_for_owned! {
            impl TryOwnedUnpack for $t {
                type Unpacked = Self;
                fn try_owned_unpack(iter: &mut std::slice::Iter<'_, u8>) -> anyhow::Result<Self::Unpacked> {
                    TryUnpackAs::<$pack_as>::try_unpack_as(iter)
                }
            }
        }
    };
);

def_fixed_len!(u8, 1, |b, v| b.write_u8(v), |buffer| Ok(buffer[0]));

def_fixed_len!(i8, 1, |b, v| b.write_i8(v), |buffer| Ok(buffer[0] as i8));

def_fixed_len!(u16, 2, |b, v| b.write_u16::<LittleEndian>(v), |buffer| Ok(LittleEndian::read_u16(
    buffer
)));

def_fixed_len!(i16, 2, |b, v| b.write_i16::<LittleEndian>(v), |buffer| Ok(LittleEndian::read_i16(
    buffer
)));

def_fixed_len!(u32, 4, |b, v| b.write_u32::<LittleEndian>(v), |buffer| Ok(LittleEndian::read_u32(
    buffer
)));

def_fixed_len!(i32, 4, |b, v| b.write_i32::<LittleEndian>(v), |buffer| Ok(LittleEndian::read_i32(
    buffer
)));

def_fixed_len!(u64, 8, |b, v| b.write_u64::<LittleEndian>(v), |buffer| Ok(LittleEndian::read_u64(
    buffer
)));

def_fixed_len!(i64, 8, |b, v| b.write_i64::<LittleEndian>(v), |buffer| Ok(LittleEndian::read_i64(
    buffer
)));

def_fixed_len!(bool, 1, |b, v| b.write_u8(v as u8), |buffer| match buffer[0] {
    0 => Ok(false),
    1 => Ok(true),
    _ => Err(UnpackingError::InvalidValue),
});

def_fixed_len!((), 0, |_b, _v| { Ok(()) }, |_b| Ok(()));

def_fixed_len!(std::net::Ipv6Addr, 16, |b, v| b.write_u128::<LittleEndian>(v.into()), |b| Ok(
    LittleEndian::read_u128(b).into()
));

def_fixed_len!(EUI64, 8, |b, v| b.write_all((&v).into()), |b| Ok(EUI64([
    b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]
])));

def_fixed_len!(EUI48, 6, |b, v| b.write_all((&v).into()), |b| Ok(EUI48([
    b[0], b[1], b[2], b[3], b[4], b[5]
])));

impl TryPack for str {
    fn pack_len(&self) -> io::Result<usize> {
        TryPackAs::<str>::pack_as_len(self)
    }

    fn try_pack<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
        TryPackAs::<str>::try_pack_as(self, buffer)
    }
}

impl TryPack for &str {
    fn pack_len(&self) -> io::Result<usize> {
        TryPackAs::<str>::pack_as_len(*self)
    }

    fn try_pack<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
        TryPackAs::<str>::try_pack_as(*self, buffer)
    }
}

impl TryPack for String {
    fn pack_len(&self) -> io::Result<usize> {
        TryPackAs::<str>::pack_as_len(self.as_str())
    }

    fn try_pack<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
        TryPackAs::<str>::try_pack_as(self.as_str(), buffer)
    }
}

impl TryPackAs<str> for str {
    fn pack_as_len(&self) -> io::Result<usize> {
        Ok(self.as_bytes().len() + 1)
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
        let bytes = self.as_bytes();
        let len = bytes.len() + 1;

        if len > std::u16::MAX as usize {
            Err(io::ErrorKind::InvalidInput.into())
        } else if buffer.write(bytes)? != bytes.len() || buffer.write(&[0u8; 1])? != 1 {
            Err(io::ErrorKind::Other.into())
        } else {
            Ok(len)
        }
    }
}

impl TryPackAs<str> for &str {
    fn pack_as_len(&self) -> io::Result<usize> {
        TryPackAs::<str>::pack_as_len(*self)
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
        TryPackAs::<str>::try_pack_as(*self, buffer)
    }
}

impl TryPackAs<str> for String {
    fn pack_as_len(&self) -> io::Result<usize> {
        TryPackAs::<str>::pack_as_len(self.as_str())
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
        TryPackAs::<str>::try_pack_as(self.as_str(), buffer)
    }
}

impl TryPackAs<str> for [u8] {
    fn pack_as_len(&self) -> io::Result<usize> {
        Ok(self.len() + 1)
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
        let string = std::str::from_utf8(self)
            .map_err(|err| std::io::Error::new(std::io::ErrorKind::InvalidData, err))?;
        TryPackAs::<str>::try_pack_as(string, buffer)
    }
}

impl TryPackAs<str> for &[u8] {
    fn pack_as_len(&self) -> io::Result<usize> {
        TryPackAs::<str>::pack_as_len(*self)
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
        TryPackAs::<str>::try_pack_as(*self, buffer)
    }
}

impl TryPackAs<str> for Vec<u8> {
    fn pack_as_len(&self) -> io::Result<usize> {
        TryPackAs::<str>::pack_as_len(self.as_slice())
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
        TryPackAs::<str>::try_pack_as(self.as_slice(), buffer)
    }
}

impl TryPack for [u8] {
    fn pack_len(&self) -> io::Result<usize> {
        TryPackAs::<[u8]>::pack_as_len(self)
    }

    fn try_pack<B: std::io::Write + ?Sized>(&self, buffer: &mut B) -> io::Result<usize> {
        TryPackAs::<[u8]>::try_pack_as(self, buffer)
    }
}

impl TryPack for Vec<u8> {
    fn pack_len(&self) -> io::Result<usize> {
        TryPackAs::<[u8]>::pack_as_len(self)
    }

    fn try_pack<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
        TryPackAs::<[u8]>::try_pack_as(self, buffer)
    }
}

impl TryPackAs<SpinelDataWlen> for [u8] {
    fn pack_as_len(&self) -> std::io::Result<usize> {
        Ok(self.len() + 2)
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> std::io::Result<usize> {
        let bytes = self;
        let len = bytes.len() + 2;

        if len > std::u16::MAX as usize {
            Err(io::ErrorKind::InvalidInput.into())
        } else {
            buffer.write_u16::<LittleEndian>((len - 2) as u16)?;
            buffer.write_all(bytes)?;
            Ok(len)
        }
    }
}

impl TryPackAs<SpinelDataWlen> for Vec<u8> {
    fn pack_as_len(&self) -> std::io::Result<usize> {
        Ok(self.len() + 2)
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> std::io::Result<usize> {
        let slice: &[u8] = &*self;
        TryPackAs::<SpinelDataWlen>::try_pack_as(slice, buffer)
    }
}

impl TryPackAs<[u8]> for [u8] {
    fn pack_as_len(&self) -> std::io::Result<usize> {
        Ok(self.len())
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> std::io::Result<usize> {
        let bytes = self;
        let len = bytes.len();

        if len > std::u16::MAX as usize {
            Err(io::ErrorKind::InvalidInput.into())
        } else {
            buffer.write_all(bytes).map(|_| len)
        }
    }
}

impl TryPackAs<[u8]> for Vec<u8> {
    fn pack_as_len(&self) -> std::io::Result<usize> {
        Ok(self.len())
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> std::io::Result<usize> {
        let slice: &[u8] = &*self;
        TryPackAs::<[u8]>::try_pack_as(slice, buffer)
    }
}

impl<'a> TryPackAs<[u8]> for &'a [u8] {
    fn pack_as_len(&self) -> std::io::Result<usize> {
        Ok(self.len())
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> std::io::Result<usize> {
        let bytes = *self;
        let len = bytes.len();

        if len > std::u16::MAX as usize {
            Err(io::ErrorKind::InvalidInput.into())
        } else {
            buffer.write_all(bytes).map(|_| len)
        }
    }
}

/// Borrowed unpack for EUI64
impl<'a> TryUnpack<'a> for &'a EUI64 {
    type Unpacked = Self;
    fn try_unpack(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self::Unpacked> {
        if iter.len() < std::mem::size_of::<EUI64>() {
            Err(UnpackingError::InvalidInternalLength)?
        }

        // Convert the iterator into a slice.
        let ret = &iter.as_slice()[..std::mem::size_of::<EUI64>()];

        // Move the iterator to the end.
        *iter = ret[ret.len()..].iter();

        Ok(ret.try_into().unwrap())
    }
}

/// Borrowed unpack for EUI48
impl<'a> TryUnpack<'a> for &'a EUI48 {
    type Unpacked = Self;
    fn try_unpack(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self::Unpacked> {
        if iter.len() < std::mem::size_of::<EUI48>() {
            Err(UnpackingError::InvalidInternalLength)?
        }

        // Convert the iterator into a slice.
        let ret = &iter.as_slice()[..std::mem::size_of::<EUI48>()];

        // Move the iterator to the end.
        *iter = ret[ret.len()..].iter();

        Ok(ret.try_into().unwrap())
    }
}

impl<'a> TryUnpack<'a> for &'a [u8] {
    type Unpacked = Self;
    fn try_unpack(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self::Unpacked> {
        // Convert the iterator into a slice.
        let ret = iter.as_slice();

        // Move the iterator to the end.
        *iter = ret[ret.len()..].iter();

        Ok(ret)
    }
}

impl<'a> TryUnpackAs<'a, [u8]> for &'a [u8] {
    fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
        Self::try_unpack(iter)
    }
}

impl<'a> TryUnpackAs<'a, [u8]> for Vec<u8> {
    fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
        Self::try_unpack(iter)
    }
}

impl<'a> TryUnpackAs<'a, SpinelDataWlen> for &'a [u8] {
    fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
        let len = u16::try_unpack(iter)? as usize;

        let ret = iter.as_slice();

        if ret.len() < len {
            Err(UnpackingError::InvalidInternalLength)?
        }

        // Move the iterator to the end.
        *iter = ret[len..].iter();

        Ok(&ret[..len])
    }
}

impl<'a> TryUnpackAs<'a, SpinelDataWlen> for Vec<u8> {
    fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
        let slice: &[u8] = TryUnpackAs::<SpinelDataWlen>::try_unpack_as(iter)?;
        Ok(slice.to_owned())
    }
}

impl<'a> TryUnpack<'a> for &'a str {
    type Unpacked = Self;
    fn try_unpack(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self::Unpacked> {
        TryUnpackAs::<str>::try_unpack_as(iter)
    }
}

impl<'a> TryUnpackAs<'a, str> for &'a str {
    fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
        let mut split = iter.as_slice().splitn(2, |c| *c == 0);

        let str_bytes: &[u8] = split.next().ok_or(UnpackingError::UnterminatedString)?.into();

        *iter = split.next().ok_or(UnpackingError::UnterminatedString)?.iter();

        std::str::from_utf8(str_bytes).context(UnpackingError::InvalidValue)
    }
}

impl<'a> TryUnpackAs<'a, str> for String {
    fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
        <&str>::try_unpack(iter).map(ToString::to_string)
    }
}

impl<'a> TryUnpackAs<'a, str> for &'a [u8] {
    fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
        <&str as TryUnpackAs<str>>::try_unpack_as(iter).map(str::as_bytes)
    }
}

impl<'a> TryUnpackAs<'a, str> for Vec<u8> {
    fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
        <&[u8] as TryUnpackAs<str>>::try_unpack_as(iter).map(<[u8]>::to_vec)
    }
}

impl_try_unpack_for_owned! {
    impl TryOwnedUnpack for String {
        type Unpacked = Self;
        fn try_owned_unpack(iter: &mut std::slice::Iter<'_, u8>) -> anyhow::Result<Self::Unpacked> {
            <&str>::try_unpack(iter).map(ToString::to_string)
        }
    }
}

impl TryPackAs<SpinelUint> for u32 {
    fn pack_as_len(&self) -> std::io::Result<usize> {
        if *self < (1 << 7) {
            Ok(1)
        } else if *self < (1 << 14) {
            Ok(2)
        } else if *self < (1 << 21) {
            Ok(3)
        } else if *self < (1 << 28) {
            Ok(4)
        } else {
            Ok(5)
        }
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> std::io::Result<usize> {
        let len = TryPackAs::<SpinelUint>::pack_as_len(self)?;

        let mut value = *self;
        let mut inner_buffer = [0u8; 5];

        for i in 0..(len - 1) {
            inner_buffer[i] = ((value & 0x7F) | 0x80) as u8;
            value = value >> 7;
        }

        inner_buffer[len - 1] = (value & 0x7F) as u8;

        buffer.write_all(&inner_buffer[..len])?;

        Ok(len)
    }
}

impl<'a> TryUnpackAs<'a, SpinelUint> for u32 {
    fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
        let mut len: usize = 0;
        let mut value: u32 = 0;
        let mut i = 0;

        loop {
            if len >= 5 {
                return Err(UnpackingError::InvalidValue.into());
            }

            let byte = iter.next().ok_or(UnpackingError::InvalidInternalLength)?;

            len += 1;

            value |= ((byte & 0x7F) as u32) << i;

            if byte & 0x80 != 0x80 {
                break;
            }

            i += 7;
        }

        Ok(value)
    }
}

impl_try_unpack_for_owned! {
    impl TryOwnedUnpack for SpinelUint {
        type Unpacked = u32;
        fn try_owned_unpack(iter: &mut std::slice::Iter<'_, u8>) -> anyhow::Result<Self::Unpacked> {
            TryUnpackAs::<SpinelUint>::try_unpack_as(iter)
        }
    }
}

impl<T> TryOwnedUnpack for [T]
where
    T: TryOwnedUnpack,
{
    type Unpacked = Vec<T::Unpacked>;
    fn try_owned_unpack(iter: &mut std::slice::Iter<'_, u8>) -> anyhow::Result<Self::Unpacked> {
        let mut ret: Self::Unpacked = Vec::with_capacity(iter.size_hint().0);

        while iter.len() != 0 {
            ret.push(T::try_owned_unpack(iter)?);
        }

        Ok(ret)
    }
}

impl<T> TryOwnedUnpack for Vec<T>
where
    T: TryOwnedUnpack,
{
    type Unpacked = Vec<T::Unpacked>;
    fn try_owned_unpack(iter: &mut std::slice::Iter<'_, u8>) -> anyhow::Result<Self::Unpacked> {
        let mut ret: Self::Unpacked = Vec::with_capacity(iter.size_hint().0);

        while iter.len() != 0 {
            ret.push(T::try_owned_unpack(iter)?);
        }

        Ok(ret)
    }
}

impl<'a, T> TryUnpack<'a> for Vec<T>
where
    T: TryUnpack<'a>,
{
    type Unpacked = Vec<T::Unpacked>;
    fn try_unpack(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self::Unpacked> {
        let mut ret: Self::Unpacked = Vec::with_capacity(iter.size_hint().0);

        while iter.len() != 0 {
            ret.push(T::try_unpack(iter)?);
        }

        Ok(ret)
    }
}

impl<T> TryOwnedUnpack for HashSet<T>
where
    T: TryOwnedUnpack,
    T::Unpacked: Eq + Hash,
{
    type Unpacked = HashSet<T::Unpacked>;
    fn try_owned_unpack(iter: &mut std::slice::Iter<'_, u8>) -> anyhow::Result<Self::Unpacked> {
        let mut ret: Self::Unpacked = Default::default();

        while iter.len() != 0 {
            ret.insert(T::try_owned_unpack(iter)?);
        }

        Ok(ret)
    }
}

impl<'a, T> TryUnpack<'a> for HashSet<T>
where
    T: TryUnpack<'a>,
    T::Unpacked: Eq + Hash,
{
    type Unpacked = HashSet<T::Unpacked>;
    fn try_unpack(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self::Unpacked> {
        let mut ret: Self::Unpacked = Default::default();

        while iter.len() != 0 {
            ret.insert(T::try_unpack(iter)?);
        }

        Ok(ret)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    #[test]
    fn test_uint_pack() {
        let mut buffer = [0u8; 500];

        let mut tmp_buffer = &mut buffer[..];

        assert_eq!(TryPackAs::<SpinelUint>::try_pack_as(&0u32, &mut tmp_buffer).unwrap(), 1,);
        assert_eq!(&tmp_buffer[0..1], &[0x00]);
        assert_matches!(
            TryUnpackAs::<SpinelUint>::try_unpack_as(&mut tmp_buffer[0..1].iter()),
            Ok(0u32)
        );

        let mut tmp_buffer = &mut buffer[..];
        assert_eq!(TryPackAs::<SpinelUint>::try_pack_as(&127u32, &mut tmp_buffer).unwrap(), 1,);
        assert_eq!(&buffer[0..1], &[0x7F]);
        assert_matches!(
            TryUnpackAs::<SpinelUint>::try_unpack_as(&mut buffer[0..1].iter()),
            Ok(127u32)
        );

        let mut tmp_buffer = &mut buffer[..];
        assert_eq!(TryPackAs::<SpinelUint>::try_pack_as(&128u32, &mut tmp_buffer).unwrap(), 2,);
        assert_eq!(&buffer[0..2], &[0x80, 0x01]);
        assert_matches!(
            TryUnpackAs::<SpinelUint>::try_unpack_as(&mut buffer[0..2].iter()),
            Ok(128u32)
        );

        let mut tmp_buffer = &mut buffer[..];
        assert_eq!(TryPackAs::<SpinelUint>::try_pack_as(&16383u32, &mut tmp_buffer).unwrap(), 2,);
        assert_eq!(&buffer[0..2], &[0xFF, 0x7F]);
        assert_matches!(
            TryUnpackAs::<SpinelUint>::try_unpack_as(&mut buffer[0..2].iter()),
            Ok(16383u32)
        );

        let mut tmp_buffer = &mut buffer[..];
        assert_eq!(TryPackAs::<SpinelUint>::try_pack_as(&16384u32, &mut tmp_buffer).unwrap(), 3,);
        assert_eq!(&buffer[0..3], &[0x80, 0x80, 0x01]);
        assert_matches!(
            TryUnpackAs::<SpinelUint>::try_unpack_as(&mut buffer[0..3].iter()),
            Ok(16384u32)
        );

        let mut tmp_buffer = &mut buffer[..];
        assert_eq!(TryPackAs::<SpinelUint>::try_pack_as(&2097151u32, &mut tmp_buffer).unwrap(), 3,);
        assert_eq!(&buffer[0..3], &[0xFF, 0xFF, 0x7F]);
        assert_matches!(
            TryUnpackAs::<SpinelUint>::try_unpack_as(&mut buffer[0..3].iter()),
            Ok(2097151u32)
        );

        let mut tmp_buffer = &mut buffer[..];
        assert_eq!(TryPackAs::<SpinelUint>::try_pack_as(&2097152u32, &mut tmp_buffer).unwrap(), 4,);
        assert_eq!(&buffer[0..4], &[0x80, 0x80, 0x80, 0x01]);
        assert_matches!(
            TryUnpackAs::<SpinelUint>::try_unpack_as(&mut buffer[0..4].iter()),
            Ok(2097152u32)
        );

        let mut tmp_buffer = &mut buffer[..];
        assert_eq!(
            TryPackAs::<SpinelUint>::try_pack_as(&268435455u32, &mut tmp_buffer).unwrap(),
            4,
        );
        assert_eq!(&buffer[0..4], &[0xFF, 0xFF, 0xFF, 0x7F]);
        assert_matches!(
            TryUnpackAs::<SpinelUint>::try_unpack_as(&mut buffer[0..4].iter()),
            Ok(268435455u32)
        );

        let mut tmp_buffer = &mut buffer[..];
        assert_eq!(
            TryPackAs::<SpinelUint>::try_pack_as(&268435456u32, &mut tmp_buffer).unwrap(),
            5,
        );
        assert_eq!(&buffer[0..5], &[0x80, 0x80, 0x80, 0x80, 0x01]);
        assert_matches!(
            TryUnpackAs::<SpinelUint>::try_unpack_as(&mut buffer[0..5].iter()),
            Ok(268435456u32)
        );

        let mut tmp_buffer = &mut buffer[..];
        assert_eq!(
            TryPackAs::<SpinelUint>::try_pack_as(&4294967295u32, &mut tmp_buffer).unwrap(),
            5,
        );
        assert_eq!(&buffer[0..5], &[0xFF, 0xFF, 0xFF, 0xFF, 0x0F]);
        assert_matches!(
            TryUnpackAs::<SpinelUint>::try_unpack_as(&mut buffer[0..5].iter()),
            Ok(4294967295u32)
        );
    }

    #[test]
    fn test_vec_owned_unpack() {
        let buffer: &[u8] = &[0x34, 0x12, 0xcd, 0xab];

        let out = Vec::<u16>::try_owned_unpack_from_slice(buffer).unwrap();

        assert_eq!(out.as_slice(), &[0x1234, 0xabcd]);
    }

    #[test]
    fn test_vec_unpack() {
        let buffer: &[u8] = &[0x31, 0x32, 0x33, 0x00, 0x34, 0x35, 0x36, 0x00];

        let out = Vec::<&str>::try_unpack_from_slice(buffer).unwrap();

        assert_eq!(out.as_slice(), &["123", "456"]);
    }

    #[test]
    fn test_string_as_vec() {
        let buffer: &[u8] = &[0x31, 0x32, 0x33, 0x00];

        let out = <&[u8] as TryUnpackAs<str>>::try_unpack_as_from_slice(buffer).unwrap();

        assert_eq!(out, &[0x31, 0x32, 0x33]);
    }

    #[test]
    fn test_hashset_owned_unpack() {
        let buffer: &[u8] = &[0x34, 0x12, 0xcd, 0xab];

        let out = HashSet::<u16>::try_owned_unpack_from_slice(buffer).unwrap();

        assert!(out.contains(&0x1234));
        assert!(out.contains(&0xabcd));
    }

    #[test]
    fn test_hashset_unpack() {
        let buffer: &[u8] = &[0x31, 0x32, 0x33, 0x00, 0x34, 0x35, 0x36, 0x00];

        let out = HashSet::<&str>::try_unpack_from_slice(buffer).unwrap();

        assert!(out.contains("123"));
        assert!(out.contains("456"));
    }
}
