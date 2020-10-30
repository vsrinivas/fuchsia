// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Interface to the Triage library.

use {
    crate::diagnostics::Selectors,
    anyhow::Error,
    std::collections::HashMap,
    triage::{ActionTagDirective, ParseResult, SnapshotTrigger},
};

pub fn evaluate_int_math(expression: &str) -> Result<i64, Error> {
    triage::evaluate_int_math(expression)
}

type ConfigFiles = HashMap<String, String>;

type DiagnosticData = triage::DiagnosticData;

pub struct TriageLib {
    triage_config: triage::ParseResult,
}

impl TriageLib {
    pub fn new(configs: ConfigFiles) -> Result<TriageLib, Error> {
        let triage_config = ParseResult::new(&configs, &ActionTagDirective::AllowAll)?;
        Ok(TriageLib { triage_config })
    }

    pub fn selectors(&self) -> Selectors {
        Selectors::new().with_inspect_selectors(triage::all_selectors(&self.triage_config))
    }

    pub fn evaluate(&self, data: Vec<DiagnosticData>) -> Vec<SnapshotTrigger> {
        triage::snapshots(&data, &self.triage_config)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use maplit::hashmap;

    const CONFIG: &str = r#"{
            select: {
                foo: "INSPECT:foo.cm:path/to:leaf",
            },
            act: {
                yes: {
                    type: "Snapshot",
                    trigger: "foo==8",
                    repeat: "Micros(42)",
                    signature: "got-it",
                },
            },
        }"#;

    const INSPECT: &str = r#"[
        {
            "moniker": "foo.cm",
            "payload": {"path": {"to": {"leaf": 8}}}
        }
    ]"#;

    #[test]
    fn library_calls_work() -> Result<(), Error> {
        fuchsia_syslog::init_with_tags(&["detect"]).unwrap();
        let configs = hashmap! { "foo.config".to_string() => CONFIG.to_string() };
        let lib = TriageLib::new(configs)?;
        let data = vec![DiagnosticData::new(
            "inspect.json".to_string(),
            triage::Source::Inspect,
            INSPECT.to_string(),
        )?];
        let expected_trigger =
            vec![SnapshotTrigger { signature: "got-it".to_string(), interval: 42_000 }];

        assert_eq!(
            lib.selectors().inspect_selectors,
            vec!["INSPECT:foo.cm:path/to:leaf".to_string()]
        );
        assert_eq!(lib.evaluate(data), expected_trigger);
        Ok(())
    }
}
