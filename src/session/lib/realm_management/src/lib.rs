// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon as zx,
};

/// Creates a child in the specified `Realm`.
///
/// # Parameters
/// - `child_name`: The name of the child to be added.
/// - `child_url`: The component URL of the child to add.
/// - `collection_name`: The name of the collection to which the child will be added.
/// - `realm`: The `Realm` to which the child will be added.
///
/// # Returns
/// `Ok` if the child is created successfully.
pub async fn create_child_component(
    child_name: &str,
    child_url: &str,
    collection_name: &str,
    realm: &fsys::RealmProxy,
) -> Result<(), fcomponent::Error> {
    let mut collection_ref = fsys::CollectionRef { name: collection_name.to_string() };
    let child_decl = fsys::ChildDecl {
        name: Some(child_name.to_string()),
        url: Some(child_url.to_string()),
        startup: Some(fsys::StartupMode::Lazy), // Dynamic children can only be started lazily.
        environment: None,
    };

    realm
        .create_child(&mut collection_ref, child_decl)
        .await
        .map_err(|_| fcomponent::Error::Internal)??;

    Ok(())
}

/// Binds a child in the specified `Realm`. This call is expected to follow a matching call to
/// `create_child`.
///
/// # Parameters
/// - `child_name`: The name of the child to bind.
/// - `collection_name`: The name of collection in which the child was created.
/// - `realm`: The `Realm` the child will bound in.
///
/// # Returns
/// `Ok` Result with a client-side channel bound to the component's `exposed_dir`. This directory
/// contains the capabilities that the child exposed to its realm (as declared, for instance, in the
/// `expose` declaration of the component's `.cml` file).
pub async fn bind_child_component(
    child_name: &str,
    collection_name: &str,
    realm: &fsys::RealmProxy,
) -> Result<zx::Channel, fcomponent::Error> {
    let mut child_ref = fsys::ChildRef {
        name: child_name.to_string(),
        collection: Some(collection_name.to_string()),
    };

    let (client_end, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .map_err(|_| fcomponent::Error::Internal)?;
    realm
        .bind_child(&mut child_ref, server_end)
        .await
        .map_err(|_| fcomponent::Error::Internal)??;

    Ok(client_end.into_channel().unwrap().into_zx_channel())
}

/// Destroys a child in the specified `Realm`. This call is expects a matching call to have been
/// made to `create_child`.
///
/// # Parameters
/// - `child_name`: The name of the child to destroy.
/// - `collection_name`: The name of collection in which the child was created.
/// - `realm`: The `Realm` the child will bound in.
///
/// # Errors
/// Returns an error if the child was not destroyed in the realm.
pub async fn destroy_child_component(
    child_name: &str,
    collection_name: &str,
    realm: &fsys::RealmProxy,
) -> Result<(), fcomponent::Error> {
    let mut child_ref = fsys::ChildRef {
        name: child_name.to_string(),
        collection: Some(collection_name.to_string()),
    };

    realm.destroy_child(&mut child_ref).await.map_err(|_| fcomponent::Error::Internal)??;

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::{bind_child_component, create_child_component, destroy_child_component},
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_component as fcomponent, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
        fuchsia_async as fasync,
        futures::prelude::*,
        lazy_static::lazy_static,
        test_util::Counter,
    };

    /// Spawns a local `fidl_fuchsia_sys2::Realm` server, and returns a proxy to the spawned server.
    /// The provided `request_handler` is notified when an incoming request is received.
    ///
    /// # Parameters
    /// - `request_handler`: A function that is called with incoming requests to the spawned
    ///                      `Realm` server.
    /// # Returns
    /// A `RealmProxy` to the spawned server.
    fn spawn_realm_server<F: 'static>(request_handler: F) -> fsys::RealmProxy
    where
        F: Fn(fsys::RealmRequest) + Send,
    {
        let (realm_proxy, mut realm_server) = create_proxy_and_stream::<fsys::RealmMarker>()
            .expect("Failed to create realm proxy and server.");

        fasync::Task::spawn(async move {
            while let Some(realm_request) = realm_server.try_next().await.unwrap() {
                request_handler(realm_request);
            }
        })
        .detach();

        realm_proxy
    }

    /// Spawns a local handler for the given `fidl_fuchsia_io::Directory` request stream.
    /// The provided `request_handler` is notified when an incoming request is received.
    ///
    /// # Parameters
    /// - `directory_server`: A server request stream from a Directory proxy server endpoint.
    /// - `request_handler`: A function that is called with incoming requests to the spawned
    ///                      `Directory` server.
    fn spawn_directory_server<F: 'static>(
        mut directory_server: fio::DirectoryRequestStream,
        request_handler: F,
    ) where
        F: Fn(fio::DirectoryRequest) + Send,
    {
        fasync::Task::spawn(async move {
            while let Some(directory_request) = directory_server.try_next().await.unwrap() {
                request_handler(directory_request);
            }
        })
        .detach();
    }

    /// Tests that creating a child results in the appropriate call to the `RealmProxy`.
    #[fasync::run_until_stalled(test)]
    async fn create_child_parameters() {
        let child_name = "test_child";
        let child_url = "test_url";
        let child_collection = "test_collection";

        let realm_proxy = spawn_realm_server(move |realm_request| match realm_request {
            fsys::RealmRequest::CreateChild { collection, decl, responder } => {
                assert_eq!(decl.name.unwrap(), child_name);
                assert_eq!(decl.url.unwrap(), child_url);
                assert_eq!(&collection.name, child_collection);

                let _ = responder.send(&mut Ok(()));
            }
            _ => {
                assert!(false);
            }
        });

        assert!(create_child_component(child_name, child_url, child_collection, &realm_proxy)
            .await
            .is_ok());
    }

    /// Tests that a success received when creating a child results in an appropriate result from
    /// `create_child`.
    #[fasync::run_until_stalled(test)]
    async fn create_child_success() {
        let realm_proxy = spawn_realm_server(move |realm_request| match realm_request {
            fsys::RealmRequest::CreateChild { collection: _, decl: _, responder } => {
                let _ = responder.send(&mut Ok(()));
            }
            _ => {
                assert!(false);
            }
        });

        assert!(create_child_component("", "", "", &realm_proxy).await.is_ok());
    }

    /// Tests that an error received when creating a child results in an appropriate error from
    /// `create_child`.
    #[fasync::run_until_stalled(test)]
    async fn create_child_error() {
        let realm_proxy = spawn_realm_server(move |realm_request| match realm_request {
            fsys::RealmRequest::CreateChild { collection: _, decl: _, responder } => {
                let _ = responder.send(&mut Err(fcomponent::Error::Internal));
            }
            _ => {
                assert!(false);
            }
        });

        assert!(create_child_component("", "", "", &realm_proxy).await.is_err());
    }

    /// Tests that `bind_child` results in the appropriate call to `RealmProxy`.
    #[fasync::run_until_stalled(test)]
    async fn bind_child_parameters() {
        let child_name = "test_child";
        let child_collection = "test_collection";

        let realm_proxy = spawn_realm_server(move |realm_request| match realm_request {
            fsys::RealmRequest::BindChild { child, exposed_dir: _, responder } => {
                assert_eq!(child.name, child_name);
                assert_eq!(child.collection, Some(child_collection.to_string()));

                let _ = responder.send(&mut Ok(()));
            }
            _ => {
                assert!(false);
            }
        });

        assert!(bind_child_component(child_name, child_collection, &realm_proxy).await.is_ok());
    }

    /// Tests that a success received when binding a child results in an appropriate result from
    /// `bind_child`.
    #[fasync::run_until_stalled(test)]
    async fn bind_child_success() {
        let realm_proxy = spawn_realm_server(move |realm_request| match realm_request {
            fsys::RealmRequest::BindChild { child: _, exposed_dir: _, responder } => {
                let _ = responder.send(&mut Ok(()));
            }
            _ => {
                assert!(false);
            }
        });

        assert!(bind_child_component("", "", &realm_proxy).await.is_ok());
    }

    /// Tests that binding a child returns the child's exposed Directory.
    #[fasync::run_until_stalled(test)]
    async fn bind_child_exposed_dir_success() {
        // Make a static call counter to avoid unneeded complexity with cloned Arc<Mutex>.
        lazy_static! {
            static ref CALL_COUNT: Counter = Counter::new(0);
        }

        let directory_request_handler = |directory_request| match directory_request {
            fio::DirectoryRequest::Open { path: fake_capability_path, .. } => {
                CALL_COUNT.inc();
                assert_eq!(fake_capability_path, "fake_capability_path");
            }
            _ => {
                assert!(false);
            }
        };

        let realm_proxy = spawn_realm_server(move |realm_request| match realm_request {
            fsys::RealmRequest::BindChild {
                child: _,
                exposed_dir: exposed_dir_server,
                responder,
            } => {
                CALL_COUNT.inc();
                spawn_directory_server(
                    exposed_dir_server.into_stream().unwrap(),
                    directory_request_handler,
                );
                let _ = responder.send(&mut Ok(()));
            }
            _ => {
                assert!(false);
            }
        });

        let exposed_dir = bind_child_component("", "", &realm_proxy).await.unwrap();

        // Create a proxy of any FIDL protocol, with any `await`-able method.
        // (`fio::DirectoryMarker` here is arbitrary.)
        let (client_end, server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();

        // Connect should succeed, but it is still an asynchronous operation.
        // The `directory_request_handler` is not called yet.
        assert!(fdio::service_connect_at(
            &exposed_dir,
            "fake_capability_path",
            server_end.into_channel()
        )
        .is_ok());

        // Attempting to invoke and await an arbitrary method to ensure the
        // `directory_request_handler` responds to the Open() method and increment
        // the DIRECTORY_OPEN_CALL_COUNT.
        //
        // Since this is a fake capability (of any arbitrary type), it should fail.
        assert!(client_end.rewind().await.is_err());

        // Calls to Realm::BindChild and Directory::Open should have happened.
        assert_eq!(CALL_COUNT.get(), 2);
    }

    /// Tests that an error received when binding a child results in an appropriate error from
    /// `bind_child`.
    #[fasync::run_until_stalled(test)]
    async fn bind_child_error() {
        let realm_proxy = spawn_realm_server(move |realm_request| match realm_request {
            fsys::RealmRequest::BindChild { child: _, exposed_dir: _, responder } => {
                let _ = responder.send(&mut Err(fcomponent::Error::Internal));
            }
            _ => {
                assert!(false);
            }
        });

        assert!(bind_child_component("", "", &realm_proxy).await.is_err());
    }

    /// Tests that `destroy_child` results in the appropriate call to `RealmProxy`.
    #[fasync::run_until_stalled(test)]
    async fn destroy_child_parameters() {
        let child_name = "test_child";
        let child_collection = "test_collection";

        let realm_proxy = spawn_realm_server(move |realm_request| match realm_request {
            fsys::RealmRequest::DestroyChild { child, responder } => {
                assert_eq!(child.name, child_name);
                assert_eq!(child.collection, Some(child_collection.to_string()));

                let _ = responder.send(&mut Ok(()));
            }
            _ => {
                assert!(false);
            }
        });

        assert!(destroy_child_component(child_name, child_collection, &realm_proxy).await.is_ok());
    }
}
