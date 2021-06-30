// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::args::{MAX_FONT_SIZE, MIN_FONT_SIZE},
    crate::colors::ColorScheme,
    crate::terminal::Terminal,
    crate::text_grid::{font_to_cell_size, TextGridFacet, TextGridMessages},
    anyhow::{anyhow, Error},
    carnelian::{
        color::Color,
        drawing::{load_font, FontFace},
        input, make_message,
        render::{rive::load_rive, Context as RenderContext},
        scene::{
            facets::{FacetId, RiveFacet},
            scene::{Scene, SceneBuilder},
        },
        AppContext, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
    },
    fidl_fuchsia_hardware_display::VirtconMode,
    fidl_fuchsia_hardware_pty::WindowSize,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    pty::{key_util::CodePoint, key_util::HidUsage, Pty},
    rive_rs as rive,
    std::{
        collections::{BTreeMap, BTreeSet},
        io::Write,
        path::PathBuf,
    },
    term_model::{
        event::{Event, EventListener},
        term::SizeInfo,
    },
};

fn is_control_only(modifiers: &input::Modifiers) -> bool {
    modifiers.control && !modifiers.shift && !modifiers.alt && !modifiers.caps_lock
}

fn get_input_sequence_for_key_event(event: &input::keyboard::Event) -> Option<String> {
    match event.phase {
        input::keyboard::Phase::Pressed | input::keyboard::Phase::Repeat => {
            match event.code_point {
                None => HidUsage(event.hid_usage).into(),
                Some(code_point) => CodePoint {
                    code_point: code_point,
                    control_pressed: is_control_only(&event.modifiers),
                }
                .into(),
            }
        }
        _ => None,
    }
}

/// Returns a scale factor given a set of DPI buckets and an actual DPI value.
/// First bucket gets scale factor 1.0, second gets 2.0, third gets 3.0, etc.
fn get_scale_factor(dpi: &BTreeSet<u32>, actual_dpi: f32) -> f32 {
    let mut scale_factor = 1.0;
    for value in dpi.iter() {
        if *value as f32 > actual_dpi {
            break;
        }
        scale_factor += 1.0;
    }
    scale_factor
}

pub struct EventProxy {
    app_context: AppContext,
    view_key: ViewKey,
    id: u32,
}

impl EventProxy {
    pub fn new(app_context: &AppContext, view_key: ViewKey, id: u32) -> Self {
        Self { app_context: app_context.clone(), view_key, id }
    }
}

impl EventListener for EventProxy {
    fn send_event(&self, event: Event) {
        match event {
            Event::MouseCursorDirty => {
                self.app_context.queue_message(
                    self.view_key,
                    make_message(ViewMessages::RequestTerminalUpdateMessage(self.id)),
                );
            }
            _ => (),
        }
    }
}

pub enum ViewMessages {
    AddTerminalMessage(u32, Terminal<EventProxy>),
    RequestTerminalUpdateMessage(u32),
}

const STATUS_COLOR_BACKGROUND: Color = Color { r: 0, g: 0, b: 0, a: 255 };
const STATUS_COLOR_DEFAULT: Color = Color { r: 170, g: 170, b: 170, a: 255 };
const STATUS_COLOR_ACTIVE: Color = Color { r: 255, g: 255, b: 85, a: 255 };
const STATUS_COLOR_UPDATED: Color = Color { r: 85, g: 255, b: 85, a: 255 };

const CELL_PADDING_FACTOR: f32 = 1.0 / 15.0;

struct Animation {
    // Artboard has weak references to data owned by file.
    _animation: rive::File,
    artboard: rive::Object<rive::Artboard>,
    instance: rive::animation::LinearAnimationInstance,
    last_presentation_time: Option<zx::Time>,
}

struct SceneDetails {
    scene: Scene,
    textgrid: Option<FacetId>,
}

pub struct VirtualConsoleViewAssistant {
    app_context: AppContext,
    view_key: ViewKey,
    color_scheme: ColorScheme,
    round_scene_corners: bool,
    font_size: f32,
    dpi: BTreeSet<u32>,
    scene_details: Option<SceneDetails>,
    terminals: BTreeMap<u32, (Terminal<EventProxy>, bool)>,
    font: FontFace,
    animation: Option<Animation>,
    active_terminal_id: u32,
    virtcon_mode: VirtconMode,
    desired_virtcon_mode: VirtconMode,
    owns_display: bool,
}

const BOOT_ANIMATION: &'static str = "/pkg/data/boot-animation.riv";
const FONT: &'static str = "/pkg/data/font.ttf";

