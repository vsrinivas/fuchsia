// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_examples_echo as echo,
    fidl_fuchsia_io::{
        DirectoryProxy, CLONE_FLAG_SAME_RIGHTS, MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE,
    },
    std::path::Path,
};

pub async fn dir_contains<'a>(
    root_proxy: &'a DirectoryProxy,
    path: &'a str,
    entry_name: &'a str,
) -> bool {
    let dir = io_util::open_directory(&root_proxy, &Path::new(path), OPEN_RIGHT_READABLE)
        .expect("Failed to open directory");
    let entries = files_async::readdir(&dir).await.expect("readdir failed");
    let listing = entries.iter().map(|entry| entry.name.clone()).collect::<Vec<String>>();
    listing.contains(&String::from(entry_name))
}

pub async fn list_directory<'a>(root_proxy: &'a DirectoryProxy) -> Vec<String> {
    let entries = files_async::readdir(&root_proxy).await.expect("readdir failed");
    let mut items = entries.iter().map(|entry| entry.name.clone()).collect::<Vec<String>>();
    items.sort();
    items
}

pub async fn list_directory_recursive<'a>(root_proxy: &'a DirectoryProxy) -> Vec<String> {
    let dir = io_util::clone_directory(&root_proxy, CLONE_FLAG_SAME_RIGHTS)
        .expect("Failed to clone DirectoryProxy");
    let entries = files_async::readdir_recursive(&dir).await.expect("readdir failed");
    let mut items = entries.iter().map(|entry| entry.name.clone()).collect::<Vec<String>>();
    items.sort();
    items
}

pub async fn read_file<'a>(root_proxy: &'a DirectoryProxy, path: &'a str) -> String {
    let file_proxy = io_util::open_file(&root_proxy, &Path::new(path), OPEN_RIGHT_READABLE)
        .expect("Failed to open file.");
    let res = io_util::read_file(&file_proxy).await;
    res.expect("Unable to read file.")
}

pub async fn call_echo<'a>(root_proxy: &'a DirectoryProxy, path: &'a str) -> String {
    let node_proxy =
        io_util::open_node(&root_proxy, &Path::new(path), OPEN_RIGHT_READABLE, MODE_TYPE_SERVICE)
            .expect("failed to open echo service");
    let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
    let res = echo_proxy.echo_string(Some("hippos")).await;
    res.expect("failed to use echo service").expect("no result from echo")
}
