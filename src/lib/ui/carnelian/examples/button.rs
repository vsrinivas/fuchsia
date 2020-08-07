// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This button sample implements a rotation scheme that is a prototype
// for how Carnelian and apps will deal with drawing to the frame buffer
// on devices where the screen is mounted rotated relative to the viewing
// angle.

use anyhow::Error;
use carnelian::{
    color::Color,
    drawing::{
        make_font_description, path_for_corner_knockouts, path_for_rectangle, DisplayAligned,
        DisplayRotation, GlyphMap, Paint, Text,
    },
    input::{self},
    make_app_assistant, make_message,
    render::{
        BlendMode, Composition, Context as RenderContext, Fill, FillRule, Layer, PreClear, Raster,
        RenderExt, Style,
    },
    App, AppAssistant, Coord, IntPoint, Message, Point, Rect, Size, ViewAssistant,
    ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use euclid::Transform2D;
use fuchsia_zircon::{AsHandleRef, ClockId, Event, Signals, Time};

/// enum that defines all messages sent with `App::queue_message` that
/// the button view assistant will understand and process.
pub enum ButtonMessages {
    Pressed(Time),
}

#[derive(Default)]
struct ButtonAppAssistant;

impl AppAssistant for ButtonAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(ButtonViewAssistant::new()?))
    }
}

fn raster_for_rectangle(
    bounds: &Rect,
    display_rotation: &DisplayRotation,
    render_context: &mut RenderContext,
) -> Raster {
    let rotation_transform =
        display_rotation.rotation().and_then(|transform| Some(transform.to_untyped()));
    let mut raster_builder = render_context.raster_builder().expect("raster_builder");
    raster_builder.add(&path_for_rectangle(bounds, render_context), rotation_transform.as_ref());
    raster_builder.build()
}

fn raster_for_corner_knockouts(
    bounds: &Rect,
    corner_radius: Coord,
    display_rotation: &DisplayRotation,
    render_context: &mut RenderContext,
) -> Raster {
    let rotation_transform =
        display_rotation.transform(&bounds.size).and_then(|transform| Some(transform.to_untyped()));
    let path = path_for_corner_knockouts(bounds, corner_radius, render_context);
    let mut raster_builder = render_context.raster_builder().expect("raster_builder");
    raster_builder.add(&path, rotation_transform.as_ref());
    raster_builder.build()
}

struct RasterAndStyle {
    location: Point,
    raster: Raster,
    style: Style,
}

struct Button {
    display_rotation: DisplayRotation,
    pub font_size: u32,
    pub padding: f32,
    bounds: Rect,
    bg_color: Color,
    bg_color_active: Color,
    bg_color_disabled: Color,
    fg_color: Color,
    fg_color_disabled: Color,
    tracking_pointer: Option<input::pointer::PointerId>,
    active: bool,
    focused: bool,
    glyphs: GlyphMap,
    label_text: String,
    label: Option<Text>,
}

impl Button {
    pub fn new(text: &str, display_rotation: DisplayRotation) -> Result<Button, Error> {
        let button = Button {
            display_rotation,
            font_size: 20,
            padding: 5.0,
            bounds: Rect::zero(),
            fg_color: Color::white(),
            bg_color: Color::from_hash_code("#B7410E")?,
            bg_color_active: Color::from_hash_code("#f0703c")?,
            fg_color_disabled: Color::from_hash_code("#A0A0A0")?,
            bg_color_disabled: Color::from_hash_code("#C0C0C0")?,
            tracking_pointer: None,
            active: false,
            focused: false,
            glyphs: GlyphMap::new_with_rotation(display_rotation),
            label_text: text.to_string(),
            label: None,
        };

        Ok(button)
    }

    pub fn set_focused(&mut self, focused: bool) {
        self.focused = focused;
        if !focused {
            self.active = false;
            self.tracking_pointer = None;
        }
    }

