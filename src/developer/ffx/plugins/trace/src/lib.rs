// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_trace_args::{TraceCommand, TraceSubCommand, DEFAULT_CATEGORIES},
    ffx_writer::Writer,
    fidl_fuchsia_developer_bridge::{self as bridge, RecordingError, TracingProxy},
    fidl_fuchsia_tracing_controller::{ControllerProxy, KnownCategory, ProviderInfo, TraceConfig},
    serde::{Deserialize, Serialize},
    std::collections::HashSet,
    std::path::{Component, PathBuf},
};

// This is to make the schema make sense as this plugin can output one of these based on the
// subcommand. An alternative is to break this one plugin into multiple plugins each with their own
// output type. That is probably preferred but either way works.
#[derive(Debug, Deserialize, Serialize)]
#[serde(untagged)]
enum TraceOutput {
    ListCategories(Vec<TraceKnownCategory>),
    ListProviders(Vec<TraceProviderInfo>),
}

// These fields are arranged this way because deriving Ord uses field declaration order.
#[derive(Debug, Deserialize, Serialize, PartialOrd, Ord, PartialEq, Eq)]
struct TraceKnownCategory {
    /// The name of the category.
    name: String,
    /// Whether this category is returned by the GetKnownCategories FIDL call.
    known: bool,
    /// Whether this category is a default category used when starting a trace.
    default: bool,
    /// A short, possibly empty description of this category.
    description: String,
}

impl From<KnownCategory> for TraceKnownCategory {
    fn from(category: KnownCategory) -> Self {
        let default = DEFAULT_CATEGORIES.iter().any(|&c| c == category.name);
        Self { name: category.name, description: category.description, default, known: true }
    }
}

impl From<&'static str> for TraceKnownCategory {
    fn from(name: &'static str) -> Self {
        Self { name: name.to_string(), description: String::new(), known: false, default: true }
    }
}

#[derive(Debug, Deserialize, Serialize)]
struct TraceProviderInfo {
    id: Option<u32>,
    pid: Option<u64>,
    name: String,
}

impl From<ProviderInfo> for TraceProviderInfo {
    fn from(info: ProviderInfo) -> Self {
        Self {
            id: info.id,
            pid: info.pid,
            name: info.name.as_ref().cloned().unwrap_or("unknown".to_string()),
        }
    }
}

fn handle_fidl_error<T>(res: Result<T, fidl::Error>) -> Result<T> {
    res.map_err(|e| anyhow!(handle_peer_closed(e)))
}

