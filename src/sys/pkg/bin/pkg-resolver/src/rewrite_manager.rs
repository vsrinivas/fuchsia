// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_pkg_rewrite_ext::{Rule, RuleConfig},
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_url::AbsolutePackageUrl,
    std::collections::VecDeque,
    thiserror::Error,
    tracing::error,
};

/// [RewriteManager] controls access to all static and dynamic rewrite rules used by the package
/// resolver.
///
/// No two instances of [RewriteManager] should be configured to use the same `dynamic_rules_path`,
/// or concurrent saves could corrupt the config file or lose edits. Instead, use the provided
/// [RewriteManager::transaction] API to safely manage concurrent edits.
#[derive(Debug)]
pub struct RewriteManager {
    enable_dynamic_configuration: bool,
    static_rules: Vec<Rule>,
    dynamic_rules: Vec<Rule>,
    generation: u32,
    data_proxy: Option<fio::DirectoryProxy>,
    dynamic_rules_path: Option<String>,
    inspect: RewriteManagerInspectState,
}

#[derive(Debug)]
struct RewriteManagerInspectState {
    static_rules_node: inspect::Node,
    static_rules_states: Vec<RuleInspectState>,
    dynamic_rules_node: inspect::Node,
    dynamic_rules_states: Vec<RuleInspectState>,
    generation_property: fuchsia_inspect::UintProperty,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    dynamic_rules_path_property: fuchsia_inspect::StringProperty,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    node: inspect::Node,
}

#[derive(Debug, Error)]
pub enum CommitError {
    #[error("the provided rule set is based on an older generation")]
    TooLate,
    #[error("editing rewrite rules is permanently disabled")]
    DynamicConfigurationDisabled,
}

impl RewriteManager {
    /// Rewrite the given [AbsolutePackageUrl] using the first dynamic or static rewrite rule that
    /// matches and produces a valid [AbsolutePackageUrl]. If no rewrite rules match or all that do
    /// produce invalid [AbsolutePackageUrl]s, return the original, unmodified [AbsolutePackageUrl].
    pub fn rewrite(&self, url: &AbsolutePackageUrl) -> AbsolutePackageUrl {
        for rule in self.list() {
            match rule.apply(&url) {
                Some(Ok(res)) => {
                    return res;
                }
                Some(Err(err)) => {
                    error!(
                        "ignoring rewrite rule {:?} that produced an invalid URL: {:#}",
                        rule,
                        anyhow!(err)
                    );
                }
                _ => {}
            }
        }
        url.clone()
    }

    async fn save(
        dynamic_rules: &mut Vec<Rule>,
        data_proxy: &fio::DirectoryProxy,
        dynamic_rules_path: &str,
    ) -> Result<(), anyhow::Error> {
        let config = RuleConfig::Version1(std::mem::replace(dynamic_rules, vec![]));

        let result = async {
            // TODO(fxbug.dev/83342): We need to reopen because `resolve_succeeds_with_broken_minfs`
            // expects it, this should be removed once the test is fixed.
            let data_proxy = fuchsia_fs::directory::open_directory(
                &data_proxy,
                ".",
                fio::OpenFlags::RIGHT_WRITABLE,
            )
            .await
            .context("opening data-proxy directory")?;

            let temp_filename = &format!("{dynamic_rules_path}.new");

            let data = serde_json::to_vec(&config).context("encoding config")?;

            crate::util::do_with_atomic_file(
                &data_proxy,
                temp_filename,
                &dynamic_rules_path,
                |proxy| async move {
                    fuchsia_fs::file::write(&proxy, &data)
                        .await
                        .with_context(|| format!("writing file: {}", temp_filename))
                },
            )
            .await
        }
        .await;

        let RuleConfig::Version1(rules) = config;
        *dynamic_rules = rules;

        result
    }

    /// Construct a new [Transaction] containing the dynamic config rules from this
    /// [RewriteManager].
    pub fn transaction(&self) -> Transaction {
        Transaction {
            dynamic_rules: self.dynamic_rules.clone().into(),
            generation: self.generation,
        }
    }

