// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::time::Duration;

// Environment file that keeps track of configuration files
pub const ENV_FILE: &str = ".ffx_env";

// Default user configuration file
pub const DEFAULT_USER_CONFIG: &str = ".ffx_user_config.json";

// Timeout for the config cache.
pub const CONFIG_CACHE_TIMEOUT: Duration = Duration::from_secs(3);
