// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::cobalt,
    fidl::endpoints::Proxy,
    fidl_fuchsia_component as fcomponent, fuchsia_async as fasync, fuchsia_zircon as zx,
    realm_management,
    thiserror::{self, Error},
    tracing::info,
};

/// Errors returned by calls startup functions.
#[derive(Debug, Error, Clone, PartialEq)]
pub enum StartupError {
    #[error("Existing session not destroyed at \"{}/{}\": {:?}", collection, name, err)]
    NotDestroyed { name: String, collection: String, err: fcomponent::Error },

    #[error("Session {} not created at \"{}/{}\": {:?}", url, collection, name, err)]
    NotCreated { name: String, collection: String, url: String, err: fcomponent::Error },

    #[error(
        "Exposed directory of session {} at \"{}/{}\" not opened: {:?}",
        url,
        collection,
        name,
        err
    )]
    ExposedDirNotOpened { name: String, collection: String, url: String, err: fcomponent::Error },

    #[error("Session {} not launched at \"{}/{}\": {:?}", url, collection, name, err)]
    NotLaunched { name: String, collection: String, url: String, err: fcomponent::Error },
}

/// The name of the session child component.
const SESSION_NAME: &str = "session";

/// The name of the child collection the session is added to, must match the declaration in
/// session_manager.cml.
const SESSION_CHILD_COLLECTION: &str = "session";

/// Launches the specified session.
///
/// Any existing session child will be destroyed prior to launching the new session.
///
/// Returns a channel to the session component's `exposed_dir` directory, or an error.
///
/// # Parameters
/// - `session_url`: The URL of the session to launch.
/// - `realm`: The realm in which to launch the session.
///
/// # Errors
/// If there was a problem creating or binding to the session component instance.
pub async fn launch_session(
    session_url: &str,
    realm: &fcomponent::RealmProxy,
) -> Result<zx::Channel, StartupError> {
    info!(session_url, "Launching session");

    let start_time = zx::Time::get_monotonic();
    let exposed_dir = set_session(&session_url, realm).await?;
    let end_time = zx::Time::get_monotonic();

    fasync::Task::local(async move {
        if let Ok(cobalt_logger) = cobalt::get_logger() {
            // The result is disregarded as there is not retry-logic if it fails, and the error is
            // not meant to be fatal.
            let _ = cobalt::log_session_launch_time(cobalt_logger, start_time, end_time).await;
        }
    })
    .detach();

    Ok(exposed_dir)
}

/// Sets the currently active session.
///
/// If an existing session is running, the session's component instance will be destroyed prior to
/// creating the new session, effectively replacing the session.
///
/// The session is launched by connecting to the fuchsia.component.Binder protocol
/// in its exposed directory. This capability bind will trigger the component
/// to start.
///
/// Returns a channel to the session component's `exposed_dir` directory, or an error.
///
/// # Parameters
/// - `session_url`: The URL of the session to instantiate.
/// - `realm`: The realm in which to create the session.
///
/// # Errors
/// Returns an error if any of the realm operations fail, or the realm is unavailable.
async fn set_session(
    session_url: &str,
    realm: &fcomponent::RealmProxy,
) -> Result<zx::Channel, StartupError> {
    realm_management::destroy_child_component(SESSION_NAME, SESSION_CHILD_COLLECTION, realm)
        .await
        .or_else(|err: fcomponent::Error| match err {
            // Since the intent is simply to clear out the existing session child if it exists,
            // related errors are disregarded.
            fcomponent::Error::InvalidArguments
            | fcomponent::Error::InstanceNotFound
            | fcomponent::Error::CollectionNotFound => Ok(()),
            _ => Err(err),
        })
        .map_err(|err| StartupError::NotDestroyed {
            name: SESSION_NAME.to_string(),
            collection: SESSION_CHILD_COLLECTION.to_string(),
            err,
        })?;

    realm_management::create_child_component(
        SESSION_NAME,
        &session_url,
        SESSION_CHILD_COLLECTION,
        realm,
    )
    .await
    .map_err(|err| StartupError::NotCreated {
        name: SESSION_NAME.to_string(),
        collection: SESSION_CHILD_COLLECTION.to_string(),
        url: session_url.to_string(),
        err,
    })?;

    let exposed_dir = realm_management::open_child_component_exposed_dir(
        SESSION_NAME,
        SESSION_CHILD_COLLECTION,
        realm,
    )
    .await
    .map_err(|err| StartupError::ExposedDirNotOpened {
        name: SESSION_NAME.to_string(),
        collection: SESSION_CHILD_COLLECTION.to_string(),
        url: session_url.to_string(),
        err,
    })?;

    // By connecting to the fuchsia.component.Binder protocol, we instruct
    // Component Manager to *start* the session component.
    let _ = fuchsia_component::client::connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(
        &exposed_dir,
    )
    .map_err(|_err| StartupError::NotLaunched {
        name: SESSION_NAME.to_string(),
        collection: SESSION_CHILD_COLLECTION.to_string(),
        url: session_url.to_string(),
        err: fcomponent::Error::InstanceCannotStart,
    })?;

    Ok(exposed_dir.into_channel().unwrap().into_zx_channel())
}