fn handle_peer_closed(err: fidl::Error) -> errors::FfxError {
    match err {
        fidl::Error::ClientChannelClosed { status, protocol_name } => {
            errors::ffx_error!("An attempt to access {} resulted in a bad status: {}.
This can happen if tracing is not supported on the product configuration you are running or if it is missing from the base image.", protocol_name, status)
        }
        _ => {
            errors::ffx_error!("Accessing the tracing controller failed: {:#?}", err)
        }
    }
}

// OPTIMIZATION: Only grab a tracing controller proxy only when necessary.
#[ffx_plugin(
    TracingProxy = "daemon::service",
    ControllerProxy = "core/appmgr:out:fuchsia.tracing.controller.Controller"
)]
pub async fn trace(
    proxy: TracingProxy,
    controller: ControllerProxy,
    #[ffx(machine = TraceOutput)] writer: Writer,
    cmd: TraceCommand,
) -> Result<()> {
    match cmd.sub_cmd {
        TraceSubCommand::ListCategories(_) => {
            let categories = handle_fidl_error(controller.get_known_categories().await)?;
            if writer.is_machine() {
                let mut categories = categories
                    .into_iter()
                    .map(TraceKnownCategory::from)
                    .collect::<Vec<TraceKnownCategory>>();

                let names = categories.iter().map(|c| c.name.as_str()).collect::<HashSet<&str>>();
                let mut extra_categories = DEFAULT_CATEGORIES
                    .iter()
                    .filter_map(|&c| if !names.contains(c) { Some(c.into()) } else { None })
                    .collect::<Vec<_>>();
                categories.append(&mut extra_categories);
                categories.sort();

                writer.machine(&TraceOutput::ListCategories(categories))?;
            } else {
                writer.line("Known Categories:")?;
                for category in &categories {
                    writer.line(format!("- {} - {}", category.name, category.description))?;
                }
                writer.line("\nDefault Categories:")?;
                for category in DEFAULT_CATEGORIES {
                    writer.line(format!("- {}", category))?;
                }
            }
        }
        TraceSubCommand::ListProviders(_) => {
            let providers = handle_fidl_error(controller.get_providers().await)?;
            if writer.is_machine() {
                let providers = providers
                    .into_iter()
                    .map(TraceProviderInfo::from)
                    .collect::<Vec<TraceProviderInfo>>();
                writer.machine(&TraceOutput::ListProviders(providers))?;
            } else {
                writer.line("Trace providers:")?;
                for provider in &providers {
                    writer.line(format!(
                        "- {}",
                        provider.name.as_ref().unwrap_or(&"unknown".to_string())
                    ))?;
                }
            }
        }
        TraceSubCommand::Start(opts) => {
            let default: Option<String> = ffx_config::get("target.default").await.ok();
            let default = default.as_ref().and_then(|s| Some(s.as_str()));
            let trace_config = TraceConfig {
                buffer_size_megabytes_hint: Some(opts.buffer_size),
                categories: Some(opts.categories),
                ..TraceConfig::EMPTY
            };
            let output = canonical_path(opts.output)?;
            let res = proxy
                .start_recording(
                    default,
                    &output,
                    bridge::TraceOptions { duration: opts.duration, ..bridge::TraceOptions::EMPTY },
                    trace_config,
                )
                .await?;
            let target = handle_recording_result(res, &output).await?;
            writer.line(format!(
                "Tracing started successfully on \"{}\".\nWriting to {}",
                target.nodename.or(target.serial_number).as_deref().unwrap_or("<UNKNOWN>"),
                output
            ))?;
        }
        TraceSubCommand::Stop(opts) => {
            let output = canonical_path(opts.output)?;
            let res = proxy.stop_recording(&output).await?;
            let target = handle_recording_result(res, &output).await?;
            // TODO(awdavies): Make a clickable link that auto-uploads the trace file if possible.
            writer.line(format!(
                "Tracing stopped successfully on \"{}\".\nResults written to {}",
                target.nodename.or(target.serial_number).as_deref().unwrap_or("<UNKNOWN>"),
                output
            ))?;
            writer.line(format!("Upload to https://ui.perfetto.dev/#!/ to view."))?;
        }
    }
    Ok(())
}

async fn handle_recording_result(
    res: Result<bridge::Target, RecordingError>,
    output: &String,
) -> Result<bridge::Target> {
    let default: Option<String> = ffx_config::get("target.default").await.ok();
    match res {
        Ok(t) => Ok(t),
        Err(e) => match e {
            RecordingError::TargetProxyOpen => {
                ffx_bail!("Trace unable to open target proxy.");
            }
            RecordingError::RecordingAlreadyStarted => {
                // TODO(85098): Also return file info (which output file is being written to).
                ffx_bail!("Trace already started for target {}", default.unwrap_or("".to_owned()));
            }
            RecordingError::DuplicateTraceFile => {
                // TODO(85098): Also return target info.
                ffx_bail!("Trace already running for file {}", output);
            }
            RecordingError::RecordingStart => {
                let log_file: String = ffx_config::get("log.dir").await?;
                ffx_bail!(
                    "Error starting Fuchsia trace. See {}/ffx.daemon.log\n
Search for lines tagged with `ffx_daemon_service_tracing`. A common issue is a
peer closed error from `fuchsia.tracing.controller.Controller`. If this is the
case either tracing is not supported in the product configuration or the tracing
package is missing from the device's system image.",
                    log_file
                );
            }
            RecordingError::RecordingStop => {
                let log_file: String = ffx_config::get("log.dir").await?;
                ffx_bail!(
                    "Error stopping Fuchsia trace. See {}/ffx.daemon.log\n
Search for lines tagged with `ffx_daemon_service_tracing`. A common issue is a
peer closed error from `fuchsia.tracing.controller.Controller`. If this is the
case either tracing is not supported in the product configuration or the tracing
package is missing from the device's system image.",
                    log_file
                );
            }
            RecordingError::NoSuchTraceFile => {
                ffx_bail!("Could not stop trace. No active traces for {}.", output);
            }
        },
    }
}

