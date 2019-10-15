// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests pkg_resolver's RewriteManager when
/// dynamic rewrite rules have been disabled.
use {
    crate::{DirOrProxy, Mounts, TestEnv},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_io::{
        DirectoryObject, DirectoryProxy, DirectoryRequest, DirectoryRequestStream,
        OPEN_FLAG_DESCRIBE,
    },
    fidl_fuchsia_pkg_rewrite::EngineProxy as RewriteEngineProxy,
    fuchsia_async as fasync,
    fuchsia_url_rewrite::{Rule, RuleConfig},
    fuchsia_zircon::Status,
    futures::{
        future::{BoxFuture, FutureExt},
        stream::StreamExt,
    },
    parking_lot::Mutex,
    serde_derive::Serialize,
    std::{collections::HashMap, convert::TryInto, fs::File, io::BufWriter, sync::Arc},
};

#[derive(Serialize)]
struct Config {
    disable_dynamic_configuration: bool,
}

impl Mounts {
    fn add_dynamic_rewrite_rules(&self, rule_config: &RuleConfig) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_data {
            let f = File::create(d.path().join("rewrites.json")).unwrap();
            serde_json::to_writer(BufWriter::new(f), rule_config).unwrap();
        } else {
            panic!("not supported");
        }
    }
    fn add_config(&self, config: &Config) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_config_data {
            let f = File::create(d.path().join("config.json")).unwrap();
            serde_json::to_writer(BufWriter::new(f), &config).unwrap();
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

async fn get_rules(rewrite_engine: &RewriteEngineProxy) -> Vec<Rule> {
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
    let env = TestEnv::new_with_mounts(mounts);

    assert_eq!(get_rules(&env.proxies.rewrite_engine).await, vec![rule]);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn no_load_dynamic_rules_if_disabled() {
    let mounts = Mounts::new();
    mounts.add_dynamic_rewrite_rules(&make_rule_config(&make_rule()));
    mounts.add_config(&Config { disable_dynamic_configuration: true });
    let env = TestEnv::new_with_mounts(mounts);

    assert_eq!(get_rules(&env.proxies.rewrite_engine).await, vec![]);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn commit_transaction_succeeds() {
    let env = TestEnv::new();

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
    let env = TestEnv::new_with_mounts(mounts);

    let (edit_transaction, edit_transaction_server) = fidl::endpoints::create_proxy().unwrap();
    env.proxies.rewrite_engine.start_edit_transaction(edit_transaction_server).unwrap();
    Status::ok(edit_transaction.add(&mut make_rule().into()).await.unwrap()).unwrap();

    assert_eq!(Status::from_raw(edit_transaction.commit().await.unwrap()), Status::ACCESS_DENIED);
    assert_eq!(get_rules(&env.proxies.rewrite_engine).await, vec![]);

    env.stop().await;
}

type OpenCounter = Arc<Mutex<HashMap<String, u64>>>;

fn handle_directory_request_stream(
    mut stream: DirectoryRequestStream,
    open_counts: OpenCounter,
) -> BoxFuture<'static, ()> {
    async move {
        while let Some(req) = stream.next().await {
            handle_directory_request(req.unwrap(), Arc::clone(&open_counts)).await;
        }
    }
        .boxed()
}

async fn handle_directory_request(req: DirectoryRequest, open_counts: OpenCounter) {
    match req {
        DirectoryRequest::Clone { flags, object, control_handle: _control_handle } => {
            let stream = DirectoryRequestStream::from_channel(
                fasync::Channel::from_channel(object.into_channel()).unwrap(),
            );
            describe_dir(flags, &stream);
            fasync::spawn(handle_directory_request_stream(stream, Arc::clone(&open_counts)));
        }
        DirectoryRequest::Open {
            flags: _flags,
            mode: _mode,
            path,
            object: _object,
            control_handle: _control_handle,
        } => {
            *open_counts.lock().entry(path).or_insert(0) += 1;
        }
        other => panic!("unhandled request type: {:?}", other),
    }
}

fn describe_dir(flags: u32, stream: &DirectoryRequestStream) {
    let ch = stream.control_handle();
    if flags & OPEN_FLAG_DESCRIBE != 0 {
        let mut ni = fidl_fuchsia_io::NodeInfo::Directory(DirectoryObject);
        ch.send_on_open_(Status::OK.into_raw(), Some(fidl::encoding::OutOfLine(&mut ni)))
            .expect("send_on_open");
    }
}

fn spawn_directory_handler() -> (DirectoryProxy, OpenCounter) {
    let (proxy, stream) =
        fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_io::DirectoryMarker>().unwrap();
    let open_counts = Arc::new(Mutex::new(HashMap::<String, u64>::new()));
    fasync::spawn(handle_directory_request_stream(stream, Arc::clone(&open_counts)));
    (proxy, open_counts)
}

#[fasync::run_singlethreaded(test)]
async fn attempt_to_open_persisted_dynamic_rules() {
    let (proxy, open_counts) = spawn_directory_handler();
    let mounts = Mounts {
        pkg_resolver_data: DirOrProxy::Proxy(proxy),
        pkg_resolver_config_data: DirOrProxy::Dir(tempfile::tempdir().expect("/tmp to exist")),
    };
    let env = TestEnv::new_with_mounts(mounts);

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
    let env = TestEnv::new_with_mounts(mounts);

    // Waits for pkg_resolver to be initialized
    get_rules(&env.proxies.rewrite_engine).await;

    assert_eq!(open_counts.lock().get("rewrites.json"), None);

    env.stop().await;
}
