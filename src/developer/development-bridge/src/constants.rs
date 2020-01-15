// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(unused)]
pub const SOCKET: &str = "/tmp/ascendd";
pub const DAEMON: &str = "daemon";
pub const ASCENDD: &str = "ascendd";
pub const CONFIG_JSON_FILE: &str = "fdb_config.json";
pub const SOCAT: &str = "socat";
pub const LOCAL_SOCAT: &str = "EXEC:\"./onet host-pipe\"";
pub const TARGET_SOCAT: &str = "EXEC:\"fx shell onet host-pipe\"";
pub const MAX_RETRY_COUNT: u32 = 10;