impl VirtualConsoleViewAssistant {
    pub fn new(
        app_context: &AppContext,
        view_key: ViewKey,
        color_scheme: ColorScheme,
        round_scene_corners: bool,
        font_size: f32,
        dpi: BTreeSet<u32>,
        boot_animation: bool,
    ) -> Result<ViewAssistantPtr, Error> {
        let scene_details = None;
        let terminals = BTreeMap::new();
        let active_terminal_id = 0;
        let font = load_font(PathBuf::from(FONT))?;
        let virtcon_mode = VirtconMode::Forced; // We always start out in forced mode.
        let (animation, desired_virtcon_mode) = if boot_animation {
            let animation = load_rive(PathBuf::from(BOOT_ANIMATION))?;
            let artboard = animation.artboard().ok_or_else(|| anyhow!("missing artboard"))?;
            let first_animation = artboard
                .as_ref()
                .animations()
                .next()
                .ok_or_else(|| anyhow!("missing animation"))?;
            let instance = rive::animation::LinearAnimationInstance::new(first_animation);
            let last_presentation_time = None;
            let animation = Some(Animation {
                _animation: animation,
                artboard,
                instance,
                last_presentation_time,
            });

            (animation, VirtconMode::Forced)
        } else {
            (None, VirtconMode::Fallback)
        };
        let owns_display = true;

        Ok(Box::new(VirtualConsoleViewAssistant {
            app_context: app_context.clone(),
            view_key,
            color_scheme,
            round_scene_corners,
            font_size,
            dpi,
            scene_details,
            terminals,
            font,
            animation,
            active_terminal_id,
            virtcon_mode,
            desired_virtcon_mode,
            owns_display,
        }))
    }

    #[cfg(test)]
    fn new_for_test(animation: bool) -> Result<ViewAssistantPtr, Error> {
        let app_context = AppContext::new_for_testing_purposes_only();
        let dpi: BTreeSet<u32> = [160, 320, 480, 640].iter().cloned().collect();
        Self::new(&app_context, 1, ColorScheme::default(), false, 15.0, dpi, animation)
    }

    // Resize all terminals for 'new_size'.
    fn resize_terminals(&mut self, new_size: &Size, new_font_size: f32) {
        let floored_size = new_size.floor();
        let cell_size = font_to_cell_size(new_font_size, new_font_size * CELL_PADDING_FACTOR);
        let size = Size::new(floored_size.width, floored_size.height - cell_size.height);
        let size_info = SizeInfo {
            width: size.width,
            height: size.height,
            cell_width: cell_size.width,
            cell_height: cell_size.height,
            padding_x: 0.0,
            padding_y: 0.0,
            dpr: 1.0,
        };

        for (terminal, _) in self.terminals.values_mut() {
            terminal.resize(&size_info);
        }

        let pty_fds: Vec<_> = self
            .terminals
            .values()
            .map(|(t, _)| t.try_clone_pty_fd().expect("failed to clone PTY fd"))
            .collect();
        let window_size = WindowSize { width: size.width as u32, height: size.height as u32 };

        fasync::Task::local(async move {
            for pty_fd in &pty_fds {
                Pty::set_window_size(pty_fd, window_size).await.expect("failed to set window size");
            }
        })
        .detach();
    }

    // This returns a vector with the status for each terminal. The return value
    // is suitable for passing to the TextGridFacet.
    fn get_status(&self) -> Vec<(String, Color)> {
        self.terminals
            .iter()
            .map(|(id, (t, updated))| {
                let color = if *id == self.active_terminal_id {
                    STATUS_COLOR_ACTIVE
                } else if *updated {
                    STATUS_COLOR_UPDATED
                } else {
                    STATUS_COLOR_DEFAULT
                };
                (format!("[{}] {}", *id, t.title()), color)
            })
            .collect()
    }

    fn cancel_animation(&mut self) {
        if self.animation.is_some() {
            self.desired_virtcon_mode = VirtconMode::Fallback;
            self.scene_details = None;
            self.animation = None;
            self.app_context.request_render(self.view_key);
        }
    }

    fn set_desired_virtcon_mode(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        if self.desired_virtcon_mode != self.virtcon_mode {
            self.virtcon_mode = self.desired_virtcon_mode;
            if let Some(fb) = context.frame_buffer.as_ref() {
                fb.borrow_mut().set_virtcon_mode(self.virtcon_mode)?;
            }
        }
        Ok(())
    }

    fn set_active_terminal(&mut self, id: u32) {
        if let Some((terminal, updated)) = self.terminals.get_mut(&id) {
            self.active_terminal_id = id;
            *updated = false;
            let terminal = terminal.clone_term();
            let new_status = self.get_status();
            if let Some(scene_details) = &mut self.scene_details {
                if let Some(textgrid) = &scene_details.textgrid {
                    scene_details.scene.send_message(
                        textgrid,
                        Box::new(TextGridMessages::<EventProxy>::ChangeStatusMessage(new_status)),
                    );
                    scene_details.scene.send_message(
                        textgrid,
                        Box::new(TextGridMessages::SetTermMessage(terminal)),
                    );
                    self.app_context.request_render(self.view_key);
                }
            }
        }
    }

