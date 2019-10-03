// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::ComponentDecl,
    fidl_fidl_examples_echo as echo, fidl_fuchsia_data as fdata,
    fidl_fuchsia_io::{
        DirectoryProxy, CLONE_FLAG_SAME_RIGHTS, MODE_TYPE_SERVICE, OPEN_FLAG_CREATE,
        OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    files_async, fuchsia_zircon as zx,
    futures::prelude::*,
    futures::{channel::mpsc, future::BoxFuture, lock::Mutex},
    std::collections::HashSet,
    std::path::Path,
    std::sync::Arc,
};

/// Returns true if the given child realm (live or deleting) exists.
pub async fn has_child<'a>(realm: &'a Realm, moniker: &'a str) -> bool {
    realm
        .lock_state()
        .await
        .as_ref()
        .expect("not resolved")
        .all_child_realms()
        .contains_key(&moniker.into())
}

/// Return the instance id of the given live child.
pub async fn get_instance_id<'a>(realm: &'a Realm, moniker: &'a str) -> u32 {
    realm
        .lock_state()
        .await
        .as_ref()
        .expect("not resolved")
        .get_live_child_instance_id(&moniker.into())
        .unwrap()
}

/// Return all monikers of the live children of the given `realm`.
pub async fn get_live_children(realm: &Realm) -> HashSet<PartialMoniker> {
    realm
        .lock_state()
        .await
        .as_ref()
        .expect("not resolved")
        .live_child_realms()
        .map(|(m, _)| m.clone())
        .collect()
}

/// Return the child realm of the given `realm` with moniker `child`.
pub async fn get_live_child<'a>(realm: &'a Realm, child: &'a str) -> Arc<Realm> {
    realm
        .lock_state()
        .await
        .as_ref()
        .expect("not resolved")
        .get_live_child_realm(&child.into())
        .unwrap()
        .clone()
}

/// Returns an empty component decl for an executable component.
pub fn default_component_decl() -> ComponentDecl {
    ComponentDecl {
        program: Some(fdata::Dictionary { entries: vec![] }),
        uses: Vec::new(),
        exposes: Vec::new(),
        offers: Vec::new(),
        children: Vec::new(),
        collections: Vec::new(),
        facets: None,
        storage: Vec::new(),
    }
}

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

pub async fn write_file<'a>(root_proxy: &'a DirectoryProxy, path: &'a str, contents: &'a str) {
    let file_proxy = io_util::open_file(
        &root_proxy,
        &Path::new(path),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE,
    )
    .expect("Failed to open file.");
    let (s, _) = file_proxy
        .write(&mut contents.as_bytes().to_vec().drain(..))
        .await
        .expect("Unable to write file.");
    let s = zx::Status::from_raw(s);
    assert_eq!(s, zx::Status::OK, "Write failed");
}

pub async fn call_echo<'a>(root_proxy: &'a DirectoryProxy, path: &'a str) -> String {
    let node_proxy =
        io_util::open_node(&root_proxy, &Path::new(path), OPEN_RIGHT_READABLE, MODE_TYPE_SERVICE)
            .expect("failed to open echo service");
    let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
    let res = echo_proxy.echo_string(Some("hippos")).await;
    res.expect("failed to use echo service").expect("no result from echo")
}

/// A hook that can block on Stop and notify on Destroy for a particular component instance.
pub struct DestroyHook {
    /// Realm for which to block `on_stop_instance`.
    moniker: AbsoluteMoniker,
    /// Receiver on which to wait to unblock `on_stop_instance`.
    stop_recv: Mutex<mpsc::Receiver<()>>,
    /// Receiver on which to wait to unblock `on_destroy_instance`.
    destroy_send: Mutex<mpsc::Sender<()>>,
}

impl DestroyHook {
    /// Returns `DestroyHook` and channels on which to signal on `on_stop_instance` and
    /// be signalled for `on_destroy_instance`.
    pub fn new(moniker: AbsoluteMoniker) -> (Arc<Self>, mpsc::Sender<()>, mpsc::Receiver<()>) {
        let (stop_send, stop_recv) = mpsc::channel(0);
        let (destroy_send, destroy_recv) = mpsc::channel(0);
        (
            Arc::new(Self {
                moniker,
                stop_recv: Mutex::new(stop_recv),
                destroy_send: Mutex::new(destroy_send),
            }),
            stop_send,
            destroy_recv,
        )
    }

    async fn on_stop_instance_async(&self, realm: Arc<Realm>) -> Result<(), ModelError> {
        if realm.abs_moniker == self.moniker {
            let mut recv = self.stop_recv.lock().await;
            recv.next().await.expect("failed to suspend stop");
        }
        Ok(())
    }

    async fn on_destroy_instance_async(&self, realm: Arc<Realm>) -> Result<(), ModelError> {
        if realm.abs_moniker == self.moniker {
            let mut send = self.destroy_send.lock().await;
            send.send(()).await.expect("failed to send destroy signal");
        }
        Ok(())
    }
}

impl Hook for DestroyHook {
    fn on<'a>(self: Arc<Self>, event: &'a Event<'_>) -> BoxFuture<'a, Result<(), ModelError>> {
        Box::pin(async move {
            match event {
                Event::DestroyInstance { realm } => {
                    self.on_destroy_instance_async(realm.clone()).await?;
                }
                Event::StopInstance { realm } => {
                    self.on_stop_instance_async(realm.clone()).await?;
                }
                _ => (),
            };
            Ok(())
        })
    }
}
