// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::Cell;

use crate::{
    bones::{Bone, Skin},
    component::Component,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    math::Mat,
    option_cell::OptionCell,
    status_code::StatusCode,
};

#[derive(Debug)]
pub struct Tendon {
    component: Component,
    bone_id: Property<u64>,
    xx: Property<f32>,
    yx: Property<f32>,
    xy: Property<f32>,
    yy: Property<f32>,
    tx: Property<f32>,
    ty: Property<f32>,
    inverse_bind: Cell<Mat>,
    bone: OptionCell<Object<Bone>>,
}

impl ObjectRef<'_, Tendon> {
    pub fn bone_id(&self) -> u64 {
        self.bone_id.get()
    }

    pub fn set_bone_id(&self, bone_id: u64) {
        self.bone_id.set(bone_id);
    }

    pub fn xx(&self) -> f32 {
        self.xx.get()
    }

    pub fn set_xx(&self, xx: f32) {
        self.xx.set(xx);
    }

    pub fn yx(&self) -> f32 {
        self.yx.get()
    }

    pub fn set_yx(&self, yx: f32) {
        self.yx.set(yx);
    }

    pub fn xy(&self) -> f32 {
        self.xy.get()
    }

    pub fn set_xy(&self, xy: f32) {
        self.xy.set(xy);
    }

    pub fn yy(&self) -> f32 {
        self.yy.get()
    }

    pub fn set_yy(&self, yy: f32) {
        self.yy.set(yy);
    }

    pub fn tx(&self) -> f32 {
        self.tx.get()
    }

    pub fn set_tx(&self, tx: f32) {
        self.tx.set(tx);
    }

    pub fn ty(&self) -> f32 {
        self.ty.get()
    }

    pub fn set_ty(&self, ty: f32) {
        self.ty.set(ty);
    }
}

impl ObjectRef<'_, Tendon> {
    pub fn inverse_bind(&self) -> Mat {
        self.inverse_bind.get()
    }

    pub fn bone(&self) -> Option<Object<Bone>> {
        self.bone.get()
    }
}

impl Core for Tendon {
    parent_types![(component, Component)];

    properties![
        (95, bone_id, set_bone_id),
        (96, xx, set_xx),
        (97, yx, set_yx),
        (98, xy, set_xy),
        (99, yy, set_yy),
        (100, tx, set_tx),
        (101, ty, set_ty),
        component,
    ];
}

impl OnAdded for ObjectRef<'_, Tendon> {
    on_added!([import], Component);

    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        let bind = Mat {
            scale_x: self.xx(),
            shear_y: self.xy(),
            shear_x: self.yx(),
            scale_y: self.yy(),
            translate_x: self.tx(),
            translate_y: self.ty(),
        };

        if let Some(inverse_bind) = bind.invert() {
            self.inverse_bind.set(inverse_bind);
        } else {
            return StatusCode::FailedInversion;
        }

        let code = self.cast::<Component>().on_added_dirty(context);
        if code != StatusCode::Ok {
            return code;
        }

        if let Some(bone) =
            context.resolve(self.bone_id() as usize).and_then(|core| core.try_cast())
        {
            self.bone.set(Some(bone));
            StatusCode::Ok
        } else {
            StatusCode::MissingObject
        }
    }

    fn on_added_clean(&self, _context: &dyn CoreContext) -> StatusCode {
        if let Some(parent) = self
            .try_cast::<Component>()
            .and_then(|component| component.parent())
            .and_then(|core| core.try_cast::<Skin>())
        {
            parent.as_ref().push_tendon(self.as_object());
        } else {
            return StatusCode::MissingObject;
        }

        StatusCode::Ok
    }
}

impl Default for Tendon {
    fn default() -> Self {
        Self {
            component: Component::default(),
            bone_id: Property::new(0),
            xx: Property::new(1.0),
            yx: Property::new(0.0),
            xy: Property::new(0.0),
            yy: Property::new(1.0),
            tx: Property::new(0.0),
            ty: Property::new(0.0),
            inverse_bind: Cell::new(Mat::default()),
            bone: OptionCell::new(),
        }
    }
}