    fn update_status(&mut self) {
        let new_status = self.get_status();
        if let Some(scene_details) = &mut self.scene_details {
            if let Some(textgrid) = &scene_details.textgrid {
                scene_details.scene.send_message(
                    textgrid,
                    Box::new(TextGridMessages::<EventProxy>::ChangeStatusMessage(new_status)),
                );
                self.app_context.request_render(self.view_key);
            }
        }
    }

    fn set_font_size(&mut self, font_size: f32) {
        self.font_size = font_size;
        self.scene_details = None;
        self.app_context.request_render(self.view_key);
    }

    fn handle_device_control_keyboard_event(
        &mut self,
        context: &mut ViewAssistantContext,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<bool, Error> {
        if keyboard_event.phase == input::keyboard::Phase::Pressed {
            if keyboard_event.code_point.is_none() {
                const HID_USAGE_KEY_ESC: u32 = 0x29;

                match keyboard_event.hid_usage {
                    HID_USAGE_KEY_ESC if keyboard_event.modifiers.alt == true => {
                        self.cancel_animation();
                        self.desired_virtcon_mode =
                            if self.desired_virtcon_mode == VirtconMode::Fallback {
                                VirtconMode::Forced
                            } else {
                                VirtconMode::Fallback
                            };
                        self.set_desired_virtcon_mode(context)?;
                        return Ok(true);
                    }
                    // TODO: Implement other interactions.
                    _ => {}
                }
            }
        }

        Ok(false)
    }

    fn handle_control_keyboard_event(
        &mut self,
        _context: &mut ViewAssistantContext,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<bool, Error> {
        if keyboard_event.phase == input::keyboard::Phase::Pressed {
            match keyboard_event.code_point {
                None => {
                    const HID_USAGE_KEY_ESC: u32 = 0x29;
                    const HID_USAGE_KEY_F1: u32 = 0x3a;
                    const HID_USAGE_KEY_F10: u32 = 0x43;

                    match keyboard_event.hid_usage {
                        HID_USAGE_KEY_ESC => {
                            self.cancel_animation();
                            return Ok(true);
                        }
                        HID_USAGE_KEY_F1..=HID_USAGE_KEY_F10
                            if keyboard_event.modifiers.alt == true =>
                        {
                            let id = keyboard_event.hid_usage - HID_USAGE_KEY_F1;
                            self.set_active_terminal(id);
                            return Ok(true);
                        }
                        // TODO: Implement other interactions.
                        _ => {}
                    }
                }
                Some(code_point) => {
                    const PLUS: u32 = 43;
                    const MINUS: u32 = 45;

                    match code_point {
                        PLUS if keyboard_event.modifiers.alt == true => {
                            let new_font_size = (self.font_size + 15.0).min(MAX_FONT_SIZE);
                            self.set_font_size(new_font_size);
                            return Ok(true);
                        }
                        MINUS if keyboard_event.modifiers.alt == true => {
                            let new_font_size = (self.font_size - 15.0).max(MIN_FONT_SIZE);
                            self.set_font_size(new_font_size);
                            return Ok(true);
                        }
                        // TODO: Implement other interactions.
                        _ => {}
                    }
                }
            }
        }

        Ok(false)
    }
}

impl ViewAssistant for VirtualConsoleViewAssistant {
    fn resize(&mut self, _new_size: &Size) -> Result<(), Error> {
        self.scene_details = None;
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: zx::Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let mut scene_details = self.scene_details.take().unwrap_or_else(|| {
            let mut builder = SceneBuilder::new()
                .background_color(self.color_scheme.back)
                .enable_mouse_cursor(false)
                .round_scene_corners(self.round_scene_corners);

            let textgrid = if let Some(animation) = &self.animation {
                builder.facet(Box::new(RiveFacet::new(context.size, animation.artboard.clone())));
                None
            } else {
                let scale_factor = if let Some(fb) = context.frame_buffer.as_ref() {
                    const MM_PER_INCH: f32 = 25.4;

                    let config = fb.borrow_mut().get_config();
                    let dpi = config.height as f32 * MM_PER_INCH / config.vertical_size_mm as f32;

                    get_scale_factor(&self.dpi, dpi)
                } else {
                    1.0
                };
                let font_size = self.font_size * scale_factor;
                let cell_size = font_to_cell_size(font_size, font_size * CELL_PADDING_FACTOR);
                let status_size = Size::new(context.size.width, cell_size.height);

                self.resize_terminals(&context.size, font_size);

                let active_term =
                    self.terminals.get(&self.active_terminal_id).map(|(t, _)| t.clone_term());
                let status = self.get_status();

                // Constraints on status bar tabs.
                const MIN_TAB_WIDTH: usize = 16;
                const MAX_TAB_WIDTH: usize = 32;

                // Determine the status bar tab width based on the current number
                // of terminals.
                let tab_width = (status_size.width as usize / (status.len() + 1))
                    .clamp(MIN_TAB_WIDTH, MAX_TAB_WIDTH);

                // Add the text grid to the scene.
                let textgrid = builder.facet(Box::new(TextGridFacet::new(
                    self.font.clone(),
                    font_size,
                    self.color_scheme.front,
                    active_term,
                    status,
                    tab_width,
                    font_size * CELL_PADDING_FACTOR,
                )));

                // Add status bar background to the scene if needed.
                if self.color_scheme.back != STATUS_COLOR_BACKGROUND {
                    builder.rectangle(status_size, STATUS_COLOR_BACKGROUND);
                }

                Some(textgrid)
            };

            SceneDetails { scene: builder.build(), textgrid }
        });

        if let Some(animation) = &mut self.animation {
            let presentation_time = context.presentation_time;
            let elapsed = if let Some(last_presentation_time) = animation.last_presentation_time {
                const NANOS_PER_SECOND: f32 = 1_000_000_000.0;
                (presentation_time - last_presentation_time).into_nanos() as f32 / NANOS_PER_SECOND
            } else {
                0.0
            };
            animation.last_presentation_time = Some(presentation_time);

            let artboard_ref = animation.artboard.as_ref();
            animation.instance.advance(elapsed);
            animation.instance.apply(animation.artboard.clone(), 1.0);
            artboard_ref.advance(elapsed);
        }

        scene_details.scene.render(render_context, ready_event, context)?;
        self.scene_details = Some(scene_details);

        if let Some(animation) = &mut self.animation {
            // Switch to fallback mode when animation ends so primary client can
            // take over. Otherwise, request another frame.
            if animation.instance.is_done() {
                self.desired_virtcon_mode = VirtconMode::Fallback;
            } else {
                context.request_render();
            }
        }

        self.set_desired_virtcon_mode(context)?;

        Ok(())
    }

    fn handle_keyboard_event(
        &mut self,
        context: &mut ViewAssistantContext,
        _event: &input::Event,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<(), Error> {
        if self.handle_device_control_keyboard_event(context, keyboard_event)? {
            return Ok(());
        }

        if !self.owns_display {
            return Ok(());
        }

        if self.handle_control_keyboard_event(context, keyboard_event)? {
            return Ok(());
        }

        // Get input sequence and write it to the active terminal.
        if let Some(string) = get_input_sequence_for_key_event(keyboard_event) {
            if let Some((terminal, _)) = self.terminals.get_mut(&self.active_terminal_id) {
                terminal
                    .write_all(string.as_bytes())
                    .unwrap_or_else(|e| println!("failed to write to terminal: {}", e));
            }
        }

        Ok(())
    }

    fn handle_message(&mut self, message: carnelian::Message) {
        if let Some(message) = message.downcast_ref::<ViewMessages>() {
            match message {
                ViewMessages::AddTerminalMessage(id, terminal) => {
                    let terminal = terminal.try_clone().expect("failed to clone terminal");
                    let updated = false;
                    self.terminals.insert(*id, (terminal, updated));
                    // Rebuild the scene after a terminal is added. This should
                    // be fine as it is rare that a terminal is added.
                    if self.animation.is_none() {
                        self.scene_details = None;
                        self.app_context.request_render(self.view_key);
                    }
                }
                ViewMessages::RequestTerminalUpdateMessage(id) => {
                    if self.animation.is_none() {
                        if let Some((_, updated)) = self.terminals.get_mut(id) {
                            if *id == self.active_terminal_id {
                                self.app_context.request_render(self.view_key);
                            } else if !*updated {
                                *updated = true;
                                self.update_status();
                            }
                        }
                    }
                }
            }
        }
    }

    fn ownership_changed(&mut self, owned: bool) -> Result<(), Error> {
        self.owns_display = owned;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn can_create_view() -> Result<(), Error> {
        let animation = false;
        let _ = VirtualConsoleViewAssistant::new_for_test(animation)?;
        Ok(())
    }

    #[test]
    fn can_create_view_with_animation() -> Result<(), Error> {
        let animation = true;
        let _ = VirtualConsoleViewAssistant::new_for_test(animation)?;
        Ok(())
    }
}
