// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use serde::{Deserialize, Serialize};
use std::{
    io::Write,
    path::PathBuf,
    process::{Command, Stdio},
};

pub mod schema;

pub trait Monorail {
    fn create_issue(&mut self, request: schema::MakeIssueRequest) -> Result<schema::Issue>;
    fn update_issues(&mut self, request: schema::ModifyIssuesRequest) -> Result<()>;
    fn list_component_defs(
        &mut self,
        request: schema::ListComponentDefsRequest,
    ) -> Result<schema::ListComponentDefsResponse>;
}

pub struct Mock {
    next_issue_id: usize,
    log_api: bool,
    prpc: Option<Prpc>,
}

impl Mock {
    pub fn new(log_api: bool, prpc: Option<Prpc>) -> Self {
        Self { next_issue_id: 0, log_api, prpc }
    }
}

impl Monorail for Mock {
    fn create_issue(&mut self, request: schema::MakeIssueRequest) -> Result<schema::Issue> {
        if self.log_api {
            println!("[mock] Filing issue {}:\n{:#?}", self.next_issue_id, request);
        }

        let name = format!("{}/issues/{}", request.parent, self.next_issue_id);
        self.next_issue_id += 1;

        Ok(schema::Issue { name: Some(name), ..request.issue })
    }

    fn update_issues(&mut self, request: schema::ModifyIssuesRequest) -> Result<()> {
        if self.log_api {
            println!("[mock] Updating issues:\n{:#?}", request);
        }

        Ok(())
    }

    fn list_component_defs(
        &mut self,
        request: schema::ListComponentDefsRequest,
    ) -> Result<schema::ListComponentDefsResponse> {
        if self.log_api {
            println!("[mock] Listing component defs:\n{:#?}", request);
        }

        if let Some(ref mut prpc) = self.prpc {
            prpc.list_component_defs(request)
        } else {
            Ok(schema::ListComponentDefsResponse {
                component_defs: Vec::new(),
                next_page_token: None,
            })
        }
    }
}

pub struct Prpc {
    binary_path: PathBuf,
    api_server: String,
}

impl Prpc {
    pub fn new(binary_path: PathBuf, api_server: String) -> Self {
        Self { binary_path, api_server }
    }

    pub fn call<I, O>(&self, name: &str, input: &I) -> Result<O>
    where
        I: Serialize,
        O: for<'de> Deserialize<'de>,
    {
        let mut prpc = Command::new(&self.binary_path)
            .args(["call", "-use-id-token", &self.api_server, name])
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .spawn()?;

        let prpc_stdin = prpc.stdin.as_mut().unwrap();
        let body = serde_json::to_string(input)?;
        prpc_stdin.write_all(body.as_bytes())?;

        let output = prpc.wait_with_output()?;

        Ok(serde_json::from_slice(&output.stdout)?)
    }
}

impl Monorail for Prpc {
    fn create_issue(&mut self, request: schema::MakeIssueRequest) -> Result<schema::Issue> {
        self.call("monorail.v3.Issues.MakeIssue", &request)
    }

    fn update_issues(&mut self, request: schema::ModifyIssuesRequest) -> Result<()> {
        self.call("monorail.v3.Issues.ModifyIssues", &request)
    }

    fn list_component_defs(
        &mut self,
        request: schema::ListComponentDefsRequest,
    ) -> Result<schema::ListComponentDefsResponse> {
        self.call("monorail.v3.Projects.ListComponentDefs", &request)
    }
}
