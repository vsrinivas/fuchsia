// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use fidl_fuchsia_bluetooth_avrcp::{
    AbsoluteVolumeHandlerProxy, TargetHandlerProxy, TargetPassthroughError,
};

/// Delegates commands received on any peer channels to the currently registered target handler and
/// absolute volume handler.
/// If no target handler or absolute volume handler is registered with the service, this delegate
/// will return appropriate stub responses.
/// If a target handler is changed or closed at any point, this delegate will handle the state
/// transitions for any outstanding and pending registered notifications.
#[derive(Debug)]
pub struct TargetDelegate {
    inner: Arc<Mutex<TargetDelegateInner>>,
}

#[derive(Debug)]
struct TargetDelegateInner {
    target_handler: Option<TargetHandlerProxy>,
    absolute_volume_handler: Option<AbsoluteVolumeHandlerProxy>,
}

#[derive(Debug)]
#[must_use = "it's required that you remove the handler when you are done with it"]
pub struct TargetHandlerRemoveHandle {
    inner: Arc<Mutex<TargetDelegateInner>>,
}

impl TargetHandlerRemoveHandle {
    /// Removes the current target handler, if any.
    pub fn remove_target_handler(self) {
        let mut inner_guard = self.inner.lock();
        inner_guard.target_handler = None;
    }
}

#[derive(Debug)]
#[must_use = "it's required that you remove the handler when you are done with it"]
#[allow(dead_code)]
pub struct AbsoluteVolumeHandlerRemoveHandle {
    inner: Arc<Mutex<TargetDelegateInner>>,
}

impl AbsoluteVolumeHandlerRemoveHandle {
    /// Removes the current handler
    #[allow(dead_code)]
    pub fn remove_absolute_volume_handler(self) {
        let mut inner_guard = self.inner.lock();
        inner_guard.absolute_volume_handler = None;
    }
}

impl TargetDelegate {
    pub fn new() -> TargetDelegate {
        TargetDelegate {
            inner: Arc::new(Mutex::new(TargetDelegateInner {
                target_handler: None,
                absolute_volume_handler: None,
            })),
        }
    }

    /// Sets the target delegate. Returns true if successful. Resets any pending registered
    /// notifications.
    /// If the target is already set to some value this method does not replace it and returns false
    pub fn set_target_handler(
        &self,
        target_handler: TargetHandlerProxy,
    ) -> Result<TargetHandlerRemoveHandle, Error> {
        let mut inner_guard = self.inner.lock();
        if inner_guard.target_handler.is_some() {
            return Err(Error::TargetBound);
        }

        inner_guard.target_handler = Some(target_handler);
        Ok(TargetHandlerRemoveHandle { inner: self.inner.clone() })
    }

    #[allow(dead_code)]
    pub fn set_absolute_volume_handler(
        &self,
        absolute_volume_handler: AbsoluteVolumeHandlerProxy,
    ) -> Result<AbsoluteVolumeHandlerRemoveHandle, Error> {
        let mut inner_guard = self.inner.lock();
        if inner_guard.absolute_volume_handler.is_some() {
            return Err(Error::TargetBound);
        }
        inner_guard.absolute_volume_handler = Some(absolute_volume_handler);
        Ok(AbsoluteVolumeHandlerRemoveHandle { inner: self.inner.clone() })
    }

    pub async fn send_passthrough_command(
        &self,
        command: AvcPanelCommand,
        pressed: bool,
    ) -> Result<(), TargetPassthroughError> {
        let send_command_fut = {
            let inner_guard = self.inner.lock();
            match &inner_guard.target_handler {
                Some(target_handler) => target_handler.send_command(command, pressed),
                None => return Err(TargetPassthroughError::CommandRejected),
            }
        };

        send_command_fut.await.map_err(|_| TargetPassthroughError::CommandRejected)?
    }
}
