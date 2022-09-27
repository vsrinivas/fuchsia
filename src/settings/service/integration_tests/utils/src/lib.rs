// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_metrics::{
    MetricEventLoggerFactoryRequest, MetricEventLoggerFactoryRequestStream,
    MetricEventLoggerRequest,
};
use fidl_fuchsia_ui_input::MediaButtonsEvent;
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_component_test::{
    Capability, ChildOptions, ChildRef, LocalComponentHandles, RealmBuilder, Ref, Route,
};
use futures::{StreamExt, TryStreamExt};

pub struct SettingsRealmInfo<'a> {
    pub builder: RealmBuilder,
    pub settings: &'a ChildRef,
    pub has_config_data: bool,
    pub capabilities: Vec<&'a str>,
}

pub async fn create_realm_basic(info: &SettingsRealmInfo<'_>) -> Result<(), Error> {
    const STORE_URL: &str = "fuchsia-pkg://fuchsia.com/stash#meta/stash.cm";
    const COBALT_URL: &str = "fuchsia-pkg://fuchsia.com/cobalt#meta/cobalt.cm";

    // setui_service needs to connect to fidl_fuchsia_stash to start its storage.
    let store = info.builder.add_child("store", STORE_URL, ChildOptions::new().eager()).await?;
    info.builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.stash.Store"))
                .from(&store)
                .to(info.settings),
        )
        .await?;
    let cobalt = info
        .builder
        .add_local_child(
            "cobalt",
            move |handles: LocalComponentHandles| Box::pin(cobalt_impl(handles)),
            ChildOptions::new().eager(),
        )
        .await?;
    info.builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name(
                    "fuchsia.metrics.MetricEventLoggerFactory",
                ))
                .from(&cobalt)
                .to(info.settings),
        )
        .await?;
    // Required by the store dependency.
    info.builder
        .add_route(
            Route::new()
                .capability(Capability::storage("data").path("/data"))
                .from(Ref::parent())
                .to(&store)
                .to(info.settings),
        )
        .await?;

    if info.has_config_data {
        // Use for reading configuration files.
        info.builder
            .add_route(
                Route::new()
                    .capability(
                        Capability::directory("config-data")
                            .path("/config/data")
                            .rights(fio::R_STAR_DIR),
                    )
                    .from(Ref::parent())
                    .to(info.settings),
            )
            .await?;
    }

    // Provide LogSink to print out logs of each service for debugging purpose.
    info.builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(info.settings)
                .to(&store),
        )
        .await?;

    for cap in &info.capabilities {
        // Route capabilities from setui_service.
        info.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(*cap))
                    .from(info.settings)
                    .to(Ref::parent()),
            )
            .await?;
    }

    Ok(())
}

// Mock cobalt impl since it's not necessary for the integration tests.
async fn cobalt_impl(handles: LocalComponentHandles) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    let _: &mut ServiceFsDir<'_, _> =
        fs.dir("svc").add_fidl_service(move |mut stream: MetricEventLoggerFactoryRequestStream| {
            fasync::Task::spawn(async move {
                while let Ok(Some(MetricEventLoggerFactoryRequest::CreateMetricEventLogger {
                    logger,
                    responder,
                    ..
                })) = stream.try_next().await
                {
                    fasync::Task::spawn(async move {
                        let mut stream = logger.into_stream().unwrap();
                        while let Some(Ok(request)) = stream.next().await {
                            match request {
                                MetricEventLoggerRequest::LogOccurrence { responder, .. } => {
                                    let _ = responder.send(&mut Ok(()));
                                }
                                MetricEventLoggerRequest::LogInteger { responder, .. } => {
                                    let _ = responder.send(&mut Ok(()));
                                }
                                MetricEventLoggerRequest::LogIntegerHistogram {
                                    responder, ..
                                } => {
                                    let _ = responder.send(&mut Ok(()));
                                }
                                MetricEventLoggerRequest::LogString { responder, .. } => {
                                    let _ = responder.send(&mut Ok(()));
                                }
                                MetricEventLoggerRequest::LogMetricEvents { responder, .. } => {
                                    let _ = responder.send(&mut Ok(()));
                                }
                            }
                        }
                    })
                    .detach();
                    let _ = responder.send(&mut Ok(()));
                }
            })
            .detach();
        });
    let _: &mut ServiceFs<_> = fs.serve_connection(handles.outgoing_dir).unwrap();
    fs.collect::<()>().await;
    Ok(())
}

// TODO(fxb/105380): Replace with test-case macro.
/// Macro for generating multiple tests from a common test function
/// # Example
/// ```
/// async_property_test!(test_to_run,
///     case1(0, String::from("abc")),
///     case2(1, String::from("xyz")),
/// );
/// async fn test_to_run(prop1: usize, prop2: String) {
///     assert!(prop1 < 2);
///     assert_eq!(prop2.chars().len(), 3);
/// }
/// ```
#[macro_export]
macro_rules! async_property_test {
    (
        $test_func:ident => [$( // Test function to call, followed by list of test cases.
            $(#[$attr:meta])* // Optional attributes.
            $test_name:ident( // Test case name.
                $($args:expr),+$(,)? // Arguments for test case.
            )
        ),+$(,)?]
    ) => {
        $(paste::paste!{
            #[allow(non_snake_case)]
            #[fuchsia_async::run_singlethreaded(test)]
            $(#[$attr])*
            async fn [<$test_func ___ $test_name>]() {
                $test_func($($args,)+).await;
            }
        })+
    }
}

/// Builder to simplify construction of fidl_fuchsia_ui_input::MediaButtonsEvent.
/// # Example usage:
/// ```
/// MediaButtonsEventBuilder::new().set_volume(1).set_mic_mute(true).build();
/// ```
pub struct MediaButtonsEventBuilder {
    volume: i8,
    mic_mute: bool,
    pause: bool,
    camera_disable: bool,
}

impl MediaButtonsEventBuilder {
    pub fn new() -> Self {
        // Create with defaults.
        Self { volume: 0, mic_mute: false, pause: false, camera_disable: false }
    }

    pub fn build(self) -> MediaButtonsEvent {
        MediaButtonsEvent {
            volume: Some(self.volume),
            mic_mute: Some(self.mic_mute),
            pause: Some(self.pause),
            camera_disable: Some(self.camera_disable),
            ..MediaButtonsEvent::EMPTY
        }
    }

    pub fn set_volume(mut self, volume: i8) -> Self {
        self.volume = volume;
        self
    }

    pub fn set_mic_mute(mut self, mic_mute: bool) -> Self {
        self.mic_mute = mic_mute;
        self
    }

    pub fn set_pause(mut self, pause: bool) -> Self {
        self.pause = pause;
        self
    }

    pub fn set_camera_disable(mut self, camera_disable: bool) -> Self {
        self.camera_disable = camera_disable;
        self
    }
}
