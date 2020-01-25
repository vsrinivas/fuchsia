// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Logs an error message if the passed in `result` is an error.
#[macro_export]
macro_rules! log_if_err {
    ($result:expr, $log_prefix:expr) => {
        if let Err(e) = $result.as_ref() {
            fx_log_err!("{}: {}", $log_prefix, e);
        }
    };
}

/// Create and connect a FIDL proxy to the service at `path`
pub fn connect_proxy<T: fidl::endpoints::ServiceMarker>(
    path: &String,
) -> Result<T::Proxy, anyhow::Error> {
    let (proxy, server) = fidl::endpoints::create_proxy::<T>()
        .map_err(|e| anyhow::format_err!("Failed to create proxy: {}", e))?;

    fdio::service_connect(path, server.into_channel())
        .map_err(|s| anyhow::format_err!("Failed to connect to service at {}: {}", path, s))?;
    Ok(proxy)
}
