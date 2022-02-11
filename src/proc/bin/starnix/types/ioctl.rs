// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::types::*;

const fn new_ioctl(dir: u32, _type: u8, number: u32, size: u32) -> u32 {
    (dir << _IOC_DIRSHIFT)
        | ((_type as u32) << _IOC_TYPESHIFT)
        | (number << _IOC_NRSHIFT)
        | (size << _IOC_SIZESHIFT)
}

#[cfg(test)]
pub const fn encode_ioctl(_type: u8, number: u32) -> u32 {
    new_ioctl(_IOC_NONE, _type, number, 0)
}

#[cfg(test)]
pub const fn encode_ioctl_read<T>(_type: u8, number: u32) -> u32 {
    new_ioctl(_IOC_READ, _type, number, std::mem::size_of::<T>() as u32)
}

#[cfg(test)]
pub const fn encode_ioctl_write<T>(_type: u8, number: u32) -> u32 {
    new_ioctl(_IOC_WRITE, _type, number, std::mem::size_of::<T>() as u32)
}

pub const fn encode_ioctl_write_read<T>(_type: u8, number: u32) -> u32 {
    new_ioctl(_IOC_READ | _IOC_WRITE, _type, number, std::mem::size_of::<T>() as u32)
}

#[cfg(test)]
mod tests {
    use super::*;

    pub struct IoctlRequest {
        pub dir: u32,
        pub _type: u32,
        pub number: u32,
        pub size: u32,
    }

    impl IoctlRequest {
        pub fn new(request: u32) -> IoctlRequest {
            IoctlRequest {
                dir: decode_ioctl_dir(request),
                _type: decode_ioctl_type(request),
                number: decode_ioctl_number(request),
                size: decode_ioctl_size(request),
            }
        }
    }

    pub fn decode_ioctl_dir(number: u32) -> u32 {
        (number >> _IOC_DIRSHIFT) & _IOC_DIRMASK
    }

    pub fn decode_ioctl_type(number: u32) -> u32 {
        (number >> _IOC_TYPESHIFT) & _IOC_TYPEMASK
    }

    pub fn decode_ioctl_number(number: u32) -> u32 {
        (number >> _IOC_NRSHIFT) & _IOC_NRMASK
    }

    pub fn decode_ioctl_size(number: u32) -> u32 {
        (number >> _IOC_SIZESHIFT) & _IOC_SIZEMASK
    }

    #[test]
    fn test_encode() {
        let encoded = encode_ioctl(7, 9);
        assert_eq!(decode_ioctl_dir(encoded), _IOC_NONE);
        assert_eq!(decode_ioctl_type(encoded), 7);
        assert_eq!(decode_ioctl_number(encoded), 9);
        assert_eq!(decode_ioctl_size(encoded), 0);
    }

    #[test]
    fn test_encode_read() {
        let encoded = encode_ioctl_read::<u32>(1, 7);
        assert_eq!(decode_ioctl_dir(encoded), _IOC_READ);
        assert_eq!(decode_ioctl_type(encoded), 1);
        assert_eq!(decode_ioctl_number(encoded), 7);
        assert_eq!(decode_ioctl_size(encoded), std::mem::size_of::<u32>() as u32);
    }

    #[test]
    fn test_encode_write() {
        let encoded = encode_ioctl_write::<u64>(2, 8);
        assert_eq!(decode_ioctl_dir(encoded), _IOC_WRITE);
        assert_eq!(decode_ioctl_type(encoded), 2);
        assert_eq!(decode_ioctl_number(encoded), 8);
        assert_eq!(decode_ioctl_size(encoded), std::mem::size_of::<u64>() as u32);
    }

    #[test]
    fn test_encode_write_read() {
        let encoded = encode_ioctl_write_read::<u32>(2, 8);
        assert_eq!(decode_ioctl_dir(encoded), _IOC_WRITE | _IOC_READ);
        assert_eq!(decode_ioctl_type(encoded), 2);
        assert_eq!(decode_ioctl_number(encoded), 8);
        assert_eq!(decode_ioctl_size(encoded), std::mem::size_of::<u32>() as u32);
    }

    #[test]
    fn test_encode_ioctl_request() {
        let encoded = encode_ioctl_write_read::<u32>(2, 8);
        let request = IoctlRequest::new(encoded);
        assert_eq!(decode_ioctl_dir(encoded), request.dir);
        assert_eq!(decode_ioctl_type(encoded), request._type);
        assert_eq!(decode_ioctl_number(encoded), request.number);
        assert_eq!(decode_ioctl_size(encoded), request.size);
    }
}
