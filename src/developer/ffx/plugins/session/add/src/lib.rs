// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    ffx_core::ffx_plugin,
    ffx_session_add_args::SessionAddCommand,
    fidl_fuchsia_element::{ManagerProxy, Spec},
};

#[ffx_plugin(ManagerProxy = "core/session-manager:expose:fuchsia.element.Manager")]
pub async fn add(manager_proxy: ManagerProxy, cmd: SessionAddCommand) -> Result<()> {
    add_impl(manager_proxy, cmd, &mut std::io::stdout()).await
}

pub async fn add_impl<W: std::io::Write>(
    manager_proxy: ManagerProxy,
    cmd: SessionAddCommand,
    writer: &mut W,
) -> Result<()> {
    writeln!(writer, "Add {} to the current session", &cmd.url)?;
    let arguments = if cmd.args.is_empty() { None } else { Some(cmd.args) };
    manager_proxy
        .propose_element(
            Spec { component_url: Some(cmd.url.to_string()), arguments, ..Spec::EMPTY },
            None,
        )
        .await?
        .map_err(|err| format_err!("{:?}", err))
}

#[cfg(test)]
mod test {
    use {super::*, fidl_fuchsia_element::ManagerRequest, lazy_static::lazy_static};

    #[fuchsia::test]
    async fn test_add_element() {
        const TEST_ELEMENT_URL: &str = "Test Element Url";

        let proxy = setup_fake_manager_proxy(|req| match req {
            ManagerRequest::ProposeElement { spec, responder, .. } => {
                assert_eq!(spec.component_url.unwrap(), TEST_ELEMENT_URL.to_string());
                let _ = responder.send(&mut Ok(()));
            }
        });

        let add_cmd = SessionAddCommand { url: TEST_ELEMENT_URL.to_string(), args: vec![] };
        let response = add(proxy, add_cmd).await;
        assert!(response.is_ok());
    }

    #[fuchsia::test]
    async fn test_add_element_args() {
        const TEST_ELEMENT_URL: &str = "Test Element Url";
        lazy_static! {
            static ref TEST_ARGS: Vec<String> = vec!["hello".to_string(), "world".to_string()];
        }

        let proxy = setup_fake_manager_proxy(|req| match req {
            ManagerRequest::ProposeElement { spec, responder, .. } => {
                let arguments = spec.arguments.expect("spec does not have annotations field set");
                assert_eq!(arguments, *TEST_ARGS);
                let _ = responder.send(&mut Ok(()));
            }
        });

        let add_cmd =
            SessionAddCommand { url: TEST_ELEMENT_URL.to_string(), args: TEST_ARGS.clone() };
        let response = add(proxy, add_cmd).await;
        assert!(response.is_ok());
    }
}