fn canonical_path(output_path: String) -> Result<String> {
    let output_path = PathBuf::from(output_path);
    let mut path = PathBuf::new();
    if !output_path.has_root() {
        path.push(std::env::current_dir()?);
    }
    path.push(output_path);
    let mut components = path.components().peekable();
    let mut res = if let Some(c @ Component::Prefix(..)) = components.peek().cloned() {
        components.next();
        PathBuf::from(c.as_os_str())
    } else {
        PathBuf::new()
    };
    for component in components {
        match component {
            Component::Prefix(..) => return Err(anyhow!("prefix unreachable")),
            Component::RootDir => {
                res.push(component.as_os_str());
            }
            Component::CurDir => {}
            Component::ParentDir => {
                res.pop();
            }
            Component::Normal(c) => {
                res.push(c);
            }
        }
    }
    res.into_os_string()
        .into_string()
        .map_err(|e| anyhow!("unable to convert OsString to string {:?}", e))
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        errors::ResultExt as _,
        ffx_trace_args::{ListCategories, ListProviders, Start, Stop},
        ffx_writer::Format,
        fidl::endpoints::{ControlHandle, Responder},
        fidl_fuchsia_developer_bridge as bridge, fidl_fuchsia_tracing_controller as trace,
        regex::Regex,
        std::matches,
    };

    #[test]
    fn test_canonical_path_has_root() {
        let p = canonical_path("what".to_string()).unwrap();
        let got = PathBuf::from(p);
        let got = got.components().next().unwrap();
        assert!(matches!(got, Component::RootDir));
    }

    #[test]
    fn test_canonical_path_cleans_dots() {
        let mut path = PathBuf::new();
        path.push(Component::RootDir);
        path.push("this");
        path.push(Component::ParentDir);
        path.push("that");
        path.push("these");
        path.push(Component::CurDir);
        path.push("what.txt");
        let got = canonical_path(path.into_os_string().into_string().unwrap()).unwrap();
        let mut want = PathBuf::new();
        want.push(Component::RootDir);
        want.push("that");
        want.push("these");
        want.push("what.txt");
        let want = want.into_os_string().into_string().unwrap();
        assert_eq!(want, got);
    }

    fn setup_fake_service() -> TracingProxy {
        setup_fake_proxy(|req| match req {
            bridge::TracingRequest::StartRecording { responder, .. } => responder
                .send(&mut Ok(bridge::Target {
                    nodename: Some("foo".to_owned()),
                    ..bridge::Target::EMPTY
                }))
                .expect("responder err"),
            bridge::TracingRequest::StopRecording { responder, .. } => responder
                .send(&mut Ok(bridge::Target {
                    nodename: Some("foo".to_owned()),
                    ..bridge::Target::EMPTY
                }))
                .expect("responder err"),
        })
    }

    fn setup_fake_controller_proxy() -> ControllerProxy {
        setup_fake_controller(|req| match req {
            trace::ControllerRequest::GetKnownCategories { responder, .. } => {
                let mut categories = fake_known_categories();
                responder.send(categories.iter_mut().by_ref()).expect("should respond");
            }
            trace::ControllerRequest::GetProviders { responder, .. } => {
                let mut providers = fake_provider_infos();
                responder.send(&mut providers.drain(..)).expect("should respond");
            }
            r => panic!("unsupported req: {:?}", r),
        })
    }

    fn fake_known_categories() -> Vec<trace::KnownCategory> {
        vec![
            trace::KnownCategory {
                name: String::from("input"),
                description: String::from("Input system"),
            },
            trace::KnownCategory {
                name: String::from("kernel"),
                description: String::from("All kernel trace events"),
            },
            trace::KnownCategory {
                name: String::from("kernel:arch"),
                description: String::from("Kernel arch events"),
            },
            trace::KnownCategory {
                name: String::from("kernel:ipc"),
                description: String::from("Kernel ipc events"),
            },
        ]
    }

    fn fake_trace_known_categories() -> Vec<TraceKnownCategory> {
        let mut categories =
            DEFAULT_CATEGORIES.iter().cloned().map(TraceKnownCategory::from).collect::<Vec<_>>();
        categories.sort();

        let mut fake_known =
            fake_known_categories().into_iter().map(TraceKnownCategory::from).collect::<Vec<_>>();
        // This inserts the data from fake_known_categories above into the vector of
        // DEFAULT_CATEGORIES in the right places to make the final output sorted by name.
        // fake_known[0] is input which is in the list already so we replace it
        let i = categories.iter().position(|c| c.name == fake_known[0].name).unwrap();
        categories[i] = fake_known.remove(0);
        // These three go before kernel:meta which is in the list, so we find the insertion point
        // and then put these three in place.
        let j = categories.iter().position(|c| c.name == "kernel:meta").unwrap();
        categories.insert(j, fake_known.remove(0));
        categories.insert(j + 1, fake_known.remove(0));
        categories.insert(j + 2, fake_known.remove(0));
        categories
    }

    fn fake_provider_infos() -> Vec<trace::ProviderInfo> {
        vec![
            trace::ProviderInfo {
                id: Some(42),
                name: Some("foo".to_string()),
                ..trace::ProviderInfo::EMPTY
            },
            trace::ProviderInfo {
                id: Some(99),
                pid: Some(1234567),
                name: Some("bar".to_string()),
                ..trace::ProviderInfo::EMPTY
            },
            trace::ProviderInfo { id: Some(2), ..trace::ProviderInfo::EMPTY },
        ]
    }

    fn fake_trace_provider_infos() -> Vec<TraceProviderInfo> {
        fake_provider_infos().into_iter().map(TraceProviderInfo::from).collect()
    }

    fn setup_closed_fake_controller_proxy() -> ControllerProxy {
        setup_fake_controller(|req| match req {
            trace::ControllerRequest::GetKnownCategories { responder, .. } => {
                responder.control_handle().shutdown();
            }
            trace::ControllerRequest::GetProviders { responder, .. } => {
                responder.control_handle().shutdown();
            }
            r => panic!("unsupported req: {:?}", r),
        })
    }

    async fn run_trace_test(cmd: TraceCommand, writer: Writer) {
        let proxy = setup_fake_service();
        let controller = setup_fake_controller_proxy();
        trace(proxy, controller, writer, cmd).await.unwrap();
    }

    fn default_categories_expected_output() -> String {
        std::iter::once("Default Categories:".to_string())
            .chain(DEFAULT_CATEGORIES.iter().map(|&c| format!("- {}", c)))
            .collect::<Vec<String>>()
            .join("\n")
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_categories() {
        let writer = Writer::new_test(None);
        run_trace_test(
            TraceCommand { sub_cmd: TraceSubCommand::ListCategories(ListCategories {}) },
            writer.clone(),
        )
        .await;
        let output = writer.test_output().unwrap();
        let known_categories_expected_output = "Known Categories:\n\
                   - input - Input system\n\
                   - kernel - All kernel trace events\n\
                   - kernel:arch - Kernel arch events\n\
                   - kernel:ipc - Kernel ipc events";
        let want = format!(
            "{}\n\n{}\n",
            known_categories_expected_output,
            default_categories_expected_output()
        );
        assert_eq!(want, output);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_categories_machine() {
        let writer = Writer::new_test(Some(Format::Json));
        run_trace_test(
            TraceCommand { sub_cmd: TraceSubCommand::ListCategories(ListCategories {}) },
            writer.clone(),
        )
        .await;
        let output = writer.test_output().unwrap();
        let want = serde_json::to_string(&fake_trace_known_categories()).unwrap();
        assert_eq!(want, output);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_categories_peer_closed() {
        let writer = Writer::new_test(None);
        let proxy = setup_fake_service();
        let controller = setup_closed_fake_controller_proxy();
        let cmd = TraceCommand { sub_cmd: TraceSubCommand::ListCategories(ListCategories {}) };
        let res = trace(proxy, controller, writer.clone(), cmd).await.unwrap_err();
        assert!(res.ffx_error().is_some());
        assert!(res.to_string().contains("This can happen if tracing is not"));
        assert!(writer.test_output().unwrap().is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_providers() {
        let writer = Writer::new_test(None);
        run_trace_test(
            TraceCommand { sub_cmd: TraceSubCommand::ListProviders(ListProviders {}) },
            writer.clone(),
        )
        .await;
        let output = writer.test_output().unwrap();
        let want = "Trace providers:\n\
                   - foo\n\
                   - bar\n\
                   - unknown\n"
            .to_string();
        assert_eq!(want, output);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_providers_peer_closed() {
        let writer = Writer::new_test(None);
        let proxy = setup_fake_service();
        let controller = setup_closed_fake_controller_proxy();
        let cmd = TraceCommand { sub_cmd: TraceSubCommand::ListProviders(ListProviders {}) };
        let res = trace(proxy, controller, writer.clone(), cmd).await.unwrap_err();
        assert!(res.ffx_error().is_some());
        assert!(res.to_string().contains("This can happen if tracing is not"));
        assert!(writer.test_output().unwrap().is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_providers_machine() {
        let writer = Writer::new_test(Some(Format::Json));
        run_trace_test(
            TraceCommand { sub_cmd: TraceSubCommand::ListProviders(ListProviders {}) },
            writer.clone(),
        )
        .await;
        let output = writer.test_output().unwrap();
        let want = serde_json::to_string(&fake_trace_provider_infos()).unwrap();
        assert_eq!(want, output);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start() {
        let writer = Writer::new_test(None);
        run_trace_test(
            TraceCommand {
                sub_cmd: TraceSubCommand::Start(Start {
                    buffer_size: 2,
                    categories: vec![],
                    duration: None,
                    output: "foo.txt".to_string(),
                }),
            },
            writer.clone(),
        )
        .await;
        let output = writer.test_output().unwrap();
        let regex_str = "Tracing started successfully on \"foo\".\nWriting to /([^/]+/)+?foo.txt";
        let want = Regex::new(regex_str).unwrap();
        assert!(want.is_match(&output), "\"{}\" didn't match regex /{}/", output, regex_str);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_stop() {
        let writer = Writer::new_test(None);
        run_trace_test(
            TraceCommand { sub_cmd: TraceSubCommand::Stop(Stop { output: "foo.txt".to_string() }) },
            writer.clone(),
        )
        .await;
        let output = writer.test_output().unwrap();
        let regex_str = "Tracing stopped successfully on \"foo\".\nResults written to /([^/]+/)+?foo.txt\nUpload to https://ui.perfetto.dev/#!/ to view.";
        let want = Regex::new(regex_str).unwrap();
        assert!(want.is_match(&output), "\"{}\" didn't match regex /{}/", output, regex_str);
    }
}
