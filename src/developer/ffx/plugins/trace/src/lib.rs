// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    async_fs::File,
    blocking::unblock,
    ffx_core::ffx_plugin,
    ffx_trace_args::{Record, TraceCommand, TraceSubCommand},
    fidl_fuchsia_tracing_controller::{
        ControllerProxy, StartOptions, StopOptions, TerminateOptions, TraceConfig,
    },
    futures::future::BoxFuture,
    futures::prelude::*,
    std::io::{stdin, stdout, BufRead, Stdin, Write},
    std::time::Duration,
};

// LineWaiter abstracts waiting for the user to press enter.  It is needed
// to unit test interactive mode.
trait LineWaiter<'a> {
    type LineWaiterFut: 'a + Future<Output = ()>;
    fn wait(&'a mut self) -> Self::LineWaiterFut;
}

impl<'a> LineWaiter<'a> for Stdin {
    type LineWaiterFut = BoxFuture<'a, ()>;

    fn wait(&'a mut self) -> Self::LineWaiterFut {
        unblock(|| {
            let mut line = String::new();
            // This is sort of a hack to avoid a borrow bound challenge with
            // self, though because this is explicitly implemented on the String
            // marker type there's a high confidence that it's ok. It is likley
            // that this can be refactored out a little, with the differently
            // shaped libraries, for example it may be possible to re-arrange to
            // wrap stdin().locked() in Unblock to get AsyncBufRead.
            let stdin = stdin();
            let mut locked = stdin.lock();
            // Ignoring error, though maybe Ack would want to bubble up errors instead?
            let _ = locked.read_line(&mut line);
        })
        .boxed()
    }
}

#[ffx_plugin(ControllerProxy = "core/appmgr:out:fuchsia.tracing.controller.Controller")]
pub async fn trace(controller_proxy: ControllerProxy, cmd: TraceCommand) -> Result<()> {
    match cmd.sub_cmd {
        TraceSubCommand::ListProviders(_sub_cmd) => {
            list_providers_cmd_impl(controller_proxy, Box::new(stdout())).await?
        }
        TraceSubCommand::Record(sub_cmd) => {
            record_cmd_impl(sub_cmd, controller_proxy, &mut stdin(), Box::new(stdout())).await?
        }
    }

    Ok(())
}

async fn list_providers_cmd_impl<W: Write>(
    controller_proxy: ControllerProxy,
    mut writer: W,
) -> Result<()> {
    let providers = controller_proxy.get_providers().await?;

    writeln!(&mut writer, "Trace providers:")?;
    for provider in &providers {
        writeln!(&mut writer, "- {}", provider.name.as_ref().unwrap_or(&"<unknown>".to_string()))?;
    }

    Ok(())
}

async fn record_trace<'a, L: LineWaiter<'a>, W: Write>(
    opts: Record,
    controller_proxy: ControllerProxy,
    server_socket: fidl::Socket,
    waiter: &'a mut L,
    mut writer: W,
) -> Result<()> {
    let trace_config = TraceConfig {
        buffer_size_megabytes_hint: Some(opts.buffer_size),
        categories: Some(opts.categories),
        ..TraceConfig::EMPTY
    };

    write!(writer, "initialize tracing\n")?;

    controller_proxy.initialize_tracing(trace_config, server_socket)?;

    write!(writer, "starting trace\n")?;

    controller_proxy
        .start_tracing(StartOptions::EMPTY)
        .await?
        .map_err(|e| anyhow!("Failed to start trace: {:?}", e))?;

    if let Some(duration) = &opts.duration {
        write!(writer, "waiting for {} seconds\n", duration)?;
        fuchsia_async::Timer::new(Duration::from_secs_f64(*duration)).await;
    } else {
        write!(writer, "press <enter> to stop trace\n")?;
        waiter.wait().await;
    }

    write!(writer, "stopping trace\n")?;

    controller_proxy
        .stop_tracing(StopOptions { write_results: Some(true), ..StopOptions::EMPTY })
        .await?;

    write!(writer, "terminating tracing\n")?;

    controller_proxy
        .terminate_tracing(TerminateOptions {
            write_results: Some(true),
            ..TerminateOptions::EMPTY
        })
        .await?;
    write!(writer, "Trace saved to {}. Use ui.perfetto.dev to visualize it.\n", &opts.output)?;

    Ok(())
}