    /// Apply the given [Transaction] object to this [RewriteManager] if and only if:
    /// * dynamic configuration is enabled, and
    /// * no other [RewriteRuleStates] have been applied since `transaction` was
    ///   cloned from this [RewriteManager].
    pub async fn apply(&mut self, transaction: Transaction) -> Result<(), CommitError> {
        if !self.enable_dynamic_configuration {
            return Err(CommitError::DynamicConfigurationDisabled);
        }
        if self.generation != transaction.generation {
            Err(CommitError::TooLate)
        } else {
            self.dynamic_rules = transaction.dynamic_rules.into();
            self.generation += 1;
            if let (Some(ref data_proxy), Some(ref dynamic_rules_path)) =
                (self.data_proxy.as_ref(), self.dynamic_rules_path.as_ref())
            {
                if let Err(err) =
                    Self::save(&mut self.dynamic_rules, data_proxy, dynamic_rules_path).await
                {
                    error!("error while saving dynamic rewrite rules: {:#}", anyhow!(err));
                }
            }
            self.update_inspect_objects();
            Ok(())
        }
    }

    /// Return an iterator through all rewrite rules in the order they should be applied to
    /// incoming `fuchsia-pkg://` URLs.
    pub fn list(&self) -> impl Iterator<Item = &Rule> {
        self.dynamic_rules.iter().chain(self.list_static())
    }

    /// Return an iterator through all static rewrite rules in the order they should be applied to
    /// incoming `fuchsia-pkg://` URLs, after all dynamic rules have been considered.
    pub fn list_static(&self) -> impl Iterator<Item = &Rule> {
        self.static_rules.iter()
    }

    fn update_rule_inspect_states(
        node: &inspect::Node,
        rules: &[Rule],
        states: &mut Vec<RuleInspectState>,
    ) {
        states.clear();
        for (i, rule) in rules.iter().enumerate() {
            let rule_node = node.create_child(&i.to_string());
            states.push(create_rule_inspect_state(rule, rule_node));
        }
    }

    fn update_inspect_objects(&mut self) {
        self.inspect.generation_property.set(self.generation.into());
        RewriteManager::update_rule_inspect_states(
            &self.inspect.dynamic_rules_node,
            &self.dynamic_rules,
            &mut self.inspect.dynamic_rules_states,
        );
    }
}

#[allow(missing_docs)]
#[derive(Debug, PartialEq, Eq)]
pub struct RuleInspectState {
    _host_match_property: inspect::StringProperty,
    _host_replacement_property: inspect::StringProperty,
    _path_prefix_match_property: inspect::StringProperty,
    _path_prefix_replacement_property: inspect::StringProperty,
    _node: inspect::Node,
}

fn create_rule_inspect_state(rule: &Rule, node: inspect::Node) -> RuleInspectState {
    RuleInspectState {
        _host_match_property: node.create_string("host_match", &rule.host_match()),
        _host_replacement_property: node
            .create_string("host_replacement", &rule.host_replacement()),
        _path_prefix_match_property: node
            .create_string("path_prefix_match", &rule.path_prefix_match()),
        _path_prefix_replacement_property: node
            .create_string("path_prefix_replacement", &rule.path_prefix_replacement()),
        _node: node,
    }
}

/// [Transaction] tracks an edit transaction to a set of dynamic rewrite rules.
#[derive(Debug, PartialEq, Eq)]
pub struct Transaction {
    dynamic_rules: VecDeque<Rule>,
    generation: u32,
}

impl Transaction {
    #[cfg(test)]
    pub fn new(dynamic_rules: Vec<Rule>, generation: u32) -> Self {
        Self { dynamic_rules: dynamic_rules.into(), generation }
    }

    /// Remove all dynamic rules from this [Transaction].
    pub fn reset_all(&mut self) {
        self.dynamic_rules.clear();
    }

    /// Add the given [Rule] to this [Transaction] with the highest match priority.
    pub fn add(&mut self, rule: Rule) {
        self.dynamic_rules.push_front(rule);
    }

