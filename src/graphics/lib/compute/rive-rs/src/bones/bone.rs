// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    bones::SkeletalComponent,
    component::Component,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    dyn_vec::{DynVec, DynVecIter},
    status_code::StatusCode,
    TransformComponent,
};

#[derive(Debug, Default)]
pub struct Bone {
    skeletal_component: SkeletalComponent,
    length: Property<f32>,
    child_bones: DynVec<Object<Self>>,
}

impl ObjectRef<'_, Bone> {
    pub fn length(&self) -> f32 {
        self.length.get()
    }

    pub fn set_length(&self, length: f32) {
        if self.length() == length {
            return;
        }

        self.length.set(length);

        for bone in self.child_bones.iter() {
            bone.cast::<TransformComponent>().as_ref().mark_transform_dirty();
        }
    }
}

impl ObjectRef<'_, Bone> {
    pub fn child_bones(&self) -> DynVecIter<'_, Object<Bone>> {
        self.child_bones.iter()
    }

    pub fn push_child_bone(&self, child_bone: Object<Bone>) {
        self.child_bones.push(child_bone);
    }

    pub(crate) fn x(&self) -> f32 {
        let parent = self.cast::<Component>().parent().expect("Bone does not have a parent");
        parent.cast::<Bone>().as_ref().length()
    }

    pub(crate) fn y(&self) -> f32 {
        0.0
    }
}

impl Core for Bone {
    parent_types![(skeletal_component, SkeletalComponent)];

    properties![(89, length, set_length), skeletal_component];
}

impl OnAdded for ObjectRef<'_, Bone> {
    on_added!([on_added_dirty, import], SkeletalComponent);

    fn on_added_clean(&self, context: &dyn CoreContext) -> StatusCode {
        self.cast::<SkeletalComponent>().on_added_clean(context);

        if let Some(parent) = self.try_cast::<Component>().and_then(|component| component.parent())
        {
            parent.cast::<Bone>().as_ref().push_child_bone(self.as_object());
        } else {
            return StatusCode::MissingObject;
        }

        StatusCode::Ok
    }
}