    fn create_rasters_and_styles(
        &mut self,
        render_context: &mut RenderContext,
    ) -> Result<(RasterAndStyle, RasterAndStyle), Error> {
        // set up paint with different backgrounds depending on whether the button
        // is active. The active state is true when a pointer has gone down in the
        // button's bounds and the pointer has not moved outside the bounds since.
        let paint = if self.focused {
            Paint {
                fg: self.fg_color,
                bg: if self.active { self.bg_color_active } else { self.bg_color },
            }
        } else {
            Paint { fg: self.fg_color_disabled, bg: self.bg_color_disabled }
        };

        let font_description = make_font_description(self.font_size, 0);

        self.label = Some(Text::new(
            render_context,
            &self.label_text,
            self.font_size as f32,
            100,
            font_description.face,
            &mut self.glyphs,
        ));

        let label = self.label.as_ref().expect("label");

        // calculate button size based on label's text size
        // plus padding.
        let bounding_box_size = label.bounding_box.size;

        let button_label_size = Size::new(bounding_box_size.width, self.font_size as f32);
        let double_padding = 2.0 * self.padding;
        let button_size = button_label_size + Size::new(double_padding, double_padding);
        let half_size = Size::new(button_size.width * 0.5, button_size.height * 0.5);
        let button_origin = Point::zero() - half_size.to_vector();
        let button_bounds = Rect::new(button_origin, button_size).round_out();

        // record bounds for hit testing
        self.bounds = button_bounds;

        // Calculate the label offset in display aligned coordinates, since the label,
        // as a raster, is pre-rotated and we just need to translate it to align with the buttons
        // bounding box.

        let center = self.bounds.center();
        let label_center = label.bounding_box.center().to_vector();
        let label_offset = center - label_center;
        let raster = raster_for_rectangle(&self.bounds, &self.display_rotation, render_context);
        let button_raster_and_style = RasterAndStyle {
            location: Point::zero(),
            raster,
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(paint.bg),
                blend_mode: BlendMode::Over,
            },
        };
        let label_raster_and_style = RasterAndStyle {
            location: label_offset,
            raster: label.raster.clone(),
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(paint.fg),
                blend_mode: BlendMode::Over,
            },
        };
        Ok((button_raster_and_style, label_raster_and_style))
    }

    pub fn handle_pointer_event(
        &mut self,
        context: &mut ViewAssistantContext,
        pointer_event: &input::pointer::Event,
    ) {
        if !self.focused {
            return;
        }

        let bounds = self.bounds;

        if self.tracking_pointer.is_none() {
            match pointer_event.phase {
                input::pointer::Phase::Down(location) => {
                    self.active = bounds.contains(location.to_f32());
                    if self.active {
                        self.tracking_pointer = Some(pointer_event.pointer_id.clone());
                    }
                }
                _ => (),
            }
        } else {
            let tracking_pointer = self.tracking_pointer.as_ref().expect("tracking_pointer");
            if tracking_pointer == &pointer_event.pointer_id {
                match pointer_event.phase {
                    input::pointer::Phase::Moved(location) => {
                        self.active = bounds.contains(location.to_f32());
                    }
                    input::pointer::Phase::Up => {
                        if self.active {
                            context.queue_message(make_message(ButtonMessages::Pressed(
                                Time::get(ClockId::Monotonic),
                            )));
                        }
                        self.tracking_pointer = None;
                        self.active = false;
                    }
                    input::pointer::Phase::Remove => {
                        self.active = false;
                        self.tracking_pointer = None;
                    }
                    input::pointer::Phase::Cancel => {
                        self.active = false;
                        self.tracking_pointer = None;
                    }
                    _ => (),
                }
            }
        }
    }
}

struct ButtonViewAssistant {
    focused: bool,
    bg_color: Color,
    button: Button,
    display_rotation: DisplayRotation,
    red_light: bool,
    composition: Composition,
}

const BUTTON_LABEL: &'static str = "Depress Me";

