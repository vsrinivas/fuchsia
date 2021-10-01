// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Context, Result},
    fidl::endpoints::{create_proxy, DiscoverableProtocolMarker},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client::connect_to_protocol_at_dir_root,
    fuchsia_zircon::Status,
    futures::future::BoxFuture,
    io_util::node::OpenError,
    log::debug,
    rand::{prelude::SliceRandom, rngs::SmallRng, Rng},
    std::path::Path,
};

const COLLECTION_NAME: &'static str = "dynamic_children";
const ECHO_CLIENT_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/component-manager-stress-tests-alt#meta/unreliable_echo_client.cm";

const NO_BINARY_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/component-manager-stress-tests-alt#meta/no_binary.cm";

const HUB_RIGHTS: u32 = fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE;

/// Used for traversal of the hub
pub struct Hub {
    pub name: String,
    dir: Directory,
}

impl Hub {
    pub fn from_namespace() -> Result<Self, Status> {
        let dir = Directory::from_namespace("/hub", HUB_RIGHTS)?;
        let name = "<root>".to_string();
        Ok(Self { name, dir })
    }

    /// Connect to a given protocol from the component's exposed directory.
    /// The component must have been resolved by this point.
    pub async fn connect_to_exposed_protocol<P: DiscoverableProtocolMarker>(
        &self,
    ) -> Result<P::Proxy> {
        let in_dir = self
            .dir
            .open_directory("resolved/expose", HUB_RIGHTS)
            .await
            .context("Could not open resolved/expose directory")?;
        connect_to_protocol_at_dir_root::<P>(&in_dir.proxy)
            .context("Could not open protocol from resolved/expose")
    }

    /// Get names of all children
    pub async fn children(&self) -> Result<Vec<String>> {
        let children_dir = self
            .dir
            .open_directory("children", HUB_RIGHTS)
            .await
            .context("Could not open children directory")?;
        children_dir.entries().await.context("Could not get filenames in children dir")
    }

    /// Open the hub of a given child
    pub async fn child_hub(&self, child_name: impl ToString) -> Result<Self> {
        let child_name = child_name.to_string();
        let child_dir_path = format!("children/{}", child_name);
        let child_dir = self
            .dir
            .open_directory(&child_dir_path, HUB_RIGHTS)
            .await
            .context("Could not open children directory")?;
        Ok(Hub { name: child_name, dir: child_dir })
    }