#[cfg(test)]
mod tests {
    use {
        super::{set_session, zx, StartupError, SESSION_CHILD_COLLECTION, SESSION_NAME},
        fidl::endpoints::spawn_stream_handler,
        fidl_fuchsia_component as fcomponent, fidl_fuchsia_io as fio,
        lazy_static::lazy_static,
        session_testing::spawn_directory_server,
        std::sync::mpsc,
        test_util::Counter,
    };

    #[fuchsia::test]
    async fn set_session_calls_realm_methods_in_appropriate_order() {
        lazy_static! {
            // The number of realm calls which have been made so far.
            static ref NUM_REALM_REQUESTS: Counter = Counter::new(0);
        }

        let session_url = "session";

        let directory_request_handler = move |directory_request| match directory_request {
            fio::DirectoryRequest::Open { path: _, .. } => {
                assert_eq!(NUM_REALM_REQUESTS.get(), 4);
            }
            _ => panic!("Directory handler received an unexpected request"),
        };

        let realm = spawn_stream_handler(move |realm_request| async move {
            match realm_request {
                fcomponent::RealmRequest::DestroyChild { child, responder } => {
                    assert_eq!(NUM_REALM_REQUESTS.get(), 0);
                    assert_eq!(child.collection, Some(SESSION_CHILD_COLLECTION.to_string()));
                    assert_eq!(child.name, SESSION_NAME);

                    let _ = responder.send(&mut Ok(()));
                }
                fcomponent::RealmRequest::CreateChild { collection, decl, args: _, responder } => {
                    assert_eq!(NUM_REALM_REQUESTS.get(), 1);
                    assert_eq!(decl.url.unwrap(), session_url);
                    assert_eq!(decl.name.unwrap(), SESSION_NAME);
                    assert_eq!(&collection.name, SESSION_CHILD_COLLECTION);

                    let _ = responder.send(&mut Ok(()));
                }
                fcomponent::RealmRequest::OpenExposedDir { child, exposed_dir, responder } => {
                    assert_eq!(NUM_REALM_REQUESTS.get(), 2);
                    assert_eq!(child.collection, Some(SESSION_CHILD_COLLECTION.to_string()));
                    assert_eq!(child.name, SESSION_NAME);

                    spawn_directory_server(exposed_dir, directory_request_handler);
                    let _ = responder.send(&mut Ok(()));
                }
                _ => panic!("Realm handler received an unexpected request"),
            };
            NUM_REALM_REQUESTS.inc();
        })
        .unwrap();

        assert!(set_session(session_url, &realm).await.is_ok());
    }

    #[fuchsia::test]
    async fn set_session_returns_channel_bound_to_exposed_dir() {
        let session_url = "session";
        let (exposed_dir_server_end_sender, exposed_dir_server_end_receiver) = mpsc::channel();

        let realm = spawn_stream_handler(move |realm_request| {
            let exposed_dir_server_end_sender = exposed_dir_server_end_sender.clone();
            async move {
                match realm_request {
                    fcomponent::RealmRequest::DestroyChild { responder, .. } => {
                        let _ = responder.send(&mut Ok(()));
                    }
                    fcomponent::RealmRequest::CreateChild { responder, .. } => {
                        let _ = responder.send(&mut Ok(()));
                    }
                    fcomponent::RealmRequest::OpenExposedDir { exposed_dir, responder, .. } => {
                        exposed_dir_server_end_sender
                            .send(exposed_dir)
                            .expect("Failed to relay `exposed_dir`");
                        let _ = responder.send(&mut Ok(()));
                    }
                    _ => panic!("Realm handler received an unexpected request"),
                }
            }
        })
        .unwrap();

        let exposed_dir_client_end = match set_session(session_url, &realm).await {
            Ok(exposed_dir_client_end) => exposed_dir_client_end,
            Err(e) => panic!("Failed to set_session() {:?}", e),
        };
        let exposed_dir_server_end =
            exposed_dir_server_end_receiver.recv().expect("Failed to read exposed_dir from relay");
        exposed_dir_server_end
            .into_channel()
            .write(b"hello world", /* handles */ &mut vec![])
            .expect("Failed to write to server end");

        let mut read_buf = zx::MessageBuf::new();
        exposed_dir_client_end.read(&mut read_buf).expect("Failed to read from client end");
        assert_eq!(read_buf.bytes(), b"hello world", "server and client channels do not match");
    }

    #[fuchsia::test]
    async fn set_session_returns_error_if_binder_connection_fails() {
        let session_url = "session";

        let realm = spawn_stream_handler(move |realm_request| async move {
            match realm_request {
                fcomponent::RealmRequest::DestroyChild { responder, .. } => {
                    let _ = responder.send(&mut Ok(()));
                }
                fcomponent::RealmRequest::CreateChild { responder, .. } => {
                    let _ = responder.send(&mut Ok(()));
                }
                fcomponent::RealmRequest::OpenExposedDir { responder, .. } => {
                    let _ = responder.send(&mut Ok(()));
                }
                _ => panic!("Realm handler received an unexpected request"),
            };
        })
        .unwrap();

        // By not implementing a handler for exposed_dir channel, the
        // set_session function will observe a PEER_CLOSED signal.
        assert_matches::assert_matches!(
            set_session(session_url, &realm).await,
            Err(StartupError::NotLaunched { .. })
        );
    }
}
