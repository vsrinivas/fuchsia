use {
    anyhow::{bail, Error},
    std::collections::HashMap,
    triage::{analyze, ActionResultFormatter, ActionTagDirective, DiagnosticData, ParseResult},
};

/// Unique identifier to resources too expensive to pass between Rust/JS layer.
pub type Handle = i32;

/// Temporary type aliases until Stage 2 API releases.
type Context = ParseResult;
type Target = DiagnosticData;

enum Value {
    Context(Context),
    Target(Target),
}

/// Object to manage lifetime of objects needed for Triage analysis.
pub struct TriageManager {
    next_unique_handle_id: Handle,
    values: HashMap<Handle, Value>,
}

macro_rules! take_value {
    ($self:ident, $id:expr, $type:path) => {{
        let value = $self.values.remove($id);
        if value.is_none() {
            bail!("No value found for handle: {}", $id);
        }
        match value.unwrap() {
            $type(typed_value) => typed_value,
            _ => {
                bail!("No value found for handle: {}", $id);
            }
        }
    }};
}

impl TriageManager {
    pub fn new() -> TriageManager {
        TriageManager { next_unique_handle_id: 0, values: HashMap::new() }
    }

    pub fn build_context(&mut self, configs: HashMap<String, String>) -> Result<Handle, Error> {
        let context = Context::new(&configs, &ActionTagDirective::AllowAll)?;
        Ok(self.insert(Value::Context(context)))
    }

    pub fn build_inspect_target(&mut self, name: &str, content: &str) -> Result<Handle, Error> {
        let target = Target::new(name.to_string(), content.to_string())?;
        Ok(self.insert(Value::Target(target)))
    }

    pub fn analyze(
        &mut self,
        target_handles: &[Handle],
        context_handle: Handle,
    ) -> Result<String, Error> {
        let context: Context = take_value!(self, &context_handle, Value::Context);

        let mut targets: Vec<Target> = Vec::new();
        for handle in target_handles.iter() {
            let target: Target = take_value!(self, &handle, Value::Target);
            targets.push(target);
        }

        let results = analyze(&targets, &context)?;
        let results_formatter = ActionResultFormatter::new(results.iter().collect());
        Ok(results_formatter.to_warnings())
    }

    fn insert(&mut self, value: Value) -> Handle {
        self.values.insert(self.next_unique_handle_id, value);
        let id = self.next_unique_handle_id;
        self.next_unique_handle_id += 1;
        id
    }
}

#[cfg(test)]
mod test {
    use super::*;

    macro_rules! map(
        { $($key:expr => $value:expr),+ } => {
            {
                let mut m = ::std::collections::HashMap::new();
                $(
                    m.insert($key.to_owned(), $value.to_owned());
                )+
                m
            }
         };
    );

    #[test]
    fn analyze() {
        let mut manager = TriageManager::new();
        let configs = map! {
            "disk" => r#"{
                select: {
                  total: "INSPECT:archivist.cmx:root/data_stats/global_data/stats:total_bytes",
                  used: "INSPECT:archivist.cmx:root/data_stats/global_data/stats:used_bytes"
                },
                eval: {
                  ratio: "used / (total + 0.0)",
                  disk98: "ratio > 0.98"
                },
                act: {
                  disk_full: {
                    trigger: "disk98",
                    print: "Disk is >98% full"
                  }
                }
            }"#
        };
        let content = r#"[
            {
                "path": "/hub/c/archivist.cmx/13141/out/global_data/storage_stats.inspect",
                "contents": {
                    "root": {
                        "data_stats": {
                            "global_data": {
                                "stats": {
                                    "total_bytes": 120057757696,
                                    "used_bytes": 704512
                                }
                            }
                        }
                    }
                }
            }]"#;

        let context = manager.build_context(configs).unwrap();
        let target = manager.build_inspect_target("inspect.json", content).unwrap();
        let determination = manager.analyze(&[target], context).unwrap();

        assert_eq!(determination, "No actions were triggered. All targets OK.");
    }

    #[test]
    fn unique_ids_are_generated_for_every_call_to_build_fns() {
        let mut manager = TriageManager::new();
        let configs = map! {
            "disk" => r#"{
                select: {},
                eval: {},
                act: {}
            }"#
        };
        let content = r#"[
            {
                "path": "/hub/c/archivist.cmx/13141/out/global_data/storage_stats.inspect",
                "contents": {
                    "root": {
                        "data_stats": {
                            "global_data": {
                                "stats": {
                                    "total_bytes": 120057757696,
                                    "used_bytes": 704512
                                }
                            }
                        }
                    }
                }
            }]"#;

        assert_ne!(
            manager.build_context(configs.clone()).unwrap(),
            manager.build_context(configs.clone()).unwrap()
        );
        assert_ne!(
            manager.build_inspect_target("filename_1", content).unwrap(),
            manager.build_inspect_target("filename_2", content).unwrap()
        );
    }

    #[test]
    fn returns_error_on_bad_input() {
        let mut manager = TriageManager::new();

        assert_eq!(
            manager
                .build_context(map! {
                    "disk" => "poorly formatted config"
                })
                .is_err(),
            true
        );
        assert_eq!(
            manager.build_inspect_target("inspect.json", "poorly formatted inspect").is_err(),
            true
        );
    }

    #[test]
    fn analyze_returns_error_on_unknown_input() {
        let mut manager = TriageManager::new();

        assert_eq!(manager.analyze(&[100], 500).is_err(), true);
    }
}
