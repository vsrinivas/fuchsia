// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Result},
    ffx_core::ffx_plugin,
    ffx_input_args::ComponentInputCommand,
    fidl_fuchsia_ui_input_config::FeaturesProxy,
};

#[ffx_plugin(FeaturesProxy = "core/ui/scene_manager:expose:fuchsia.ui.input.config.Features")]
pub async fn feature(features_proxy: FeaturesProxy, cmd: ComponentInputCommand) -> Result<()> {
    match (cmd.enable, cmd.disable) {
        (None, None) => Err(format_err!("Need --enable or --disable")),
        (Some(_), Some(_)) => Err(format_err!("Only allow --enable or --disable")),
        (Some(feature), None) => match feature.as_str() {
            "touchpad_mode" => features_proxy.set_touchpad_mode(true).context("set_touchpad_mode"),
            _ => Err(format_err!("unknown feature: {}", feature)),
        },
        (None, Some(feature)) => match feature.as_str() {
            "touchpad_mode" => features_proxy.set_touchpad_mode(false).context("set_touchpad_mode"),
            _ => Err(format_err!("unknown feature: {}", feature)),
        },
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl_fuchsia_ui_input_config::{FeaturesMarker, FeaturesRequest},
        fuchsia_async::Task,
        futures::prelude::*,
        test_case::test_case,
    };

    fn setup_fake_features_service(
        mut sender: futures::channel::mpsc::Sender<FeaturesRequest>,
    ) -> FeaturesProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<FeaturesMarker>().unwrap();

        Task::local(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                let _ = sender.send(req).await;
            }
        })
        .detach();

        proxy
    }

    #[test_case(None, None; "no enable or disable")]
    #[test_case(Some(String::from("touchpad_mode")), Some(String::from("touchpad_mode")); "both enable and disable")]
    #[test_case(Some(String::from("unknown_feature")), None; "enable unknown_feature")]
    #[test_case(None, Some(String::from("unknown_feature")); "disable unknown_feature")]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_input_config_features_err(enable: Option<String>, disable: Option<String>) {
        let (sender, _receiver) = futures::channel::mpsc::channel::<FeaturesRequest>(1);
        let proxy = setup_fake_features_service(sender);

        let cmd = ComponentInputCommand { enable, disable };
        let res = feature(proxy, cmd).await;
        assert_matches!(res, Err(_));
    }

    #[test_case(Some(String::from("touchpad_mode")), None, true; "enable touchpad_mode")]
    #[test_case(None, Some(String::from("touchpad_mode")), false; "disable touchpad_mode")]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_input_config_features_touchpad_mode_request(
        enable: Option<String>,
        disable: Option<String>,
        expected_touchpad_mode: bool,
    ) {
        let (sender, mut receiver) = futures::channel::mpsc::channel::<FeaturesRequest>(1);
        let proxy = setup_fake_features_service(sender);

        let cmd = ComponentInputCommand { enable, disable };
        let res = feature(proxy, cmd).await;
        assert_matches!(res, Ok(()));

        match receiver.next().await {
            Some(FeaturesRequest::SetTouchpadMode { enable, .. }) => {
                pretty_assertions::assert_eq!(enable, expected_touchpad_mode);
            }
            _ => panic!("receive unexpected request"),
        }
    }
}
