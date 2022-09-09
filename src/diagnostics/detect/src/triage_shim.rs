// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Interface to the Triage library.

use {
    crate::diagnostics::Selectors,
    anyhow::Error,
    fuchsia_triage::{ActionTagDirective, ParseResult, SnapshotTrigger},
    std::collections::HashMap,
};

pub fn evaluate_int_math(expression: &str) -> Result<i64, Error> {
    fuchsia_triage::evaluate_int_math(expression)
}

type ConfigFiles = HashMap<String, String>;

type DiagnosticData = fuchsia_triage::DiagnosticData;

pub struct TriageLib {
    triage_config: fuchsia_triage::ParseResult,
}

impl TriageLib {
    pub fn new(configs: ConfigFiles) -> Result<TriageLib, Error> {
        let triage_config = ParseResult::new(&configs, &ActionTagDirective::AllowAll)?;
        Ok(TriageLib { triage_config })
    }

    pub fn selectors(&self) -> Selectors {
        Selectors::new().with_inspect_selectors(fuchsia_triage::all_selectors(&self.triage_config))
    }

    pub fn evaluate(
        &self,
        data: Vec<DiagnosticData>,
    ) -> (Vec<SnapshotTrigger>, fuchsia_triage::WarningVec) {
        fuchsia_triage::snapshots(&data, &self.triage_config)
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

    #[fuchsia::test]
    fn library_calls_work() -> Result<(), Error> {
        let configs = hashmap! { "foo.triage".to_string() => CONFIG.to_string() };
        let lib = TriageLib::new(configs)?;
        let data = vec![DiagnosticData::new(
            "inspect.json".to_string(),
            fuchsia_triage::Source::Inspect,
            INSPECT.to_string(),
        )?];
        let expected_trigger =
            vec![SnapshotTrigger { signature: "got-it".to_string(), interval: 42_000 }];

        assert_eq!(
            lib.selectors().inspect_selectors,
            vec!["INSPECT:foo.cm:path/to:leaf".to_string()]
        );
        assert_eq!(lib.evaluate(data), (expected_trigger, vec![]));
        Ok(())
    }
}
