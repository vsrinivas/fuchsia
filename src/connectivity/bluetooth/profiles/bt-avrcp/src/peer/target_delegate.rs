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
    pub fn set_target_handler(&self, target_handler: TargetHandlerProxy) -> Result<(), Error> {
        let mut inner_guard = self.inner.lock();
        if inner_guard.target_handler.is_some() {
            return Err(Error::TargetBound);
        }

        let target_handler_event_stream = target_handler.take_event_stream();
        // We were able to set the target delegate so spawn a task to watch for it
        // to close.
        let inner_ref = self.inner.clone();
        fasync::spawn(async move {
            let _ = target_handler_event_stream.map(|_| ()).collect::<()>().await;
            inner_ref.lock().target_handler = None;
        });

        inner_guard.target_handler = Some(target_handler);
        Ok(())
    }

    pub fn set_absolute_volume_handler(
        &self,
        absolute_volume_handler: AbsoluteVolumeHandlerProxy,
    ) -> Result<(), Error> {
        let mut inner_guard = self.inner.lock();
        if inner_guard.absolute_volume_handler.is_some() {
            return Err(Error::TargetBound);
        }

        let volume_event_stream = absolute_volume_handler.take_event_stream();
        // We were able to set the target delegate so spawn a task to watch for it
        // to close.
        let inner_ref = self.inner.clone();
        fasync::spawn(async move {
            let _ = volume_event_stream.map(|_| ()).collect::<()>().await;
            inner_ref.lock().absolute_volume_handler = None;
        });

        inner_guard.absolute_volume_handler = Some(absolute_volume_handler);
        Ok(())
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
