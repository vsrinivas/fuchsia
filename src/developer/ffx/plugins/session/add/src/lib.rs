// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    ffx_core::ffx_plugin,
    ffx_session_add_args::SessionAddCommand,
    fidl::encoding::Decodable,
    fidl_fuchsia_session::{ElementManagerProxy, ElementSpec},
};

#[ffx_plugin(ElementManagerProxy = "core/session-manager:expose:fuchsia.session.ElementManager")]
pub async fn add(element_manager_proxy: ElementManagerProxy, cmd: SessionAddCommand) -> Result<()> {
    println!("Add {} to the current session", &cmd.url);
    element_manager_proxy
        .propose_element(
            ElementSpec { component_url: Some(cmd.url.to_string()), ..ElementSpec::new_empty() },
            None,
        )
        .await?
        .map_err(|err| format_err!("{:?}", err))
}

#[cfg(test)]
mod test {
    use {super::*, fidl_fuchsia_session::ElementManagerRequest};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_element() {
        const TEST_ELEMENT_URL: &str = "Test Element Url";

        let proxy = setup_fake_element_manager_proxy(|req| match req {
            ElementManagerRequest::ProposeElement { spec, responder, .. } => {
                assert_eq!(spec.component_url.unwrap(), TEST_ELEMENT_URL.to_string());
                let _ = responder.send(&mut Ok(()));
            }
        });

        let add_cmd = SessionAddCommand { url: TEST_ELEMENT_URL.to_string() };
        let response = add(proxy, add_cmd).await;
        assert!(response.is_ok());
    }
}
