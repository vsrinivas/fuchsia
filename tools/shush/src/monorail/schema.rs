// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

// Proto definitions from:
// https://source.chromium.org/chromium/infra/infra/+/master:appengine/monorail/api/v3/api_proto/

macro_rules! impl_from_string {
    ($ty:ty, $field:ident) => {
        impl From<String> for $ty {
            fn from($field: String) -> Self {
                Self { $field }
            }
        }
    };
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum NotifyType {
    NotifyTypeUnspecified,
    Email,
    NoNotification,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct StatusValue {
    pub status: String,
}

impl_from_string!(StatusValue, status);

#[derive(Debug, Deserialize, Serialize)]
pub struct UserValue {
    pub user: String,
}

impl_from_string!(UserValue, user);

#[derive(Debug, Deserialize, Serialize)]
pub struct LabelValue {
    pub label: String,
}

impl_from_string!(LabelValue, label);

#[derive(Debug, Deserialize, Serialize)]
pub struct ComponentValue {
    pub component: String,
}

impl_from_string!(ComponentValue, component);

#[derive(Debug, Deserialize, Serialize)]
pub struct FieldValue {
    pub field: String,
    pub value: String,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct IssueRef {
    pub issue: String,
}

impl_from_string!(IssueRef, issue);

#[derive(Debug, Deserialize, Serialize)]
pub struct Issue {
    pub name: Option<String>,
    pub summary: Option<String>,
    pub status: Option<StatusValue>,
    pub owner: Option<UserValue>,
    pub cc_users: Option<Vec<UserValue>>,
    pub labels: Option<Vec<LabelValue>>,
    pub components: Option<Vec<ComponentValue>>,
    pub field_values: Option<Vec<FieldValue>>,
    pub merged_into_issue_ref: Option<IssueRef>,
    pub blocked_on_issue_refs: Option<Vec<IssueRef>>,
    pub blocking_issue_refs: Option<Vec<IssueRef>>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct MakeIssueRequest {
    pub parent: &'static str,
    pub issue: Issue,
    pub description: String,
    pub notify_type: NotifyType,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct IssueDelta {
    pub issue: Issue,
    pub update_mask: String,
    pub components_remove: Option<Vec<String>>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct ModifyIssuesRequest {
    pub deltas: Option<Vec<IssueDelta>>,
    pub notify_type: NotifyType,
    pub comment_content: Option<String>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct ListComponentDefsRequest {
    pub parent: String,
    pub page_size: i32,
    pub page_token: Option<String>,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum ComponentDefState {
    ComponentDefStateUnspecified,
    Deprecated,
    Active,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ComponentDef {
    pub name: String,
    pub value: String,
    pub docstring: Option<String>,
    pub admins: Option<Vec<String>>,
    pub ccs: Option<Vec<String>>,
    pub state: ComponentDefState,
    pub creator: Option<String>,
    pub modifier: Option<String>,
    pub create_time: Option<String>,
    pub modify_time: Option<String>,
    pub labels: Option<Vec<String>>,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ListComponentDefsResponse {
    pub component_defs: Vec<ComponentDef>,
    pub next_page_token: Option<String>,
}