    /// Return an iterator through all dynamic rewrite rules in the order they should be applied to
    /// incoming `fuchsia-pkg://` URLs.
    pub fn list_dynamic(&self) -> impl Iterator<Item = &Rule> {
        self.dynamic_rules.iter()
    }
}

#[derive(Debug)]
pub struct UnsetInspectNode;

/// [RewriteManagerBuilder] constructs a [RewriteManager], optionally initializing it with [Rule]s
/// passed in directly or loaded out of the filesystem.
#[derive(Debug)]
pub struct RewriteManagerBuilder<N> {
    static_rules: Vec<Rule>,
    dynamic_rules: Vec<Rule>,
    data_proxy: Option<fio::DirectoryProxy>,
    dynamic_rules_path: Option<String>,
    enable_dynamic_configuration: bool,
    inspect_node: N,
}

impl RewriteManagerBuilder<UnsetInspectNode> {
    /// Create a new [RewriteManagerBuilder] and initialize it with the dynamic [Rule]s from the
    /// provided path. If the provided dynamic rule config file does not exist or is corrupt, this
    /// method returns an [RewriteManagerBuilder] initialized with no rules and configured with the
    /// given dynamic config path.
    pub async fn new<P>(
        data_proxy: Option<fio::DirectoryProxy>,
        dynamic_rules_path: Option<P>,
    ) -> Result<Self, (Self, LoadRulesError)>
    where
        P: Into<String>,
    {
        let enable_dynamic_configuration = dynamic_rules_path.is_some();
        let mut builder = RewriteManagerBuilder {
            static_rules: vec![],
            dynamic_rules: vec![],
            data_proxy: data_proxy.clone(),
            dynamic_rules_path: dynamic_rules_path.map(|p| p.into()),
            inspect_node: UnsetInspectNode,
            enable_dynamic_configuration,
        };
        if let Some(ref dynamic_rules_path) = builder.dynamic_rules_path {
            match Self::load_rules(&data_proxy, dynamic_rules_path).await {
                Ok(rules) => {
                    builder.dynamic_rules = rules;
                    Ok(builder)
                }
                Err(err) => Err((builder, err)),
            }
        } else {
            Ok(builder)
        }
    }

    /// Use the given inspect_node in the [RewriteManager].
    pub fn inspect_node(self, inspect_node: inspect::Node) -> RewriteManagerBuilder<inspect::Node> {
        RewriteManagerBuilder {
            static_rules: self.static_rules,
            dynamic_rules: self.dynamic_rules,
            data_proxy: self.data_proxy,
            dynamic_rules_path: self.dynamic_rules_path,
            enable_dynamic_configuration: self.enable_dynamic_configuration,
            inspect_node,
        }
    }
}

