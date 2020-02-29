// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_pkg_rewrite_ext::{Rule, RuleConfig, RuleInspectState},
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_syslog::fx_log_err,
    fuchsia_url::pkg_url::PkgUrl,
    std::{
        collections::VecDeque,
        fs::{self, File},
        io,
        path::{Path, PathBuf},
    },
    thiserror::Error,
};

/// [RewriteManager] controls access to all static and dynamic rewrite rules used by the package
/// resolver.
///
/// No two instances of [RewriteManager] should be configured to use the same `dynamic_rules_path`,
/// or concurrent saves could corrupt the config file or lose edits. Instead, use the provided
/// [RewriteManager::transaction] API to safely manage concurrent edits.
#[derive(Debug)]
pub struct RewriteManager {
    static_rules: Vec<Rule>,
    dynamic_rules: Vec<Rule>,
    generation: u32,
    dynamic_rules_path: Option<PathBuf>,
    inspect: RewriteManagerInspectState,
}

#[derive(Debug)]
struct RewriteManagerInspectState {
    static_rules_node: inspect::Node,
    static_rules_states: Vec<RuleInspectState>,
    dynamic_rules_node: inspect::Node,
    dynamic_rules_states: Vec<RuleInspectState>,
    generation_property: fuchsia_inspect::UintProperty,
    dynamic_rules_path_property: fuchsia_inspect::StringProperty,
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
    /// Rewrite the given [PkgUrl] using the first dynamic or static rewrite rule that matches and
    /// produces a valid [PkgUrl]. If no rewrite rules match or all that do produce invalid
    /// [PkgUrl]s, return the original, unmodified [PkgUrl].
    pub fn rewrite(&self, url: PkgUrl) -> PkgUrl {
        for rule in self.list() {
            match rule.apply(&url) {
                Some(Ok(res)) => {
                    return res;
                }
                Some(Err(err)) => {
                    fx_log_err!("re-write rule {:?} produced an invalid URL, ignoring rule", err);
                }
                _ => {}
            }
        }
        url
    }

    fn save(dynamic_rules: &mut Vec<Rule>, dynamic_rules_path: &Path) -> io::Result<()> {
        let config = RuleConfig::Version1(std::mem::replace(dynamic_rules, vec![]));

        let result = (|| {
            let mut temp_path = dynamic_rules_path.to_owned().into_os_string();
            temp_path.push(".new");
            let temp_path = PathBuf::from(temp_path);
            {
                let f = File::create(&temp_path)?;
                serde_json::to_writer(io::BufWriter::new(f), &config)?;
            };
            fs::rename(temp_path, dynamic_rules_path)
        })();

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

    /// Apply the given [Transaction] object to this [RewriteManager] iff no other
    /// [RewriteRuleStates] have been applied since `transaction` was cloned from this
    /// [RewriteManager].
    pub fn apply(&mut self, transaction: Transaction) -> Result<(), CommitError> {
        if let Some(ref dynamic_rules_path) = self.dynamic_rules_path {
            if self.generation != transaction.generation {
                Err(CommitError::TooLate)
            } else {
                self.dynamic_rules = transaction.dynamic_rules.into();
                self.generation += 1;
                // FIXME(kevinwells) synchronous I/O in an async context
                if let Err(err) = Self::save(&mut self.dynamic_rules, dynamic_rules_path) {
                    fx_log_err!("error while saving dynamic rewrite rules: {}", err);
                }
                self.update_inspect_objects();
                Ok(())
            }
        } else {
            Err(CommitError::DynamicConfigurationDisabled)
        }
    }

    /// Return an iterator through all rewrite rules in the order they should be applied to
    /// incoming `fuchsia-pkg://` URLs.
    pub fn list<'a>(&'a self) -> impl Iterator<Item = &'a Rule> {
        self.dynamic_rules.iter().chain(self.list_static())
    }

    /// Return an iterator through all static rewrite rules in the order they should be applied to
    /// incoming `fuchsia-pkg://` URLs, after all dynamic rules have been considered.
    pub fn list_static<'a>(&'a self) -> impl Iterator<Item = &'a Rule> {
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
            states.push(rule.create_inspect_state(rule_node));
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
    pub fn list_dynamic<'a>(&'a self) -> impl Iterator<Item = &'a Rule> {
        self.dynamic_rules.iter()
    }
}

#[derive(Debug)]
pub struct UnsetInspectNode;

