// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    failure::{self, format_err, Error, ResultExt},
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

    realm_management::create_child(SESSION_NAME, &session_url, SESSION_CHILD_COLLECTION, &realm)
        .await
        .map_err(|err| format_err!("Could not create session {:?}", err))?;
    realm_management::bind_child(SESSION_NAME, SESSION_CHILD_COLLECTION, &realm)
        .await
        .map_err(|err| format_err!("Could not bind to session {:?}", err))?;

    Ok(())
}
