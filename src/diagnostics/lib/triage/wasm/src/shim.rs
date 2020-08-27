use {
    anyhow::{bail, Error},
    num_traits::cast::FromPrimitive,
    std::collections::HashMap,
    triage::{
        analyze, ActionResultFormatter, ActionTagDirective, DiagnosticData, ParseResult, Source,
    },
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
        let target = Target::new(name.to_string(), Source::Inspect, content.to_string())?;
        Ok(self.insert(Value::Target(target)))
    }

    pub fn build_target(
        &mut self,
        name: &str,
        source_int: u32,
        content: &str,
    ) -> Result<Handle, Error> {
        let source = match Source::from_u32(source_int) {
            Some(source) => source,
            None => bail!("Source index {} not supported", source_int),
        };
        let target = Target::new(name.to_string(), source, content.to_string())?;
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
        let results_formatter = ActionResultFormatter::new(&results);
        Ok(results_formatter.to_text())
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
    use pretty_assertions::assert_eq;

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

    const DISK_CONFIG: &str = r#"{
        select: {
          total: "INSPECT:bootstrap/fshost:root/data_stats/stats:total_bytes",
          used: "INSPECT:bootstrap/fshost:root/data_stats/stats:used_bytes"
        },
        eval: {
          ratio: "used / (total + 0.0)",
          disk98: "ratio > 0.98"
        },
        act: {
          disk_full: {
            type: "Warning",
            trigger: "disk98",
            print: "Disk is >98% full"
          }
        }
    }"#;

    const INSPECT_CONTENT: &str = r#"[
        {
            "moniker": "bootstrap/fshost",
            "payload": {
                "root": {
                    "data_stats": {
                        "stats": {
                            "total_bytes": 120057757696,
                            "used_bytes": 704512
                        }
                    }
                }
            }
        }]"#;

    const SYSLOG_CONTENT: &str = r#"[00009.433][13936][13938][pkg-resolver] INFO: opening rewrite transaction
        [00009.434][13936][13938][pkg-resolver] INFO: rewrite transaction committed
        [00010.247][00000][00000][klog] INFO: memory-pressure: memory availability state - Normal
        [00031.618][13936][13938][pkg-resolver, tuf::metadata] WARN: Key ID KeyId("8bcc00bc3575eea5fb608dea6a521845d4f287d2fa80baa40a902f1f3ec7911e") was not found in the set of authorized keys.
        [00031.633][13936][13938][pkg-resolver] INFO: AutoClient for "http://[fe80::2438:dbff:fe0b:afe1%25ethp0003]:8083/auto" connecting.
        "#;

    const KLOG_CONTENT: &str = r#"[00005.453] 22815.22817> mDNS: Verifying uniqueness of host name step-atom-yard-juicy.local.
        [00005.454] 22815.22817> Starting mDNS on interface ethp0002 fe80::5054:ff:fe63:5e7a using port 5353
        [00005.455] 20259.20572> [00005.455615][3937501459][0][netstack] INFO: socket_conv.go(828): unimplemented setsockopt: SOL_IPV6 name=16 optVal=ff000000
        [00005.455] 20259.25153> [00005.455904][3937501459][0][netstack] INFO: socket_conv.go(828): unimplemented setsockopt: SOL_IPV6 name=52 optVal=01000000
        [00006.224] 22815.22817> mDNS: Using unique host name step-atom-yard-juicy.local.
        [00010.247] 00000.00000> memory-pressure: memory availability state - Normal"#;

    const BOOTLOG_CONTENT: &str = r#"[00009.431][13936][13938][pkg-resolver] INFO: inserting repository Some("fuchsia-pkg://devhost")
        [00010.247][00000][00000][klog] INFO: memory-pressure: memory availability state - Normal
        [00031.618][13936][13938][pkg-resolver, tuf::metadata] WARN: Key ID KeyId("8bcc00bc3575eea5fb608dea6a521845d4f287d2fa80baa40a902f1f3ec7911e") was not found in the set of authorized keys.
        [00031.633][13936][13938][pkg-resolver] INFO: AutoClient for "http://[fe80::2438:dbff:fe0b:afe1%25ethp0003]:8083/auto" connecting.
        "#;

    const ANNOTATIONS_CONTENT: &str = r#"{
        "build.board": "chromebook-x64",
        "build.is_debug": "true",
        "build.latest-commit-date": "2020-06-11T22:12:48+00:00",
        "build.product": "",
        "build.version": "2020-06-11T22:12:48+00:00",
        "device.board-name": "Standard PC (Q35 + ICH9, 2009)",
        "device.feedback-id": "f94a2bd4-ea1d-42df-b4e2-b1c655298b30",
        "device.uptime": "0d0h0m39s",
        "device.utc-time": "2020-06-17 00:10:57 GMT",
        "hardware.product.manufacturer": "default-manufacturer",
        "hardware.product.model": "default-model",
        "hardware.product.name": "default-fuchsia",
        "system.last-reboot.reason": "cold",
        "system.update-channel.current": "fuchsia.com"
    }"#;

    const SYS_K_CONFIG: &str = r#"{
        act: {
          k_mDNS_ok: {
            type: "Warning",
            trigger: "Not(KlogHas('Starting mDNS on interface'))",
            print: "mDNS not started"
          },
          sys_rewrite_ok: {
            type: "Warning",
            trigger: "Not(SyslogHas('rewrite transaction committed'))",
            print: "rewrite not committed"
          },

        }
    }"#;

    const BOOT_CONFIG: &str = r#"{
        act: {
            boot_devhost: {
                type: "Warning",
                trigger: "Not(BootlogHas('inserting repository.*devhost'))",
                print: "devhost not inserted"
            }
        }
    }"#;

    const ANNOTATIONS_CONFIG: &str = r#"{
        act: {
            boot_devhost: {
                type: "Warning",
                trigger: "Not(Annotation('system.update-channel.current') == 'fuchsia.com')",
                print: "Update channel not Fuchsia"
            }
        }
    }"#;

    const OK_PLUGINS_PREFIX: &str = "Process Crashes Plugin - OK\n";

    #[test]
    fn analyze() {
        let mut manager = TriageManager::new();
        let inspect_configs = map! {
            "disk" => DISK_CONFIG
        };
        let context = manager.build_context(inspect_configs).unwrap();
        let inspect_target = manager.build_inspect_target("inspect.json", INSPECT_CONTENT).unwrap();
        let determination = manager.analyze(&[inspect_target], context).unwrap();

        assert_eq!(
            determination,
            OK_PLUGINS_PREFIX.to_owned() + "No actions were triggered. All targets OK."
        );
    }

    fn targets_without_boot(manager: &mut TriageManager) -> Box<[i32]> {
        let inspect_target =
            manager.build_target("inspect.json", Source::Inspect as u32, INSPECT_CONTENT).unwrap();
        let k_target = manager.build_target("klog", Source::Klog as u32, KLOG_CONTENT).unwrap();
        let sys_target =
            manager.build_target("syslog", Source::Syslog as u32, SYSLOG_CONTENT).unwrap();
        let annotations_target =
            manager.build_target("anno", Source::Annotations as u32, ANNOTATIONS_CONTENT).unwrap();
        Box::new([inspect_target, k_target, sys_target, annotations_target])
    }

    fn targets_with_boot(manager: &mut TriageManager) -> Box<[i32]> {
        let boot_target =
            manager.build_target("bootlog", Source::Bootlog as u32, BOOTLOG_CONTENT).unwrap();
        let t = targets_without_boot(manager);
        assert_eq!(t.len(), 4);
        Box::new([t[0], t[1], t[2], t[3], boot_target])
    }

    fn targets_only_log(manager: &mut TriageManager, syslog: &str) -> Box<[i32]> {
        let sys_target = manager.build_target("syslog", Source::Syslog as u32, syslog).unwrap();
        Box::new([sys_target])
    }

    fn context_without_boot(manager: &mut TriageManager) -> i32 {
        let configs_without_boot = map! {
            "disk" => DISK_CONFIG,
            "log_sys_k" => SYS_K_CONFIG,
            "annotations" => ANNOTATIONS_CONFIG
        };
        manager.build_context(configs_without_boot).unwrap()
    }

    fn context_with_boot(manager: &mut TriageManager) -> i32 {
        let configs_with_boot = map! {
            "disk" => DISK_CONFIG,
            "log_sys_k" => SYS_K_CONFIG,
            "log_boot" => BOOT_CONFIG,
            "annotations" => ANNOTATIONS_CONFIG
        };
        manager.build_context(configs_with_boot).unwrap()
    }

    fn empty_context(manager: &mut TriageManager) -> i32 {
        manager.build_context(HashMap::new()).unwrap()
    }

    #[test]
    fn analyze_all_files() {
        // Make sure everything works properly with and without previous-bootlog.
        // We need to rebuild targets and context each time because analyze()
        // consumes them.
        let mut manager = TriageManager::new();
        let targets;
        let context;
        {
            // I'd like to do:
            //   manager.analyze(&targets_with_boot(&mut manger), context_with_boot(&mut manager))
            // But the compiler complains if I do a '&mut manager' borrow in the same
            // scope as a manger.analyze (which uses mut manager). But I can do
            // two &mut manager borrows in the same block - as long as I do put
            // them in a block. Weird.
            targets = targets_with_boot(&mut manager);
            context = context_with_boot(&mut manager);
        }
        assert_eq!(
            manager.analyze(&targets, context).unwrap(),
            OK_PLUGINS_PREFIX.to_owned() + "No actions were triggered. All targets OK."
        );
        let targets;
        let context;
        {
            targets = targets_without_boot(&mut manager);
            context = context_without_boot(&mut manager);
        }
        assert_eq!(
            manager.analyze(&targets, context).unwrap(),
            OK_PLUGINS_PREFIX.to_owned() + "No actions were triggered. All targets OK."
        );
        let targets;
        let context;
        {
            targets = targets_with_boot(&mut manager);
            context = context_without_boot(&mut manager);
        }
        assert_eq!(
            manager.analyze(&targets, context).unwrap(),
            OK_PLUGINS_PREFIX.to_owned() + "No actions were triggered. All targets OK."
        );
        // For this last test, bootlog is effectively empty, so the bootlog test will fail.
        let targets;
        let context;
        {
            targets = targets_without_boot(&mut manager);
            context = context_with_boot(&mut manager);
        }
        assert_eq!(
            manager.analyze(&targets, context).unwrap() + "\n",
            "Warnings\n--------\n[WARNING] devhost not inserted.\n\n".to_owned()
                + OK_PLUGINS_PREFIX
        );

        let targets;
        let context;
        {
            targets = targets_only_log(
                &mut manager,
                r#"
                [3661.123] fatal : my_component.cmx[1234]
                [3661.124] CRASH: my_component.cmx[1235]
                "#,
            );
            context = empty_context(&mut manager);
        }
        assert_eq!(
            manager.analyze(&targets, context).unwrap(),
            r#"Process Crashes Plugin
Warnings
--------
[WARNING]: my_component.cmx crashed at 1h1m1.123s [3661.123]
[WARNING]: my_component.cmx crashed at 1h1m1.124s [3661.124]
"#
        );
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
                "moniker": "bootstrap/fshost",
                "payload": {
                    "root": {
                        "data_stats": {
                            "stats": {
                                "total_bytes": 120057757696,
                                "used_bytes": 704512
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
