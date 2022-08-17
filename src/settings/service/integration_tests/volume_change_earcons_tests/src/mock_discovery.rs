// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_media_sessions2::{DiscoveryRequest, DiscoveryRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_component_test::LocalComponentHandles;
use futures::StreamExt;
use futures::TryStreamExt;

pub async fn discovery_service_mock(handles: LocalComponentHandles) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    let _: &mut ServiceFsDir<'_, _> =
        fs.dir("svc").add_fidl_service(move |mut stream: DiscoveryRequestStream| {
            fasync::Task::spawn(async move {
                while let Ok(Some(req)) = stream.try_next().await {
                    if let DiscoveryRequest::WatchSessions {
                        watch_options: _,
                        session_watcher: _,
                        control_handle: _,
                    } = req
                    {
                        // Consume requests but ignore them.
                    }
                }
            })
            .detach();
        });
    let _: &mut ServiceFs<_> = fs.serve_connection(handles.outgoing_dir.into_channel()).unwrap();
    fs.collect::<()>().await;
    Ok(())
}
