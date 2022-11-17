// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_utils::hanging_get::client::HangingGetStream;
use fidl_fuchsia_settings::{LightGroup as LightGroupFidl, LightProxy, LightValue};
use fidl_fuchsia_ui_brightness::ControlProxy;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_warn;
#[cfg(test)]
use futures::channel::mpsc;
use futures::channel::oneshot;
use futures::StreamExt;
use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::Rc;

#[derive(Debug, Clone)]
pub struct LightGroup {
    /// Name of the light group.
    name: String,
    /// None brightness implies the light is disabled.
    brightness: Option<f32>,
}

impl LightGroup {
    #[cfg(test)]
    pub(crate) fn new(name: String, brightness: Option<f32>) -> Self {
        Self { name, brightness }
    }

    pub(crate) fn name(&self) -> &String {
        &self.name
    }

    pub(crate) fn brightness(&self) -> Option<f32> {
        self.brightness
    }

    #[cfg(test)]
    pub(crate) fn set_brightness_for_test(&mut self, brightness: Option<f32>) {
        self.brightness = brightness;
    }

    fn map_from_light_groups(light_groups: Vec<LightGroupFidl>) -> HashMap<String, LightGroup> {
        light_groups
            .into_iter()
            .filter_map(|light_group| {
                let enabled = light_group.enabled;
                let lights = light_group.lights;
                light_group.name.and_then(|name| {
                    if enabled.unwrap_or(false) {
                        lights
                            .as_ref()
                            .and_then(|lights| lights.get(0))
                            .and_then(|light| light.value.as_ref())
                            .map(|value| match value {
                                LightValue::On(true) => Some(1.0),
                                LightValue::On(false) => Some(0.0),
                                LightValue::Brightness(b) => Some(*b as f32),
                                LightValue::Color(_) => None,
                            })
                            .map(|brightness| (name.clone(), LightGroup { name, brightness }))
                    } else {
                        Some((name.clone(), LightGroup { name, brightness: None }))
                    }
                })
            })
            .collect()
    }
}

pub struct LedWatcher {
    backlight_brightness: Rc<RefCell<f32>>,
    light_groups: Rc<RefCell<HashMap<String, LightGroup>>>,
    #[cfg(test)]
    update: Option<RefCell<mpsc::Sender<Update>>>,
}

#[cfg(test)]
enum Update {
    Brightness,
    LightGroups,
}

impl LedWatcher {
    /// Create a new `LedWatcher` for the `light_groups` with the supplied calibration. Only the
    /// [LightGroup]s that have a corresponding calibration entry in [Calibration].`leds` will be
    /// tracked.
    // TODO(fxbug.dev/100664) Remove allow once used.
    #[allow(dead_code)]
    pub(crate) fn new(light_groups: Vec<LightGroupFidl>) -> Self {
        Self {
            backlight_brightness: Rc::new(RefCell::new(0.0)),
            light_groups: Rc::new(RefCell::new(LightGroup::map_from_light_groups(light_groups))),
            #[cfg(test)]
            update: None,
        }
    }

    #[cfg(test)]
    fn new_with_sender(light_groups: Vec<LightGroupFidl>, update: mpsc::Sender<Update>) -> Self {
        Self {
            backlight_brightness: Rc::new(RefCell::new(0.0)),
            light_groups: Rc::new(RefCell::new(LightGroup::map_from_light_groups(light_groups))),
            update: Some(RefCell::new(update)),
        }
    }

    /// Spawn a local task to continuously watch for light group changes. Returns a tuple with a
    /// [LedWatcherHandle], which can be used to get the current led states, and a task, which can
    /// be used to track completion of the spawned task.
    pub(crate) fn handle_light_groups_and_brightness_watch(
        self,
        light_proxy: LightProxy,
        brightness_proxy: ControlProxy,
        mut cancelation_rx: oneshot::Receiver<()>,
    ) -> (LedWatcherHandle, fasync::Task<()>) {
        let light_groups = Rc::clone(&self.light_groups);
        let backlight_brightness = Rc::clone(&self.backlight_brightness);
        let task = fasync::Task::local(async move {
            let mut light_groups_stream =
                HangingGetStream::new(light_proxy, LightProxy::watch_light_groups);
            let mut brightness_stream =
                HangingGetStream::new(brightness_proxy, ControlProxy::watch_current_brightness);
            let mut light_group_fut = light_groups_stream.select_next_some();
            let mut brightness_fut = brightness_stream.select_next_some();
            loop {
                futures::select! {
                    watch_result = light_group_fut => {
                        match watch_result {
                            Ok(light_groups) => self.update_light_groups(light_groups),
                            Err(e) => fx_log_warn!("Failed to get light group update: {:?}", e),
                        }
                        light_group_fut = light_groups_stream.select_next_some()
                    }
                    watch_result = brightness_fut => {
                        match watch_result {
                            Ok(brightness) => self.update_brightness(brightness),
                            Err(e) => fx_log_warn!("Failed to get brightness update: {:?}", e),
                        }
                        brightness_fut = brightness_stream.select_next_some();
                    }
                    _ = cancelation_rx => break,
                    complete => break,
                }
            }
        });

        (LedWatcherHandle { light_groups, backlight_brightness }, task)
    }

    fn update_light_groups(&self, light_groups: Vec<LightGroupFidl>) {
        *self.light_groups.borrow_mut() = LightGroup::map_from_light_groups(light_groups);
        #[cfg(test)]
        if let Some(sender) = &self.update {
            sender.borrow_mut().try_send(Update::LightGroups).expect("Failed to send update");
        }
    }

    fn update_brightness(&self, brightness: f32) {
        *self.backlight_brightness.borrow_mut() = brightness;
        #[cfg(test)]
        if let Some(sender) = &self.update {
            sender.borrow_mut().try_send(Update::Brightness).expect("Failed to send update");
        }
    }
}

#[derive(Clone)]
pub struct LedWatcherHandle {
    light_groups: Rc<RefCell<HashMap<String, LightGroup>>>,
    backlight_brightness: Rc<RefCell<f32>>,
}

impl LedState for LedWatcherHandle {
    fn light_groups(&self) -> HashMap<String, LightGroup> {
        Clone::clone(&*self.light_groups.borrow())
    }

    fn backlight_brightness(&self) -> f32 {
        *self.backlight_brightness.borrow()
    }
}

pub trait LedState {
    fn light_groups(&self) -> HashMap<String, LightGroup>;
    fn backlight_brightness(&self) -> f32;
}

pub struct CancelableTask {
    inner: fasync::Task<()>,
    cancelation_tx: oneshot::Sender<()>,
}

impl CancelableTask {
    pub(crate) fn new(cancelation_tx: oneshot::Sender<()>, task: fasync::Task<()>) -> Self {
        Self { cancelation_tx, inner: task }
    }

    /// Submit a cancelation request and wait for the task to end.
    pub async fn cancel(self) {
        // If the send fails, the watcher has already ended so there's no need to worry about the
        // result.
        let _ = self.cancelation_tx.send(());
        self.inner.await
    }
}

#[cfg(test)]
mod led_watcher_tests;