impl ButtonViewAssistant {
    fn new() -> Result<ButtonViewAssistant, Error> {
        let bg_color = Color::from_hash_code("#EBD5B3")?;
        let display_rotation = DisplayRotation::Deg0;
        let composition = Composition::new(bg_color);
        Ok(ButtonViewAssistant {
            focused: false,
            bg_color,
            display_rotation,
            button: Button::new(BUTTON_LABEL, display_rotation)?,
            red_light: false,
            composition,
        })
    }

    fn rotate(&mut self) -> Result<(), Error> {
        self.display_rotation = match self.display_rotation {
            DisplayRotation::Deg0 => DisplayRotation::Deg90,
            DisplayRotation::Deg90 => DisplayRotation::Deg180,
            DisplayRotation::Deg180 => DisplayRotation::Deg270,
            DisplayRotation::Deg270 => DisplayRotation::Deg0,
        };
        self.button = Button::new(BUTTON_LABEL, self.display_rotation)?;
        Ok(())
    }

    fn target_size(&self, size: Size) -> Size {
        match self.display_rotation {
            DisplayRotation::Deg90 => Size::new(size.height, size.width),
            DisplayRotation::Deg180 => Size::new(size.width, size.height),
            DisplayRotation::Deg270 => Size::new(size.height, size.width),
            DisplayRotation::Deg0 => size,
        }
    }

    fn button_center(&self, size: Size) -> Point {
        Point::new(size.width * 0.5, size.height * 0.7)
    }

    fn transform_pointer_location(
        location: IntPoint,
        button_center: Point,
        transform: &Transform2D<Coord, DisplayAligned, euclid::UnknownUnit>,
    ) -> IntPoint {
        let transformed_location = transform
            .transform_point(location.to_f32().cast_unit::<DisplayAligned>())
            .cast_unit::<euclid::UnknownUnit>();
        (transformed_location - button_center.to_vector()).to_i32()
    }
}

impl ViewAssistant for ButtonViewAssistant {
    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        // Emulate the size that Carnelian passes when the display is rotated
        let target_size = self.target_size(context.size);

        // Create a presentation to display tranformation
        let transform = self.display_rotation.transform(&target_size);

        // Calculate all locations in the presentation-aligned coordinate space
        let center_x = target_size.width * 0.5;

        let min_dimension = target_size.width.min(target_size.height);
        let font_size = (min_dimension / 5.0).ceil().min(64.0) as u32;
        let padding = (min_dimension / 20.0).ceil().max(8.0);

        self.button.padding = padding;
        self.button.font_size = font_size;

        let corner_knockouts = raster_for_corner_knockouts(
            &Rect::from_size(target_size),
            10.0,
            &self.display_rotation,
            render_context,
        );

