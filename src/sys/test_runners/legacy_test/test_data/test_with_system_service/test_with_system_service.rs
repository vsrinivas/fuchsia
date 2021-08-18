// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io as fio, fidl_fuchsia_sys as fsys,
    fuchsia_component::client::connect_to_protocol,
};

#[fuchsia::test]
async fn system_services() {
    let env_proxy = connect_to_protocol::<fsys::EnvironmentMarker>().expect("failed to connect");
    let (dir_proxy, directory_request) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    env_proxy.get_directory(directory_request.into_channel()).unwrap();

    let mut protocols = files_async::readdir(&dir_proxy)
        .await
        .unwrap()
        .into_iter()
        .map(|d| d.name)
        .collect::<Vec<_>>();

    // this protocol is added for coverage builders.
    protocols.retain(|s| s != "fuchsia.debugdata.DebugData");
    // make sure we are getting access to system services.
    assert_eq!(
        protocols,
        vec![
            "fuchsia.device.NameProvider",
            "fuchsia.logger.LogSink",
            "fuchsia.process.Launcher",
            "fuchsia.sys.Environment"
        ]
    )
}
