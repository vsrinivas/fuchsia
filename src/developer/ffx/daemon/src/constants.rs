// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
pub const LOG_FILE_PREFIX: &str = "ffx.daemon";

#[cfg(not(test))]
pub async fn get_socket() -> String {
    const OVERNET_SOCKET: &str = "overnet.socket";
    const DEFAULT_SOCKET: &str = "/tmp/ascendd";
    ffx_config::get(OVERNET_SOCKET).await.unwrap_or(DEFAULT_SOCKET.to_string())
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

pub(crate) const CURRENT_EXE_HASH: &str = "current.hash";
