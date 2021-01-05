// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fuchsia_zircon as zx;

#[fuchsia_async::run_singlethreaded(test)]
async fn start_with_cache_no_space() {
    use {futures::FutureExt as _, vfs::directory::entry::DirectoryEntry as _};

    struct NoSpaceEntryConstructor {
        paths: std::sync::Mutex<Vec<String>>,
    }

    impl vfs::directory::mutable::entry_constructor::EntryConstructor for NoSpaceEntryConstructor {
        fn create_entry(
            self: std::sync::Arc<Self>,
            _parent: std::sync::Arc<dyn vfs::directory::entry::DirectoryEntry>,
            _what: vfs::directory::mutable::entry_constructor::NewEntryType,
            name: &str,
            _path: &vfs::path::Path,
        ) -> Result<std::sync::Arc<dyn vfs::directory::entry::DirectoryEntry>, zx::Status> {
            let Self { paths } = &*self;
            let () = paths.lock().unwrap().push(name.into());
            Err(zx::Status::NO_SPACE)
        }
    }

    let constructor =
        std::sync::Arc::new(NoSpaceEntryConstructor { paths: std::sync::Mutex::new(Vec::new()) });

    let root = vfs::mut_pseudo_directory! {};
    let (client, server) =
        fidl::endpoints::create_endpoints().expect("failed to create FIDL endpoints");
    let () = root.open(
        vfs::execution_scope::ExecutionScope::build().entry_constructor(constructor.clone()).new(),
        fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        0,
        vfs::path::Path::empty(),
        server,
    );

    let launcher = fuchsia_component::client::launcher().expect("failed to get launcher");
    let mut options = fuchsia_component::client::LaunchOptions::new();
    let _: &mut fuchsia_component::client::LaunchOptions =
        options.add_handle_to_namespace("/cache".into(), client.into());
    let app = fuchsia_component::client::launch_with_options(
        &launcher,
        "fuchsia-pkg://fuchsia.com/netstack-integration-tests#meta/netstack-debug.cmx".into(),
        None,
        options,
    )
    .expect("failed to launch netstack");

    let (client_channel, server_channel) =
        zx::Channel::create().expect("failed to create zircon channel");
    let () =
        app.pass_to_named_service(".", server_channel).expect("failed to connect to svc directory");
    let client_channel = fuchsia_async::Channel::from_channel(client_channel)
        .expect("failed to create async channel");
    let proxy = fidl_fuchsia_io::DirectoryProxy::new(client_channel);

    let () = futures::select! {
        res = app.wait_with_output().fuse() => {
            let fuchsia_component::client::Output {
                exit_status,
                stdout,
                stderr,
            } = res.expect("failed to wait for netstack output");
            panic!(
                "netstack unexpectedly exited; exit_status={} stdout={} stderr={}",
                exit_status,
                String::from_utf8_lossy(&stdout),
                String::from_utf8_lossy(&stderr)
            );
        },
        res = proxy.describe() => {
            let info = res.expect("failed to describe svc directory");
            assert_eq!(
                info,
                fidl_fuchsia_io::NodeInfo::Directory(fidl_fuchsia_io::DirectoryObject,)
            );
        }
    };

    let paths = {
        let NoSpaceEntryConstructor { paths } = &*constructor;
        paths.lock().unwrap().clone()
    };

    assert_eq!(paths[..], ["pprof"]);
}
