// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    fidl::endpoints::RequestStream,
    fidl_fuchsia_io::{
        DirectoryObject, DirectoryProxy, DirectoryRequest, DirectoryRequestStream,
        OPEN_FLAG_DESCRIBE,
    },
    futures::future::BoxFuture,
    parking_lot::Mutex,
    std::{collections::HashMap, sync::Arc},
};

type OpenCounter = Arc<Mutex<HashMap<String, u64>>>;

fn handle_directory_request_stream(
    mut stream: DirectoryRequestStream,
    open_counts: OpenCounter,
) -> BoxFuture<'static, ()> {
    async move {
        while let Some(req) = stream.next().await {
            handle_directory_request(req.unwrap(), Arc::clone(&open_counts)).await;
        }
    }
    .boxed()
}

async fn handle_directory_request(req: DirectoryRequest, open_counts: OpenCounter) {
    match req {
        DirectoryRequest::Clone { flags, object, control_handle: _control_handle } => {
            let stream = DirectoryRequestStream::from_channel(
                fasync::Channel::from_channel(object.into_channel()).unwrap(),
            );
            describe_dir(flags, &stream);
            fasync::spawn(handle_directory_request_stream(stream, Arc::clone(&open_counts)));
        }
        DirectoryRequest::Open {
            flags: _flags,
            mode: _mode,
            path,
            object: _object,
            control_handle: _control_handle,
        } => {
            *open_counts.lock().entry(path).or_insert(0) += 1;
        }
        other => panic!("unhandled request type: {:?}", other),
    }
}

fn describe_dir(flags: u32, stream: &DirectoryRequestStream) {
    let ch = stream.control_handle();
    if flags & OPEN_FLAG_DESCRIBE != 0 {
        let mut ni = fidl_fuchsia_io::NodeInfo::Directory(DirectoryObject);
        ch.send_on_open_(Status::OK.into_raw(), Some(&mut ni)).expect("send_on_open");
    }
}

pub fn spawn_directory_handler() -> (DirectoryProxy, OpenCounter) {
    let (proxy, stream) =
        fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_io::DirectoryMarker>().unwrap();
    let open_counts = Arc::new(Mutex::new(HashMap::<String, u64>::new()));
    fasync::spawn(handle_directory_request_stream(stream, Arc::clone(&open_counts)));
    (proxy, open_counts)
}