/// [RewriteManagerBuilder] constructs a [RewriteManager], optionally initializing it with [Rule]s
/// passed in directly or loaded out of the filesystem.
#[derive(Debug, PartialEq, Eq)]
pub struct RewriteManagerBuilder<N> {
    static_rules: Vec<Rule>,
    dynamic_rules: Vec<Rule>,
    dynamic_rules_path: Option<PathBuf>,
    inspect_node: N,
}

impl RewriteManagerBuilder<UnsetInspectNode> {
    /// Create a new [RewriteManagerBuilder] and initialize it with the dynamic [Rule]s from the
    /// provided path. If the provided dynamic rule config file does not exist or is corrupt, this
    /// method returns an [RewriteManagerBuilder] initialized with no rules and configured with the
    /// given dynamic config path.
    pub fn new<T>(dynamic_rules_path: Option<T>) -> Result<Self, (Self, io::Error)>
    where
        T: Into<PathBuf>,
    {
        let mut builder = RewriteManagerBuilder {
            static_rules: vec![],
            dynamic_rules: vec![],
            dynamic_rules_path: dynamic_rules_path.map(|p| p.into()),
            inspect_node: UnsetInspectNode,
        };

        if let Some(ref path) = builder.dynamic_rules_path {
            match Self::load_rules(path) {
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
            dynamic_rules_path: self.dynamic_rules_path,
            inspect_node,
        }
    }
}

impl<N> RewriteManagerBuilder<N> {
    /// Load [Rule]s from the provided path and register them as static rewrite rules. On error,
    /// return this [RewriteManagerBuilder] unmodified along with the encountered error.
    pub fn static_rules_path<T>(mut self, path: T) -> Result<Self, (Self, io::Error)>
    where
        T: AsRef<Path>,
    {
        match Self::load_rules(path) {
            Ok(rules) => {
                self.static_rules = rules;
                Ok(self)
            }
            Err(err) => Err((self, err)),
        }
    }

    fn load_rules<T>(path: T) -> Result<Vec<Rule>, io::Error>
    where
        T: AsRef<Path>,
    {
        let f = File::open(path.as_ref())?;
        let RuleConfig::Version1(rules) = serde_json::from_reader(io::BufReader::new(f))?;
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
            dynamic_rules_path: self.dynamic_rules_path,
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
        super::*, anyhow::Error, fuchsia_inspect::assert_inspect_tree, matches::assert_matches,
        serde_json::json,
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

    #[test]
    fn test_empty_configs() {
        let config = make_rule_config(vec![]);

        let manager = RewriteManagerBuilder::new(Some(&config))
            .unwrap()
            .static_rules_path(&config)
            .unwrap()
            .build();

        assert_eq!(manager.list_static().cloned().collect::<Vec<_>>(), vec![]);
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![]);
    }

    #[test]
    fn test_load_single_static_rule() {
        let rules = vec![rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice")];

        let dynamic_config = make_rule_config(vec![]);
        let static_config = make_rule_config(rules.clone());
        let manager = RewriteManagerBuilder::new(Some(&dynamic_config))
            .unwrap()
            .static_rules_path(&static_config)
            .unwrap()
            .build();

        assert_eq!(manager.list_static().cloned().collect::<Vec<_>>(), rules);
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);
    }

    #[test]
    fn test_load_single_dynamic_rule() {
        let rules = vec![rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice")];

        let dynamic_config = make_rule_config(rules.clone());
        let manager = RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build();

        assert_eq!(manager.list_static().cloned().collect::<Vec<_>>(), vec![]);
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);
    }

    #[test]
    fn test_rejects_invalid_static_config() {
        let rules = vec![rule!("fuchsia.com" => "fuchsia.com", "/a" => "/b")];
        let dynamic_config = make_rule_config(rules.clone());
        let static_config = make_temp_file(|writer| {
            write!(
                writer,
                "{}",
                json!({
                    "version": "1",
                    "content": {} // should be an array
                })
            )
        });
        let (builder, _) = RewriteManagerBuilder::new(Some(&dynamic_config))
            .unwrap()
            .static_rules_path(&static_config)
            .unwrap_err();
        let manager = builder.build();

        assert_eq!(manager.list_static().cloned().collect::<Vec<_>>(), vec![]);
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);
    }

