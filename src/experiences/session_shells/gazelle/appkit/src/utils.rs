// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    fidl_fuchsia_ui_composition as ui_comp,
    futures::channel::mpsc::{UnboundedReceiver, UnboundedSender},
};

use crate::event::Event;

#[derive(Debug)]
pub struct EventSender<T>(pub UnboundedSender<Event<T>>);

impl<T> Clone for EventSender<T> {
    fn clone(&self) -> Self {
        Self(self.0.clone())
    }
}

impl<T> EventSender<T> {
    pub fn new() -> (Self, UnboundedReceiver<Event<T>>) {
        let (sender, receiver) = futures::channel::mpsc::unbounded::<Event<T>>();

        let event_sender = EventSender::<T>(sender);
        event_sender.send(Event::<T>::Init).expect("Failed to send Event::Init event");

        (event_sender, receiver)
    }

    pub fn send(&self, event: Event<T>) -> Result<(), Error> {
        self.0.unbounded_send(event).map_err(|_| anyhow!("EventSender failed to send event."))?;
        Ok(())
    }
}

pub(crate) struct Presenter {
    flatland: ui_comp::FlatlandProxy,
    can_update: bool,
    needs_update: bool,
}

impl Presenter {
    pub fn new(flatland: ui_comp::FlatlandProxy) -> Self {
        Presenter { flatland, can_update: true, needs_update: false }
    }

    pub fn on_next_frame(
        &mut self,
        _next_frame_info: ui_comp::OnNextFrameBeginValues,
    ) -> Result<(), Error> {
        if self.needs_update {
            self.needs_update = false;
            self.can_update = false;
            self.present()
        } else {
            self.can_update = true;
            Ok(())
        }
    }

    pub fn redraw(&mut self) -> Result<(), Error> {
        if self.can_update {
            self.needs_update = false;
            self.can_update = false;
            self.present()
        } else {
            self.needs_update = true;
            Ok(())
        }
    }

    fn present(&self) -> Result<(), Error> {
        self.flatland.present(ui_comp::PresentArgs::EMPTY)?;
        Ok(())
    }
}
