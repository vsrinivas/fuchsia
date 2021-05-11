// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

/// This module tests pkg_resolver's RewriteManager when
/// dynamic rewrite rules have been disabled.
use {
    fidl_fuchsia_pkg_rewrite_ext::{Rule, RuleConfig},
    fuchsia_async as fasync,
    fuchsia_zircon::Status,
    lib::{get_rules, mock_filesystem, Config, DirOrProxy, MountsBuilder, TestEnvBuilder},
};

fn make_rule_config(rule: &Rule) -> RuleConfig {
    RuleConfig::Version1(vec![rule.clone()])
}

fn make_rule() -> Rule {
    Rule::new("example.com", "example.com", "/", "/").unwrap()
}

#[fasync::run_singlethreaded(test)]
async fn load_dynamic_rules() {
    let rule = make_rule();
    let env = TestEnvBuilder::new()
        .mounts(
            MountsBuilder::new()
                .dynamic_rewrite_rules(make_rule_config(&rule))
                .config(Config { enable_dynamic_configuration: true })
                .build(),
        )
        .build()
        .await;

    assert_eq!(get_rules(&env.proxies.rewrite_engine).await, vec![rule]);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn no_load_dynamic_rules_if_disabled() {
    let env = TestEnvBuilder::new()
        .mounts(
            MountsBuilder::new()
                .dynamic_rewrite_rules(make_rule_config(&make_rule()))
                .config(Config { enable_dynamic_configuration: false })
                .build(),
        )
        .build()
        .await;

    assert_eq!(get_rules(&env.proxies.rewrite_engine).await, vec![]);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn commit_transaction_succeeds() {
    let env = TestEnvBuilder::new().build().await;

    let (edit_transaction, edit_transaction_server) = fidl::endpoints::create_proxy().unwrap();
    env.proxies.rewrite_engine.start_edit_transaction(edit_transaction_server).unwrap();
    let rule = make_rule();
    let () = edit_transaction.add(&mut rule.clone().into()).await.unwrap().unwrap();

    edit_transaction.commit().await.unwrap().unwrap();
    assert_eq!(get_rules(&env.proxies.rewrite_engine).await, vec![rule]);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn commit_transaction_fails_if_disabled() {
    let env = TestEnvBuilder::new()
        .mounts(MountsBuilder::new().config(Config { enable_dynamic_configuration: false }).build())
        .build()
        .await;

    let (edit_transaction, edit_transaction_server) = fidl::endpoints::create_proxy().unwrap();
    env.proxies.rewrite_engine.start_edit_transaction(edit_transaction_server).unwrap();
    let () = edit_transaction.add(&mut make_rule().into()).await.unwrap().unwrap();

    assert_eq!(
        Status::from_raw(edit_transaction.commit().await.unwrap().unwrap_err()),
        Status::ACCESS_DENIED
    );
    assert_eq!(get_rules(&env.proxies.rewrite_engine).await, vec![]);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn attempt_to_open_persisted_dynamic_rules() {
    let (proxy, open_counts) = mock_filesystem::spawn_directory_handler();
    let env = TestEnvBuilder::new()
        .mounts(
            MountsBuilder::default()
                .pkg_resolver_data(DirOrProxy::Proxy(proxy))
                .config(Config { enable_dynamic_configuration: true })
                .build(),
        )
        .build()
        .await;

    // Waits for pkg_resolver to be initialized
    get_rules(&env.proxies.rewrite_engine).await;

    assert_eq!(open_counts.lock().get("rewrites.json"), Some(&1));

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn no_attempt_to_open_persisted_dynamic_rules_if_disabled() {
    let (proxy, open_counts) = mock_filesystem::spawn_directory_handler();
    let env = TestEnvBuilder::new()
        .mounts(
            MountsBuilder::new()
                .pkg_resolver_data(DirOrProxy::Proxy(proxy))
                .config(Config { enable_dynamic_configuration: false })
                .build(),
        )
        .build()
        .await;

    // Waits for pkg_resolver to be initialized
    get_rules(&env.proxies.rewrite_engine).await;

    assert_eq!(open_counts.lock().get("rewrites.json"), None);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn dynamic_rewrites_disabled_if_missing_config() {
    let env = TestEnvBuilder::new().mounts(MountsBuilder::new().build()).build().await;

    let (edit_transaction, edit_transaction_server) = fidl::endpoints::create_proxy().unwrap();
    env.proxies.rewrite_engine.start_edit_transaction(edit_transaction_server).unwrap();
    let () = edit_transaction.add(&mut make_rule().into()).await.unwrap().unwrap();

    assert_eq!(
        Status::from_raw(edit_transaction.commit().await.unwrap().unwrap_err()),
        Status::ACCESS_DENIED
    );
    assert_eq!(get_rules(&env.proxies.rewrite_engine).await, vec![]);

    env.stop().await;
}
