// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_session_add_args::SessionAddCommand,
    fidl_fuchsia_element::{ManagerProxy, Spec},
    fidl_fuchsia_session::{ElementManagerProxy, ElementSpec},
};

#[ffx_plugin(
    ElementManagerProxy = "core/session-manager:expose:fuchsia.session.ElementManager",
    ManagerProxy = "core/session-manager:expose:fuchsia.element.Manager"
)]
pub async fn add(
    element_manager_proxy: Option<ElementManagerProxy>,
    manager_proxy: Option<ManagerProxy>,
    cmd: SessionAddCommand,
) -> Result<()> {
    add_impl(element_manager_proxy, manager_proxy, cmd, &mut std::io::stdout()).await
}

pub async fn add_impl<W: std::io::Write>(
    element_manager_proxy: Option<ElementManagerProxy>,
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
    } else if let Some(element_manager_proxy) = element_manager_proxy {
        element_manager_proxy
            .propose_element(
                ElementSpec { component_url: Some(cmd.url.to_string()), ..ElementSpec::EMPTY },
                None,
            )
            .await?
            .map_err(|err| format_err!("{:?}", err))
    } else {
        ffx_bail!("Session does not expose an element manager");
    }
}

#[cfg(test)]
mod test {
    use {
        super::*, fidl_fuchsia_element::ManagerRequest, fidl_fuchsia_session::ElementManagerRequest,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_element_legacy() {
        const TEST_ELEMENT_URL: &str = "Test Element Url";

        let proxy = setup_fake_element_manager_proxy(|req| match req {
            ElementManagerRequest::ProposeElement { spec, responder, .. } => {
                assert_eq!(spec.component_url.unwrap(), TEST_ELEMENT_URL.to_string());
                let _ = responder.send(&mut Ok(()));
            }
        });

        let add_cmd = SessionAddCommand { url: TEST_ELEMENT_URL.to_string() };
        let response = add(Some(proxy), None, add_cmd).await;
        assert!(response.is_ok());
    }

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
        let response = add(None, Some(proxy), add_cmd).await;
        assert!(response.is_ok());
    }
}
