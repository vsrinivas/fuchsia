// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        amber::AmberSourceSelector,
        rewrite_manager::{CommitError, RewriteManager},
    },
    failure::Error,
    fidl_fuchsia_pkg_rewrite::{
        EditTransactionRequest, EditTransactionRequestStream, EngineRequest, EngineRequestStream,
        RuleIteratorRequest, RuleIteratorRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    fuchsia_url_rewrite::Rule,
    fuchsia_zircon::Status,
    futures::prelude::*,
    parking_lot::RwLock,
    std::{convert::TryInto, sync::Arc},
};

const LIST_CHUNK_SIZE: usize = 100;

#[derive(Debug, Clone)]
pub struct RewriteService<A>
where
    A: AmberSourceSelector,
{
    state: Arc<RwLock<RewriteManager>>,
    amber: A,
}

impl<A> RewriteService<A>
where
    A: AmberSourceSelector + 'static,
{
    pub fn new(state: Arc<RwLock<RewriteManager>>, amber: A) -> Self {
        RewriteService { state, amber }
    }

    pub async fn handle_client(&mut self, mut stream: EngineRequestStream) -> Result<(), Error> {
        while let Some(request) = await!(stream.try_next())? {
            match request {
                EngineRequest::List { iterator, control_handle: _control_handle } => {
                    let iterator = iterator.into_stream()?;
                    self.serve_list(iterator);
                }
                EngineRequest::StartEditTransaction {
                    transaction,
                    control_handle: _control_handle,
                } => {
                    let transaction = transaction.into_stream()?;
                    self.serve_edit_transaction(transaction);
                }
            }
        }

        Ok(())
    }

    pub(self) fn serve_list(&mut self, stream: RuleIteratorRequestStream) {
        let rules = self.state.read().list().cloned().collect();

        Self::serve_rule_iterator(rules, stream);
    }

    pub(self) fn serve_edit_transaction(&mut self, mut stream: EditTransactionRequestStream) {
        let state = self.state.clone();
        let amber = self.amber.clone();
        let mut transaction = state.read().transaction();

        fasync::spawn(
            async move {
                while let Some(request) = await!(stream.try_next())? {
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
                            let (status, work) = {
                                let mut state = state.write();
                                let old_source_name = state.amber_source_name();
                                match state.apply(transaction) {
                                    Ok(()) => {
                                        // Before reporting success, make a best effort to push the
                                        // rewrite rule configs down to the Amber service.
                                        let work =
                                            match (old_source_name, state.amber_source_name()) {
                                                (None, None) => None,
                                                (Some(_), None) => {
                                                    Some(amber.disable_all_sources())
                                                }
                                                (None, Some(after)) => {
                                                    Some(amber.enable_source_exclusive(&after))
                                                }
                                                (Some(before), Some(after)) => {
                                                    if before != after {
                                                        Some(amber.enable_source_exclusive(&after))
                                                    } else {
                                                        None
                                                    }
                                                }
                                            };
                                        (Status::OK, work)
                                    }
                                    Err(CommitError::TooLate) => (Status::UNAVAILABLE, None),
                                }
                            };
                            if let Some(work) = work {
                                await!(work);
                            }
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
                while let Some(request) = await!(stream.try_next())? {
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
        futures::future::BoxFuture,
        parking_lot::Mutex,
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
            assert_eq!(Status::from_raw(await!($expr).unwrap()), $status);
        };
    }

    #[derive(Debug, Clone, Default)]
    struct FakeAmberSourceSelector {
        events: Arc<Mutex<Vec<AmberSourceEvent>>>,
    }

    #[derive(Debug, Clone, PartialEq, Eq)]
    enum AmberSourceEvent {
        DisableAllSources,
        EnableSource(String),
    }

    impl FakeAmberSourceSelector {
        fn take_events(&mut self) -> Vec<AmberSourceEvent> {
            std::mem::replace(&mut self.events.lock(), vec![])
        }
    }

    impl AmberSourceSelector for FakeAmberSourceSelector {
        fn disable_all_sources(&self) -> BoxFuture<'_, ()> {
            self.events.lock().push(AmberSourceEvent::DisableAllSources);
            future::ready(()).boxed()
        }
        fn enable_source_exclusive(&self, id: &str) -> BoxFuture<'_, ()> {
            self.events.lock().push(AmberSourceEvent::EnableSource(id.to_owned()));
            future::ready(()).boxed()
        }
    }

    #[derive(Debug, Clone, Default)]
    struct UnreachableAmberSourceSelector;

    impl AmberSourceSelector for UnreachableAmberSourceSelector {
        fn disable_all_sources(&self) -> BoxFuture<'_, ()> {
            unreachable!();
        }
        fn enable_source_exclusive(&self, _id: &str) -> BoxFuture<'_, ()> {
            unreachable!();
        }
    }

    async fn collect_iterator<F, I>(mut next: impl FnMut() -> F) -> Vec<I>
    where
        F: Future<Output = Result<Vec<I>, fidl::Error>>,
    {
        let mut res = Vec::new();
        loop {
            let more = await!(next()).unwrap();
            if more.is_empty() {
                break;
            }
            res.extend(more);
        }
        assert!(await!(next()).unwrap().is_empty());
        res
    }

    async fn verify_list_call(
        state: Arc<RwLock<RewriteManager>>,
        expected: Vec<fidl_fuchsia_pkg_rewrite::Rule>,
    ) {
        let (iterator, request_stream) = create_proxy_and_stream::<RuleIteratorMarker>().unwrap();
        RewriteService::new(state, UnreachableAmberSourceSelector).serve_list(request_stream);

        let rules = await!(collect_iterator(|| iterator.next()));
        assert_eq!(rules, expected);
    }

    fn transaction_list_dynamic_rules(
        client: &EditTransactionProxy,
    ) -> impl Future<Output = Vec<Rule>> {
        let (iterator, request_stream) = create_proxy::<RuleIteratorMarker>().unwrap();
        client.list_dynamic(request_stream).unwrap();

        async move {
            await!(collect_iterator(|| iterator.next()))
                .into_iter()
                .map(|rule| rule.try_into().unwrap())
                .collect::<Vec<Rule>>()
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn test_list() {
        let inspector = fuchsia_inspect::Inspector::new();
        let node = inspector.root().create_child("rewrite-manager");
        let rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/"),
        ];
        let dynamic_config = make_rule_config(rules.clone());
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(node, &dynamic_config).unwrap().build(),
        ));

        let expected = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice").into(),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/").into(),
        ];
        await!(verify_list_call(state.clone(), expected));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_reset_all() {
        let inspector = fuchsia_inspect::Inspector::new();
        let node = inspector.root().create_child("rewrite-manager");
        let rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/"),
        ];
        let dynamic_config = make_rule_config(rules.clone());
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(node, &dynamic_config).unwrap().build(),
        ));
        let mut service = RewriteService::new(state.clone(), UnreachableAmberSourceSelector);

        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream);

        client.reset_all().unwrap();
        assert_yields_status!(client.commit(), Status::OK);

        assert_eq!(state.read().transaction(), Transaction::new(vec![], 1));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_transaction_list_dynamic() {
        let inspector = fuchsia_inspect::Inspector::new();
        let node = inspector.root().create_child("rewrite-manager");
        let dynamic_rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/"),
        ];
        let dynamic_config = make_rule_config(dynamic_rules.clone());
        let static_rules = vec![rule!("fuchsia.com" => "static.fuchsia.com", "/" => "/")];
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(node, &dynamic_config)
                .unwrap()
                .static_rules(static_rules)
                .build(),
        ));
        let mut service = RewriteService::new(state.clone(), UnreachableAmberSourceSelector);

        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream);

        // The transaction should list all dynamic rules.
        assert_eq!(await!(transaction_list_dynamic_rules(&client)), dynamic_rules.clone());

        // Start a list call, but don't drain it yet.
        let pending_list = transaction_list_dynamic_rules(&client);

        // Remove all dynamic rules and ensure the transaction lists no rules.
        client.reset_all().unwrap();
        assert_eq!(await!(transaction_list_dynamic_rules(&client)), vec![]);

        assert_yields_status!(client.commit(), Status::OK);

        // Ensure the list call from earlier lists the dynamic rules available at the time the
        // iterator was created.
        assert_eq!(await!(pending_list), dynamic_rules);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_concurrent_edit() {
        let inspector = fuchsia_inspect::Inspector::new();
        let node = inspector.root().create_child("rewrite-manager");
        let rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/"),
        ];
        let dynamic_config = make_rule_config(rules.clone());
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(node, &dynamic_config).unwrap().build(),
        ));
        let amber = FakeAmberSourceSelector::default();
        let mut service = RewriteService::new(state.clone(), amber);

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
        let inspector = fuchsia_inspect::Inspector::new();
        let node = inspector.root().create_child("rewrite-manager");
        let dynamic_config = make_rule_config(vec![]);
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(node, &dynamic_config).unwrap().build(),
        ));
        let mut amber = FakeAmberSourceSelector::default();
        let mut service = RewriteService::new(state.clone(), amber.clone());

        await!(verify_list_call(state.clone(), vec![]));

        let edit_client = {
            let (client, request_stream) =
                create_proxy_and_stream::<EditTransactionMarker>().unwrap();
            service.serve_edit_transaction(request_stream);
            client
        };

        let rule = rule!("fuchsia.com" => "devhost.fuchsia.com", "/" => "/");

        assert_yields_status!(edit_client.add(&mut rule.clone().into()), Status::OK);

        await!(verify_list_call(state.clone(), vec![]));

        let long_list_call = {
            let (client, request_stream) = create_proxy_and_stream::<RuleIteratorMarker>().unwrap();
            service.serve_list(request_stream);
            client
        };

        assert_yields_status!(edit_client.commit(), Status::OK);

        assert_eq!(await!(long_list_call.next()).unwrap(), vec![]);

        await!(verify_list_call(state.clone(), vec![rule.into()]));
        assert_eq!(
            amber.take_events(),
            vec![AmberSourceEvent::EnableSource("devhost.fuchsia.com".to_owned())]
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn test_enables_amber_source() {
        let inspector = fuchsia_inspect::Inspector::new();
        let node = inspector.root().create_child("rewrite-manager");
        let dynamic_config = make_rule_config(vec![]);
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(node, &dynamic_config).unwrap().build(),
        ));
        let mut amber = FakeAmberSourceSelector::default();
        let mut service = RewriteService::new(state.clone(), amber.clone());

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

        assert_eq!(
            amber.take_events(),
            vec![AmberSourceEvent::EnableSource("correct.fuchsia.com".to_owned())]
        );

        // Adding a duplicate of the currently enabled source is a no-op.
        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream);
        let rule = rule!("fuchsia.com" => "correct.fuchsia.com", "/" => "/");
        assert_yields_status!(client.add(&mut rule.into()), Status::OK);
        assert_yields_status!(client.commit(), Status::OK);
        assert_eq!(amber.take_events(), vec![]);

        // Adding a different entry with higher priority enables the new source.
        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream);
        let rule = rule!("fuchsia.com" => "correcter.fuchsia.com", "/" => "/");
        assert_yields_status!(client.add(&mut rule.into()), Status::OK);
        assert_yields_status!(client.commit(), Status::OK);
        assert_eq!(
            amber.take_events(),
            vec![AmberSourceEvent::EnableSource("correcter.fuchsia.com".to_owned())]
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn test_disables_amber_sources() {
        let rules = vec![rule!("fuchsia.com" => "enabled.fuchsia.com", "/" => "/")];

        let inspector = fuchsia_inspect::Inspector::new();
        let node = inspector.root().create_child("rewrite-manager");
        let dynamic_config = make_rule_config(rules);
        let state = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(node, &dynamic_config).unwrap().build(),
        ));
        let mut amber = FakeAmberSourceSelector::default();
        let mut service = RewriteService::new(state.clone(), amber.clone());

        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream);
        client.reset_all().unwrap();
        assert_yields_status!(client.commit(), Status::OK);
        assert_eq!(amber.take_events(), vec![AmberSourceEvent::DisableAllSources]);

        // Edits that don't change the enabled source are a no-op.
        let (client, request_stream) = create_proxy_and_stream::<EditTransactionMarker>().unwrap();
        service.serve_edit_transaction(request_stream);
        client.reset_all().unwrap();
        assert_yields_status!(client.commit(), Status::OK);
        assert_eq!(amber.take_events(), vec![]);
    }
}