async fn record_cmd_impl<'a, L: LineWaiter<'a>, W: Write>(
    opts: Record,
    controller_proxy: ControllerProxy,
    waiter: &'a mut L,
    writer: W,
) -> Result<()> {
    let (client_socket, server_socket) =
        fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed ot create socket")?;
    let client_socket = fidl::AsyncSocket::from_socket(client_socket)?;

    let f = File::create(&opts.output).await?;

    let t = fuchsia_async::Task::spawn(async move {
        let mut f = f;
        futures::io::copy(client_socket, &mut f).await
    });

    record_trace(opts, controller_proxy, server_socket, waiter, writer).await?;

    // The trace controller closes the socket on tracing termination.  This
    // task will complete when that happens.
    t.await.map_err(|e| anyhow!("Error while copying trace data: {}", e))?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_tracing_controller::{ControllerRequest, ProviderInfo, TerminateResult};
    use std::sync::{Arc, Mutex};

    struct NullWaiter {}

    impl<'a> LineWaiter<'a> for NullWaiter {
        type LineWaiterFut = BoxFuture<'a, ()>;

        fn wait(&'a mut self) -> Self::LineWaiterFut {
            async move { () }.boxed()
        }
    }

    const TEST_TRACE_OUTPUT: &'static [u8] = &[0x00, 0x55, 0xaa, 0xff];

    #[derive(Debug)]
    struct FakeControllerState {
        buffer_size: u32,
        categories: Vec<String>,
        output: Option<fidl::Socket>,
    }

    fn setup_fake_controller_server() -> (Arc<Mutex<FakeControllerState>>, ControllerProxy) {
        let state = Arc::new(Mutex::new(FakeControllerState {
            buffer_size: 0,
            categories: vec![],
            output: None,
        }));
        (
            state.clone(),
            setup_fake_controller_proxy(move |req| match req {
                ControllerRequest::InitializeTracing { config, output, control_handle: _ } => {
                    let mut state = state.lock().unwrap();
                    if let Some(buffer_size) = config.buffer_size_megabytes_hint {
                        state.buffer_size = buffer_size;
                    }
                    if let Some(categories) = config.categories {
                        state.categories = categories;
                    }
                    state.output = Some(output);
                }
                ControllerRequest::TerminateTracing { options: _, responder } => {
                    let mut state = state.lock().unwrap();
                    state.output = None;
                    responder.send(TerminateResult::EMPTY).unwrap();
                }
                ControllerRequest::StartTracing { options: _, responder } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                ControllerRequest::StopTracing { options: _, responder } => {
                    let state = state.lock().unwrap();
                    state.output.as_ref().unwrap().write(TEST_TRACE_OUTPUT).unwrap();

                    responder.send().unwrap();
                }
                ControllerRequest::GetProviders { responder } => {
                    let a = ProviderInfo {
                        id: Some(1),
                        pid: Some(10),
                        name: Some("provider_a".to_string()),
                        ..ProviderInfo::EMPTY
                    };

                    let b = ProviderInfo {
                        id: Some(2),
                        pid: Some(20),
                        name: Some("provider_b".to_string()),
                        ..ProviderInfo::EMPTY
                    };

                    responder.send(&mut vec![a, b].into_iter()).unwrap();
                }
                ControllerRequest::GetKnownCategories { responder: _ } => (),
                ControllerRequest::WatchAlert { responder: _ } => (),
            }),
        )
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_providers() {
        const TEST_OUTPUT: &'static [u8] = b"\
            Trace providers:\
          \n- provider_a\
          \n- provider_b\
          \n";

        let mut output = Vec::new();
        let (_state, proxy) = setup_fake_controller_server();
        list_providers_cmd_impl(proxy, &mut output).await.expect("list_providers_cmd_impl");

        assert_eq!(output, TEST_OUTPUT);
    }

    async fn do_record_test<'a, L: LineWaiter<'a>>(
        waiter: &'a mut L,
        duration: Option<f64>,
        expected_output: &str,
    ) {
        let trace_file = tempfile::NamedTempFile::new().unwrap();
        let trace_path = trace_file.into_temp_path();

        let mut output = Vec::new();
        let (state, proxy) = setup_fake_controller_server();
        record_cmd_impl(
            Record {
                buffer_size: 1,
                categories: vec!["a".to_string(), "b".to_string(), "c".to_string()],
                duration: duration,
                output: trace_path.to_str().unwrap().to_string(),
            },
            proxy,
            waiter,
            &mut output,
        )
        .await
        .expect("record_cmd_impl");

        let out_string = String::from_utf8(output).unwrap();
        let state = state.lock().unwrap();

        // Verify that record handles its arguments correctly.
        assert_eq!(state.categories, vec!["a".to_string(), "b".to_string(), "c".to_string(),]);
        assert_eq!(state.buffer_size, 1);
        assert!(out_string.contains(expected_output));

        // Verify that the trace was written correctly.
        let trace_output = std::fs::read(&trace_path).unwrap();
        assert_eq!(trace_output, TEST_TRACE_OUTPUT);

        // Verify that we've cleaned up the temp file.
        trace_path.close().unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_interactive_record() {
        do_record_test(&mut NullWaiter {}, None, "press <enter> to stop trace").await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_duration_record() {
        do_record_test(&mut NullWaiter {}, Some(0.5), "waiting for 0.5 seconds").await;
    }
}
