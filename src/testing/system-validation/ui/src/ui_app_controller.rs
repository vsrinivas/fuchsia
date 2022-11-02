// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::app_monitor::AppMonitor,
    crate::screencapture::take_screenshot,
    crate::single_session_trace::SingleSessionTrace,
    argh::FromArgs,
    fidl_fuchsia_session_scene as scene, fidl_fuchsia_ui_app as ui_app, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_scenic as scenic,
    std::convert::TryInto,
    std::sync::{Arc, Mutex},
    tracing::info,
};

mod app_monitor;
mod screencapture;
mod single_session_trace;

/// Args to control how to run system validation test.
/// Specify them through *_system_validation.cml
#[derive(FromArgs, PartialEq, Debug)]
struct RunTestCmd {
    /// trace configurations
    /// example: --trace-config "system_metrics" --trace-config "input"
    #[argh(option)]
    trace_config: Vec<String>,

    /// number of seconds to run example app
    #[argh(option)]
    run_duration_sec: Option<usize>,
}

// Note: Maybe we want to make this an argument.
const SAMPLE_APP_MONIKER: &str = "./sample-app";

#[fuchsia::main]
async fn main() {
    let args: RunTestCmd = argh::from_env();
    // Hook up to scene_manager
    let scene_manager = connect_to_protocol::<scene::ManagerMarker>()
        .expect("failed to connect to fuchsia.scene.Manager");
    let view_provider = connect_to_protocol::<ui_app::ViewProviderMarker>()
        .expect("failed to connect to ViewProvider");
    let mut link_token_pair = scenic::flatland::ViewCreationTokenPair::new().unwrap();
    let app_monitor = AppMonitor::new(SAMPLE_APP_MONIKER.to_string());

    // Use view provider to initiate creation of the view which will be connected to the
    // viewport that we create below.
    view_provider
        .create_view2(ui_app::CreateView2Args {
            view_creation_token: Some(link_token_pair.view_creation_token),
            ..ui_app::CreateView2Args::EMPTY
        })
        .expect("Cannot invoke create_view2");
    let _root_view_created =
        scene_manager.present_root_view(&mut link_token_pair.viewport_creation_token);
    app_monitor.wait_for_start_event().await;

    let sample_app_stopped: Arc<Mutex<bool>> = Arc::new(Mutex::new(false));
    app_monitor.monitor_for_stop_event(&sample_app_stopped);

    // Collect trace.
    let trace = SingleSessionTrace::new();
    let collect_trace = args.trace_config.len() > 0;
    if collect_trace {
        info!("Collecting trace: {:?}", args.trace_config);
        trace.initialize(args.trace_config).await.unwrap();
        trace.start().await.unwrap();
    }
    // Let the UI App run for [run_duration_sec], then the test_runner will shutdown the sample_app
    let duration_sec = args.run_duration_sec.unwrap_or(5);
    info!("Running sample app for {} sec", duration_sec);
    fasync::Timer::new(fasync::Time::after(fasync::Duration::from_seconds(
        duration_sec.try_into().unwrap(),
    )))
    .await;

    if collect_trace {
        trace.stop().await.unwrap();
        trace.terminate().await.unwrap();
    }

    info!("Taking screenshot.");
    take_screenshot().await;

    info!("Checking that sample-app did not crash.");
    if *sample_app_stopped.lock().unwrap() {
        panic!("Sample app unexpectedly stopped");
    }
}
