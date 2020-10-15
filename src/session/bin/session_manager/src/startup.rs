// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::cobalt,
    argh::FromArgs,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon as zx, realm_management,
    serde::{Deserialize, Serialize},
    serde_json,
    std::fs,
    thiserror::{self, Error},
};

#[derive(FromArgs)]
/// The session manager component.
pub struct SessionManagerArgs {
    #[argh(option, short = 's')]
    /// the URL for the session to start.
    pub session_url: Option<String>,
}

#[derive(Serialize, Deserialize)]
/// The session manager component.
pub struct SessionManagerConfigs {
    /// the URL for the session to start.
    pub session_url: String,
}

/// Errors returned by calls startup functions.
#[derive(Debug, Error, Clone, PartialEq)]
pub enum StartupError {
    #[error("Could not connect to Realm.")]
    RealmConnection,

    #[error("Existing session not destroyed at \"{}/{}\": {:?}", collection, name, err)]
    NotDestroyed { name: String, collection: String, err: fcomponent::Error },

    #[error("Session {} not created at \"{}/{}\": {:?}", url, collection, name, err)]
    NotCreated { name: String, collection: String, url: String, err: fcomponent::Error },

    #[error("Session {} not bound at \"{}/{}\": {:?}", url, collection, name, err)]
    NotBound { name: String, collection: String, url: String, err: fcomponent::Error },
}

/// The name of the session child component.
const SESSION_NAME: &str = "session";

/// The name of the child collection the session is added to, must match the declaration in
/// session_manager.cml.
const SESSION_CHILD_COLLECTION: &str = "session";

/// The path to the configuration file for the session.
const CONFIG_PATH: &str = "/config/data/startup.config";

/// Gets the session url from `/config/data/startup.config`.
///
/// If no configuration file exists, gets the session url from `std::env::args()`.
/// Fails with a comment about the missing --session_url option if the argument isn't provided.
///
/// # Returns
/// `String` if the session url argument exists, else `None`.
pub fn get_session_url() -> Option<String> {
    let mut session_url: Option<String> = None;
    if let Ok(config_str) = fs::read_to_string(CONFIG_PATH) {
        if let Ok(session_manager_args) = serde_json::from_str::<SessionManagerConfigs>(&config_str)
        {
            session_url = Some(session_manager_args.session_url);
        }
    }

    if session_url.is_none() {
        let SessionManagerArgs { session_url } = argh::from_env();
        return session_url;
    }

    session_url
}

/// Launches the specified session.
///
/// Any existing session child will be destroyed prior to launching the new session.
///
/// # Parameters
/// - `session_url`: The URL of the session to launch.
///
/// # Errors
/// If there was a problem creating or binding to the session component instance.
pub async fn launch_session(session_url: &str) -> Result<(), StartupError> {
    let realm =
        connect_to_service::<fsys::RealmMarker>().map_err(|_| StartupError::RealmConnection)?;

    let start_time = zx::Time::get_monotonic();
    set_session(&session_url, realm).await?;
    let end_time = zx::Time::get_monotonic();

    let url = session_url.to_string();
    fasync::Task::local(async move {
        if let Ok(cobalt_logger) = cobalt::get_logger() {
            // The result is disregarded as there is not retry-logic if it fails, and the error is
            // not meant to be fatal.
            let _ =
                cobalt::log_session_launch_time(cobalt_logger, &url, start_time, end_time).await;
        }
    })
    .detach();

    Ok(())
}

/// Sets the currently active session.
///
/// If an existing session is running, the session's component instance will be destroyed prior to
/// creating the new session, effectively replacing the session.
///
/// # Parameters
/// - `session_url`: The URL of the session to instantiate.
/// - `realm`: The realm in which to create the session.
///
/// # Errors
/// Returns an error if any of the realm operations fail, or the realm is unavailable.
async fn set_session(session_url: &str, realm: fsys::RealmProxy) -> Result<(), StartupError> {
    realm_management::destroy_child_component(SESSION_NAME, SESSION_CHILD_COLLECTION, &realm)
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
        &realm,
    )
    .await
    .map_err(|err| StartupError::NotCreated {
        name: SESSION_NAME.to_string(),
        collection: SESSION_CHILD_COLLECTION.to_string(),
        url: session_url.to_string(),
        err,
    })?;

    realm_management::bind_child_component(SESSION_NAME, SESSION_CHILD_COLLECTION, &realm)
        .await
        .map_err(|err| StartupError::NotBound {
            name: SESSION_NAME.to_string(),
            collection: SESSION_CHILD_COLLECTION.to_string(),
            url: session_url.to_string(),
            err,
        })?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::{set_session, SESSION_CHILD_COLLECTION, SESSION_NAME},
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
        futures::prelude::*,
    };

    /// Spawns a local `fidl_fuchsia_sys2::Realm` server, and returns a proxy to the spawned server.
    /// The provided `request_handler` is notified when an incoming request is received.
    ///
    /// # Parameters
    /// - `request_handler`: A function which is called with incoming requests to the spawned
    ///                      `Realm` server.
    /// # Returns
    /// A `RealmProxy` to the spawned server.
    fn spawn_realm_server<F: 'static>(mut request_handler: F) -> fsys::RealmProxy
    where
        F: FnMut(fsys::RealmRequest) + Send,
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

    /// Tests that setting the session calls the appropriate realm methods in the appropriate
    /// order.
    #[fasync::run_singlethreaded(test)]
    async fn set_session_test() {
        let session_url = "session";
        // The number of realm calls which have been made so far.
        let mut num_realm_requests: i32 = 0;

        let realm = spawn_realm_server(move |realm_request| {
            match realm_request {
                fsys::RealmRequest::DestroyChild { child, responder } => {
                    assert_eq!(num_realm_requests, 0);
                    assert_eq!(child.collection, Some(SESSION_CHILD_COLLECTION.to_string()));
                    assert_eq!(child.name, SESSION_NAME);

                    let _ = responder.send(&mut Ok(()));
                }
                fsys::RealmRequest::CreateChild { collection, decl, responder } => {
                    assert_eq!(num_realm_requests, 1);
                    assert_eq!(decl.url.unwrap(), session_url);
                    assert_eq!(decl.name.unwrap(), SESSION_NAME);
                    assert_eq!(&collection.name, SESSION_CHILD_COLLECTION);

                    let _ = responder.send(&mut Ok(()));
                }
                fsys::RealmRequest::BindChild { child, exposed_dir: _, responder } => {
                    assert_eq!(num_realm_requests, 2);
                    assert_eq!(child.collection, Some(SESSION_CHILD_COLLECTION.to_string()));
                    assert_eq!(child.name, SESSION_NAME);

                    let _ = responder.send(&mut Ok(()));
                }
                _ => {
                    assert!(false);
                }
            };
            num_realm_requests += 1;
        });

        assert!(set_session(session_url, realm).await.is_ok());
    }
}
