// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use once_cell::sync::OnceCell;
use serde::{Deserialize, Serialize};

use std::collections::HashMap;
use std::fmt::Write;

use crate::{
    lint::{Lint, LintFile},
    monorail::{schema, Monorail},
    owners::FileOwnership,
};

struct ComponentDefs {
    defs: HashMap<String, u64>,
}

impl ComponentDefs {
    fn load(monorail: &mut (impl Monorail + ?Sized)) -> Result<Self> {
        let mut defs = HashMap::new();

        let mut page_token = None;
        loop {
            let response = monorail.list_component_defs(schema::ListComponentDefsRequest {
                parent: "projects/fuchsia".to_string(),
                page_size: 100,
                page_token,
            })?;
            for component_def in response.component_defs {
                let id = component_def
                    .name
                    .strip_prefix("projects/fuchsia/componentDefs/")
                    .unwrap()
                    .parse()
                    .unwrap();
                defs.insert(component_def.value, id);
            }
            page_token = response.next_page_token;
            if page_token.is_none() {
                break;
            }
        }

        Ok(Self { defs })
    }

    fn get(&self, component: &str) -> Option<String> {
        self.defs.get(component).map(|id| format!("projects/fuchsia/componentDefs/{id}"))
    }
}

pub struct IssueTemplate<'a> {
    // Settings
    filter: &'a [String],
    codesearch_tag: Option<&'a str>,
    template: Option<String>,
    blocking_issue: Option<&'a str>,
    labels: &'a [String],
    max_cc_users: usize,

    // Cache
    component_defs: OnceCell<ComponentDefs>,
}

const HOLDING_COMPONENT: &'static str = "Rust>tools>Shush>Rollout";

impl<'a> IssueTemplate<'a> {
    pub fn new(
        filter: &'a [String],
        codesearch_tag: Option<&'a str>,
        template: Option<String>,
        blocking_issue: Option<&'a str>,
        labels: &'a [String],
        max_cc_users: usize,
    ) -> Self {
        Self {
            filter,
            codesearch_tag,
            template,
            blocking_issue,
            labels,
            max_cc_users,

            component_defs: OnceCell::new(),
        }
    }

    fn format_lints(&self, mut description: String, file: &LintFile) -> String {
        let mut insertions =
            file.lints.iter().map(|l| self.annotation_msg(l, &file.path)).collect::<Vec<_>>();
        insertions.sort();

        write!(
            &mut description,
            "\n[{}]({})\n{}\n",
            file.path,
            self.codesearch_url(&file.path, None),
            insertions.join("\n"),
        )
        .unwrap();

        description
    }

    fn annotation_msg(&self, l: &Lint, path: &str) -> String {
        let lints_url = "https://rust-lang.github.io/rust-clippy/master#".to_owned();
        let cs_url = self.codesearch_url(path, Some(l.span.start.line));

        // format both clippy and normal rustc lints in markdown
        if let Some(name) = l.name.strip_prefix("clippy::") {
            format!(
                "- [{name}]({}) on [line {}]({})",
                &(lints_url + name),
                l.span.start.line,
                cs_url
            )
        } else {
            format!("- {} on [line {}]({})", l.name, l.span.start.line, cs_url)
        }
    }

    fn codesearch_url(&self, path: &str, line: Option<usize>) -> String {
        let mut link = format!(
            "https://cs.opensource.google/fuchsia/fuchsia/+/{}:{}",
            self.codesearch_tag.unwrap_or("main"),
            path
        );
        if let Some(line) = line {
            link.push_str(&format!(";l={}", line))
        }
        link
    }

    fn get_component_defs(
        &self,
        monorail: &mut (impl Monorail + ?Sized),
    ) -> Result<&ComponentDefs> {
        self.component_defs.get_or_try_init(|| ComponentDefs::load(monorail))
    }