        let corner_knockouts_layer = Layer {
            raster: corner_knockouts,
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(Color::new()),
                blend_mode: BlendMode::Over,
            },
        };

        // Position and size the indicator in presentation space
        let indicator_y = target_size.height / 5.0;
        let indicator_len = target_size.height.min(target_size.width) / 8.0;
        let indicator_size = Size::new(indicator_len * 2.0, indicator_len);
        let indicator_pos = Point::new(center_x - indicator_len, indicator_y - indicator_len / 2.0);

        // Now transform the indicator location to the display aligned coordinate system
        let display_indicator_pos = if let Some(transform) = transform {
            transform.transform_point(indicator_pos)
        } else {
            indicator_pos.cast_unit::<DisplayAligned>()
        };

        // create a raster for the indicator. raster_for_rectangle will transform it
        // based on the display orientation. After, translate it to the correct position.
        let indicator_raster = raster_for_rectangle(
            &Rect::new(Point::zero(), indicator_size),
            &self.display_rotation,
            render_context,
        )
        .translate(display_indicator_pos.to_vector().cast_unit::<euclid::UnknownUnit>().to_i32());

        let indicator_color = if self.red_light {
            Color::from_hash_code("#ff0000")?
        } else {
            Color::from_hash_code("#00ff00")?
        };

        // Create a layer for the indicator using its pre-transformed raster and
        // transformed position.
        let indicator_layer = Layer {
            raster: indicator_raster,
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(indicator_color),
                blend_mode: BlendMode::Over,
            },
        };

        let button_center = self.button_center(target_size);
        self.button.set_focused(self.focused);

        // Let the button render itself, returning rasters, styles and zero-relative
        // positions.
        let (button_raster_and_style, label_raster_and_style) =
            self.button.create_rasters_and_styles(render_context)?;

        // Calculate the button location in presentation space
        let button_location = button_center + button_raster_and_style.location.to_vector();

        // Transform it to display space.
        let button_location = if let Some(transform) = transform {
            transform.transform_point(button_location)
        } else {
            button_location.cast_unit::<DisplayAligned>()
        };

        // Calculate the label location in presentation space
        let label_location = button_center + label_raster_and_style.location.to_vector();

        // Transform it to display space.
        let label_location = if let Some(transform) = transform {
            transform.transform_point(label_location)
        } else {
            label_location.cast_unit::<DisplayAligned>()
        };

        // Create layers from the rasters, styles and transformed locations.
        let button_layer = Layer {
            raster: button_raster_and_style
                .raster
                .translate(button_location.to_vector().cast_unit::<euclid::UnknownUnit>().to_i32()),
            style: button_raster_and_style.style,
        };
        let label_layer = Layer {
            raster: label_raster_and_style
                .raster
                .translate(label_location.to_vector().cast_unit::<euclid::UnknownUnit>().to_i32()),
            style: label_raster_and_style.style,
        };
        self.composition.replace(
            ..,
            std::iter::once(corner_knockouts_layer)
                .chain(std::iter::once(label_layer))
                .chain(std::iter::once(button_layer))
                .chain(std::iter::once(indicator_layer)),
        );

        let image = render_context.get_current_image(context);
        let ext =
            RenderExt { pre_clear: Some(PreClear { color: self.bg_color }), ..Default::default() };
        render_context.render(&self.composition, None, image, &ext);
        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;

        Ok(())
    }

    fn handle_message(&mut self, message: Message) {
        if let Some(button_message) = message.downcast_ref::<ButtonMessages>() {
            match button_message {
                ButtonMessages::Pressed(value) => {
                    println!("value = {:#?}", value);
                    self.red_light = !self.red_light
                }
            }
        }
    }

    fn handle_pointer_event(
        &mut self,
        context: &mut ViewAssistantContext,
        _event: &input::Event,
        pointer_event: &input::pointer::Event,
    ) -> Result<(), Error> {
        let target_size = self.target_size(context.size);
        let button_center = self.button_center(target_size);
        // create an inverse transform to convert pointer event locations
        // into presentation space.
        let transform = self
            .display_rotation
            .transform(&target_size)
            .and_then(|transform| transform.inverse())
            .unwrap_or(Transform2D::identity());
        let translated_pointer_event = match pointer_event.phase {
            input::pointer::Phase::Down(location) => input::pointer::Event {
                phase: input::pointer::Phase::Down(Self::transform_pointer_location(
                    location,
                    button_center,
                    &transform,
                )),
                ..pointer_event.clone()
            },
            input::pointer::Phase::Moved(location) => input::pointer::Event {
                phase: input::pointer::Phase::Moved(Self::transform_pointer_location(
                    location,
                    button_center,
                    &transform,
                )),
                ..pointer_event.clone()
            },
            _ => pointer_event.clone(),
        };
        self.button.handle_pointer_event(context, &translated_pointer_event);
        context.request_render();
        Ok(())
    }

    fn handle_focus_event(
        &mut self,
        context: &mut ViewAssistantContext,
        focused: bool,
    ) -> Result<(), Error> {
        self.focused = focused;
        context.request_render();
        Ok(())
    }

    fn handle_keyboard_event(
        &mut self,
        context: &mut ViewAssistantContext,
        _event: &input::Event,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<(), Error> {
        if let Some(code_point) = keyboard_event.code_point {
            if code_point == ' ' as u32 && keyboard_event.phase == input::keyboard::Phase::Pressed {
                self.rotate()?;
                context.request_render();
            }
        }
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    App::run(make_app_assistant::<ButtonAppAssistant>())
}
