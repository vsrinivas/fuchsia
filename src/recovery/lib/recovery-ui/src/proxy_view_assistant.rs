// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(feature = "debug_console")]
use crate::console::ConsoleMessages;

use carnelian::input::{self};
use std::collections::VecDeque;

use anyhow::Error;
use carnelian::render::Context;
use carnelian::{Message, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr};
use fuchsia_zircon::Event;

#[cfg(test)]
use mockall::{predicate::*, *};

pub enum ProxyMessages {
    NewViewAssistant(Option<ViewAssistantPtr>),
    PopViewAssistant,
}

pub struct ProxyViewAssistant {
    #[cfg(feature = "debug_console")]
    console_view_assistant: Option<ViewAssistantPtr>,
    view_assistant_stack: VecDeque<ViewAssistantPtr>,
    first_call_after_switch: bool,
    #[cfg(feature = "debug_console")]
    console_active: bool,
}

impl ProxyViewAssistant {
    /// Creates a new ProxyViewAssistant with the provided view assistants.
    /// Console is disabled if `console_view_assistant` is `None`.
    pub fn new(
        #[cfg(feature = "debug_console")] console_view_assistant: Option<ViewAssistantPtr>,
        view_assistant_ptr: ViewAssistantPtr,
    ) -> Result<ProxyViewAssistant, Error> {
        let mut view_assistant_stack = VecDeque::new();
        view_assistant_stack.push_front(view_assistant_ptr);

        Ok(ProxyViewAssistant {
            #[cfg(feature = "debug_console")]
            console_view_assistant,
            view_assistant_stack,
            first_call_after_switch: true,
            #[cfg(feature = "debug_console")]
            console_active: false,
        })
    }

    /// Returns true if event should toggle display of Console (and consider the event consumed).
    #[cfg(feature = "debug_console")]
    fn should_console_toggle(&mut self, event: &input::Event) -> bool {
        if self.console_view_assistant.is_none() {
            return false;
        }
        let mut toggle_requested = false;

        match &event.event_type {
            input::EventType::Touch(touch_event) => {
                for contact in &touch_event.contacts {
                    let pointer_event = &input::pointer::Event::new_from_contact(contact);
                    match pointer_event.phase {
                        input::pointer::Phase::Down(location) => {
                            if location.x < 100 && location.y < 100 {
                                toggle_requested = true;
                                break;
                            }
                        }
                        _ => {}
                    };
                }
            }
            _ => {}
        }

        toggle_requested
    }
}

