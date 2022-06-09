// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
pub const LOG_FILE_PREFIX: &str = "ffx.daemon";

#[cfg(not(test))]
pub async fn get_socket() -> String {
    if let Ok(sock_path) = std::env::var("ASCENDD") {
        String::from(sock_path)
    } else if let Ok(sock_path) = ffx_config::get("overnet.socket").await {
        sock_path
    } else {
        hoist::default_ascendd_path()
    }
}

#[cfg(test)]
pub async fn get_socket() -> String {
    std::thread_local! {
        static DEFAULT_SOCKET: String = {
            tempfile::Builder::new()
                .prefix("ascendd_for_test")
                .suffix(".sock")
                .tempfile().unwrap()
                .path()
                .file_name().and_then(std::ffi::OsStr::to_str).unwrap().to_string()
        };
    }

    DEFAULT_SOCKET.with(|k| k.clone())
}

pub(crate) const CURRENT_EXE_BUILDID: &str = "current.buildid";