    pub fn create(
        &self,
        monorail: &mut (impl Monorail + ?Sized),
        ownership: &FileOwnership,
        files: &[LintFile],
    ) -> Result<Issue> {
        let component_defs = self.get_component_defs(monorail)?;

        let mut summary = "Please inspect ".to_string();
        match self.filter {
            [a] => write!(summary, "these {}", a)?,
            [a, b] => write!(summary, "these {} and {}", a, b)?,
            _ => write!(summary, "multiple new")?,
        }
        write!(summary, " lints")?;

        let mut components = None;
        let mut owner = None;
        let mut cc_users = None;

        if let Some(component) = &ownership.component {
            write!(summary, " in {}", component)?;
            if let Some(component) = component_defs.get(component) {
                components = Some(vec![schema::ComponentValue::from(component)]);
            } else {
                eprintln!("could not find component '{component}' in components map, skipping");
            }
        }

        if !ownership.owners.is_empty() {
            // A small price to pay for salvation
            owner =
                Some(schema::UserValue::from(format!("users/{}@google.com", ownership.owners[0])));

            if ownership.component.is_none() && ownership.owners.len() > 1 {
                cc_users = Some(
                    ownership
                        .owners
                        .iter()
                        // skip the owner
                        .skip(1)
                        .take(self.max_cc_users)
                        .map(|owner| {
                            schema::UserValue::from(format!("users/{}@google.com", owner.clone()))
                        })
                        .collect(),
                );
            }
        }

        let details = files.iter().fold(String::new(), |d, f| self.format_lints(d, f));
        let description = if let Some(ref template) = self.template {
            template.replace("INSERT_DETAILS_HERE", &details)
        } else {
            details
        };

        let blocking_issue_refs = self.blocking_issue.map(|issue| {
            vec![schema::IssueRef::from(format!("projects/fuchsia/issues/{}", issue))]
        });

        // Ensure RVG label present.
        let mut label_vec = self.labels.to_vec();
        let rvg_label = "Restrict-View-Google".to_string();
        if !label_vec.contains(&rvg_label) {
            label_vec.push(rvg_label);
        }

        let labels =
            Some(label_vec.iter().cloned().map(schema::LabelValue::from).collect::<Vec<_>>());

        let holding_component = component_defs.get(HOLDING_COMPONENT).expect("couldn't find holding component definition, you may need to rerun with --prpc to fetch component definitions");
        let request = schema::MakeIssueRequest {
            parent: "projects/fuchsia",
            issue: schema::Issue {
                name: None,
                summary: Some(summary),
                status: Some(schema::StatusValue::from("NeedsInfo".to_string())),
                owner: None,
                cc_users: None,
                labels,
                components: Some(vec![schema::ComponentValue::from(holding_component)]),
                field_values: None,
                merged_into_issue_ref: None,
                blocked_on_issue_refs: None,
                blocking_issue_refs,
            },
            description,
            notify_type: schema::NotifyType::NotifyTypeUnspecified,
        };

        let issue = monorail.create_issue(request)?;
        // The returned issue name is of the form "fuchsia/issues/{number}".
        let id = issue.name.unwrap().split('/').last().unwrap().parse()?;

        Ok(Issue { id, components, owner, cc_users })
    }
}

#[derive(Deserialize, Serialize)]
pub struct Issue {
    id: usize,
    components: Option<Vec<schema::ComponentValue>>,
    owner: Option<schema::UserValue>,
    cc_users: Option<Vec<schema::UserValue>>,
}

impl Issue {
    fn update_from_deltas(
        monorail: &mut (impl Monorail + ?Sized),
        deltas: Vec<schema::IssueDelta>,
    ) -> Result<()> {
        monorail.update_issues(schema::ModifyIssuesRequest {
            deltas: Some(deltas),
            notify_type: schema::NotifyType::Email,
            comment_content: None,
        })
    }

    pub fn id(&self) -> usize {
        self.id
    }

    pub fn rollout(
        mut issues: Vec<Self>,
        monorail: &mut (impl Monorail + ?Sized),
        verbose: bool,
    ) -> Result<()> {
        let component_defs = ComponentDefs::load(monorail)?;

        // Monorail modification requests are batched up in groups of up to 100 modifications.
        let mut deltas = Vec::new();
        let issues_len = issues.len();
        for (i, issue) in issues.drain(..).enumerate() {
            if verbose {
                println!("[{i}/{issues_len}] Rolling out fxbug.dev/{}", issue.id());
            }
            let mut update_mask = "status,components".to_string();
            if issue.owner.is_some() {
                update_mask += ",owner";
            }
            if issue.cc_users.is_some() {
                update_mask += ",cc_users";
            }

            deltas.push(schema::IssueDelta {
                issue: schema::Issue {
                    name: Some(format!("projects/fuchsia/issues/{}", issue.id())),
                    summary: None,
                    status: Some(schema::StatusValue::from("Available".to_string())),
                    owner: issue.owner,
                    cc_users: issue.cc_users,
                    labels: None,
                    components: issue.components,
                    field_values: None,
                    merged_into_issue_ref: None,
                    blocked_on_issue_refs: None,
                    blocking_issue_refs: None,
                },
                update_mask,
                components_remove: Some(vec![component_defs.get(HOLDING_COMPONENT).unwrap()]),
            });
            if deltas.len() >= 100 {
                let deltas = ::core::mem::replace(&mut deltas, Vec::new());
                Self::update_from_deltas(monorail, deltas)?;
            }
        }
        if !deltas.is_empty() {
            Self::update_from_deltas(monorail, deltas)?;
        }
        Ok(())
    }
}
