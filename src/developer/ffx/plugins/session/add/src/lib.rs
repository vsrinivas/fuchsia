// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_session_add_args::SessionAddCommand,
    fidl_fuchsia_element::{ManagerProxy, Spec},
};

#[ffx_plugin(ManagerProxy = "core/session-manager:expose:fuchsia.element.Manager")]
pub async fn add(manager_proxy: Option<ManagerProxy>, cmd: SessionAddCommand) -> Result<()> {
    add_impl(manager_proxy, cmd, &mut std::io::stdout()).await
}

pub async fn add_impl<W: std::io::Write>(
    manager_proxy: Option<ManagerProxy>,
    cmd: SessionAddCommand,
    writer: &mut W,
) -> Result<()> {
    writeln!(writer, "Add {} to the current session", &cmd.url)?;
    if let Some(manager_proxy) = manager_proxy {
        manager_proxy
            .propose_element(Spec { component_url: Some(cmd.url.to_string()), ..Spec::EMPTY }, None)
            .await?
            .map_err(|err| format_err!("{:?}", err))
    } else {
        ffx_bail!("Could not discover the element manager protocol");
    }
}

#[cfg(test)]
mod test {
    use {super::*, fidl_fuchsia_element::ManagerRequest};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_element() {
        const TEST_ELEMENT_URL: &str = "Test Element Url";

        let proxy = setup_fake_manager_proxy(|req| match req {
            ManagerRequest::ProposeElement { spec, responder, .. } => {
                assert_eq!(spec.component_url.unwrap(), TEST_ELEMENT_URL.to_string());
                let _ = responder.send(&mut Ok(()));
            }
        });

        let add_cmd = SessionAddCommand { url: TEST_ELEMENT_URL.to_string() };
        let response = add(Some(proxy), add_cmd).await;
        assert!(response.is_ok());
    }
}
