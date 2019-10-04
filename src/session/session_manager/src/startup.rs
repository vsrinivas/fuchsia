// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    failure::{self, Error, ResultExt},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client::connect_to_service,
    realm_management,
};

#[derive(FromArgs)]
/// The session manager component.
pub struct SessionManagerArgs {
    #[argh(option, short = 's')]
    /// the URL for the session to start.
    pub session_url: String,
}

/// The name of the session child component.
const SESSION_NAME: &str = "session";

/// The name of the child collection the session is added to, must match the declaration in
/// session_manager.cml.
const SESSION_CHILD_COLLECTION: &str = "session";

/// Gets the session url from `std::env::args()`. Fails with a comment about the missing
/// --session_url option if the argument isn't provided.
///
/// # Returns
/// `String` if the session url argument exists.
pub fn get_session_url() -> String {
    let SessionManagerArgs { session_url } = argh::from_env();
    session_url
}

/// Launches the "root" session, as specified in `std::env::args()`.
///
/// # Returns
/// `Ok` if the session component is added and bound successfully.
pub async fn launch_session() -> Result<(), Error> {
    let session_url = get_session_url();
    let realm =
        connect_to_service::<fsys::RealmMarker>().context("Could not connect to Realm service.")?;
    add_session_to_realm(&session_url, &realm).await?;
    bind_session(&realm).await?;

    Ok(())
}

/// Adds the session component as a child in the provided realm.
///
/// # Parameters
/// - `session_url`: The URL of the session component.
/// - `realm`: The realm to create the child in.
///
/// # Returns
/// `Ok` if the child was successfully added to the realm.
async fn add_session_to_realm(session_url: &str, realm: &fsys::RealmProxy) -> Result<(), Error> {
    realm_management::create_child(SESSION_NAME, session_url, SESSION_CHILD_COLLECTION, realm).await
}

/// Binds the session child which runs the session component.
///
/// # Parameters
/// - `realm`: The realm used to bind the child.
///
/// # Returns
/// `Ok` if the session child was successfully bound.
async fn bind_session(realm: &fsys::RealmProxy) -> Result<(), Error> {
    realm_management::bind_child(SESSION_NAME, SESSION_CHILD_COLLECTION, realm).await
}
