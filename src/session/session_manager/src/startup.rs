// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{self, format_err, Error, ResultExt},
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client::connect_to_service,
};

/// The name of the root session.
const ROOT_SESSION_NAME: &str = "root_session";
/// The name of the collection child session components will be added to.
const COLLECTION_NAME: &str = "session";
/// The name of this component.
const SESSION_MANAGER_NAME: &str = "session_manager";

/// Parse arguments from [std::env::args()].
/// A single non-flag argument is expected for the root session URL.
///
/// `args`: Arguments from [std::env::args()]
fn parse_session_url(args: Vec<String>) -> Result<String, Error> {
    if args.len() < 1 {
        return Err(format_err!("Please specify a startup session."));
    } else if args.len() > 2 {
        return Err(format_err!("Multiple arguments given."));
    }

    Ok(args[0].clone())
}

/// Returns a usage message for session_manager.
fn usage() -> String {
    format!(
        "Usage: {} <session-url>\n",
        std::env::args().next().unwrap_or(SESSION_MANAGER_NAME.to_string())
    )
}

/// Launches the root session specified in [std::env::args()] as a child component. Returns Ok(())
/// if this successfully creates and binds to the session.
pub async fn launch_root_session() -> Result<(), Error> {
    let root_session_url = parse_session_url(std::env::args().skip(1).collect()).context(usage());

    // Create the session child component.
    let realm =
        connect_to_service::<fsys::RealmMarker>().context("Could not connect to Realm service.")?;
    let mut collection_ref = fsys::CollectionRef { name: COLLECTION_NAME.to_string() };
    let child_decl = fsys::ChildDecl {
        name: Some(ROOT_SESSION_NAME.to_string()),
        url: Some(root_session_url?.to_string()),
        startup: Some(fsys::StartupMode::Lazy), // Dynamic children can only be started lazily.
    };
    match realm.create_child(&mut collection_ref, child_decl).await {
        Ok(_) => {}
        Err(err) => {
            return Err(format_err!("Failed to create root session. Error: {}", err));
        }
    };

    // Run the session component.
    let mut child_ref = fsys::ChildRef {
        name: ROOT_SESSION_NAME.to_string(),
        collection: Some(COLLECTION_NAME.to_string()),
    };
    let (_, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
    match realm.bind_child(&mut child_ref, server_end).await {
        Ok(_) => {}
        Err(err) => {
            return Err(format_err!("Failed to bind root session. Error: {}", err));
        }
    };

    Ok(())
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync};

    #[fasync::run_singlethreaded(test)]
    async fn parse_session_url_test() -> Result<(), Error> {
        let session_url = "fuchsia-pkg://fuchsia.com/session_pkg#meta/session.cm";
        let second_url = "fuchsia-pkg://fuchsia.com/session_pkg#meta/another.cm";
        let flag = "--flag";

        // Single session url is parsed correctly.
        match parse_session_url(vec![session_url.to_string()]) {
            Ok(url) => {
                assert_eq!(url, session_url);
            }
            Err(err) => {
                panic!("Session url not parsed correctly. Error: {}", err);
            }
        };

        // Zero or multiple arguments should fail parsing. Must be exactly one URL.
        assert!(parse_session_url(vec![]).is_err());
        assert!(parse_session_url(vec![
            session_url.to_string(),
            second_url.to_string(),
            flag.to_string()
        ])
        .is_err());
        Ok(())
    }
}
