// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::rewrite_manager::{CommitError, RewriteManager},
    anyhow::Error,
    fidl_fuchsia_pkg_rewrite::{
        EditTransactionRequest, EditTransactionRequestStream, EngineRequest, EngineRequestStream,
        RuleIteratorRequest, RuleIteratorRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    fuchsia_url::pkg_url::PkgUrl,
    fuchsia_url_rewrite::Rule,
    fuchsia_zircon::Status,
    futures::prelude::*,
    parking_lot::RwLock,
    std::{convert::TryInto, sync::Arc},
};

const LIST_CHUNK_SIZE: usize = 100;

#[derive(Debug, Clone)]
pub struct RewriteService {
    state: Arc<RwLock<RewriteManager>>,
}

impl RewriteService {
    pub fn new(state: Arc<RwLock<RewriteManager>>) -> Self {
        RewriteService { state }
    }

    pub async fn handle_client(&mut self, mut stream: EngineRequestStream) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            match request {
                EngineRequest::List { iterator, control_handle: _control_handle } => {
                    let iterator = iterator.into_stream()?;
                    self.serve_list(iterator);
                }
                EngineRequest::ListStatic { iterator, control_handle: _control_handle } => {
                    let iterator = iterator.into_stream()?;
                    self.serve_list_static(iterator);
                }
                EngineRequest::StartEditTransaction {
                    transaction,
                    control_handle: _control_handle,
                } => {
                    let transaction = transaction.into_stream()?;
                    self.serve_edit_transaction(transaction);
                }
                EngineRequest::TestApply { url, responder } => {
                    responder.send(
                        &mut self
                            .handle_test_apply(url.as_str())
                            .map(|url| url.to_string())
                            .map_err(|e| e.into_raw()),
                    )?;
                }
            }
        }

        Ok(())
    }

    pub(self) fn handle_test_apply(&self, url: &str) -> Result<PkgUrl, Status> {
        let url = url.parse().map_err(|e| {
            fx_log_err!("client provided invalid URL ({:?}): {:?}", url, e);
            Status::INVALID_ARGS
        })?;

        let rewritten = self.state.read().rewrite(url);
        Ok(rewritten)
    }

    pub(self) fn serve_list(&mut self, stream: RuleIteratorRequestStream) {
        let rules = self.state.read().list().cloned().collect();

        Self::serve_rule_iterator(rules, stream);
    }

    pub(self) fn serve_list_static(&mut self, stream: RuleIteratorRequestStream) {
        let rules = self.state.read().list_static().cloned().collect();

        Self::serve_rule_iterator(rules, stream);
    }

    pub(self) fn serve_edit_transaction(&mut self, mut stream: EditTransactionRequestStream) {
        let state = self.state.clone();
        let mut transaction = state.read().transaction();

        fasync::spawn(
            async move {
                while let Some(request) = stream.try_next().await? {
                    match request {
                        EditTransactionRequest::ListDynamic {
                            iterator,
                            control_handle: _control_handle,
                        } => {
                            let rules = transaction.list_dynamic().cloned().collect();
                            let iterator = iterator.into_stream()?;
                            Self::serve_rule_iterator(rules, iterator);
                        }
                        EditTransactionRequest::ResetAll { control_handle: _control_handle } => {
                            transaction.reset_all();
                        }
                        EditTransactionRequest::Add { rule, responder } => {
                            let status = match rule.try_into() {
                                Ok(rule) => {
                                    transaction.add(rule);
                                    Status::OK
                                }
                                Err(_) => Status::INVALID_ARGS,
                            };
                            responder.send(status.into_raw())?;
                        }
                        EditTransactionRequest::Commit { responder } => {
                            let status = match state.write().apply(transaction) {
                                Ok(()) => Status::OK,
                                Err(CommitError::TooLate) => Status::UNAVAILABLE,
                                Err(CommitError::DynamicConfigurationDisabled) => {
                                    Status::ACCESS_DENIED
                                }
                            };
                            responder.send(status.into_raw())?;
                            return Ok(());
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| {
                fx_log_err!("while serving rewrite rule edit transaction: {:?}", e)
            }),
        )
    }

    fn serve_rule_iterator(rules: Vec<Rule>, mut stream: RuleIteratorRequestStream) {
        let mut rules = rules.into_iter().map(|rule| rule.into()).collect::<Vec<_>>();

        fasync::spawn(
            async move {
                let mut iter = rules.iter_mut();
                while let Some(request) = stream.try_next().await? {
                    let RuleIteratorRequest::Next { responder } = request;

                    responder.send(&mut iter.by_ref().take(LIST_CHUNK_SIZE))?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: fidl::Error| {
                fx_log_err!("while serving rewrite rule iterator: {:?}", e)
            }),
        );
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::rewrite_manager::{tests::make_rule_config, RewriteManagerBuilder, Transaction},
        fidl::endpoints::{create_proxy, create_proxy_and_stream},
        fidl_fuchsia_pkg_rewrite::{
            EditTransactionMarker, EditTransactionProxy, RuleIteratorMarker,
        },
    };

    macro_rules! rule {
        ($host_match:expr => $host_replacement:expr,
         $path_prefix_match:expr => $path_prefix_replacement:expr) => {
            fuchsia_url_rewrite::Rule::new(
                $host_match.to_owned(),
                $host_replacement.to_owned(),
                $path_prefix_match.to_owned(),
                $path_prefix_replacement.to_owned(),
            )
            .unwrap()
        };
    }

    /// Given the future of a FIDL API call, wait for it to complete, and assert that it
    /// successfully returns the given [`fuchsia_zircon::Status`].
    macro_rules! assert_yields_status {
        ($expr:expr, $status:expr) => {
            assert_eq!(Status::from_raw($expr.await.unwrap()), $status);
        };
    }

    async fn collect_iterator<F, I, O>(mut next: impl FnMut() -> F) -> Vec<O>
    where
        F: Future<Output = Result<Vec<I>, fidl::Error>>,
        I: TryInto<O>,
        <I as TryInto<O>>::Error: std::fmt::Debug,
    {
        let mut res = Vec::new();
        loop {
            let more = next().await.unwrap();
            if more.is_empty() {
                break;
            }
            res.extend(more.into_iter().map(|item| item.try_into().unwrap()));
        }
        assert!(next().await.unwrap().is_empty());
        res
    }

    fn list_rules(state: Arc<RwLock<RewriteManager>>) -> impl Future<Output = Vec<Rule>> {
        let (iterator, request_stream) = create_proxy_and_stream::<RuleIteratorMarker>().unwrap();
        RewriteService::new(state).serve_list(request_stream);

        collect_iterator(move || iterator.next())
    }

    fn list_static_rules(state: Arc<RwLock<RewriteManager>>) -> impl Future<Output = Vec<Rule>> {
        let (iterator, request_stream) = create_proxy_and_stream::<RuleIteratorMarker>().unwrap();
        RewriteService::new(state).serve_list_static(request_stream);

        collect_iterator(move || iterator.next())
    }

    fn transaction_list_dynamic_rules(
        client: &EditTransactionProxy,
    ) -> impl Future<Output = Vec<Rule>> {
        let (iterator, request_stream) = create_proxy::<RuleIteratorMarker>().unwrap();
        client.list_dynamic(request_stream).unwrap();

        collect_iterator(move || iterator.next())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_list() {
        let dynamic_rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/"),
        ];
        let dynamic_config = make_rule_config(dynamic_rules.clone());
        let static_rules = vec![
            rule!("fuchsia.com" => "static.fuchsia.com", "/3" => "/3"),
            rule!("fuchsia.com" => "static.fuchsia.com", "/4" => "/4"),
        ];
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(Some(&dynamic_config))
                .unwrap()
                .static_rules(static_rules.clone())
                .build(),
        ));

        let mut expected = dynamic_rules;
        expected.extend(static_rules);
        assert_eq!(list_rules(state.clone()).await, expected);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_list_static() {
        let dynamic_config = make_rule_config(vec![
            rule!("fuchsia.com" => "dynamic.fuchsia.com", "/1" => "/1"),
            rule!("fuchsia.com" => "dynamic.fuchsia.com", "/2" => "/2"),
        ]);
        let static_rules = vec![
            rule!("fuchsia.com" => "static.fuchsia.com", "/3" => "/3"),
            rule!("fuchsia.com" => "static.fuchsia.com", "/4" => "/4"),
        ];
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(Some(&dynamic_config))
                .unwrap()
                .static_rules(static_rules.clone())
                .build(),
        ));

        assert_eq!(list_static_rules(state.clone()).await, static_rules);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_reset_all() {
        let rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/"),
        ];
        let dynamic_config = make_rule_config(rules.clone());
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build(),
        ));
        let mut service = RewriteService::new(state.clone());

        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream);

        client.reset_all().unwrap();
        assert_yields_status!(client.commit(), Status::OK);

        assert_eq!(state.read().transaction(), Transaction::new(vec![], 1));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_transaction_list_dynamic() {
        let dynamic_rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/"),
        ];
        let dynamic_config = make_rule_config(dynamic_rules.clone());
        let static_rules = vec![rule!("fuchsia.com" => "static.fuchsia.com", "/" => "/")];
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(Some(&dynamic_config))
                .unwrap()
                .static_rules(static_rules)
                .build(),
        ));
        let mut service = RewriteService::new(state.clone());

        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream);

        // The transaction should list all dynamic rules.
        assert_eq!(transaction_list_dynamic_rules(&client).await, dynamic_rules.clone());

        // Start a list call, but don't drain it yet.
        let pending_list = transaction_list_dynamic_rules(&client);

        // Remove all dynamic rules and ensure the transaction lists no rules.
        client.reset_all().unwrap();
        assert_eq!(transaction_list_dynamic_rules(&client).await, vec![]);

        assert_yields_status!(client.commit(), Status::OK);

        // Ensure the list call from earlier lists the dynamic rules available at the time the
        // iterator was created.
        assert_eq!(pending_list.await, dynamic_rules);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_concurrent_edit() {
        let rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/"),
        ];
        let dynamic_config = make_rule_config(rules.clone());
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build(),
        ));
        let mut service = RewriteService::new(state.clone());

        let client1 = {
            let (client, request_stream) =
                create_proxy_and_stream::<EditTransactionMarker>().unwrap();
            service.serve_edit_transaction(request_stream);
            client
        };

        let client2 = {
            let (client, request_stream) =
                create_proxy_and_stream::<EditTransactionMarker>().unwrap();
            service.serve_edit_transaction(request_stream);
            client
        };

        client1.reset_all().unwrap();
        client2.reset_all().unwrap();

        let rule = rule!("fuchsia.com" => "fuchsia.com", "/foo" => "/foo");
        assert_yields_status!(client1.add(&mut rule.clone().into()), Status::OK);

        assert_yields_status!(client1.commit(), Status::OK);
        assert_yields_status!(client2.commit(), Status::UNAVAILABLE);

        assert_eq!(state.read().transaction(), Transaction::new(vec![rule], 1),);

        let client2 = {
            let (client, request_stream) =
                create_proxy_and_stream::<EditTransactionMarker>().unwrap();
            service.serve_edit_transaction(request_stream);
            client
        };
        client2.reset_all().unwrap();
        assert_yields_status!(client2.commit(), Status::OK);
        assert_eq!(state.read().transaction(), Transaction::new(vec![], 2));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_concurrent_list_and_edit() {
        let dynamic_config = make_rule_config(vec![]);
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build(),
        ));
        let mut service = RewriteService::new(state.clone());

        assert_eq!(list_rules(state.clone()).await, vec![]);

        let edit_client = {
            let (client, request_stream) =
                create_proxy_and_stream::<EditTransactionMarker>().unwrap();
            service.serve_edit_transaction(request_stream);
            client
        };

        let rule = rule!("fuchsia.com" => "devhost.fuchsia.com", "/" => "/");

        assert_yields_status!(edit_client.add(&mut rule.clone().into()), Status::OK);

        assert_eq!(list_rules(state.clone()).await, vec![]);

        let long_list_call = {
            let (client, request_stream) = create_proxy_and_stream::<RuleIteratorMarker>().unwrap();
            service.serve_list(request_stream);
            client
        };

        assert_yields_status!(edit_client.commit(), Status::OK);

        assert_eq!(long_list_call.next().await.unwrap(), vec![]);

        assert_eq!(list_rules(state.clone()).await, vec![rule]);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_rewrite() {
        let dynamic_config = make_rule_config(vec![]);
        let static_rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/old/" => "/new/"),
            rule!("fuchsia.com" => "devhost", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/identity" => "/identity"),
        ];
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(Some(&dynamic_config))
                .unwrap()
                .static_rules(static_rules.clone())
                .build(),
        ));
        let service = RewriteService::new(state.clone());

        for (url, rewritten) in &[
            ("fuchsia-pkg://fuchsia.com/old/a", "fuchsia-pkg://fuchsia.com/new/a"),
            ("fuchsia-pkg://fuchsia.com/rolldice", "fuchsia-pkg://devhost/rolldice"),
            ("fuchsia-pkg://fuchsia.com/identity", "fuchsia-pkg://fuchsia.com/identity"),
        ] {
            assert_eq!(service.handle_test_apply(url), Ok(rewritten.parse().unwrap()));
        }

        for url in &[
            "fuchsia-pkg://subdomain.fuchsia.com/unmatcheddomain",
            "fuchsia-pkg://devhost/unmatcheddomain",
            "fuchsia-pkg://fuchsia.com/new/unmatcheddir",
        ] {
            assert_eq!(service.handle_test_apply(url), Ok(url.parse().unwrap()));
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn test_rewrite_rejects_invalid_inputs() {
        let dynamic_config = make_rule_config(vec![]);
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build(),
        ));
        let service = RewriteService::new(state.clone());

        for url in &["not-fuchsia-pkg://fuchsia.com/test", "fuchsia-pkg://fuchsia.com/a*"] {
            assert_eq!(service.handle_test_apply(url), Err(Status::INVALID_ARGS));
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn test_concurrent_rewrite_and_edit() {
        let dynamic_config =
            make_rule_config(vec![rule!("fuchsia.com" => "fuchsia.com", "/a" => "/b")]);
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build(),
        ));
        let mut service = RewriteService::new(state.clone());

        let (edit_client, request_stream) =
            create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream);

        let replacement_rule = rule!("fuchsia.com" => "fuchsia.com", "/a" => "/c");

        edit_client.reset_all().unwrap();
        assert_yields_status!(edit_client.add(&mut replacement_rule.into()), Status::OK);

        // Pending transaction does not affect apply call.
        assert_eq!(
            service.handle_test_apply("fuchsia-pkg://fuchsia.com/a"),
            Ok("fuchsia-pkg://fuchsia.com/b".parse().unwrap())
        );

        assert_yields_status!(edit_client.commit(), Status::OK);

        // New rule set now applies.
        assert_eq!(
            service.handle_test_apply("fuchsia-pkg://fuchsia.com/a"),
            Ok("fuchsia-pkg://fuchsia.com/c".parse().unwrap())
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn test_enables_amber_source() {
        let dynamic_config = make_rule_config(vec![]);
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build(),
        ));
        let mut service = RewriteService::new(state.clone());

        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream);

        let rules = vec![
            rule!("example.com" => "wrong_host.fuchsia.com", "/" => "/"),
            rule!("fuchsia.com" => "correct.fuchsia.com", "/" => "/"),
            rule!("fuchsia.com" => "wrong_priority.fuchsia.com", "/" => "/"),
            rule!("fuchsia.com" => "wrong_match.fuchsia.com", "/foo/" => "/"),
            rule!("fuchsia.com" => "wrong_replacement.fuchsia.com", "/" => "/bar/"),
        ];
        for rule in rules.into_iter().rev() {
            assert_yields_status!(client.add(&mut rule.into()), Status::OK);
        }
        assert_yields_status!(client.commit(), Status::OK);

        // Adding a duplicate of the currently enabled source is a no-op.
        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream);
        let rule = rule!("fuchsia.com" => "correct.fuchsia.com", "/" => "/");
        assert_yields_status!(client.add(&mut rule.into()), Status::OK);
        assert_yields_status!(client.commit(), Status::OK);

        // Adding a different entry with higher priority enables the new source.
        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream);
        let rule = rule!("fuchsia.com" => "correcter.fuchsia.com", "/" => "/");
        assert_yields_status!(client.add(&mut rule.into()), Status::OK);
        assert_yields_status!(client.commit(), Status::OK);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_disables_amber_sources() {
        let rules = vec![rule!("fuchsia.com" => "enabled.fuchsia.com", "/" => "/")];

        let dynamic_config = make_rule_config(rules);
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build(),
        ));
        let mut service = RewriteService::new(state.clone());

        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream);
        client.reset_all().unwrap();
        assert_yields_status!(client.commit(), Status::OK);

        // Edits that don't change the enabled source are a no-op.
        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream);
        client.reset_all().unwrap();
        assert_yields_status!(client.commit(), Status::OK);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_transaction_commit_fails_if_no_dynamic_rules_path() {
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(Option::<&std::path::Path>::None).unwrap().build(),
        ));
        let mut service = RewriteService::new(state);

        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream);

        let status = Status::from_raw(client.commit().await.unwrap());

        assert_eq!(status, Status::ACCESS_DENIED);
    }
}
