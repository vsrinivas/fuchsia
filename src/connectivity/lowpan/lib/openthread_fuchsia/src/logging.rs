// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_syslog::macros::*;
use openthread::prelude::*;
use std::convert::TryInto;
use std::ffi::CStr;

#[no_mangle]
pub unsafe extern "C" fn otPlatLogLine(
    log_level: otsys::otLogLevel,
    log_region: otsys::otLogRegion,
    line: *const ::std::os::raw::c_char,
) {
    let line = CStr::from_ptr(line).to_str().expect("otPlatLogLine string was bad UTF8");
    fx_log!(
        match log_level.try_into().unwrap() {
            otsys::OT_LOG_LEVEL_CRIT => fuchsia_syslog::levels::ERROR,
            otsys::OT_LOG_LEVEL_WARN => fuchsia_syslog::levels::WARN,
            otsys::OT_LOG_LEVEL_NOTE | otsys::OT_LOG_LEVEL_INFO => fuchsia_syslog::levels::INFO,
            otsys::OT_LOG_LEVEL_DEBG => fuchsia_syslog::levels::DEBUG,
            _ => fuchsia_syslog::levels::FATAL,
        },
        "[OT-{:?}] {}",
        ot::LogRegion::from(log_region),
        line
    );
}
