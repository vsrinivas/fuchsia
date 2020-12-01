// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_trace_args::{TraceCommand, TraceSubCommand},
    fidl_fuchsia_tracing_controller::ControllerProxy,
    std::io::{stdout, Write},
};

#[ffx_plugin("tracing", ControllerProxy = "core/appmgr:out:fuchsia.tracing.controller.Controller")]
pub async fn trace(controller_proxy: ControllerProxy, cmd: TraceCommand) -> Result<()> {
    match cmd.sub_cmd {
        TraceSubCommand::ListProviders(_sub_cmd) => {
            list_providers_cmd_impl(controller_proxy, Box::new(stdout())).await?
        }
    }

    Ok(())
}

async fn list_providers_cmd_impl<W: Write>(
    controller_proxy: ControllerProxy,
    mut writer: W,
) -> Result<()> {
    let providers = controller_proxy.get_providers().await?;

    write!(&mut writer, "Trace providers:\n")?;
    for provider in &providers {
        write!(&mut writer, "- {}\n", provider.name.as_ref().unwrap_or(&"<unknown>".to_string()))?;
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_tracing_controller::{ControllerRequest, ProviderInfo};

    fn setup_fake_controller_server() -> ControllerProxy {
        setup_fake_controller_proxy(move |req| match req {
            ControllerRequest::InitializeTracing { config: _, output: _, control_handle: _ } => (),
            ControllerRequest::TerminateTracing { options: _, responder: _ } => (),
            ControllerRequest::StartTracing { options: _, responder: _ } => (),
            ControllerRequest::StopTracing { options: _, responder: _ } => (),
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
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_providers() {
        const TEST_OUTPUT: &'static [u8] = b"\
            Trace providers:\
          \n- provider_a\
          \n- provider_b\
          \n";

        let mut output = Vec::new();
        list_providers_cmd_impl(setup_fake_controller_server(), &mut output)
            .await
            .expect("list_providers_cmd_impl");

        assert_eq!(output, TEST_OUTPUT);
    }
}
