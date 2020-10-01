// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::ffi::CString;

/// trait that can ping.
pub trait Pinger {
    /// returns true if url is reachable, false otherwise.
    fn ping(&self, url: &str) -> bool;
}

#[link(name = "ext_ping", kind = "static")]
extern "C" {
    fn c_ping(url: *const libc::c_char) -> libc::ssize_t;
}

pub struct IcmpPinger;
impl Pinger for IcmpPinger {
    // pings an IPv4 address.
    // returns true if there has been a response, false otherwise.
    fn ping(&self, url: &str) -> bool {
        let ret;
        // unsafe needed as we are calling C code.
        let c_str = CString::new(url).unwrap();
        unsafe {
            ret = c_ping(c_str.as_ptr());
        }
        ret == 0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    #[ignore] // TODO(dpradilla): enable once test infra is present.
    fn test_ping_succesful() {
        assert_eq!(IcmpPinger {}.ping("1.2.3.4"), true);
    }
    #[test]
    fn test_ping_error() {
        assert_eq!(IcmpPinger {}.ping("8.8.8.8"), false);
    }
}
