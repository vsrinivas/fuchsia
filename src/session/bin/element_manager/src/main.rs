// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Component that serves the `fuchsia.element.Manager` protocol.
//!
//! Elements launched through the `Manager` protocol are created in a collection as
//! children in of this component.
//!
//! # Lifecycle
//!
//! If a client closes its connection to `Manager`, any running elements that it
//! has proposed without an associated `fuchsia.element.Controller` will continue to run.

use {
    anyhow::Error,
    element_management::{ElementManager, SimpleElementManager},
    fidl_fuchsia_element as felement, fidl_fuchsia_sys2 as fsys2, fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_protocol, server::ServiceFs},
    futures::StreamExt,
    std::cell::RefCell,
};

/// This enum allows the session to match on incoming messages.
enum ExposedServices {
    Manager(felement::ManagerRequestStream),
}

/// The child collection to add elements to. This must match a collection name declared in
/// element_manager's CML file.
const ELEMENT_COLLECTION_NAME: &str = "elements";

/// The maximum number of concurrent requests.
const NUM_CONCURRENT_REQUESTS: usize = 5;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["element_manager"]).expect("Failed to initialize logger");

    let realm =
        connect_to_protocol::<fsys2::RealmMarker>().expect("Failed to connect to Realm service");
    let element_manager = RefCell::new(SimpleElementManager::new(realm, ELEMENT_COLLECTION_NAME));

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(ExposedServices::Manager);

    fs.take_and_serve_directory_handle()?;

    fs.for_each_concurrent(NUM_CONCURRENT_REQUESTS, |service_request: ExposedServices| async {
        match service_request {
            ExposedServices::Manager(request_stream) => {
                element_manager
                    .borrow_mut()
                    .handle_requests(request_stream)
                    .await
                    .expect("Failed to handle element manager requests");
            }
        }
    })
    .await;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::ELEMENT_COLLECTION_NAME,
        element_management::{ElementManager, SimpleElementManager},
        fidl::endpoints::ProtocolMarker,
        fidl::endpoints::{create_proxy_and_stream, spawn_stream_handler},
        fidl_fuchsia_component as fcomponent, fidl_fuchsia_element as felement,
        fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys2, fuchsia_async as fasync,
        lazy_static::lazy_static,
        session_testing::spawn_directory_server,
        test_util::Counter,
    };

    /// Spawns a local `Manager` server.
    ///
    /// # Parameters
    /// - `element_manager`: The `ElementManager` that Manager uses to launch elements.
    ///
    /// # Returns
    /// A `ManagerProxy` to the spawned server.
    fn spawn_manager_server(
        mut element_manager: Box<dyn ElementManager + Send + Sync>,
    ) -> felement::ManagerProxy {
        let (proxy, stream) = create_proxy_and_stream::<felement::ManagerMarker>()
            .expect("Failed to create Manager proxy and stream");

        fasync::Task::spawn(async move {
            element_manager
                .handle_requests(stream)
                .await
                .expect("Failed to handle element manager requests");
        })
        .detach();

        proxy
    }

    /// Tests that ProposeElement launches the element as a child in a realm.
    #[fasync::run_until_stalled(test)]
    async fn propose_element_launches_element() {
        lazy_static! {
            static ref CREATE_CHILD_CALL_COUNT: Counter = Counter::new(0);
            static ref BINDER_CONNECTION_COUNT: Counter = Counter::new(0);
        }

        let component_url = "fuchsia-pkg://fuchsia.com/simple_element#meta/simple_element.cm";

        let directory_request_handler = move |directory_request| match directory_request {
            fio::DirectoryRequest::Open { path, .. } => {
                if path == fcomponent::BinderMarker::DEBUG_NAME {
                    BINDER_CONNECTION_COUNT.inc();
                }
            }
            _ => panic!("Directory handler received an unexpected request"),
        };

        let realm = spawn_stream_handler(move |realm_request| async move {
            match realm_request {
                fsys2::RealmRequest::CreateChild { collection: _, decl, args: _, responder } => {
                    assert_eq!(decl.url.unwrap(), component_url);
                    CREATE_CHILD_CALL_COUNT.inc();
                    let _ = responder.send(&mut Ok(()));
                }
                fsys2::RealmRequest::OpenExposedDir { child: _, exposed_dir, responder } => {
                    spawn_directory_server(exposed_dir, directory_request_handler);
                    let _ = responder.send(&mut Ok(()));
                }
                _ => {
                    panic!("Realm handler received unexpected request");
                }
            }
        })
        .unwrap();

        let launcher = spawn_stream_handler(move |_launcher_request| async move {
            panic!("Launcher should not receive any requests as it's only used for v1 components");
        })
        .unwrap();

        let element_manager = Box::new(SimpleElementManager::new_with_sys_launcher(
            realm,
            ELEMENT_COLLECTION_NAME,
            launcher,
        ));
        let manager_proxy = spawn_manager_server(element_manager);

        let result = manager_proxy
            .propose_element(
                felement::Spec {
                    component_url: Some(component_url.to_string()),
                    ..felement::Spec::EMPTY
                },
                None,
            )
            .await;
        assert!(result.is_ok());

        assert_eq!(CREATE_CHILD_CALL_COUNT.get(), 1);
        assert_eq!(BINDER_CONNECTION_COUNT.get(), 1);
    }
}
