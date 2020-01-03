// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::{
    app::MessageInternal,
    message::Message,
    view::{
        scenic_present, scenic_present_done,
        strategies::base::{ViewStrategy, ViewStrategyPtr},
        ScenicResources, ViewAssistantContext, ViewAssistantPtr, ViewDetails,
    },
};
use async_trait::async_trait;
use fidl_fuchsia_ui_views::ViewToken;
use fuchsia_scenic::SessionPtr;
use fuchsia_zircon::{ClockId, Time};
use futures::channel::mpsc::UnboundedSender;

pub(crate) struct ScenicViewStrategy {
    scenic_resources: ScenicResources,
}

impl ScenicViewStrategy {
    pub(crate) fn new(
        session: &SessionPtr,
        view_token: ViewToken,
        app_sender: UnboundedSender<MessageInternal>,
    ) -> ViewStrategyPtr {
        let scenic_resources = ScenicResources::new(session, view_token, app_sender);
        Box::new(ScenicViewStrategy { scenic_resources })
    }

    fn make_view_assistant_context(&self, view_details: &ViewDetails) -> ViewAssistantContext<'_> {
        ViewAssistantContext {
            key: view_details.key,
            logical_size: view_details.logical_size,
            size: view_details.physical_size,
            metrics: view_details.metrics,
            presentation_time: Time::get(ClockId::Monotonic),
            messages: Vec::new(),
            scenic_resources: Some(&self.scenic_resources),
            canvas: None,
            buffer_count: None,
            wait_event: None,
        }
    }
}

#[async_trait(?Send)]
impl ViewStrategy for ScenicViewStrategy {
    fn setup(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        let context = self.make_view_assistant_context(view_details);
        view_assistant.setup(&context).unwrap_or_else(|e| panic!("Setup error: {:?}", e));
    }

    async fn update(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        let context = self.make_view_assistant_context(view_details);
        view_assistant.update(&context).unwrap_or_else(|e| panic!("Update error: {:?}", e));
    }

    fn present(&mut self, view_details: &ViewDetails) {
        scenic_present(&mut self.scenic_resources, view_details.key);
    }

    fn present_done(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
    ) {
        scenic_present_done(&mut self.scenic_resources);
    }

    fn handle_input_event(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
        event: &fidl_fuchsia_ui_input::InputEvent,
    ) -> Vec<Message> {
        let mut context = self.make_view_assistant_context(view_details);

        view_assistant
            .handle_input_event(&mut context, &event)
            .unwrap_or_else(|e| eprintln!("handle_event: {:?}", e));

        context.messages
    }
}
