// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::ffi::CString;
use std::os::raw;

/// trait that can ping.
pub trait Pinger {
    /// returns true if url is reachable, false otherwise.
    fn ping(&self, url: &str) -> bool;
}

#[link(name = "ext_ping", kind = "static")]
extern "C" {
    fn c_ping(url: *const raw::c_char) -> raw::c_int;
}

pub struct IcmpPinger;
impl Pinger for IcmpPinger {
    // pings an IPv4 address.
    // returns true if there has been a response, false otherwise.
    fn ping(&self, url: &str) -> bool {
        let ret;
        // unsafe needed as we are calling C code.
        unsafe {
            ret = c_ping((CString::new(url).unwrap()).as_ptr());
        }
        if ret == 0 {
            debug!("ping {:#?} succeeded!", url);
        } else {
            info!("ping {:#?} failed (err={:#?})!", url, ret);
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
    fn test_ping_fail() {
        assert_eq!(IcmpPinger {}.ping("8.8.8.8"), false);
    }
}
