// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    ffx_core::ffx_plugin,
    ffx_trace_args::{TraceCommand, TraceSubCommand},
    fidl_fuchsia_developer_bridge::{self as bridge, TracingProxy},
    fidl_fuchsia_tracing_controller::{ControllerProxy, TraceConfig},
    std::io::{stdout, Write},
    std::path::{Component, PathBuf},
};

// OPTIMIZATION: Only grab a tracing controller proxy only when necessary.
#[ffx_plugin(
    TracingProxy = "daemon::service",
    ControllerProxy = "core/appmgr:out:fuchsia.tracing.controller.Controller"
)]
pub async fn trace(
    proxy: TracingProxy,
    controller: ControllerProxy,
    cmd: TraceCommand,
) -> Result<()> {
    trace_impl(proxy, controller, cmd, Box::new(stdout())).await
}

async fn trace_impl<W: Write>(
    proxy: TracingProxy,
    controller: ControllerProxy,
    cmd: TraceCommand,
    mut write: W,
) -> Result<()> {
    let default: Option<String> = ffx_config::get("target.default").await.ok();
    let default = default.as_ref().and_then(|s| Some(s.as_str()));
    match cmd.sub_cmd {
        TraceSubCommand::ListProviders(_) => {
            let providers = match controller.get_providers().await {
                Ok(providers) => providers,
                Err(fidl::Error::ClientChannelClosed { status, protocol_name }) => {
                    errors::ffx_bail!("An attempt to access {} resulted in a bad status: {}.
This can happen if tracing is not supported on the product configuration you are running or if it is missing from the base image.", protocol_name, status);
                }
                Err(e) => {
                    errors::ffx_bail!("Accessing the tracing controller failed: {:#?}", e);
                }
            };
            writeln!(write, "Trace providers:")?;
            for provider in &providers {
                writeln!(write, "- {}", provider.name.as_ref().unwrap_or(&"unknown".to_string()))?;
            }
        }
        TraceSubCommand::Start(opts) => {
            let trace_config = TraceConfig {
                buffer_size_megabytes_hint: Some(opts.buffer_size),
                categories: Some(opts.categories),
                ..TraceConfig::EMPTY
            };
            let output = canonical_path(opts.output)?;
            proxy
                .start_recording(
                    default,
                    &output,
                    bridge::TraceOptions { duration: opts.duration, ..bridge::TraceOptions::EMPTY },
                    trace_config,
                )
                .await?
                .map_err(|e| anyhow!("recording start error {:?}", e))?;
        }
        TraceSubCommand::Stop(opts) => {
            let output = canonical_path(opts.output)?;
            proxy
                .stop_recording(&output)
                .await?
                .map_err(|e| anyhow!("recording stop error {:?}", e))?;
        }
    }
    Ok(())
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
        ffx_trace_args::{ListProviders, Start, Stop},
        fidl::endpoints::{ControlHandle, Responder},
        fidl_fuchsia_developer_bridge as bridge, fidl_fuchsia_tracing_controller as trace,
        std::io::BufWriter,
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
            bridge::TracingRequest::StartRecording { responder, .. } => {
                responder.send(&mut Ok(())).expect("responder err")
            }
            bridge::TracingRequest::StopRecording { responder, .. } => {
                responder.send(&mut Ok(())).expect("responder err")
            }
        })
    }

    fn setup_fake_controller_proxy() -> ControllerProxy {
        setup_fake_controller(|req| match req {
            trace::ControllerRequest::GetProviders { responder, .. } => {
                let mut providers = vec![
                    trace::ProviderInfo {
                        id: Some(42),
                        name: Some("foo".to_string()),
                        ..trace::ProviderInfo::EMPTY
                    },
                    trace::ProviderInfo {
                        id: Some(99),
                        name: Some("bar".to_string()),
                        ..trace::ProviderInfo::EMPTY
                    },
                    trace::ProviderInfo { id: Some(2), ..trace::ProviderInfo::EMPTY },
                ];
                responder.send(&mut providers.drain(..)).expect("should respond");
            }
            r => panic!("unsupported req: {:?}", r),
        })
    }

    fn setup_closed_fake_controller_proxy() -> ControllerProxy {
        setup_fake_controller(|req| match req {
            trace::ControllerRequest::GetProviders { responder, .. } => {
                responder.control_handle().shutdown();
            }
            r => panic!("unsupported req: {:?}", r),
        })
    }

    async fn run_trace_test(cmd: TraceCommand) -> String {
        let mut output = String::new();
        let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
        let proxy = setup_fake_service();
        let controller = setup_fake_controller_proxy();
        trace_impl(proxy, controller, cmd, writer).await.unwrap();
        output
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_providers() {
        let output = run_trace_test(TraceCommand {
            sub_cmd: TraceSubCommand::ListProviders(ListProviders {}),
        })
        .await;
        let want = "Trace providers:\n\
                   - foo\n\
                   - bar\n\
                   - unknown\n"
            .to_string();
        assert_eq!(want, output);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_providers_peer_closed() {
        let mut output = String::new();
        let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
        let proxy = setup_fake_service();
        let controller = setup_closed_fake_controller_proxy();
        let cmd = TraceCommand { sub_cmd: TraceSubCommand::ListProviders(ListProviders {}) };
        let res = trace_impl(proxy, controller, cmd, writer).await.unwrap_err();
        assert!(res.ffx_error().is_some());
        assert!(res.to_string().contains("This can happen if tracing is not"));
        assert!(output.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start() {
        run_trace_test(TraceCommand {
            sub_cmd: TraceSubCommand::Start(Start {
                buffer_size: 2,
                categories: vec![],
                duration: None,
                output: "foo.txt".to_string(),
            }),
        })
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_stop() {
        run_trace_test(TraceCommand {
            sub_cmd: TraceSubCommand::Stop(Stop { output: "foo.txt".to_string() }),
        })
        .await;
    }
}
