// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests pkg_resolver's RewriteManager when
/// dynamic rewrite rules have been disabled.
use {
    super::*,
    crate::mock_filesystem::spawn_directory_handler,
    fidl_fuchsia_pkg_rewrite::EngineProxy as RewriteEngineProxy,
    fidl_fuchsia_pkg_rewrite_ext::{Rule, RuleConfig},
    fuchsia_async as fasync,
    fuchsia_zircon::Status,
    std::{convert::TryInto, fs::File},
};

impl Mounts {
    fn add_dynamic_rewrite_rules(&self, rule_config: &RuleConfig) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_data {
            let f = File::create(d.path().join("rewrites.json")).unwrap();
            serde_json::to_writer(BufWriter::new(f), rule_config).unwrap();
        } else {
            panic!("not supported");
        }
    }
}

fn make_rule_config(rule: &Rule) -> RuleConfig {
    RuleConfig::Version1(vec![rule.clone()])
}

fn make_rule() -> Rule {
    Rule::new("example.com", "example.com", "/", "/").unwrap()
}

pub async fn get_rules(rewrite_engine: &RewriteEngineProxy) -> Vec<Rule> {
    let (rule_iterator, rule_iterator_server) =
        fidl::endpoints::create_proxy().expect("create rule iterator proxy");
    rewrite_engine.list(rule_iterator_server).expect("list rules");
    let mut ret = vec![];
    loop {
        let rules = rule_iterator.next().await.expect("advance rule iterator");
        if rules.is_empty() {
            return ret;
        }
        ret.extend(rules.into_iter().map(|r| r.try_into().unwrap()))
    }
}

#[fasync::run_singlethreaded(test)]
async fn load_dynamic_rules() {
    let mounts = Mounts::new();
    let rule = make_rule();
    mounts.add_dynamic_rewrite_rules(&make_rule_config(&rule));
    mounts.add_config(&Config { disable_dynamic_configuration: false });
    let env = TestEnvBuilder::new().mounts(mounts).build();

    assert_eq!(get_rules(&env.proxies.rewrite_engine).await, vec![rule]);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn no_load_dynamic_rules_if_disabled() {
    let mounts = Mounts::new();
    mounts.add_dynamic_rewrite_rules(&make_rule_config(&make_rule()));
    mounts.add_config(&Config { disable_dynamic_configuration: true });
    let env = TestEnvBuilder::new().mounts(mounts).build();

    assert_eq!(get_rules(&env.proxies.rewrite_engine).await, vec![]);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn commit_transaction_succeeds() {
    let env = TestEnvBuilder::new().build();

    let (edit_transaction, edit_transaction_server) = fidl::endpoints::create_proxy().unwrap();
    env.proxies.rewrite_engine.start_edit_transaction(edit_transaction_server).unwrap();
    let rule = make_rule();
    Status::ok(edit_transaction.add(&mut rule.clone().into()).await.unwrap()).unwrap();

    assert_eq!(Status::from_raw(edit_transaction.commit().await.unwrap()), Status::OK);
    assert_eq!(get_rules(&env.proxies.rewrite_engine).await, vec![rule]);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn commit_transaction_fails_if_disabled() {
    let mounts = Mounts::new();
    mounts.add_config(&Config { disable_dynamic_configuration: true });
    let env = TestEnvBuilder::new().mounts(mounts).build();

    let (edit_transaction, edit_transaction_server) = fidl::endpoints::create_proxy().unwrap();
    env.proxies.rewrite_engine.start_edit_transaction(edit_transaction_server).unwrap();
    Status::ok(edit_transaction.add(&mut make_rule().into()).await.unwrap()).unwrap();

    assert_eq!(Status::from_raw(edit_transaction.commit().await.unwrap()), Status::ACCESS_DENIED);
    assert_eq!(get_rules(&env.proxies.rewrite_engine).await, vec![]);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn attempt_to_open_persisted_dynamic_rules() {
    let (proxy, open_counts) = spawn_directory_handler();
    let mounts = Mounts {
        pkg_resolver_data: DirOrProxy::Proxy(proxy),
        pkg_resolver_config_data: DirOrProxy::Dir(tempfile::tempdir().expect("/tmp to exist")),
    };
    let env = TestEnvBuilder::new().mounts(mounts).build();

    // Waits for pkg_resolver to be initialized
    get_rules(&env.proxies.rewrite_engine).await;

    assert_eq!(open_counts.lock().get("rewrites.json"), Some(&1));

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn no_attempt_to_open_persisted_dynamic_rules_if_disabled() {
    let (proxy, open_counts) = spawn_directory_handler();
    let mounts = Mounts {
        pkg_resolver_data: DirOrProxy::Proxy(proxy),
        pkg_resolver_config_data: DirOrProxy::Dir(tempfile::tempdir().expect("/tmp to exist")),
    };
    mounts.add_config(&Config { disable_dynamic_configuration: true });
    let env = TestEnvBuilder::new().mounts(mounts).build();

    // Waits for pkg_resolver to be initialized
    get_rules(&env.proxies.rewrite_engine).await;

    assert_eq!(open_counts.lock().get("rewrites.json"), None);

    env.stop().await;
}
