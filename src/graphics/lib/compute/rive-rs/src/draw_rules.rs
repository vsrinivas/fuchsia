// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    component::Component,
    component_dirt::ComponentDirt,
    container_component::ContainerComponent,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    draw_target::DrawTarget,
    option_cell::OptionCell,
    status_code::StatusCode,
};

#[derive(Debug, Default)]
pub struct DrawRules {
    container_component: ContainerComponent,
    draw_target_id: Property<u64>,
    active_target: OptionCell<Object<DrawTarget>>,
}

impl ObjectRef<'_, DrawRules> {
    pub fn draw_target_id(&self) -> u64 {
        self.draw_target_id.get()
    }

    pub fn set_draw_target_id(&self, draw_target_id: u64) {
        if self.draw_target_id() == draw_target_id {
            return;
        }

        self.draw_target_id.set(draw_target_id);

        if let Some(artboard) = self.cast::<Component>().artboard() {
            let draw_target = artboard
                .as_ref()
                .resolve(self.draw_target_id() as usize)
                .and_then(|core| core.try_cast());

            self.active_target.set(draw_target);

            artboard.cast::<Component>().as_ref().add_dirt(ComponentDirt::DRAW_ORDER, false);
        }
    }
}

impl ObjectRef<'_, DrawRules> {
    pub fn active_target(&self) -> Option<Object<DrawTarget>> {
        self.active_target.get()
    }
}

impl Core for DrawRules {
    parent_types![(container_component, ContainerComponent)];

    properties![(121, draw_target_id, set_draw_target_id), container_component];
}

impl OnAdded for ObjectRef<'_, DrawRules> {
    on_added!([on_added_clean, import], ContainerComponent);

    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        let code = self.cast::<Component>().on_added_dirty(context);
        if code != StatusCode::Ok {
            return code;
        }

        let draw_target =
            context.resolve(self.draw_target_id() as usize).and_then(|core| core.try_cast());

        self.active_target.set(draw_target);

        StatusCode::Ok
    }
}