    #[test]
    fn test_recovers_from_invalid_dynamic_config() {
        let dynamic_config = make_temp_file(|writer| write!(writer, "invalid"));
        let rule = rule!("test.com" => "test.com", "/a" => "/b");

        {
            let (builder, _) = RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap_err();
            let mut manager = builder.build();

            assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![]);

            let mut transaction = manager.transaction();
            transaction.add(rule.clone());
            manager.apply(transaction).unwrap();

            assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![rule.clone()]);
        }

        // Verify the dynamic config file is no longer corrupt and contains the newly added rule.
        let manager = RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![rule]);
    }

    #[test]
    fn test_rewrite_identity_if_no_rules_match() {
        let rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/a" => "/aa"),
            rule!("fuchsia.com" => "fuchsia.com", "/b" => "/bb"),
        ];

        let dynamic_config = make_rule_config(rules);
        let manager = RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build();

        let url: PkgUrl = "fuchsia-pkg://fuchsia.com/c".parse().unwrap();
        assert_eq!(manager.rewrite(url.clone()), url);
    }

    #[test]
    fn test_rewrite_first_rule_wins() {
        let rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/package" => "/remapped"),
            rule!("fuchsia.com" => "fuchsia.com", "/package" => "/incorrect"),
        ];

        let dynamic_config = make_rule_config(rules);
        let manager = RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build();

        let url = "fuchsia-pkg://fuchsia.com/package".parse().unwrap();
        assert_eq!(manager.rewrite(url), "fuchsia-pkg://fuchsia.com/remapped".parse().unwrap());
    }

    #[test]
    fn test_rewrite_dynamic_rules_override_static_rules() {
        let dynamic_config = make_rule_config(vec![
            rule!("fuchsia.com" => "fuchsia.com", "/package" => "/remapped"),
        ]);
        let static_config = make_rule_config(vec![
            rule!("fuchsia.com" => "fuchsia.com", "/package" => "/incorrect"),
        ]);
        let manager = RewriteManagerBuilder::new(Some(&dynamic_config))
            .unwrap()
            .static_rules_path(&static_config)
            .unwrap()
            .build();

        let url = "fuchsia-pkg://fuchsia.com/package".parse().unwrap();
        assert_eq!(manager.rewrite(url), "fuchsia-pkg://fuchsia.com/remapped".parse().unwrap());
    }

    #[test]
    fn test_rewrite_replace_dynamic_configs() {
        let static_rules = vec![rule!("fuchsia.com" => "foo.com", "/" => "/")];
        let dynamic_rules = vec![rule!("fuchsia.com" => "bar.com", "/" => "/")];
        let new_dynamic_rules = vec![rule!("fuchsia.com" => "baz.com", "/" => "/")];

        let static_config = make_rule_config(static_rules);
        let dynamic_config = make_rule_config(dynamic_rules);

        let manager = RewriteManagerBuilder::new(Some(&dynamic_config))
            .unwrap()
            .static_rules_path(&static_config)
            .unwrap()
            .replace_dynamic_rules(new_dynamic_rules.clone())
            .build();

        let url = "fuchsia-pkg://fuchsia.com/package".parse().unwrap();
        assert_eq!(manager.rewrite(url), "fuchsia-pkg://baz.com/package".parse().unwrap());
    }

    #[test]
    fn test_rewrite_with_pending_transaction() {
        let override_rule = rule!("fuchsia.com" => "fuchsia.com", "/a" => "/c");
        let dynamic_config =
            make_rule_config(vec![rule!("fuchsia.com" => "fuchsia.com", "/a" => "/b")]);
        let mut manager = RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build();

        let mut transaction = manager.transaction();
        transaction.add(override_rule.clone());

        // new rule is not yet committed and should not be used yet
        let url: PkgUrl = "fuchsia-pkg://fuchsia.com/a".parse().unwrap();
        assert_eq!(manager.rewrite(url.clone()), "fuchsia-pkg://fuchsia.com/b".parse().unwrap());

        manager.apply(transaction).unwrap();

        let url = "fuchsia-pkg://fuchsia.com/a".parse().unwrap();
        assert_eq!(manager.rewrite(url), "fuchsia-pkg://fuchsia.com/c".parse().unwrap());
    }

    #[test]
    fn test_commit_additional_rule() {
        let existing_rule = rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice");
        let new_rule = rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/");

        let rules = vec![existing_rule.clone()];
        let dynamic_config = make_rule_config(rules.clone());
        let mut manager = RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);

        // Fork the existing state, add a rule, and verify both instances are distinct
        let new_rules = vec![new_rule.clone(), existing_rule.clone()];
        let mut transaction = manager.transaction();
        transaction.add(new_rule);
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);
        assert_eq!(transaction.list_dynamic().cloned().collect::<Vec<_>>(), new_rules);

        // Commit the new rule set
        assert_eq!(manager.apply(transaction).unwrap(), ());
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), new_rules);

        // Ensure new rules are persisted to the dynamic config file
        let manager = RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), new_rules);
    }

    #[test]
    fn test_erase_all_dynamic_rules() {
        let rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/"),
        ];

        let dynamic_config = make_rule_config(rules.clone());
        let mut manager = RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);

        let mut transaction = manager.transaction();
        transaction.reset_all();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);
        assert_eq!(transaction.list_dynamic().cloned().collect::<Vec<_>>(), vec![]);

        assert_eq!(manager.apply(transaction).unwrap(), ());
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![]);

        let manager = RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![]);
    }

    #[test]
    fn test_building_rewrite_manager_populates_inspect() {
        let inspector = fuchsia_inspect::Inspector::new();
        let node = inspector.root().create_child("rewrite_manager");
        let dynamic_rules = vec![
            rule!("this.example.com" => "that.example.com", "/this_rolldice" => "/that_rolldice"),
        ];
        let dynamic_config = make_rule_config(dynamic_rules.clone());
        let static_rules =
            vec![rule!("example.com" => "example.org", "/this_throwdice" => "/that_throwdice")];
        let static_config = make_rule_config(static_rules.clone());

        let _manager = RewriteManagerBuilder::new(Some(&dynamic_config))
            .unwrap()
            .static_rules_path(&static_config)
            .unwrap()
            .inspect_node(node)
            .build();

        assert_inspect_tree!(
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
                    dynamic_rules_path: format!("{:?}", Some(&*dynamic_config)),
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

    #[test]
    fn test_inspect_rewrite_manager_no_dynamic_rules_path() {
        let inspector = fuchsia_inspect::Inspector::new();
        let node = inspector.root().create_child("rewrite_manager");

        let _manager =
            RewriteManagerBuilder::new(Option::<&Path>::None).unwrap().inspect_node(node).build();

        assert_inspect_tree!(
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

    #[test]
    fn test_transaction_updates_inspect() {
        let inspector = fuchsia_inspect::Inspector::new();
        let node = inspector.root().create_child("rewrite_manager");
        let dynamic_config = make_rule_config(vec![]);
        let mut manager =
            RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().inspect_node(node).build();
        assert_inspect_tree!(
            inspector,
            root: {
                rewrite_manager: {
                    dynamic_rules: {},
                    dynamic_rules_path: format!("{:?}", Some(&*dynamic_config)),
                    static_rules: {},
                    generation: 0u64,
                }
            }
        );

        let mut transaction = manager.transaction();
        transaction
            .add(rule!("example.com" => "example.org", "/this_rolldice/" => "/that_rolldice/"));
        manager.apply(transaction).unwrap();

        assert_inspect_tree!(
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
                    dynamic_rules_path: format!("{:?}", Some(&*dynamic_config)),
                    static_rules: {},
                    generation: 1u64,
                }
            }
        );
    }

    #[test]
    fn test_no_dynamic_rules_if_no_dynamic_rules_path() {
        let manager = RewriteManagerBuilder::new(Option::<&Path>::None).unwrap().build();

        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![]);
    }

    #[test]
    fn test_same_dynamic_rules_if_apply_fails() {
        let dynamic_config = make_rule_config(vec![]);
        let rule0 = rule!("test0.com" => "test0.com", "/a" => "/b");
        let rule1 = rule!("test1.com" => "test1.com", "/a" => "/b");
        let mut manager = RewriteManagerBuilder::new(Some(&dynamic_config)).unwrap().build();
        assert_eq!(manager.list().collect::<Vec<_>>(), Vec::<&Rule>::new());

        // transaction0 adds a dynamic rule
        let mut transaction0 = manager.transaction();
        let mut transaction1 = manager.transaction();
        transaction0.add(rule0.clone());
        manager.apply(transaction0).unwrap();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![rule0.clone()]);

        // transaction1 fails to apply b/c it was created before transaction0 was applied
        // the dynamic rewrite rules should be unchanged
        transaction1.add(rule1.clone());
        assert_matches!(manager.apply(transaction1), Err(CommitError::TooLate));
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![rule0.clone()]);

        // transaction2 applies the rule from transaction1
        let mut transaction2 = manager.transaction();
        transaction2.add(rule1.clone());
        manager.apply(transaction2).unwrap();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![rule1.clone(), rule0.clone()]);
    }

    #[test]
    fn test_apply_fails_with_no_change_if_no_dynamic_rules_path() {
        let mut manager = RewriteManagerBuilder::new(Option::<&Path>::None).unwrap().build();
        let mut transaction = manager.transaction();
        transaction.add(rule!("test0.com" => "test0.com", "/a" => "/b"));

        assert_matches!(manager.apply(transaction), Err(CommitError::DynamicConfigurationDisabled));
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![]);
    }
}
