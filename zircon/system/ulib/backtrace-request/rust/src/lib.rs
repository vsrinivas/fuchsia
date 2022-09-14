// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Issues a backtrace request to the system crash service.
#[inline]
pub fn backtrace_request() {
    unsafe { ext::backtrace_request_for_rust() };
}

mod ext {
    #[link(name = "backtrace-request", kind = "static")]
    extern "C" {
        pub(crate) fn backtrace_request_for_rust();
    }
}

#[cfg(test)]
mod tests {

    #[test]
    fn call_backtrace() {
        super::backtrace_request();
    }
}
