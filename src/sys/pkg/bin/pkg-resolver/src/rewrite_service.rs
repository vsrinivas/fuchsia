// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::rewrite_manager::{CommitError, RewriteManager},
    anyhow::{anyhow, Error},
    async_lock::RwLock,
    fidl_fuchsia_pkg_rewrite::{
        EditTransactionRequest, EditTransactionRequestStream, EngineRequest, EngineRequestStream,
        RuleIteratorRequest, RuleIteratorRequestStream,
    },
    fidl_fuchsia_pkg_rewrite_ext::Rule,
    fuchsia_async as fasync,
    fuchsia_zircon::Status,
    futures::prelude::*,
    std::{convert::TryInto, sync::Arc},
    tracing::{error, info, warn},
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
                    self.serve_list(iterator).await;
                }
                EngineRequest::ListStatic { iterator, control_handle: _control_handle } => {
                    let iterator = iterator.into_stream()?;
                    self.serve_list_static(iterator).await;
                }
                EngineRequest::StartEditTransaction {
                    transaction,
                    control_handle: _control_handle,
                } => {
                    let transaction = transaction.into_stream()?;
                    self.serve_edit_transaction(transaction).await;
                }
                EngineRequest::TestApply { url, responder } => {
                    responder.send(
                        &mut self
                            .handle_test_apply(url.as_str())
                            .await
                            .map(|url| url.to_string())
                            .map_err(|e| e.into_raw()),
                    )?;
                }
            }
        }

        Ok(())
    }

    pub(self) async fn handle_test_apply(
        &self,
        url: &str,
    ) -> Result<fuchsia_url::AbsolutePackageUrl, Status> {
        let url = url.parse::<fuchsia_url::AbsolutePackageUrl>().map_err(|e| {
            error!("client provided invalid URL ({:?}): {:#}", url, anyhow!(e));
            Status::INVALID_ARGS
        })?;

        let rewritten = self.state.read().await.rewrite(&url);
        Ok(rewritten)
    }

    pub(self) async fn serve_list(&mut self, stream: RuleIteratorRequestStream) {
        let rules = self.state.read().await.list().cloned().collect();

        Self::serve_rule_iterator(rules, stream);
    }

    pub(self) async fn serve_list_static(&mut self, stream: RuleIteratorRequestStream) {
        let rules = self.state.read().await.list_static().cloned().collect();

        Self::serve_rule_iterator(rules, stream);
    }

    pub(self) async fn serve_edit_transaction(&mut self, mut stream: EditTransactionRequestStream) {
        let state = self.state.clone();
        let mut transaction = state.read().await.transaction();

        fasync::Task::spawn(
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
                            let mut response = match rule.try_into() {
                                Ok(rule) => {
                                    transaction.add(rule);
                                    Ok(())
                                }
                                Err(_) => Err(Status::INVALID_ARGS.into_raw()),
                            };
                            responder.send(&mut response)?;
                        }
                        EditTransactionRequest::Commit { responder } => {
                            let stringified = format!("{:?}", transaction);
                            let mut response = match state.write().await.apply(transaction).await {
                                Ok(()) => {
                                    info!("rewrite transaction committed: {}", stringified);
                                    Ok(())
                                }
                                Err(CommitError::TooLate) => {
                                    warn!("rewrite transaction out of date");
                                    Err(Status::UNAVAILABLE.into_raw())
                                }
                                Err(CommitError::DynamicConfigurationDisabled) => {
                                    error!(
                                        "rewrite transaction failed, dynamic configuration is \
                                         disabled"
                                    );
                                    Err(Status::ACCESS_DENIED.into_raw())
                                }
                            };
                            responder.send(&mut response)?;
                            return Ok(());
                        }
                    }
                }

                info!("rewrite transaction dropped");

                Ok(())
            }
            .unwrap_or_else(|e: Error| {
                error!("while serving rewrite rule edit transaction: {:#}", anyhow!(e))
            }),
        )
        .detach()
    }

    fn serve_rule_iterator(rules: Vec<Rule>, mut stream: RuleIteratorRequestStream) {
        let mut rules = rules.into_iter().map(|rule| rule.into()).collect::<Vec<_>>();

        fasync::Task::spawn(
            async move {
                let mut iter = rules.iter_mut().peekable();
                while let Some(request) = stream.try_next().await? {
                    let RuleIteratorRequest::Next { responder } = request;

                    match iter.peek() {
                        Some(_) => {
                            responder.send(&mut iter.by_ref().take(LIST_CHUNK_SIZE))?;
                        }
                        None => {
                            responder.send(&mut vec![].into_iter())?;
                            return Ok(());
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: fidl::Error| {
                error!("while serving rewrite rule iterator: {:#}", anyhow!(e))
            }),
        )
        .detach();
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::rewrite_manager::{
            tests::{make_rule_config, temp_path_into_proxy_and_path},
            RewriteManagerBuilder, Transaction,
        },
        fidl::endpoints::{create_proxy, create_proxy_and_stream},
        fidl_fuchsia_pkg_rewrite::{
            EditTransactionMarker, EditTransactionProxy, RuleIteratorMarker,
        },
    };

    macro_rules! rule {
        ($host_match:expr => $host_replacement:expr,
         $path_prefix_match:expr => $path_prefix_replacement:expr) => {
            fidl_fuchsia_pkg_rewrite_ext::Rule::new(
                $host_match.to_owned(),
                $host_replacement.to_owned(),
                $path_prefix_match.to_owned(),
                $path_prefix_replacement.to_owned(),
            )
            .unwrap()
        };
    }

    /// Given the future of a FIDL API call, wait for it to complete, and assert that it
    /// successfully returns the given Result<(),[`fuchsia_zircon::Status`]>.
    macro_rules! assert_yields_result {
        ($expr:expr, $result:expr) => {
            assert_eq!($expr.await.unwrap().map_err(Status::from_raw), $result);
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
        res
    }

    async fn list_rules(state: Arc<RwLock<RewriteManager>>) -> Vec<Rule> {
        let (iterator, request_stream) = create_proxy_and_stream::<RuleIteratorMarker>().unwrap();
        RewriteService::new(state).serve_list(request_stream).await;

        collect_iterator(move || iterator.next()).await
    }

    async fn list_static_rules(state: Arc<RwLock<RewriteManager>>) -> Vec<Rule> {
        let (iterator, request_stream) = create_proxy_and_stream::<RuleIteratorMarker>().unwrap();
        RewriteService::new(state).serve_list_static(request_stream).await;

        collect_iterator(move || iterator.next()).await
    }

    fn transaction_list_dynamic_rules(
        client: &EditTransactionProxy,
    ) -> impl Future<Output = Vec<Rule>> {
        let (iterator, request_stream) = create_proxy::<RuleIteratorMarker>().unwrap();
        client.list_dynamic(request_stream).unwrap();

        collect_iterator(move || iterator.next())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list() {
        let dynamic_rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/"),
        ];
        let path = make_rule_config(dynamic_rules.clone());
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let static_rules = vec![
            rule!("fuchsia.com" => "static.fuchsia.com", "/3" => "/3"),
            rule!("fuchsia.com" => "static.fuchsia.com", "/4" => "/4"),
        ];
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
                .await
                .unwrap()
                .static_rules(static_rules.clone())
                .build(),
        ));

        let mut expected = dynamic_rules;
        expected.extend(static_rules);
        assert_eq!(list_rules(state.clone()).await, expected);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_static() {
        let path = make_rule_config(vec![
            rule!("fuchsia.com" => "dynamic.fuchsia.com", "/1" => "/1"),
            rule!("fuchsia.com" => "dynamic.fuchsia.com", "/2" => "/2"),
        ]);
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let static_rules = vec![
            rule!("fuchsia.com" => "static.fuchsia.com", "/3" => "/3"),
            rule!("fuchsia.com" => "static.fuchsia.com", "/4" => "/4"),
        ];
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
                .await
                .unwrap()
                .static_rules(static_rules.clone())
                .build(),
        ));

        assert_eq!(list_static_rules(state.clone()).await, static_rules);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reset_all() {
        let rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/"),
        ];
        let path = make_rule_config(rules.clone());
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
                .await
                .unwrap()
                .build(),
        ));
        let mut service = RewriteService::new(state.clone());

        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream).await;

        client.reset_all().unwrap();
        assert_yields_result!(client.commit(), Ok(()));

        assert_eq!(state.read().await.transaction(), Transaction::new(vec![], 1));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_transaction_list_dynamic() {
        let dynamic_rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/"),
        ];
        let path = make_rule_config(dynamic_rules.clone());
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let static_rules = vec![rule!("fuchsia.com" => "static.fuchsia.com", "/" => "/")];
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
                .await
                .unwrap()
                .static_rules(static_rules)
                .build(),
        ));
        let mut service = RewriteService::new(state.clone());

        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream).await;

        // The transaction should list all dynamic rules.
        assert_eq!(transaction_list_dynamic_rules(&client).await, dynamic_rules.clone());

        // Start a list call, but don't drain it yet.
        let pending_list = transaction_list_dynamic_rules(&client);

        // Remove all dynamic rules and ensure the transaction lists no rules.
        client.reset_all().unwrap();
        assert_eq!(transaction_list_dynamic_rules(&client).await, vec![]);

        assert_yields_result!(client.commit(), Ok(()));

        // Ensure the list call from earlier lists the dynamic rules available at the time the
        // iterator was created.
        assert_eq!(pending_list.await, dynamic_rules);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_concurrent_edit() {
        let rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/"),
        ];
        let path = make_rule_config(rules.clone());
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
                .await
                .unwrap()
                .build(),
        ));
        let mut service = RewriteService::new(state.clone());

        let client1 = {
            let (client, request_stream) =
                create_proxy_and_stream::<EditTransactionMarker>().unwrap();
            service.serve_edit_transaction(request_stream).await;
            client
        };

        let client2 = {
            let (client, request_stream) =
                create_proxy_and_stream::<EditTransactionMarker>().unwrap();
            service.serve_edit_transaction(request_stream).await;
            client
        };

        client1.reset_all().unwrap();
        client2.reset_all().unwrap();

        let rule = rule!("fuchsia.com" => "fuchsia.com", "/foo" => "/foo");
        assert_yields_result!(client1.add(&mut rule.clone().into()), Ok(()));

        assert_yields_result!(client1.commit(), Ok(()));
        assert_yields_result!(client2.commit(), Err(Status::UNAVAILABLE));

        assert_eq!(state.read().await.transaction(), Transaction::new(vec![rule], 1),);

        let client2 = {
            let (client, request_stream) =
                create_proxy_and_stream::<EditTransactionMarker>().unwrap();
            service.serve_edit_transaction(request_stream).await;
            client
        };
        client2.reset_all().unwrap();
        assert_yields_result!(client2.commit(), Ok(()));
        assert_eq!(state.read().await.transaction(), Transaction::new(vec![], 2));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_concurrent_list_and_edit() {
        let path = make_rule_config(vec![]);
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
                .await
                .unwrap()
                .build(),
        ));
        let mut service = RewriteService::new(state.clone());

        assert_eq!(list_rules(state.clone()).await, vec![]);

        let edit_client = {
            let (client, request_stream) =
                create_proxy_and_stream::<EditTransactionMarker>().unwrap();
            service.serve_edit_transaction(request_stream).await;
            client
        };

        let rule = rule!("fuchsia.com" => "devhost.fuchsia.com", "/" => "/");

        assert_yields_result!(edit_client.add(&mut rule.clone().into()), Ok(()));

        assert_eq!(list_rules(state.clone()).await, vec![]);

        let long_list_call = {
            let (client, request_stream) = create_proxy_and_stream::<RuleIteratorMarker>().unwrap();
            service.serve_list(request_stream).await;
            client
        };

        assert_yields_result!(edit_client.commit(), Ok(()));

        assert_eq!(long_list_call.next().await.unwrap(), vec![]);

        assert_eq!(list_rules(state.clone()).await, vec![rule]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_rewrite() {
        let path = make_rule_config(vec![]);
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let static_rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/old/" => "/new/"),
            rule!("fuchsia.com" => "devhost", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/identity" => "/identity"),
        ];
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
                .await
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
            assert_eq!(service.handle_test_apply(url).await, Ok(rewritten.parse().unwrap()));
        }

        for url in &[
            "fuchsia-pkg://subdomain.fuchsia.com/unmatcheddomain",
            "fuchsia-pkg://devhost/unmatcheddomain",
            "fuchsia-pkg://fuchsia.com/new/unmatcheddir",
        ] {
            assert_eq!(service.handle_test_apply(url).await, Ok(url.parse().unwrap()));
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_rewrite_rejects_invalid_inputs() {
        let path = make_rule_config(vec![]);
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
                .await
                .unwrap()
                .build(),
        ));
        let service = RewriteService::new(state.clone());

        for url in &["not-fuchsia-pkg://fuchsia.com/test", "fuchsia-pkg://fuchsia.com/a*"] {
            assert_eq!(service.handle_test_apply(url).await, Err(Status::INVALID_ARGS));
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_concurrent_rewrite_and_edit() {
        let path = make_rule_config(vec![rule!("fuchsia.com" => "fuchsia.com", "/a" => "/b")]);
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
                .await
                .unwrap()
                .build(),
        ));
        let mut service = RewriteService::new(state.clone());

        let (edit_client, request_stream) =
            create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream).await;

        let replacement_rule = rule!("fuchsia.com" => "fuchsia.com", "/a" => "/c");

        edit_client.reset_all().unwrap();
        assert_yields_result!(edit_client.add(&mut replacement_rule.into()), Ok(()));

        // Pending transaction does not affect apply call.
        assert_eq!(
            service.handle_test_apply("fuchsia-pkg://fuchsia.com/a").await,
            Ok("fuchsia-pkg://fuchsia.com/b".parse().unwrap())
        );

        assert_yields_result!(edit_client.commit(), Ok(()));

        // New rule set now applies.
        assert_eq!(
            service.handle_test_apply("fuchsia-pkg://fuchsia.com/a").await,
            Ok("fuchsia-pkg://fuchsia.com/c".parse().unwrap())
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_enables_amber_source() {
        let path = make_rule_config(vec![]);
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
                .await
                .unwrap()
                .build(),
        ));
        let mut service = RewriteService::new(state.clone());

        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream).await;

        let rules = vec![
            rule!("example.com" => "wrong-host.fuchsia.com", "/" => "/"),
            rule!("fuchsia.com" => "correct.fuchsia.com", "/" => "/"),
            rule!("fuchsia.com" => "wrong-priority.fuchsia.com", "/" => "/"),
            rule!("fuchsia.com" => "wrong-match.fuchsia.com", "/foo/" => "/"),
            rule!("fuchsia.com" => "wrong-replacement.fuchsia.com", "/" => "/bar/"),
        ];
        for rule in rules.into_iter().rev() {
            assert_yields_result!(client.add(&mut rule.into()), Ok(()));
        }
        assert_yields_result!(client.commit(), Ok(()));

        // Adding a duplicate of the currently enabled source is a no-op.
        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream).await;
        let rule = rule!("fuchsia.com" => "correct.fuchsia.com", "/" => "/");
        assert_yields_result!(client.add(&mut rule.into()), Ok(()));
        assert_yields_result!(client.commit(), Ok(()));

        // Adding a different entry with higher priority enables the new source.
        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream).await;
        let rule = rule!("fuchsia.com" => "correcter.fuchsia.com", "/" => "/");
        assert_yields_result!(client.add(&mut rule.into()), Ok(()));
        assert_yields_result!(client.commit(), Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_disables_amber_sources() {
        let rules = vec![rule!("fuchsia.com" => "enabled.fuchsia.com", "/" => "/")];

        let path = make_rule_config(rules);
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
                .await
                .unwrap()
                .build(),
        ));
        let mut service = RewriteService::new(state.clone());

        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream).await;
        client.reset_all().unwrap();
        assert_yields_result!(client.commit(), Ok(()));

        // Edits that don't change the enabled source are a no-op.
        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream).await;
        client.reset_all().unwrap();
        assert_yields_result!(client.commit(), Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_transaction_commit_fails_if_no_dynamic_rules_path() {
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(None, Option::<&str>::None).await.unwrap().build(),
        ));
        let mut service = RewriteService::new(state);

        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream).await;

        let status = Status::from_raw(client.commit().await.unwrap().unwrap_err());

        assert_eq!(status, Status::ACCESS_DENIED);
    }
}
