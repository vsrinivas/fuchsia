// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{mem, rc::Rc};

use crate::{
    component::Component,
    component_dirt::ComponentDirt,
    core::{Core, CoreContext, ObjectRef, OnAdded, Property},
    option_cell::OptionCell,
    shapes::{
        paint::{stroke_effect::StrokeEffect, Stroke},
        CommandPath, CommandPathBuilder, MetricsPath,
    },
    status_code::StatusCode,
};

#[derive(Debug, Default)]
pub struct TrimPath {
    component: Component,
    start: Property<f32>,
    end: Property<f32>,
    offset: Property<f32>,
    mode_value: Property<u64>,
    command_path: OptionCell<Rc<CommandPath>>,
}

impl ObjectRef<'_, TrimPath> {
    pub fn start(&self) -> f32 {
        self.start.get()
    }

    pub fn set_start(&self, start: f32) {
        self.start.set(start);
        self.invalidate_effect();
    }

    pub fn end(&self) -> f32 {
        self.end.get()
    }

    pub fn set_end(&self, end: f32) {
        self.end.set(end);
        self.invalidate_effect();
    }

    pub fn offset(&self) -> f32 {
        self.offset.get()
    }

    pub fn set_offset(&self, offset: f32) {
        self.offset.set(offset);
        self.invalidate_effect();
    }

    pub fn mode_value(&self) -> u64 {
        self.mode_value.get()
    }

    pub fn set_mode_value(&self, mode_value: u64) {
        self.mode_value.set(mode_value);
        self.invalidate_effect();
    }
}

impl StrokeEffect for ObjectRef<'_, TrimPath> {
    fn effect_path(&self, source: &mut MetricsPath) -> Rc<CommandPath> {
        if let Some(command_path) = self.command_path.get() {
            return command_path;
        }

        let mut render_offset = self.offset().fract();
        if render_offset.is_sign_negative() {
            render_offset += 1.0;
        }

        // todo!("implement mode 2");

        let total_len = source.compute_length();
        let mut start_len = total_len * (self.start() + render_offset);
        let mut end_len = total_len * (self.end() + render_offset);

        if end_len < start_len {
            mem::swap(&mut start_len, &mut end_len);
        }

        if start_len > total_len {
            start_len -= total_len;
            end_len -= total_len;
        }

        let mut builder = CommandPathBuilder::new();

        while end_len > 0.0 {
            if start_len < total_len {
                source.trimmed(&mut builder, start_len, end_len, true);
                end_len -= total_len;
                start_len = 0.0;
            } else {
                start_len -= total_len;
                end_len -= total_len;
            }
        }

        let command_path = Rc::new(builder.build());
        self.command_path.set(Some(command_path.clone()));

        command_path
    }

    fn invalidate_effect(&self) {
        self.command_path.set(None);
        let stroke = self.cast::<Component>().parent().unwrap().cast::<Stroke>();

        stroke.as_ref().outlined_stroke.set(None);
        stroke
            .cast::<Component>()
            .as_ref()
            .parent()
            .unwrap()
            .cast::<Component>()
            .as_ref()
            .add_dirt(ComponentDirt::PAINT, false);
    }
}

impl Core for TrimPath {
    parent_types![(component, Component)];

    properties![
        (114, start, set_start),
        (115, end, set_end),
        (116, offset, set_offset),
        (117, mode_value, set_mode_value),
        component,
    ];
}

impl OnAdded for ObjectRef<'_, TrimPath> {
    on_added!([on_added_dirty, import], Component);

    fn on_added_clean(&self, _context: &dyn CoreContext) -> StatusCode {
        if let Some(stroke) =
            self.cast::<Component>().parent().and_then(|parent| parent.try_cast::<Stroke>())
        {
            stroke.as_ref().set_stroke_effect(Some(self.as_object().into()));
            StatusCode::Ok
        } else {
            StatusCode::InvalidObject
        }
    }
}
