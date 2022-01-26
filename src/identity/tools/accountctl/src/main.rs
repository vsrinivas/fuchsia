// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod args;

use {
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_identity_account::{AccountManagerMarker, AccountManagerProxy},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
};

/// Performs the operation defined by the supplied command line arguments using the supplied
/// `AccountManager`.
async fn perform_command(
    command: args::Command,
    account_manager: &AccountManagerProxy,
) -> Result<(), Error> {
    match command.subcommand {
        args::Subcommand::AccountIds(_) => print_account_ids(account_manager).await,
        args::Subcommand::RemoveAll(_) => remove_all(account_manager).await,
    }
}

/// Prints the current account IDs as reported by the supplied account manager.
async fn print_account_ids(account_manager: &AccountManagerProxy) -> Result<(), Error> {
    let ids = account_manager.get_account_ids().await?;
    println!("Account IDs = {:?}", ids);
    Ok(())
}

/// Removes all accounts reported by the supplied account manager.
async fn remove_all(account_manager: &AccountManagerProxy) -> Result<(), Error> {
    let ids = account_manager.get_account_ids().await?;
    if ids.is_empty() {
        println!("No accounts to remove.");
        return Ok(());
    }
    for id in ids {
        account_manager
            .remove_account(id, true)
            .await?
            .map_err(|err| anyhow!("Failed to remove account {}: {:?}", id, err))?;
        println!("Removed account {}", id);
    }
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");

    let command: args::Command = argh::from_env();
    println!("Connecting to AccountManager");
    let account_manager = connect_to_protocol::<AccountManagerMarker>()
        .context("Failed to connect to account manager")?;

    perform_command(command, &account_manager).await
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::anyhow,
        args::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_identity_account::{AccountManagerRequest, AccountManagerRequestStream},
        fuchsia_async::futures::TryStreamExt as _,
        std::sync::Arc,
    };

    /// Defines how the fake server should handle a single `AccountManagerRequest`.
    trait RequestHandler: Send + Sync {
        fn handle(&self, req: AccountManagerRequest) -> Result<(), Error>;
    }

    /// Spawns a task to handle requests from an AccountManagerStream, running the supplied
    /// sequence of request handlers on the sequence of incoming requests.
    fn handle_stream(
        mut stream: AccountManagerRequestStream,
        handlers: Vec<Arc<dyn RequestHandler>>,
    ) {
        fasync::Task::spawn(async move {
            let mut handler_it = handlers.into_iter();
            loop {
                match (stream.try_next().await.unwrap(), handler_it.next()) {
                    (Some(req), Some(handler)) => handler.handle(req).unwrap(),
                    (None, Some(_)) => {
                        panic!("Stream closed before sending all expected requests.")
                    }
                    (Some(_), None) => panic!("Stream did not close after all expected requests."),
                    (None, None) => break,
                }
            }
        })
        .detach();
    }

    struct GetAccountIdsHandler {
        ids: Vec<u64>,
    }

    impl RequestHandler for GetAccountIdsHandler {
        fn handle(&self, req: AccountManagerRequest) -> Result<(), Error> {
            match req {
                AccountManagerRequest::GetAccountIds { responder } => {
                    responder.send(&self.ids).context("Failed to send response")
                }
                _ => Err(anyhow!("Did not expect {:?}")),
            }
        }
    }

    struct RemoveAccountHandler {
        id: u64,
    }

    impl RequestHandler for RemoveAccountHandler {
        fn handle(&self, req: AccountManagerRequest) -> Result<(), Error> {
            match req {
                AccountManagerRequest::RemoveAccount { id, force, responder } => {
                    if id != self.id {
                        Err(anyhow!("Received RemoveAccount for {}, expected {}", id, self.id))
                    } else if force != true {
                        Err(anyhow!("Received RemoveAccount for {} without force set", id))
                    } else {
                        responder.send(&mut Ok(())).context("Failed to send response")
                    }
                }
                _ => Err(anyhow!("Did not expect {:?}")),
            }
        }
    }

    #[fuchsia::test]
    async fn test_get_account_ids() {
        let command = Command { subcommand: Subcommand::AccountIds(AccountIds {}) };
        let (proxy, stream) = create_proxy_and_stream::<AccountManagerMarker>().unwrap();
        handle_stream(stream, vec![Arc::new(GetAccountIdsHandler { ids: vec![1] })]);

        assert!(perform_command(command, &proxy).await.is_ok());
    }

    #[fuchsia::test]
    async fn test_remove_all_no_accounts() {
        let command = Command { subcommand: Subcommand::RemoveAll(RemoveAll {}) };
        let (proxy, stream) = create_proxy_and_stream::<AccountManagerMarker>().unwrap();
        handle_stream(stream, vec![Arc::new(GetAccountIdsHandler { ids: vec![] })]);

        assert!(perform_command(command, &proxy).await.is_ok());
    }

    #[fuchsia::test]
    async fn test_remove_all_multiple_accounts() {
        let command = Command { subcommand: Subcommand::RemoveAll(RemoveAll {}) };
        let (proxy, stream) = create_proxy_and_stream::<AccountManagerMarker>().unwrap();
        handle_stream(
            stream,
            vec![
                Arc::new(GetAccountIdsHandler { ids: vec![3, 2, 1] }),
                Arc::new(RemoveAccountHandler { id: 3 }),
                Arc::new(RemoveAccountHandler { id: 2 }),
                Arc::new(RemoveAccountHandler { id: 1 }),
            ],
        );

        assert!(perform_command(command, &proxy).await.is_ok());
    }
}
