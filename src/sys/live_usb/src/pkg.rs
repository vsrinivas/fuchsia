// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_pkg_rewrite::{
        EditTransactionMarker, EngineMarker, EngineProxy, LiteralRule, Rule,
    },
    fuchsia_zircon as zx,
};

pub async fn disable_updates() -> Result<(), Error> {
    let engine = fuchsia_component::client::connect_to_protocol::<EngineMarker>()
        .context("connecting to rewrite engine")?;
    disable_updates_at(engine).await
}

async fn disable_updates_at(engine: EngineProxy) -> Result<(), Error> {
    let (edit_transaction, server) = fidl::endpoints::create_proxy::<EditTransactionMarker>()
        .context("creating edit transaction")?;

    engine.start_edit_transaction(server).context("starting edit transaction")?;

    let mut rule = Rule::Literal(LiteralRule {
        host_match: "fuchsia.com".to_owned(),
        host_replacement: "url.invalid".to_owned(),
        path_prefix_match: "/update".to_owned(),
        path_prefix_replacement: "/not-an-update".to_owned(),
    });

    edit_transaction
        .add(&mut rule)
        .await
        .context("send adding rule to edit transaction")?
        .map_err(zx::Status::from_raw)
        .context("adding rule to edit transaction")?;
    edit_transaction
        .commit()
        .await
        .context("send commiting transaction")?
        .map_err(zx::Status::from_raw)
        .context("comitting transaction")?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_pkg_rewrite::{
            EditTransactionRequest, EditTransactionRequestStream, EngineRequest,
            EngineRequestStream,
        },
        fidl_fuchsia_pkg_rewrite_ext::Rule,
        fuchsia_async as fasync,
        fuchsia_url::pkg_url::PkgUrl,
        futures::prelude::*,
        std::convert::TryInto,
    };

    /// A mock implementation of fuchsia.pkg.rewrite.Engine.
    struct MockRewriteEngine {
        rules: Vec<Rule>,
    }

    impl MockRewriteEngine {
        pub fn new() -> Self {
            MockRewriteEngine { rules: Vec::new() }
        }

        pub async fn handle_engine_stream(mut self, mut stream: EngineRequestStream) -> Self {
            while let Some(request) = stream.try_next().await.unwrap() {
                match request {
                    EngineRequest::StartEditTransaction { transaction, .. } => {
                        self.handle_edit_transaction_stream(transaction.into_stream().unwrap())
                            .await;
                        return self;
                    }

                    other => panic!("Unexpected request {:?}", other),
                }
            }

            panic!("Stream closed too early");
        }

        async fn handle_edit_transaction_stream(
            &mut self,
            mut stream: EditTransactionRequestStream,
        ) {
            while let Some(request) = stream.try_next().await.unwrap() {
                match request {
                    EditTransactionRequest::Add { rule, responder } => {
                        let rule: Rule = rule.try_into().unwrap();
                        self.rules.push(rule);
                        responder.send(&mut Ok(())).expect("send reply ok");
                    }
                    EditTransactionRequest::Commit { responder } => {
                        responder.send(&mut Ok(())).expect("send reply ok");
                    }
                    e => panic!("Unexpected request {:?}", e),
                }
            }
        }

        pub fn apply_rules(&self, url: &PkgUrl) -> PkgUrl {
            self.rules
                .iter()
                .fold(url.clone(), |url, rule| rule.apply(&url).unwrap().expect("rule ok"))
        }
    }

    #[fuchsia::test]
    async fn test_disable_updates() {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<EngineMarker>()
            .expect("create proxy and stream succeeds");
        let engine = MockRewriteEngine::new();

        let task = fasync::Task::spawn(engine.handle_engine_stream(stream));
        disable_updates_at(proxy).await.expect("Disabling updates succeeds");

        let engine = task.await;

        let update_url = PkgUrl::parse("fuchsia-pkg://fuchsia.com/update").unwrap();
        // Expect that the update url changes.
        assert_ne!(engine.apply_rules(&update_url), update_url);
    }
}
