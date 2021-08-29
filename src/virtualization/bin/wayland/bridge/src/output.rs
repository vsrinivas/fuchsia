// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::Client,
    crate::object::{ObjectRef, RequestReceiver},
    anyhow::Error,
    fidl_fuchsia_ui_gfx::DisplayInfo,
    fuchsia_async as fasync, fuchsia_wayland_core as wl,
    futures::prelude::*,
    wayland::{wl_output, WlOutput, WlOutputEvent, WlOutputRequest},
};

/// An implementation of the wl_output global.
pub struct Output;

impl Output {
    /// Creates a new `Output`.
    pub fn new() -> Self {
        Output
    }

    /// Queries the system display info and posts back to the client.
    pub fn update_display_info(this: wl::ObjectId, client: &mut Client) {
        let scenic = client.display().scenic().clone();
        let task_queue = client.task_queue();
        fasync::Task::local(scenic.get_display_info().map(move |result| {
            if let Ok(display_info) = result {
                let display_info = DisplayInfo { ..display_info };
                task_queue.post(move |client| {
                    Self::post_display_info(this.into(), client, &display_info)?;
                    Ok(())
                });
            }
        }))
        .detach();
    }

    fn post_display_info(
        this: ObjectRef<Self>,
        client: &mut Client,
        display_info: &DisplayInfo,
    ) -> Result<(), Error> {
        // Only post messages if the underlying output is still valid. This is
        // to guard against the case where the wl_output has been released
        // after querying scenic for display info and before the response has
        // been received. This isn't an error, so we'll just no-op here.
        if !this.is_valid(client) {
            return Ok(());
        }

        // Just report the current display info as our only mode.
        client.event_queue().post(
            this.id(),
            WlOutputEvent::Mode {
                flags: wl_output::Mode::Current | wl_output::Mode::Preferred,
                width: display_info.width_in_px as i32,
                height: display_info.height_in_px as i32,
                refresh: 60,
            },
        )?;

        // TODO(tjdetwiler): geometry and scale are not exposed by scenic today.
        // For now we'll provide some placeholder values to allow clients that
        // depend on these to behave reasonably.
        client.event_queue().post(
            this.id(),
            WlOutputEvent::Geometry {
                make: "unknown".to_string(),
                model: "unknown".to_string(),
                x: 0,
                y: 0,
                subpixel: wl_output::Subpixel::None,
                transform: wl_output::Transform::Normal,
                // TODO(tjdetwiler): at 96dpi pixels would be ~.264mm.
                // Approximate this as .25 as a placeholder until we can query
                // scenic for real resolution.
                physical_width: display_info.width_in_px as i32 / 4,
                physical_height: display_info.height_in_px as i32 / 4,
            },
        )?;
        client.event_queue().post(this.id(), WlOutputEvent::Scale { factor: 1 })?;

        // Any series of output events must be concluded with a 'done' event.
        client.event_queue().post(this.id(), WlOutputEvent::Done)?;
        Ok(())
    }
}

impl RequestReceiver<WlOutput> for Output {
    fn receive(
        this: ObjectRef<Self>,
        request: WlOutputRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        let WlOutputRequest::Release = request;
        client.delete_id(this.id())?;
        Ok(())
    }
}