    /// Create a child of this component. `rng` is used to generate a random name for the child
    /// component.
    pub async fn add_child(&self, rng: &mut SmallRng) -> Result<()> {
        let name = format!("C{}", rng.gen::<u64>());
        let parent_realm_svc = self.connect_to_exposed_protocol::<fsys::RealmMarker>().await?;

        let url = if rng.gen_bool(0.5) { ECHO_CLIENT_URL } else { NO_BINARY_URL };

        let decl = fsys::ChildDecl {
            name: Some(name.clone()),
            url: Some(url.to_string()),
            startup: Some(fsys::StartupMode::Lazy),
            ..fsys::ChildDecl::EMPTY
        };

        let mut coll_ref = fsys::CollectionRef { name: COLLECTION_NAME.to_string() };

        let result = parent_realm_svc
            .create_child(&mut coll_ref, decl, fsys::CreateChildArgs::EMPTY)
            .await
            .context("Could not send FIDL request to create child component")?;

        if let Err(e) = result {
            bail!("Could not create child component: {:?}", e)
        }

        let mut child_ref =
            fsys::ChildRef { name: name.clone(), collection: Some(COLLECTION_NAME.to_string()) };

        let (exposed_dir, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();

        let result = parent_realm_svc
            .open_exposed_dir(&mut child_ref, server_end)
            .await
            .context("Could not send FIDL request to open exposed dir of child component")?;

        if let Err(e) = result {
            bail!("Could not open exposed dir of child component: {:?}", e);
        }

        if let Err(e) = fuchsia_component::client::connect_to_protocol_at_dir_root::<
            fcomponent::BinderMarker,
        >(&exposed_dir)
        {
            bail!("Could not connect to fuchsia.component.Binder: {:?}", e);
        }

        Ok(())
    }

    /// Delete the given child component.
    pub async fn delete_child(&self, child_name: impl ToString) -> Result<()> {
        let mut child_ref = fsys::ChildRef {
            name: child_name.to_string(),
            collection: Some(COLLECTION_NAME.to_string()),
        };

        let realm_svc = self
            .connect_to_exposed_protocol::<fsys::RealmMarker>()
            .await
            .context("Could not connect to Realm protocol")?;
        let result = realm_svc
            .destroy_child(&mut child_ref)
            .await
            .context("Could not send FIDL request to destroy child")?;

        if let Err(e) = result {
            bail!("Could not destroy child component: {:?}", e);
        }

        Ok(())
    }

    /// Traverse the topology and at a random position, perform a random mutation to the tree.
    pub fn traverse_and_delete<'a>(&'a self, mut rng: SmallRng) -> BoxFuture<'a, Result<()>> {
        Box::pin(async move {
            let child_names = self.children().await?;

            if child_names.is_empty() {
                // No children. Nothing to do.
                Ok(())
            } else if rng.gen_bool(0.85) {
                // Bias towards traversal. This encourages deeper trees.
                // Pick a random child and traverse
                let child_name = child_names.choose(&mut rng).unwrap();
                let child = self
                    .child_hub(child_name)
                    .await
                    .context("Could not open child hub for traversal")?;
                child.traverse_and_delete(rng).await
            } else {
                // Pick a random child and delete it
                let child_name = child_names.choose(&mut rng).unwrap();

                // Remove collection name from child name
                let prefix = format!("{}:", COLLECTION_NAME);
                let child_name = child_name.strip_prefix(&prefix).unwrap();

                self.delete_child(child_name).await
            }
        })
    }

    /// Traverse the topology and at a random position, perform a random mutation to the tree.
    pub fn traverse_and_add<'a>(&'a self, mut rng: SmallRng) -> BoxFuture<'a, Result<()>> {
        Box::pin(async move {
            let child_names = self.children().await?;

            if child_names.is_empty() || rng.gen_bool(0.15) {
                // Create a child at this position in the tree
                self.add_child(&mut rng).await
            } else {
                // Bias towards traversal. This encourages deeper trees.
                // Pick a random child and traverse
                let child_name = child_names.choose(&mut rng).unwrap();
                let child = self
                    .child_hub(child_name)
                    .await
                    .context("Could not open child hub for traversal")?;
                child.traverse_and_add(rng).await
            }
        })
    }
}

// A convenience wrapper over a FIDL DirectoryProxy.
// Functions of this struct do not tolerate FIDL errors and will panic when they encounter them.
struct Directory {
    pub proxy: fio::DirectoryProxy,
}

impl Directory {
    // Opens a path in the namespace as a Directory.
    pub fn from_namespace(path: impl AsRef<Path>, flags: u32) -> Result<Directory, Status> {
        let path = path.as_ref().to_str().unwrap();
        match io_util::directory::open_in_namespace(path, flags) {
            Ok(proxy) => Ok(Directory { proxy }),
            Err(OpenError::OpenError(s)) => {
                debug!("from_namespace {} failed: {}", path, s);
                Err(s)
            }
            Err(OpenError::SendOpenRequest(e)) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during open: {}", e);
                }
            }
            Err(OpenError::OnOpenEventStreamClosed) => Err(Status::PEER_CLOSED),
            Err(OpenError::Namespace(s)) => Err(s),
            Err(e) => panic!("Unexpected error during open: {}", e),
        }
    }

    // Open a directory in the parent dir with the given `filename`.
    pub async fn open_directory(&self, filename: &str, flags: u32) -> Result<Directory, Status> {
        match io_util::directory::open_directory(&self.proxy, filename, flags).await {
            Ok(proxy) => Ok(Directory { proxy }),
            Err(OpenError::OpenError(s)) => {
                debug!("open_directory({},{}) failed: {}", filename, flags, s);
                Err(s)
            }
            Err(OpenError::SendOpenRequest(e)) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during open: {}", e);
                }
            }
            Err(OpenError::OnOpenEventStreamClosed) => Err(Status::PEER_CLOSED),
            Err(e) => panic!("Unexpected error during open: {}", e),
        }
    }

    // Return a list of filenames in the directory
    pub async fn entries(&self) -> Result<Vec<String>, Status> {
        match files_async::readdir(&self.proxy).await {
            Ok(entries) => Ok(entries.iter().map(|entry| entry.name.clone()).collect()),
            Err(files_async::Error::Fidl(_, e)) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error reading dirents: {}", e);
                }
            }
            Err(files_async::Error::ReadDirents(s)) => Err(s),
            Err(e) => {
                panic!("Unexpected error reading dirents: {}", e);
            }
        }
    }
}