#[derive(Debug, Error)]
pub enum LoadRulesError {
    #[error("directory open")]
    DirOpen(#[source] anyhow::Error),
    #[error("file open")]
    FileOpen(#[from] fuchsia_fs::node::OpenError),
    #[error("read file")]
    ReadFile(#[source] anyhow::Error),
    #[error("parse")]
    Parse(#[from] serde_json::Error),
}

impl<N> RewriteManagerBuilder<N> {
    /// Load [Rule]s from the provided path and register them as static rewrite rules. On error,
    /// return this [RewriteManagerBuilder] unmodified along with the encountered error.
    pub async fn static_rules_path(
        mut self,
        config_proxy: Option<fio::DirectoryProxy>,
        static_rules_path: &str,
    ) -> Result<Self, (Self, LoadRulesError)> {
        match Self::load_rules(&config_proxy, static_rules_path).await {
            Ok(rules) => {
                self.static_rules = rules;
                Ok(self)
            }
            Err(err) => Err((self, err)),
        }
    }

    async fn load_rules(
        dir_proxy: &Option<fio::DirectoryProxy>,
        path: &str,
    ) -> Result<Vec<Rule>, LoadRulesError> {
        let dir_proxy = dir_proxy
            .as_ref()
            .ok_or_else(|| LoadRulesError::DirOpen(anyhow!("failed to open config directory")))?;
        let file_proxy =
            fuchsia_fs::directory::open_file(&dir_proxy, &path, fio::OpenFlags::RIGHT_READABLE)
                .await?;
        let contents =
            fuchsia_fs::read_file(&file_proxy).await.map_err(LoadRulesError::ReadFile)?;
        let RuleConfig::Version1(rules) = serde_json::from_str(&contents)?;
        Ok(rules)
    }

    /// Append the given [Rule]s to the static rewrite rules.
    #[cfg(test)]
    pub fn static_rules<T>(mut self, iter: T) -> Self
    where
        T: IntoIterator<Item = Rule>,
    {
        self.static_rules.extend(iter);
        self
    }

    /// Replace the dynamic rules with the given [Rule]s.
    pub fn replace_dynamic_rules<T>(mut self, iter: T) -> Self
    where
        T: IntoIterator<Item = Rule>,
    {
        self.dynamic_rules.clear();
        self.dynamic_rules.extend(iter);
        self
    }
}

#[cfg(test)]
impl RewriteManagerBuilder<UnsetInspectNode> {
    /// In test configurations, allow building the [RewriteManager] without a configured inspect
    /// node.
    pub fn build(self) -> RewriteManager {
        let node = inspect::Inspector::new().root().create_child("test");
        self.inspect_node(node).build()
    }
}

impl RewriteManagerBuilder<inspect::Node> {
    /// Build the [RewriteManager].
    pub fn build(self) -> RewriteManager {
        let inspect = RewriteManagerInspectState {
            static_rules_node: self.inspect_node.create_child("static_rules"),
            static_rules_states: vec![],
            dynamic_rules_node: self.inspect_node.create_child("dynamic_rules"),
            dynamic_rules_states: vec![],
            generation_property: self.inspect_node.create_uint("generation", 0),
            dynamic_rules_path_property: self
                .inspect_node
                .create_string("dynamic_rules_path", &format!("{:?}", self.dynamic_rules_path)),
            node: self.inspect_node,
        };

        let mut rw = RewriteManager {
            static_rules: self.static_rules,
            dynamic_rules: self.dynamic_rules,
            generation: 0,
            data_proxy: self.data_proxy,
            dynamic_rules_path: self.dynamic_rules_path,
            enable_dynamic_configuration: self.enable_dynamic_configuration,
            inspect,
        };
        RewriteManager::update_rule_inspect_states(
            &rw.inspect.static_rules_node,
            &rw.static_rules,
            &mut rw.inspect.static_rules_states,
        );
        rw.update_inspect_objects();
        rw
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use {
        super::*,
        anyhow::Error,
        assert_matches::assert_matches,
        fuchsia_async as fasync,
        fuchsia_inspect::assert_data_tree,
        serde_json::json,
        std::{io, path::Path},
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

    pub(crate) fn make_temp_file<CB, E>(writer: CB) -> tempfile::TempPath
    where
        CB: FnOnce(&mut dyn io::Write) -> Result<(), E>,
        E: Into<Error>,
    {
        let mut f = tempfile::NamedTempFile::new().unwrap();
        writer(f.as_file_mut()).map_err(|err| err.into()).unwrap();
        f.into_temp_path()
    }

    pub(crate) fn make_rule_config(rules: Vec<Rule>) -> tempfile::TempPath {
        let config = RuleConfig::Version1(rules);
        make_temp_file(|writer| serde_json::to_writer(writer, &config))
    }

    pub(crate) fn temp_path_into_proxy_and_path(
        path: &tempfile::TempPath,
    ) -> (Option<fio::DirectoryProxy>, Option<String>) {
        let filename = Some(path.file_name().unwrap().to_str().unwrap().to_string());
        let dir = path.parent().unwrap().to_str().unwrap().to_string();
        let proxy = fuchsia_fs::directory::open_in_namespace(
            &dir,
            fuchsia_fs::OpenFlags::RIGHT_READABLE | fuchsia_fs::OpenFlags::RIGHT_WRITABLE,
        )
        .ok();
        (proxy, filename)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_empty_configs() {
        let path = make_rule_config(vec![]);
        let (config_dir, config_file) = temp_path_into_proxy_and_path(&path);

        let manager = RewriteManagerBuilder::new(config_dir.clone(), config_file.clone())
            .await
            .unwrap()
            .static_rules_path(config_dir, &config_file.unwrap())
            .await
            .unwrap()
            .build();

        assert_eq!(manager.list_static().cloned().collect::<Vec<_>>(), vec![]);
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_single_static_rule() {
        let rules = vec![rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice")];

        let dynamic_path = make_rule_config(vec![]);
        let (dynamic_config_dir, dynamic_config_file) =
            temp_path_into_proxy_and_path(&dynamic_path);
        let path = make_rule_config(rules.clone());
        let (static_config_dir, static_config_file) = temp_path_into_proxy_and_path(&path);
        let manager = RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
            .await
            .unwrap()
            .static_rules_path(static_config_dir, &static_config_file.unwrap())
            .await
            .unwrap()
            .build();

        assert_eq!(manager.list_static().cloned().collect::<Vec<_>>(), rules);
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_single_dynamic_rule() {
        let rules = vec![rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice")];

        let path = make_rule_config(rules.clone());
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let manager = RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
            .await
            .unwrap()
            .build();

        assert_eq!(manager.list_static().cloned().collect::<Vec<_>>(), vec![]);
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_rejects_invalid_static_config() {
        let rules = vec![rule!("fuchsia.com" => "fuchsia.com", "/a" => "/b")];
        let dynamic_path = make_rule_config(rules.clone());
        let (dynamic_config_dir, dynamic_config_file) =
            temp_path_into_proxy_and_path(&dynamic_path);
        let path = make_temp_file(|writer| {
            write!(
                writer,
                "{}",
                json!({
                    "version": "1",
                    "content": {} // should be an array
                })
            )
        });
        let (static_config_dir, static_config_file) = temp_path_into_proxy_and_path(&path);
        let (builder, _) = RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
            .await
            .unwrap()
            .static_rules_path(static_config_dir, &static_config_file.unwrap())
            .await
            .unwrap_err();
        let manager = builder.build();

        assert_eq!(manager.list_static().cloned().collect::<Vec<_>>(), vec![]);
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_recovers_from_invalid_dynamic_config() {
        let path = make_temp_file(|writer| write!(writer, "invalid"));
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let rule = rule!("test.com" => "test.com", "/a" => "/b");

        {
            let (builder, _) =
                RewriteManagerBuilder::new(dynamic_config_dir.clone(), dynamic_config_file.clone())
                    .await
                    .unwrap_err();
            let mut manager = builder.build();

            assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![]);

            let mut transaction = manager.transaction();
            transaction.add(rule.clone());
            manager.apply(transaction).await.unwrap();

            assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![rule.clone()]);
        }

        // Verify the dynamic config file is no longer corrupt and contains the newly added rule.
        let manager = RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
            .await
            .unwrap()
            .build();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![rule]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_rewrite_identity_if_no_rules_match() {
        let rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/a" => "/aa"),
            rule!("fuchsia.com" => "fuchsia.com", "/b" => "/bb"),
        ];

        let path = make_rule_config(rules);
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let manager = RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
            .await
            .unwrap()
            .build();

        let url: AbsolutePackageUrl = "fuchsia-pkg://fuchsia.com/c".parse().unwrap();
        assert_eq!(manager.rewrite(&url), url);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_rewrite_first_rule_wins() {
        let rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/package" => "/remapped"),
            rule!("fuchsia.com" => "fuchsia.com", "/package" => "/incorrect"),
        ];

        let path = make_rule_config(rules);
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let manager = RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
            .await
            .unwrap()
            .build();

        let url = "fuchsia-pkg://fuchsia.com/package".parse().unwrap();
        assert_eq!(manager.rewrite(&url), "fuchsia-pkg://fuchsia.com/remapped".parse().unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_rewrite_dynamic_rules_override_static_rules() {
        let dynamic_path = make_rule_config(vec![
            rule!("fuchsia.com" => "fuchsia.com", "/package" => "/remapped"),
        ]);
        let (dynamic_config_dir, dynamic_config_file) =
            temp_path_into_proxy_and_path(&dynamic_path);
        let path = make_rule_config(vec![
            rule!("fuchsia.com" => "fuchsia.com", "/package" => "/incorrect"),
        ]);
        let (static_config_dir, static_config_file) = temp_path_into_proxy_and_path(&path);
        let manager = RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
            .await
            .unwrap()
            .static_rules_path(static_config_dir, &static_config_file.unwrap())
            .await
            .unwrap()
            .build();

        let url = "fuchsia-pkg://fuchsia.com/package".parse().unwrap();
        assert_eq!(manager.rewrite(&url), "fuchsia-pkg://fuchsia.com/remapped".parse().unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_rewrite_replace_dynamic_configs() {
        let static_rules = vec![rule!("fuchsia.com" => "foo.com", "/" => "/")];
        let dynamic_rules = vec![rule!("fuchsia.com" => "bar.com", "/" => "/")];
        let new_dynamic_rules = vec![rule!("fuchsia.com" => "baz.com", "/" => "/")];

        let static_path = make_rule_config(static_rules);
        let (static_config_dir, static_config_file) = temp_path_into_proxy_and_path(&static_path);
        let path = make_rule_config(dynamic_rules);
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);

        let manager = RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
            .await
            .unwrap()
            .static_rules_path(static_config_dir, &static_config_file.unwrap())
            .await
            .unwrap()
            .replace_dynamic_rules(new_dynamic_rules.clone())
            .build();

        let url = "fuchsia-pkg://fuchsia.com/package".parse().unwrap();
        assert_eq!(manager.rewrite(&url), "fuchsia-pkg://baz.com/package".parse().unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_rewrite_with_pending_transaction() {
        let override_rule = rule!("fuchsia.com" => "fuchsia.com", "/a" => "/c");
        let path = make_rule_config(vec![rule!("fuchsia.com" => "fuchsia.com", "/a" => "/b")]);
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let mut manager = RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
            .await
            .unwrap()
            .build();

        let mut transaction = manager.transaction();
        transaction.add(override_rule.clone());

        // new rule is not yet committed and should not be used yet
        let url: AbsolutePackageUrl = "fuchsia-pkg://fuchsia.com/a".parse().unwrap();
        assert_eq!(manager.rewrite(&url), "fuchsia-pkg://fuchsia.com/b".parse().unwrap());

        manager.apply(transaction).await.unwrap();

        let url = "fuchsia-pkg://fuchsia.com/a".parse().unwrap();
        assert_eq!(manager.rewrite(&url), "fuchsia-pkg://fuchsia.com/c".parse().unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_commit_additional_rule() {
        let existing_rule = rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice");
        let new_rule = rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/");

        let rules = vec![existing_rule.clone()];
        let path = make_rule_config(rules.clone());
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let mut manager =
            RewriteManagerBuilder::new(dynamic_config_dir.clone(), dynamic_config_file.clone())
                .await
                .unwrap()
                .build();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);

        // Fork the existing state, add a rule, and verify both instances are distinct
        let new_rules = vec![new_rule.clone(), existing_rule.clone()];
        let mut transaction = manager.transaction();
        transaction.add(new_rule);
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);
        assert_eq!(transaction.list_dynamic().cloned().collect::<Vec<_>>(), new_rules);

        // Commit the new rule set
        let () = manager.apply(transaction).await.unwrap();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), new_rules);

        // Ensure new rules are persisted to the dynamic config file
        let manager = RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
            .await
            .unwrap()
            .build();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), new_rules);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_erase_all_dynamic_rules() {
        let rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/"),
        ];

        let path = make_rule_config(rules.clone());
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let mut manager =
            RewriteManagerBuilder::new(dynamic_config_dir.clone(), dynamic_config_file.clone())
                .await
                .unwrap()
                .build();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);

        let mut transaction = manager.transaction();
        transaction.reset_all();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);
        assert_eq!(transaction.list_dynamic().cloned().collect::<Vec<_>>(), vec![]);

        let () = manager.apply(transaction).await.unwrap();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![]);

        let manager = RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
            .await
            .unwrap()
            .build();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_building_rewrite_manager_populates_inspect() {
        let inspector = fuchsia_inspect::Inspector::new();
        let node = inspector.root().create_child("rewrite_manager");
        let dynamic_rules = vec![
            rule!("this.example.com" => "that.example.com", "/this_rolldice" => "/that_rolldice"),
        ];
        let dynamic_path = make_rule_config(dynamic_rules.clone());
        let (dynamic_config_dir, dynamic_config_file) =
            temp_path_into_proxy_and_path(&dynamic_path);

        let static_rules =
            vec![rule!("example.com" => "example.org", "/this_throwdice" => "/that_throwdice")];
        let path = make_rule_config(static_rules.clone());
        let (static_config_dir, static_config_file) = temp_path_into_proxy_and_path(&path);

        let _manager = RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file.clone())
            .await
            .unwrap()
            .static_rules_path(static_config_dir, &static_config_file.unwrap())
            .await
            .unwrap()
            .inspect_node(node)
            .build();

        assert_data_tree!(
            inspector,
            root: {
                rewrite_manager: {
                    dynamic_rules: {
                        "0": {
                            host_match: "this.example.com",
                            host_replacement: "that.example.com",
                            path_prefix_match: "/this_rolldice",
                            path_prefix_replacement: "/that_rolldice",
                        },
                    },
                    dynamic_rules_path: format!("{:?}", dynamic_config_file),
                    static_rules: {
                        "0": {
                            host_match: "example.com",
                            host_replacement: "example.org",
                            path_prefix_match: "/this_throwdice",
                            path_prefix_replacement: "/that_throwdice",
                        },
                    },
                    generation: 0u64,
                }
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_rewrite_manager_no_dynamic_rules_path() {
        let inspector = fuchsia_inspect::Inspector::new();
        let node = inspector.root().create_child("rewrite_manager");

        let _manager = RewriteManagerBuilder::new(None, Option::<&str>::None)
            .await
            .unwrap()
            .inspect_node(node)
            .build();

        assert_data_tree!(
            inspector,
            root: {
                rewrite_manager: {
                    dynamic_rules: {},
                    dynamic_rules_path: format!("{:?}", Option::<&Path>::None),
                    static_rules: {},
                    generation: 0u64,
                }
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_transaction_updates_inspect() {
        let inspector = fuchsia_inspect::Inspector::new();
        let node = inspector.root().create_child("rewrite_manager");
        let path = make_rule_config(vec![]);
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let mut manager =
            RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file.clone())
                .await
                .unwrap()
                .inspect_node(node)
                .build();
        assert_data_tree!(
            inspector,
            root: {
                rewrite_manager: {
                    dynamic_rules: {},
                    dynamic_rules_path: format!("{:?}", dynamic_config_file.clone()),
                    static_rules: {},
                    generation: 0u64,
                }
            }
        );

        let mut transaction = manager.transaction();
        transaction
            .add(rule!("example.com" => "example.org", "/this_rolldice/" => "/that_rolldice/"));
        manager.apply(transaction).await.unwrap();

        assert_data_tree!(
            inspector,
            root: {
                rewrite_manager: {
                    dynamic_rules: {
                        "0": {
                            host_match: "example.com",
                            host_replacement: "example.org",
                            path_prefix_match: "/this_rolldice/",
                            path_prefix_replacement: "/that_rolldice/",
                        },
                    },
                    dynamic_rules_path: format!("{:?}", dynamic_config_file),
                    static_rules: {},
                    generation: 1u64,
                }
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_dynamic_rules_if_no_dynamic_rules_path() {
        let manager = RewriteManagerBuilder::new(None, Option::<&str>::None).await.unwrap().build();

        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_same_dynamic_rules_if_apply_fails() {
        let path = make_rule_config(vec![]);
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        let rule0 = rule!("test0.com" => "test0.com", "/a" => "/b");
        let rule1 = rule!("test1.com" => "test1.com", "/a" => "/b");
        let mut manager = RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file)
            .await
            .unwrap()
            .build();
        assert_eq!(manager.list().collect::<Vec<_>>(), Vec::<&Rule>::new());

        // transaction0 adds a dynamic rule
        let mut transaction0 = manager.transaction();
        let mut transaction1 = manager.transaction();
        transaction0.add(rule0.clone());
        manager.apply(transaction0).await.unwrap();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![rule0.clone()]);

        // transaction1 fails to apply b/c it was created before transaction0 was applied
        // the dynamic rewrite rules should be unchanged
        transaction1.add(rule1.clone());
        assert_matches!(manager.apply(transaction1).await, Err(CommitError::TooLate));
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![rule0.clone()]);

        // transaction2 applies the rule from transaction1
        let mut transaction2 = manager.transaction();
        transaction2.add(rule1.clone());
        manager.apply(transaction2).await.unwrap();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![rule1.clone(), rule0.clone()]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_apply_fails_with_no_change_if_no_dynamic_rules_path() {
        let mut manager =
            RewriteManagerBuilder::new(None, Option::<&str>::None).await.unwrap().build();
        let mut transaction = manager.transaction();
        transaction.add(rule!("test0.com" => "test0.com", "/a" => "/b"));

        assert_matches!(
            manager.apply(transaction).await,
            Err(CommitError::DynamicConfigurationDisabled)
        );
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_rewrite_works_when_data_inaccessible() {
        let rule0 = rule!("test0.com" => "test0.com", "/a" => "/b");
        let (builder, _) =
            RewriteManagerBuilder::new(None, Some("nonemptyfilename")).await.unwrap_err();
        let mut manager = builder.build();

        // transaction0 adds a dynamic rule
        let mut transaction0 = manager.transaction();
        transaction0.add(rule0.clone());
        manager.apply(transaction0).await.unwrap();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![rule0.clone()]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_constructor_returns_not_found_if_file_missing() {
        let path = make_rule_config(vec![]);
        let (dynamic_config_dir, dynamic_config_file) = temp_path_into_proxy_and_path(&path);
        // Delete the config file.
        path.close().unwrap();

        assert_matches!(
            RewriteManagerBuilder::new(dynamic_config_dir, dynamic_config_file).await,
            Err((
                _,
                LoadRulesError::FileOpen(fuchsia_fs::node::OpenError::OpenError(
                    fuchsia_zircon::Status::NOT_FOUND
                ))
            ))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_static_rules_return_not_found_if_file_missing() {
        let path = make_rule_config(vec![]);
        let (config_dir, config_file) = temp_path_into_proxy_and_path(&path);
        // Delete the config file.
        path.close().unwrap();

        let builder = RewriteManagerBuilder::new(None, Option::<&str>::None).await.unwrap();
        assert_matches!(
            builder.static_rules_path(config_dir, &config_file.unwrap()).await,
            Err((
                _,
                LoadRulesError::FileOpen(fuchsia_fs::node::OpenError::OpenError(
                    fuchsia_zircon::Status::NOT_FOUND
                ))
            ))
        );
    }

    #[test]
    fn test_create_rule_inspect_state_passes_through_fields() {
        let inspector = inspect::Inspector::new();
        let node = inspector.root().create_child("rule_node");

        let rule = rule!("fuchsia.com" => "example.com", "/foo" => "/bar");
        let _state = create_rule_inspect_state(&rule, node);

        assert_data_tree!(
            inspector,
            root: {
                rule_node: {
                    host_match: rule.host_match().to_string(),
                    host_replacement: rule.host_replacement().to_string(),
                    path_prefix_match: rule.path_prefix_match().to_string(),
                    path_prefix_replacement: rule.path_prefix_replacement().to_string(),
                }
            }
        );
    }
}
