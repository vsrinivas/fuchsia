// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{convert::TryInto, ffi::CString};

/// trait that can ping.
pub trait Pinger {
    /// returns true if url is reachable, false otherwise.
    fn ping(&self, url: &str) -> bool;
}

#[link(name = "ping", kind = "static")]
extern "C" {
    fn ping(argc: libc::c_int, argv: *const *const libc::c_char) -> libc::c_int;
}

pub struct IcmpPinger;
impl Pinger for IcmpPinger {
    // pings an IPv4 address.
    // returns true if there has been a response, false otherwise.
    fn ping(&self, url: &str) -> bool {
        let c_name = CString::new("ping").unwrap();
        let c_url = CString::new(url).unwrap();
        let args = [c_name.as_ptr(), c_url.as_ptr(), std::ptr::null()];
        // unsafe needed as we are calling C code.
        let ret = unsafe { ping((args.len() - 1).try_into().unwrap(), args.as_ptr()) };
        if ret == 0 {
            debug!("ping {} succeeded", url);
        } else {
            info!("ping {} failed: {}", url, std::io::Error::last_os_error());
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
