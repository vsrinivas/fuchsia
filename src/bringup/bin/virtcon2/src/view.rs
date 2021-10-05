// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::app::EventProxy,
    crate::args::{MAX_FONT_SIZE, MIN_FONT_SIZE},
    crate::colors::{ColorScheme, DARK_COLOR_SCHEME, LIGHT_COLOR_SCHEME, SPECIAL_COLOR_SCHEME},
    crate::terminal::Terminal,
    crate::text_grid::{font_to_cell_size, TextGridFacet, TextGridMessages},
    anyhow::{anyhow, Error},
    carnelian::{
        drawing::{load_font, FontFace},
        input,
        render::{rive::load_rive, Context as RenderContext},
        scene::{
            facets::{FacetId, RiveFacet},
            scene::{Scene, SceneBuilder},
        },
        AppContext, Point, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
    },
    fidl_fuchsia_hardware_display::VirtconMode,
    fidl_fuchsia_hardware_power_statecontrol::{AdminMarker, AdminSynchronousProxy, RebootReason},
    fidl_fuchsia_hardware_pty::WindowSize,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_channel_to_protocol,
    fuchsia_zircon::{self as zx, prelude::*},
    pty::{key_util::CodePoint, key_util::HidUsage, Pty},
    rive_rs as rive,
    std::{
        collections::{BTreeMap, BTreeSet},
        io::Write,
        mem,
        path::PathBuf,
    },
    term_model::{
        ansi::TermInfo,
        grid::Scroll,
        term::{color::Rgb, SizeInfo},
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

pub enum ViewMessages {
    AddTerminalMessage(u32, Terminal<EventProxy>, bool),
    RequestTerminalUpdateMessage(u32),
}

// Constraints on status bar tabs.
const MIN_TAB_WIDTH: usize = 16;
const MAX_TAB_WIDTH: usize = 32;

// Status bar colors.
const STATUS_COLOR_DEFAULT: Rgb = Rgb { r: 170, g: 170, b: 170 };
const STATUS_COLOR_ACTIVE: Rgb = Rgb { r: 255, g: 255, b: 85 };
const STATUS_COLOR_UPDATED: Rgb = Rgb { r: 85, g: 255, b: 85 };

// Padding between text and cell size.
const CELL_PADDING_FACTOR: f32 = 2.0 / 14.0;

// Amount of change to font size when zooming.
const FONT_SIZE_INCREMENT: f32 = 14.0;

// Maximum terminal size.
// TODO(fxbug.dev/85164): Increase these when Mold/Carnelian is not limited to
// 65536 layers.
const MAX_COLUMNS: usize = 240;
const MAX_ROWS: usize = 135;

struct Animation {
    // Artboard has weak references to data owned by file.
    _file: rive::File,
    artboard: rive::Object<rive::Artboard>,
    instance: rive::animation::LinearAnimationInstance,
    last_presentation_time: Option<zx::Time>,
}

struct SceneDetails {
    scene: Scene,
    textgrid: Option<FacetId>,
}

#[derive(Clone, Copy, Eq, PartialEq)]
struct TerminalStatus {
    pub has_output: bool,
    pub at_top: bool,
    pub at_bottom: bool,
}

pub struct VirtualConsoleViewAssistant {
    app_context: AppContext,
    view_key: ViewKey,
    color_scheme: ColorScheme,
    round_scene_corners: bool,
    font_size: f32,
    dpi: BTreeSet<u32>,
    cell_size: Size,
    tab_width: usize,
    scene_details: Option<SceneDetails>,
    terminals: BTreeMap<u32, (Terminal<EventProxy>, TerminalStatus)>,
    font: FontFace,
    animation: Option<Animation>,
    active_terminal_id: u32,
    virtcon_mode: VirtconMode,
    desired_virtcon_mode: VirtconMode,
    owns_display: bool,
    active_pointer_id: Option<input::pointer::PointerId>,
    start_pointer_location: Point,
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
        let cell_size = Size::new(8.0, 16.0);
        let tab_width = MIN_TAB_WIDTH;
        let scene_details = None;
        let terminals = BTreeMap::new();
        let active_terminal_id = 0;
        let font = load_font(PathBuf::from(FONT))?;
        let virtcon_mode = VirtconMode::Forced; // We always start out in forced mode.
        let (animation, desired_virtcon_mode) = if boot_animation {
            let file = load_rive(BOOT_ANIMATION)?;
            let artboard = file.artboard().ok_or_else(|| anyhow!("missing artboard"))?;
            let artboard_ref = artboard.as_ref();
            let color_scheme_name = match color_scheme {
                DARK_COLOR_SCHEME => "dark",
                LIGHT_COLOR_SCHEME => "light",
                SPECIAL_COLOR_SCHEME => "special",
                _ => "other",
            };
            // Find animation that matches color scheme or fallback to first animation
            // if not found.
            let animation = artboard_ref
                .animations()
                .find(|animation| {
                    let name = animation.cast::<rive::animation::Animation>().as_ref().name();
                    name == color_scheme_name
                })
                .or_else(|| artboard_ref.animations().next())
                .ok_or_else(|| anyhow!("missing animation"))?;
            let instance = rive::animation::LinearAnimationInstance::new(animation);
            let last_presentation_time = None;
            let animation =
                Some(Animation { _file: file, artboard, instance, last_presentation_time });

            (animation, VirtconMode::Forced)
        } else {
            (None, VirtconMode::Fallback)
        };
        let owns_display = true;
        let active_pointer_id = None;
        let start_pointer_location = Point::zero();

        Ok(Box::new(VirtualConsoleViewAssistant {
            app_context: app_context.clone(),
            view_key,
            color_scheme,
            round_scene_corners,
            font_size,
            dpi,
            cell_size,
            tab_width,
            scene_details,
            terminals,
            font,
            animation,
            active_terminal_id,
            virtcon_mode,
            desired_virtcon_mode,
            owns_display,
            active_pointer_id,
            start_pointer_location,
        }))
    }

    #[cfg(test)]
    fn new_for_test(animation: bool) -> Result<ViewAssistantPtr, Error> {
        let app_context = AppContext::new_for_testing_purposes_only();
        let dpi: BTreeSet<u32> = [160, 320, 480, 640].iter().cloned().collect();
        Self::new(&app_context, 1, ColorScheme::default(), false, 14.0, dpi, animation)
    }

    // Resize all terminals for 'new_size'.
    fn resize_terminals(&mut self, new_size: &Size, new_font_size: f32) {
        let cell_size = font_to_cell_size(new_font_size, new_font_size * CELL_PADDING_FACTOR);
        let grid_size =
            Size::new(new_size.width / cell_size.width, new_size.height / cell_size.height).floor();
        let clamped_grid_size = grid_size.min(Size::new(MAX_COLUMNS as f32, MAX_ROWS as f32));
        let clamped_size = Size::new(
            clamped_grid_size.width * cell_size.width,
            clamped_grid_size.height * cell_size.height,
        );
        let size = Size::new(clamped_size.width, clamped_size.height - cell_size.height);
        let size_info = SizeInfo {
            width: size.width,
            height: size.height,
            cell_width: cell_size.width,
            cell_height: cell_size.height,
            padding_x: 0.0,
            padding_y: 0.0,
            dpr: 1.0,
        };

        self.cell_size = cell_size;

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
                if let Some(fd) = pty_fd {
                    Pty::set_window_size(fd, window_size).await.expect("failed to set window size");
                }
            }
        })
        .detach();
    }

    // This returns a vector with the status for each terminal. The return value
    // is suitable for passing to the TextGridFacet.
    fn get_status(&self) -> Vec<(String, Rgb)> {
        self.terminals
            .iter()
            .map(|(id, (t, status))| {
                let fg = if *id == self.active_terminal_id {
                    STATUS_COLOR_ACTIVE
                } else if status.has_output {
                    STATUS_COLOR_UPDATED
                } else {
                    STATUS_COLOR_DEFAULT
                };

                let left = if status.at_top { '[' } else { '<' };
                let right = if status.at_bottom { ']' } else { '>' };

                (format!("{}{}{} {}", left, *id, right, t.title()), fg)
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
        if let Some((terminal, status)) = self.terminals.get_mut(&id) {
            self.active_terminal_id = id;
            status.has_output = false;
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

    fn next_active_terminal(&mut self) {
        let first = self.terminals.keys().next();
        let last = self.terminals.keys().next_back();
        if let Some((first, last)) = first.and_then(|first| last.map(|last| (first, last))) {
            let active = self.active_terminal_id;
            let id = if active == *last { *first } else { active + 1 };
            self.set_active_terminal(id);
        }
    }

    fn previous_active_terminal(&mut self) {
        let first = self.terminals.keys().next();
        let last = self.terminals.keys().next_back();
        if let Some((first, last)) = first.and_then(|first| last.map(|last| (first, last))) {
            let active = self.active_terminal_id;
            let id = if active == *first { *last } else { active - 1 };
            self.set_active_terminal(id);
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

    fn scroll_active_terminal(&mut self, scroll: Scroll) {
        if let Some((terminal, _)) = self.terminals.get_mut(&self.active_terminal_id) {
            terminal.scroll(scroll);
        }
    }

    fn handle_device_control_keyboard_event(
        &mut self,
        context: &mut ViewAssistantContext,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<bool, Error> {
        if keyboard_event.phase == input::keyboard::Phase::Pressed {
            if keyboard_event.code_point.is_none() {
                const HID_USAGE_KEY_ESC: u32 = 0x29;
                const HID_USAGE_KEY_DELETE: u32 = 0x4c;

                let modifiers = &keyboard_event.modifiers;
                match keyboard_event.hid_usage {
                    HID_USAGE_KEY_ESC if modifiers.alt => {
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
                    // Provides a CTRL-ALT-DEL reboot sequence.
                    HID_USAGE_KEY_DELETE if modifiers.control && modifiers.alt => {
                        let (server_end, client_end) = zx::Channel::create()?;
                        connect_channel_to_protocol::<AdminMarker>(server_end)?;
                        let admin = AdminSynchronousProxy::new(client_end);
                        match admin
                            .reboot(RebootReason::UserRequest, zx::Time::after(5.second()))?
                        {
                            Ok(()) => {
                                // Wait for the world to end.
                                zx::Time::INFINITE.sleep();
                            }
                            Err(e) => println!("Failed to reboot, status: {}", e),
                        }
                        return Ok(true);
                    }
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
        match keyboard_event.phase {
            input::keyboard::Phase::Pressed | input::keyboard::Phase::Repeat => {
                let modifiers = &keyboard_event.modifiers;
                match keyboard_event.code_point {
                    None => {
                        const HID_USAGE_KEY_ESC: u32 = 0x29;
                        const HID_USAGE_KEY_TAB: u32 = 0x2b;
                        const HID_USAGE_KEY_F1: u32 = 0x3a;
                        const HID_USAGE_KEY_F10: u32 = 0x43;
                        const HID_USAGE_KEY_HOME: u32 = 0x4a;
                        const HID_USAGE_KEY_PAGEUP: u32 = 0x4b;
                        const HID_USAGE_KEY_END: u32 = 0x4d;
                        const HID_USAGE_KEY_PAGEDOWN: u32 = 0x4e;
                        const HID_USAGE_KEY_DOWN: u32 = 0x51;
                        const HID_USAGE_KEY_UP: u32 = 0x52;
                        const HID_USAGE_KEY_VOL_DOWN: u32 = 0xe8;
                        const HID_USAGE_KEY_VOL_UP: u32 = 0xe9;

                        match keyboard_event.hid_usage {
                            HID_USAGE_KEY_ESC => {
                                self.cancel_animation();
                                return Ok(true);
                            }
                            HID_USAGE_KEY_F1..=HID_USAGE_KEY_F10 if modifiers.alt => {
                                let id = keyboard_event.hid_usage - HID_USAGE_KEY_F1;
                                self.set_active_terminal(id);
                                return Ok(true);
                            }
                            HID_USAGE_KEY_TAB if modifiers.alt => {
                                if modifiers.shift {
                                    self.previous_active_terminal();
                                } else {
                                    self.next_active_terminal();
                                }
                                return Ok(true);
                            }
                            HID_USAGE_KEY_VOL_UP if modifiers.alt => {
                                self.previous_active_terminal();
                                return Ok(true);
                            }
                            HID_USAGE_KEY_VOL_DOWN if modifiers.alt => {
                                self.next_active_terminal();
                                return Ok(true);
                            }
                            HID_USAGE_KEY_UP if modifiers.alt => {
                                self.scroll_active_terminal(Scroll::Lines(1));
                                return Ok(true);
                            }
                            HID_USAGE_KEY_DOWN if modifiers.alt => {
                                self.scroll_active_terminal(Scroll::Lines(-1));
                                return Ok(true);
                            }
                            HID_USAGE_KEY_PAGEUP if modifiers.shift => {
                                self.scroll_active_terminal(Scroll::PageUp);
                                return Ok(true);
                            }
                            HID_USAGE_KEY_PAGEDOWN if modifiers.shift => {
                                self.scroll_active_terminal(Scroll::PageDown);
                                return Ok(true);
                            }
                            HID_USAGE_KEY_HOME if modifiers.shift => {
                                self.scroll_active_terminal(Scroll::Top);
                                return Ok(true);
                            }
                            HID_USAGE_KEY_END if modifiers.shift => {
                                self.scroll_active_terminal(Scroll::Bottom);
                                return Ok(true);
                            }
                            _ => {}
                        }
                    }
                    Some(code_point) if modifiers.alt == true => {
                        const PLUS: u32 = 43;
                        const EQUAL: u32 = 61;
                        const MINUS: u32 = 45;

                        match code_point {
                            PLUS | EQUAL => {
                                let new_font_size =
                                    (self.font_size + FONT_SIZE_INCREMENT).min(MAX_FONT_SIZE);
                                self.set_font_size(new_font_size);
                                return Ok(true);
                            }
                            MINUS => {
                                let new_font_size =
                                    (self.font_size - FONT_SIZE_INCREMENT).max(MIN_FONT_SIZE);
                                self.set_font_size(new_font_size);
                                return Ok(true);
                            }
                            _ => {}
                        }
                    }
                    _ => {}
                }
            }
            _ => {}
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
                .round_scene_corners(self.round_scene_corners)
                .mutable(false);

            let textgrid = if let Some(animation) = &self.animation {
                builder.facet(Box::new(RiveFacet::new(context.size, animation.artboard.clone())));
                None
            } else {
                let scale_factor = if let Some(fb) = context.frame_buffer.as_ref() {
                    let config = fb.borrow_mut().get_config();
                    // Use 1.0 scale factor when fallback sizes are used as apposed
                    // to actual values reported by the display.
                    if config.using_fallback_size {
                        1.0
                    } else {
                        const MM_PER_INCH: f32 = 25.4;

                        let dpi =
                            config.height as f32 * MM_PER_INCH / config.vertical_size_mm as f32;

                        get_scale_factor(&self.dpi, dpi)
                    }
                } else {
                    1.0
                };
                let font_size = self.font_size * scale_factor;
                let cell_size = font_to_cell_size(font_size, font_size * CELL_PADDING_FACTOR);

                self.resize_terminals(&context.size, font_size);

                let active_term =
                    self.terminals.get(&self.active_terminal_id).map(|(t, _)| t.clone_term());
                let status = self.get_status();
                let columns = active_term.as_ref().map(|t| t.borrow().cols().0).unwrap_or(1);

                // Determine the status bar tab width based on the current number
                // of terminals.
                let tab_width =
                    (columns as usize / (status.len() + 1)).clamp(MIN_TAB_WIDTH, MAX_TAB_WIDTH);

                // Add the text grid to the scene.
                let textgrid = builder.facet(Box::new(TextGridFacet::new(
                    self.font.clone(),
                    font_size,
                    self.color_scheme,
                    active_term,
                    status,
                    tab_width,
                    font_size * CELL_PADDING_FACTOR,
                )));

                self.cell_size = cell_size;
                self.tab_width = tab_width;

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

            // Scroll to bottom on input.
            self.scroll_active_terminal(Scroll::Bottom);
        }

        Ok(())
    }

    fn handle_pointer_event(
        &mut self,
        _context: &mut ViewAssistantContext,
        _event: &input::Event,
        pointer_event: &input::pointer::Event,
    ) -> Result<(), Error> {
        match &pointer_event.phase {
            input::pointer::Phase::Down(location) => {
                self.active_pointer_id = Some(pointer_event.pointer_id.clone());
                self.start_pointer_location = location.to_f32();
            }
            input::pointer::Phase::Moved(location) => {
                if Some(pointer_event.pointer_id.clone()) == self.active_pointer_id {
                    let location_offset = location.to_f32() - self.start_pointer_location;

                    fn div_and_trunc(value: f32, divisor: f32) -> isize {
                        (value / divisor).trunc() as isize
                    }

                    // Movement along X-axis changes active terminal.
                    let tab_width = self.tab_width as f32 * self.cell_size.width;
                    let mut terminal_offset = div_and_trunc(location_offset.x, tab_width);
                    while terminal_offset > 0 {
                        self.previous_active_terminal();
                        self.start_pointer_location.x += tab_width;
                        terminal_offset -= 1;
                    }
                    while terminal_offset < 0 {
                        self.next_active_terminal();
                        self.start_pointer_location.x -= tab_width;
                        terminal_offset += 1;
                    }

                    // Movement along Y-axis scrolls active terminal.
                    let cell_offset = div_and_trunc(location_offset.y, self.cell_size.height);
                    if cell_offset != 0 {
                        self.scroll_active_terminal(Scroll::Lines(cell_offset));
                        self.start_pointer_location.y += cell_offset as f32 * self.cell_size.height;
                    }
                }
            }
            input::pointer::Phase::Up => {
                if Some(pointer_event.pointer_id.clone()) == self.active_pointer_id {
                    self.active_pointer_id = None;
                }
            }
            _ => (),
        }
        Ok(())
    }

    fn handle_message(&mut self, message: carnelian::Message) {
        if let Some(message) = message.downcast_ref::<ViewMessages>() {
            match message {
                ViewMessages::AddTerminalMessage(id, terminal, make_active) => {
                    let terminal = terminal.try_clone().expect("failed to clone terminal");
                    let has_output = true;
                    let display_offset = terminal.display_offset();
                    let at_top = display_offset == terminal.history_size();
                    let at_bottom = display_offset == 0;
                    self.terminals
                        .insert(*id, (terminal, TerminalStatus { has_output, at_top, at_bottom }));
                    // Rebuild the scene after a terminal is added. This should
                    // be fine as it is rare that a terminal is added.
                    if self.animation.is_none() {
                        self.scene_details = None;
                        self.app_context.request_render(self.view_key);
                    }
                    if *make_active {
                        self.set_active_terminal(*id);
                    }
                }
                ViewMessages::RequestTerminalUpdateMessage(id) => {
                    if let Some((terminal, status)) = self.terminals.get_mut(id) {
                        let has_output = if *id == self.active_terminal_id {
                            self.app_context.request_render(self.view_key);
                            false
                        } else {
                            true
                        };
                        let display_offset = terminal.display_offset();
                        let at_top = display_offset == terminal.history_size();
                        let at_bottom = display_offset == 0;
                        let new_status = TerminalStatus { has_output, at_top, at_bottom };
                        let old_status = mem::replace(status, new_status);
                        if new_status != old_status {
                            self.update_status();
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
