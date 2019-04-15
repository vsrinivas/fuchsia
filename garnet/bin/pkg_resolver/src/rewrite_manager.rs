// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Fail,
    fuchsia_uri_rewrite::{Rule, RuleConfig},
    std::{
        collections::VecDeque,
        fs::{self, File},
        io,
        path::{Path, PathBuf},
    },
};

/// [RewriteManager] controls access to all static and dynamic rewrite rules used by the package
/// resolver.
///
/// No two instances of [RewriteManager] should be configured to use the same `dynamic_rules_path`,
/// or concurrent saves could corrupt the config file or lose edits. Instead, use the provided
/// [RewriteManager::transaction] API to safely manage concurrent edits.
#[derive(Debug, PartialEq, Eq)]
pub struct RewriteManager {
    rules: Vec<Rule>,
    generation: u32,
    dynamic_rules_path: PathBuf,
}

#[derive(Debug, Fail)]
pub enum CommitError {
    #[fail(display = "the provided rule set is based on an older generation")]
    TooLate,

    #[fail(display = "unable to persist new rule set: {}", _0)]
    IoError(#[cause] io::Error),
}

impl RewriteManager {
    /// Construct a new, empty [RewriteManager] configured to persist dynamic rewrite rules to
    /// `dynamic_rules_path`.
    pub fn new(dynamic_rules_path: PathBuf) -> Self {
        RewriteManager { rules: vec![], generation: 0, dynamic_rules_path }
    }

    /// Load a dynamic rewrite config file tinto a [RewriteManager], or report an error if the
    /// provided `dynamic_rule_path` does not exist, is corrupt, or loading encounters an IO error.
    pub fn load(dynamic_rules_path: &Path) -> io::Result<Self> {
        let f = File::open(dynamic_rules_path)?;
        let RuleConfig::Version1(rules) = serde_json::from_reader(f)?;
        let state = RewriteManager {
            rules: rules,
            generation: 0,
            dynamic_rules_path: dynamic_rules_path.to_owned(),
        };
        Ok(state)
    }

    fn save(dynamic_rules_path: &Path, rules: Vec<Rule>) -> io::Result<Vec<Rule>> {
        let config = RuleConfig::Version1(rules);
        let mut temp_path = dynamic_rules_path.to_owned().into_os_string();
        temp_path.push(".new");
        let temp_path = PathBuf::from(temp_path);
        {
            let f = File::create(&temp_path)?;
            serde_json::to_writer(f, &config)?;
        };
        fs::rename(temp_path, dynamic_rules_path)?;
        let RuleConfig::Version1(rules) = config;
        Ok(rules)
    }

    /// Construct a new [Transaction] containing the dynamic config rules from this
    /// [RewriteManager].
    pub fn transaction(&self) -> Transaction {
        Transaction { rules: self.rules.clone().into(), generation: self.generation }
    }

    /// Apply the given [Transaction] object to this [RewriteManager] iff no other
    /// [RewriteRuleStates] have been applied since `state` was cloned from this [RewriteManager].
    pub fn apply(&mut self, state: Transaction) -> Result<(), CommitError> {
        if self.generation != state.generation {
            Err(CommitError::TooLate)
        } else {
            // FIXME(kevinwells) synchronous I/O in an async context
            self.rules = RewriteManager::save(&self.dynamic_rules_path, state.rules.into())
                .map_err(|e| CommitError::IoError(e))?;
            self.generation += 1;
            Ok(())
        }
    }

    /// Return an iterator through all rewrite rules in the order they should be applied to
    /// incoming `fuchsia-pkg://` URIs.
    pub fn list<'a>(&'a self) -> impl Iterator<Item = &'a Rule> {
        self.rules.iter()
    }
}

/// [Transaction] tracks an edit transaction to a set of dynamic rewrite rules.
#[derive(Debug, PartialEq, Eq)]
pub struct Transaction {
    rules: VecDeque<Rule>,
    generation: u32,
}

impl Transaction {
    #[cfg(test)]
    pub fn new(rules: Vec<Rule>, generation: u32) -> Self {
        Self { rules: rules.into(), generation }
    }

    /// Remove all dynamic rules from this [Transaction].
    pub fn reset_all(&mut self) {
        self.rules.clear();
    }

    /// Add the given [Rule] to this [Transaction] with the highest match priority.
    pub fn add(&mut self, rule: Rule) {
        self.rules.push_front(rule);
    }

    #[cfg(test)]
    fn list<'a>(&'a self) -> impl Iterator<Item = &'a Rule> {
        self.rules.iter()
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    macro_rules! rule {
        ($host_match:expr => $host_replacement:expr,
         $path_prefix_match:expr => $path_prefix_replacement:expr) => {
            fuchsia_uri_rewrite::Rule::new(
                $host_match.to_owned(),
                $host_replacement.to_owned(),
                $path_prefix_match.to_owned(),
                $path_prefix_replacement.to_owned(),
            )
            .unwrap()
        };
    }

    pub(crate) fn make_dynamic_rule_config(rules: Vec<Rule>) -> tempfile::TempPath {
        let mut f = tempfile::NamedTempFile::new().unwrap();
        let config = RuleConfig::Version1(rules);
        serde_json::to_writer(f.as_file_mut(), &config).unwrap();
        f.into_temp_path()
    }

    #[test]
    fn test_empty_config() {
        let dynamic_config = make_dynamic_rule_config(vec![]);

        let manager = RewriteManager::load(&dynamic_config).unwrap();

        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![]);
    }

    #[test]
    fn test_load_single_rule() {
        let rules = vec![rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice")];

        let dynamic_config = make_dynamic_rule_config(rules.clone());
        let manager = RewriteManager::load(&dynamic_config).unwrap();

        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);
    }

    #[test]
    fn test_commit_additional_rule() {
        let existing_rule = rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice");
        let new_rule = rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/");

        let rules = vec![existing_rule.clone()];
        let dynamic_config = make_dynamic_rule_config(rules.clone());
        let mut manager = RewriteManager::load(&dynamic_config).unwrap();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);

        // Fork the existing state, add a rule, and verify both instances are distinct
        let new_rules = vec![new_rule.clone(), existing_rule.clone()];
        let mut transaction = manager.transaction();
        transaction.add(new_rule);
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);
        assert_eq!(transaction.list().cloned().collect::<Vec<_>>(), new_rules);

        // Commit the new rule set
        assert_eq!(manager.apply(transaction).unwrap(), ());
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), new_rules);

        // Ensure new rules are persisted to the dynamic config file
        let manager = RewriteManager::load(&dynamic_config).unwrap();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), new_rules);
    }

    #[test]
    fn test_erase_all_rules() {
        let rules = vec![
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice" => "/rolldice"),
            rule!("fuchsia.com" => "fuchsia.com", "/rolldice/" => "/rolldice/"),
        ];

        let dynamic_config = make_dynamic_rule_config(rules.clone());
        let mut manager = RewriteManager::load(&dynamic_config).unwrap();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);

        let mut transaction = manager.transaction();
        transaction.reset_all();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), rules);
        assert_eq!(transaction.list().cloned().collect::<Vec<_>>(), vec![]);

        assert_eq!(manager.apply(transaction).unwrap(), ());
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![]);

        let manager = RewriteManager::load(&dynamic_config).unwrap();
        assert_eq!(manager.list().cloned().collect::<Vec<_>>(), vec![]);
    }
}
