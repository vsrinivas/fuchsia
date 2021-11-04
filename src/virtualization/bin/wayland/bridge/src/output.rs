// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::aura_shell::AuraOutput,
    crate::client::Client,
    crate::object::{ObjectRef, RequestReceiver},
    anyhow::Error,
    fidl_fuchsia_ui_gfx::DisplayInfo,
    fuchsia_async as fasync, fuchsia_wayland_core as wl,
    futures::prelude::*,
    wayland::{wl_output, WlOutput, WlOutputEvent, WlOutputRequest},
};

/// An implementation of the wl_output global.
pub struct Output {
    aura_output: Option<ObjectRef<AuraOutput>>,
}

impl Output {
    /// Creates a new `Output`.
    pub fn new() -> Self {
        Output { aura_output: None }
    }

    pub fn set_aura_output(&mut self, aura_output: ObjectRef<AuraOutput>) {
        self.aura_output = Some(aura_output);
    }

    /// Queries the system display info and posts back to the client.
    pub fn update_display_info(this: wl::ObjectId, client: &mut Client) {
        let scenic = client.display().scenic().clone();
        let task_queue = client.task_queue();
        fasync::Task::local(scenic.get_display_info().map(move |result| {
            if let Ok(display_info) = result {
                let display_info = DisplayInfo { ..display_info };
                task_queue.post(move |client| {
                    // Early out if display has not changed.
                    if client.display_info() == display_info {
                        return Ok(());
                    }
                    client.set_display_info(&display_info);
                    // Only post messages if the underlying output is still valid. This is
                    // to guard against the case where the wl_output has been released
                    // after querying scenic for display info and before the response has
                    // been received. This isn't an error, so we'll just no-op here.
                    let output_ref: ObjectRef<Self> = this.into();
                    if output_ref.is_valid(client) {
                        Self::post_display_info(output_ref, client, &display_info)?;
                    }
                    Ok(())
                });
            }
        }))
        .detach();
    }

    pub fn post_output_info(
        this: wl::ObjectId,
        client: &Client,
        display_info: &DisplayInfo,
    ) -> Result<(), Error> {
        // Just report the current display info as our only mode.
        client.event_queue().post(
            this,
            WlOutputEvent::Mode {
                flags: wl_output::Mode::Current | wl_output::Mode::Preferred,
                width: display_info.width_in_px as i32,
                height: display_info.height_in_px as i32,
                // Vertical refresh rate in mHz.
                refresh: 60 * 1000,
            },
        )?;
        client.event_queue().post(
            this,
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
        client.event_queue().post(this, WlOutputEvent::Scale { factor: 1 })?;
        Ok(())
    }

    pub fn post_output_done(this: wl::ObjectId, client: &Client) -> Result<(), Error> {
        client.event_queue().post(this, WlOutputEvent::Done)
    }

    pub fn post_display_info(
        this: ObjectRef<Self>,
        client: &Client,
        display_info: &DisplayInfo,
    ) -> Result<(), Error> {
        let output = this.get(client)?;

        // Post basic output info.
        Self::post_output_info(this.id(), client, display_info)?;

        // Post additional Aura output info if requested.
        if let Some(aura_output) = output.aura_output {
            AuraOutput::post_display_info(aura_output, client, display_info)?;
        }

        // Any series of output events must be concluded with a 'done' event.
        Self::post_output_done(this.id(), client)?;
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
