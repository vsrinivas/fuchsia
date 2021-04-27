// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::Cell, iter};

use once_cell::unsync::OnceCell;

use crate::{
    bones::{
        skinnable::{self, Skinnable},
        Tendon,
    },
    component::Component,
    component_dirt::ComponentDirt,
    container_component::ContainerComponent,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    dyn_vec::DynVec,
    math::Mat,
    option_cell::OptionCell,
    shapes::PathVertex,
    status_code::StatusCode,
    TransformComponent,
};

#[derive(Debug)]
pub struct Skin {
    container_component: ContainerComponent,
    xx: Property<f32>,
    yx: Property<f32>,
    xy: Property<f32>,
    yy: Property<f32>,
    tx: Property<f32>,
    ty: Property<f32>,
    world_transform: Cell<Mat>,
    tendons: DynVec<Object<Tendon>>,
    bone_transforms: OnceCell<Box<[Cell<Mat>]>>,
    skinnable: OptionCell<Object>,
}

impl ObjectRef<'_, Skin> {
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

impl ObjectRef<'_, Skin> {
    pub fn push_tendon(&self, tendon: Object<Tendon>) {
        self.tendons.push(tendon);
    }

    pub fn deform(&self, vertices: impl Iterator<Item = Object<PathVertex>>) {
        let bone_transforms =
            self.bone_transforms.get().expect("bone_transforms should already be set");
        for vertex in vertices {
            vertex.as_ref().deform(self.world_transform.get(), bone_transforms);
        }
    }

    pub fn build_dependencies(&self) {
        for tendon in self.tendons.iter() {
            let tendon = tendon.as_ref();
            let bone = tendon.bone().expect("Tendon's bone must be set");
            bone.as_ref().cast::<Component>().push_dependent(self.cast::<Component>().as_object());
        }

        self.bone_transforms
            .set(iter::repeat(Cell::new(Mat::default())).take(self.tendons.len() + 1).collect())
            .expect("bone_transform has been set twice");
    }

    pub fn on_dirty(&self, _dirt: ComponentDirt) {
        let skinnable = self.skinnable.get().expect("Skinnable should already be set");
        skinnable::as_ref(skinnable.as_ref()).mark_skin_dirty();
    }

    pub fn update(&self, _value: ComponentDirt) {
        if let Some(bone_transforms) = self.bone_transforms.get() {
            for (bone_transform, tendon) in bone_transforms.iter().skip(1).zip(self.tendons.iter())
            {
                let tendon = tendon.as_ref();
                let bone =
                    tendon.bone().expect("Tendon's bone must be set").cast::<TransformComponent>();
                bone_transform.set(bone.as_ref().world_transform() * tendon.inverse_bind());
            }
        }
    }
}

impl Core for Skin {
    parent_types![(container_component, ContainerComponent)];

    properties![
        (104, xx, set_xx),
        (105, yx, set_yx),
        (106, xy, set_xy),
        (107, yy, set_yy),
        (108, tx, set_tx),
        (109, ty, set_ty),
        container_component,
    ];
}

impl OnAdded for ObjectRef<'_, Skin> {
    on_added!([on_added_dirty, import], ContainerComponent);

    fn on_added_clean(&self, _context: &dyn CoreContext) -> StatusCode {
        self.world_transform.set(Mat {
            scale_x: self.xx(),
            shear_y: self.xy(),
            shear_x: self.yx(),
            scale_y: self.yy(),
            translate_x: self.tx(),
            translate_y: self.ty(),
        });

        self.skinnable.set(
            self.cast::<Component>().parent().and_then(|parent| skinnable::try_from(parent.into())),
        );
        if let Some(skinnable) = self.skinnable.get() {
            skinnable::as_ref(skinnable.as_ref()).set_skin(self.as_object());
        } else {
            return StatusCode::MissingObject;
        }

        StatusCode::Ok
    }
}

impl Default for Skin {
    fn default() -> Self {
        Self {
            container_component: ContainerComponent::default(),
            xx: Property::new(1.0),
            yx: Property::new(0.0),
            xy: Property::new(0.0),
            yy: Property::new(1.0),
            tx: Property::new(0.0),
            ty: Property::new(0.0),
            world_transform: Cell::new(Mat::default()),
            tendons: DynVec::new(),
            bone_transforms: OnceCell::new(),
            skinnable: OptionCell::new(),
        }
    }
}
