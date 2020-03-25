// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![allow(unused)]

use std::time::Duration;

pub const SOCKET: &str = "/tmp/ascendd";
pub const DAEMON: &str = "daemon";
pub const ASCENDD: &str = "ascendd";
pub const CONFIG_JSON_FILE: &str = "ffx_config.json";
pub const SOCAT: &str = "socat";
pub const LOCAL_SOCAT: &str = "EXEC:\"fx onet host-pipe\"";
pub const TARGET_SOCAT: &str = "EXEC:\"fx shell onet host-pipe\"";

pub const MAX_RETRY_COUNT: u32 = 30;
// Number of retry attempts after which we'll try to auto-start RCS.
// Anecdotally this needs to be fairly high since ascendd can be slow to start,
// and we don't want to double-start the RCS.
pub const AUTOSTART_MIN_RETRY_COUNT: u32 = 15;
// Delay between retry attempts to find the RCS.
pub const RETRY_DELAY: Duration = Duration::from_millis(200);
