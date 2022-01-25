// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Unit test utilities for clients of the `fuchsia.hardware.display` FIDL API.

use {
    fidl::endpoints::{RequestStream, ServerEnd},
    fidl_fuchsia_hardware_display::{self as display, ControllerMarker, ControllerRequestStream},
    fuchsia_zircon as zx,
    itertools::Itertools,
    std::collections::HashMap,
    thiserror::Error,
};

/// Errors that can be returned by `MockController`.
#[derive(Error, Debug)]
pub enum MockControllerError {
    /// Duplicate IDs were given to a function that expects objects with unique IDs. For example,
    /// MockController has been assigned multiple displays with clashing IDs.
    #[error("duplicate IDs provided")]
    DuplicateIds,

    /// Error from the underlying FIDL bindings or channel transport.
    #[error("FIDL error: {0}")]
    FidlError(#[from] fidl::Error),
}

/// MockControllerError Result type alias.
pub type Result<T> = std::result::Result<T, MockControllerError>;

/// `MockController` implements the server-end of the `fuchsia.hardware.display.Controller`
/// protocol. It minimally reproduces the display controller driver state machine to respond to
/// FIDL messages in a predictable and configurable manner.
pub struct MockController {
    #[allow(unused)]
    stream: ControllerRequestStream,
    control_handle: <ControllerRequestStream as RequestStream>::ControlHandle,

    displays: HashMap<DisplayId, display::Info>,
}

#[derive(Eq, Hash, Ord, PartialOrd, PartialEq)]
struct DisplayId(u64);

impl MockController {
    /// Bind a new `MockController` to the server end of a FIDL channel.
    pub fn new(server_end: ServerEnd<ControllerMarker>) -> Result<MockController> {
        let (stream, control_handle) = server_end.into_stream_and_control_handle()?;
        Ok(MockController { stream, control_handle, displays: HashMap::new() })
    }

    /// Replace the list of available display devices with the given collection and send a
    /// `fuchsia.hardware.display.Controller.OnDisplaysChanged` event reflecting the changes.
    ///
    /// All the new displays will be reported as added while previously present displays will
    /// be reported as removed, regardless of their content.
    ///
    /// Returns an error if `displays` contains entries with repeated display IDs.
    pub fn assign_displays(&mut self, displays: Vec<display::Info>) -> Result<()> {
        let mut added = HashMap::new();
        if !displays.into_iter().all(|info| added.insert(DisplayId(info.id), info).is_none()) {
            return Err(MockControllerError::DuplicateIds);
        }

        let removed: Vec<u64> = self.displays.iter().map(|(_, info)| info.id).collect();
        self.displays = added;
        self.control_handle.send_on_displays_changed(
            &mut self.displays.iter_mut().sorted().map(|(_, info)| info),
            &removed,
        )?;
        Ok(())
    }

    /// Sends a single OnVsync event to the client. The vsync event will appear to be sent from the
    /// given `display_id` even if a corresponding fake display has not been assigned by a call to
    /// `assign_displays`.
    pub fn emit_vsync_event(&self, display_id: u64, mut stamp: display::ConfigStamp) -> Result<()> {
        self.control_handle
            .send_on_vsync(display_id, zx::Time::get_monotonic().into_nanos() as u64, &mut stamp, 0)
            .map_err(MockControllerError::from)
    }
}

/// Create a Zircon channel and return both endpoints with the server end bound to a
/// `MockController`.
///
/// NOTE: This function instantiates FIDL bindings and thus requires a fuchsia-async executor to
/// have been created beforehand.
pub fn create_proxy_and_mock() -> Result<(display::ControllerProxy, MockController)> {
    let (proxy, server) = fidl::endpoints::create_proxy::<ControllerMarker>()?;
    Ok((proxy, MockController::new(server)?))
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        anyhow::{Context, Result},
        fidl_fuchsia_hardware_display as display,
        futures::{future, TryStreamExt},
    };

    async fn wait_for_displays_changed_event(
        events: &mut display::ControllerEventStream,
    ) -> Result<(Vec<display::Info>, Vec<u64>)> {
        let mut stream = events.try_filter_map(|event| match event {
            display::ControllerEvent::OnDisplaysChanged { added, removed } => {
                future::ok(Some((added, removed)))
            }
            _ => future::ok(None),
        });
        stream.try_next().await?.context("failed to listen to controller events")
    }

    #[fuchsia::test]
    async fn assign_displays_fails_with_duplicate_display_ids() {
        let displays = vec![
            display::Info {
                id: 1,
                modes: Vec::new(),
                pixel_format: Vec::new(),
                cursor_configs: Vec::new(),
                manufacturer_name: "Foo".to_string(),
                monitor_name: "what".to_string(),
                monitor_serial: "".to_string(),
                horizontal_size_mm: 0,
                vertical_size_mm: 0,
                using_fallback_size: false,
            },
            display::Info {
                id: 1,
                modes: Vec::new(),
                pixel_format: Vec::new(),
                cursor_configs: Vec::new(),
                manufacturer_name: "Bar".to_string(),
                monitor_name: "who".to_string(),
                monitor_serial: "".to_string(),
                horizontal_size_mm: 0,
                vertical_size_mm: 0,
                using_fallback_size: false,
            },
        ];

        let (_proxy, mut mock) = create_proxy_and_mock().expect("failed to create MockController");
        let result = mock.assign_displays(displays);
        assert!(result.is_err());
    }

    #[fuchsia::test]
    async fn assign_displays_displays_added() -> Result<()> {
        let displays = vec![
            display::Info {
                id: 1,
                modes: Vec::new(),
                pixel_format: Vec::new(),
                cursor_configs: Vec::new(),
                manufacturer_name: "Foo".to_string(),
                monitor_name: "what".to_string(),
                monitor_serial: "".to_string(),
                horizontal_size_mm: 0,
                vertical_size_mm: 0,
                using_fallback_size: false,
            },
            display::Info {
                id: 2,
                modes: Vec::new(),
                pixel_format: Vec::new(),
                cursor_configs: Vec::new(),
                manufacturer_name: "Bar".to_string(),
                monitor_name: "who".to_string(),
                monitor_serial: "".to_string(),
                horizontal_size_mm: 0,
                vertical_size_mm: 0,
                using_fallback_size: false,
            },
        ];

        let (proxy, mut mock) = create_proxy_and_mock().expect("failed to create MockController");
        mock.assign_displays(displays.clone())?;

        let mut events = proxy.take_event_stream();
        let (added, removed) = wait_for_displays_changed_event(&mut events).await?;
        assert_eq!(added, displays);
        assert_eq!(removed, vec![]);

        Ok(())
    }

    #[fuchsia::test]
    async fn assign_displays_displays_removed() -> Result<()> {
        let displays = vec![display::Info {
            id: 1,
            modes: Vec::new(),
            pixel_format: Vec::new(),
            cursor_configs: Vec::new(),
            manufacturer_name: "Foo".to_string(),
            monitor_name: "what".to_string(),
            monitor_serial: "".to_string(),
            horizontal_size_mm: 0,
            vertical_size_mm: 0,
            using_fallback_size: false,
        }];

        let (proxy, mut mock) = create_proxy_and_mock().expect("failed to create MockController");
        mock.assign_displays(displays)?;

        let mut events = proxy.take_event_stream();
        let _ = wait_for_displays_changed_event(&mut events).await?;

        // Remove all displays.
        mock.assign_displays(vec![])?;
        let (added, removed) = wait_for_displays_changed_event(&mut events).await?;
        assert_eq!(added, vec![]);
        assert_eq!(removed, vec![1]);

        Ok(())
    }
}