#[cfg_attr(test, automock)]
impl ViewAssistant for ProxyViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        self.view_assistant_stack.front_mut().unwrap().setup(context)
    }

    fn resize(&mut self, new_size: &Size) -> Result<(), Error> {
        self.view_assistant_stack.front_mut().unwrap().resize(new_size)
    }

    fn render(
        &mut self,
        render_context: &mut Context,
        buffer_ready_event: Event,
        view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        #[cfg(feature = "debug_console")]
        if self.console_active {
            if let Some(console) = self.console_view_assistant.as_mut() {
                return console.render(render_context, buffer_ready_event, view_context);
            } else {
                eprintln!("Error: Console could not be found to render");
            }
        }

        self.view_assistant_stack.front_mut().unwrap().render(
            render_context,
            buffer_ready_event,
            view_context,
        )
    }

    fn handle_input_event(
        &mut self,
        context: &mut ViewAssistantContext,
        event: &input::Event,
    ) -> Result<(), Error> {
        #[cfg(feature = "debug_console")]
        if self.should_console_toggle(event) {
            self.console_active = !self.console_active;
            return Ok(()); // Consume the console-toggling event.
        }

        #[cfg(feature = "debug_console")]
        if self.console_active {
            return Ok(()); // Consume the event when the console is active.
        }

        if self.first_call_after_switch {
            self.first_call_after_switch = false;
            self.handle_focus_event(context, true)?;
        }
        self.view_assistant_stack.front_mut().unwrap().handle_input_event(context, event)
    }

    fn handle_mouse_event(
        &mut self,
        context: &mut ViewAssistantContext,
        event: &input::Event,
        mouse_event: &input::mouse::Event,
    ) -> Result<(), Error> {
        self.view_assistant_stack.front_mut().unwrap().handle_mouse_event(
            context,
            event,
            mouse_event,
        )
    }

    fn handle_touch_event(
        &mut self,
        context: &mut ViewAssistantContext,
        event: &input::Event,
        touch_event: &input::touch::Event,
    ) -> Result<(), Error> {
        self.view_assistant_stack.front_mut().unwrap().handle_touch_event(
            context,
            event,
            touch_event,
        )
    }

    fn handle_pointer_event(
        &mut self,
        context: &mut ViewAssistantContext,
        event: &input::Event,
        pointer_event: &input::pointer::Event,
    ) -> Result<(), Error> {
        if self.first_call_after_switch {
            self.first_call_after_switch = false;
            self.handle_focus_event(context, true)?;
        }
        self.view_assistant_stack.front_mut().unwrap().handle_pointer_event(
            context,
            event,
            pointer_event,
        )
    }

    fn handle_keyboard_event(
        &mut self,
        context: &mut ViewAssistantContext,
        event: &input::Event,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<(), Error> {
        self.view_assistant_stack.front_mut().unwrap().handle_keyboard_event(
            context,
            event,
            keyboard_event,
        )
    }

    fn handle_consumer_control_event(
        &mut self,
        context: &mut ViewAssistantContext,
        event: &input::Event,
        consumer_control_event: &input::consumer_control::Event,
    ) -> Result<(), Error> {
        self.view_assistant_stack.front_mut().unwrap().handle_consumer_control_event(
            context,
            event,
            consumer_control_event,
        )
    }

    fn handle_focus_event(
        &mut self,
        context: &mut ViewAssistantContext,
        focused: bool,
    ) -> Result<(), Error> {
        self.view_assistant_stack.front_mut().unwrap().handle_focus_event(context, focused)
    }

    fn handle_message(&mut self, message: Message) {
        if message.is::<ProxyMessages>() {
            let proxy_message = message.downcast::<ProxyMessages>().unwrap();
            match *proxy_message {
                ProxyMessages::NewViewAssistant(mut view_assistant_ptr) => {
                    let view_assistant_ptr =
                        std::mem::replace(&mut view_assistant_ptr, None).unwrap();
                    self.first_call_after_switch = true;
                    self.view_assistant_stack.push_front(view_assistant_ptr);
                }
                ProxyMessages::PopViewAssistant => {
                    if self.view_assistant_stack.len() > 1 {
                        self.first_call_after_switch = true;
                        self.view_assistant_stack.pop_front();
                    }
                }
            }
            return;
        }

        if cfg!(feature = "debug_console") {
            #[cfg(feature = "debug_console")]
            if message.is::<ConsoleMessages>() {
                if let Some(console) = self.console_view_assistant.as_mut() {
                    console.handle_message(message);
                } else {
                    eprintln!("Error: Unable to find console to pass ConsoleMessages");
                }
                return;
            }
        }

        self.view_assistant_stack.front_mut().unwrap().handle_message(message);
    }

    fn uses_pointer_events(&self) -> bool {
        self.view_assistant_stack.front().unwrap().uses_pointer_events()
    }

    fn ownership_changed(&mut self, _owned: bool) -> Result<(), Error> {
        self.view_assistant_stack.front_mut().unwrap().ownership_changed(_owned)
    }

    fn get_render_offset(&mut self) -> Option<i64> {
        self.view_assistant_stack.front_mut().unwrap().get_render_offset()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use carnelian::make_message;

    enum MockMessageType {
        MockMessage,
    }

    fn get_input_event() -> input::Event {
        let mouse_event = input::mouse::Event {
            buttons: Default::default(),
            phase: input::mouse::Phase::Moved,
            location: Default::default(),
        };
        input::Event {
            event_time: 0,
            device_id: Default::default(),
            event_type: input::EventType::Mouse(mouse_event),
        }
    }

    #[test]
    fn test_proxy_view_assistant_switching() -> std::result::Result<(), anyhow::Error> {
        #[cfg(feature = "debug_console")]
        let mock0 = MockProxyViewAssistant::new();
        let mut mock1 = MockProxyViewAssistant::new();
        mock1.expect_handle_message().times(2).return_const(());
        let mut mock2 = MockProxyViewAssistant::new();
        mock2.expect_handle_message().times(1).return_const(());
        let mut proxy = ProxyViewAssistant::new(
            #[cfg(feature = "debug_console")]
            Box::new(mock0),
            Box::new(mock1),
        )
        .unwrap();
        assert_eq!(proxy.view_assistant_stack.len(), 1);
        proxy.handle_message(make_message(MockMessageType::MockMessage));
        proxy.handle_message(make_message(ProxyMessages::NewViewAssistant(Some(Box::new(mock2)))));
        assert_eq!(proxy.view_assistant_stack.len(), 2);
        proxy.handle_message(make_message(MockMessageType::MockMessage));
        proxy.handle_message(make_message(ProxyMessages::PopViewAssistant));
        assert_eq!(proxy.view_assistant_stack.len(), 1);
        proxy.handle_message(make_message(MockMessageType::MockMessage));
        Ok(())
    }

    #[test]
    fn test_proxy_view_assistant_pop_first_entry() -> std::result::Result<(), anyhow::Error> {
        #[cfg(feature = "debug_console")]
        let mock0 = MockProxyViewAssistant::new();
        let mut mock1 = MockProxyViewAssistant::new();
        mock1.expect_handle_message().times(0).return_const(());
        let mut proxy = ProxyViewAssistant::new(
            #[cfg(feature = "debug_console")]
            Box::new(mock0),
            Box::new(mock1),
        )
        .unwrap();
        assert_eq!(proxy.view_assistant_stack.len(), 1);
        proxy.handle_message(make_message(ProxyMessages::PopViewAssistant));
        assert_eq!(proxy.view_assistant_stack.len(), 1);
        Ok(())
    }

    #[test]
    fn test_proxy_view_assistant_first_call_pointer_event() -> std::result::Result<(), anyhow::Error>
    {
        let context = &mut ViewAssistantContext::new_for_testing();
        #[cfg(feature = "debug_console")]
        let mock0 = MockProxyViewAssistant::new();
        let mut mock1 = MockProxyViewAssistant::new();
        mock1.expect_handle_pointer_event().times(2).returning(|_, _, _| Ok(()));
        mock1.expect_handle_focus_event().times(1).returning(|_, _| Ok(()));
        let mut proxy = ProxyViewAssistant::new(
            #[cfg(feature = "debug_console")]
            Box::new(mock0),
            Box::new(mock1),
        )
        .unwrap();
        let event = get_input_event();
        let pointer_id =
            input::pointer::PointerId::Mouse(input::DeviceId("Mouse Event".to_owned()));
        let pointer_event =
            input::pointer::Event { phase: input::pointer::Phase::Up, pointer_id: pointer_id };
        assert_eq!(proxy.first_call_after_switch, true);
        proxy.handle_pointer_event(context, &event, &pointer_event)?;
        assert_eq!(proxy.first_call_after_switch, false);
        proxy.handle_pointer_event(context, &event, &pointer_event)?;
        assert_eq!(proxy.first_call_after_switch, false);
        Ok(())
    }

    #[test]
    fn test_proxy_view_assistant_first_call_input_event() -> std::result::Result<(), anyhow::Error>
    {
        let context = &mut ViewAssistantContext::new_for_testing();
        #[cfg(feature = "debug_console")]
        let mock0 = MockProxyViewAssistant::new();
        let mut mock1 = MockProxyViewAssistant::new();
        mock1.expect_handle_input_event().times(2).returning(|_, _| Ok(()));
        mock1.expect_handle_focus_event().times(1).returning(|_, _| Ok(()));
        let mut proxy = ProxyViewAssistant::new(
            #[cfg(feature = "debug_console")]
            Box::new(mock0),
            Box::new(mock1),
        )
        .unwrap();
        let event = get_input_event();
        assert_eq!(proxy.first_call_after_switch, true);
        proxy.handle_input_event(context, &event)?;
        assert_eq!(proxy.first_call_after_switch, false);
        proxy.handle_input_event(context, &event)?;
        assert_eq!(proxy.first_call_after_switch, false);
        Ok(())
    }
    #[test]

    fn test_proxy_view_assistant_first_call_input_pointer_event(
    ) -> std::result::Result<(), anyhow::Error> {
        let context = &mut ViewAssistantContext::new_for_testing();
        #[cfg(feature = "debug_console")]
        let mock0 = MockProxyViewAssistant::new();
        let mut mock1 = MockProxyViewAssistant::new();
        mock1.expect_handle_pointer_event().times(1).returning(|_, _, _| Ok(()));
        mock1.expect_handle_input_event().times(1).returning(|_, _| Ok(()));
        mock1.expect_handle_focus_event().times(1).returning(|_, _| Ok(()));
        let mut proxy = ProxyViewAssistant::new(
            #[cfg(feature = "debug_console")]
            Box::new(mock0),
            Box::new(mock1),
        )
        .unwrap();
        let event = get_input_event();
        let pointer_id =
            input::pointer::PointerId::Mouse(input::DeviceId("Mouse Event".to_owned()));
        let pointer_event =
            input::pointer::Event { phase: input::pointer::Phase::Up, pointer_id: pointer_id };
        assert_eq!(proxy.first_call_after_switch, true);
        proxy.handle_pointer_event(context, &event, &pointer_event)?;
        assert_eq!(proxy.first_call_after_switch, false);
        proxy.handle_input_event(context, &event)?;
        assert_eq!(proxy.first_call_after_switch, false);
        Ok(())
    }
}
