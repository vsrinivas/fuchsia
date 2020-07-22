// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::time::Duration;

// Config keys
pub const LOG_DIR: &str = "log-dir";
pub const LOG_ENABLED: &str = "log-enabled";
pub const PACKAGE_REPO: &str = "package-repo";
pub const SSH_PUB: &str = "ssh-pub";
pub const SSH_PRIV: &str = "ssh-priv";
pub const SSH_PORT: &str = "ssh-port";
pub const ASCENDD_SOCKET: &str = "ascendd-socket";

// Environment file that keeps track of configuration files
pub const ENV_FILE: &str = ".ffx_env";

// Timeout for the config cache.
pub const CONFIG_CACHE_TIMEOUT: Duration = Duration::from_secs(3);
