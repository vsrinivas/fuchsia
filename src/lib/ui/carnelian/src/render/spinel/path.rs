// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, rc::Rc};

use spinel_rs_sys::*;

use crate::{
    render::{
        spinel::{init, InnerContext, Spinel},
        PathBuilder,
    },
    Point,
};

#[derive(Clone, Debug)]
pub struct SpinelPath {
    context: Rc<RefCell<InnerContext>>,
    pub(crate) path: Rc<SpnPath>,
}

impl Drop for SpinelPath {
    fn drop(&mut self) {
        if let Some(context) = self.context.borrow().get_checked() {
            if Rc::strong_count(&self.path) == 1 {
                unsafe {
                    spn!(spn_path_release(context, &*self.path as *const _, 1));
                }
            }
        }
    }
}

impl Eq for SpinelPath {}

impl PartialEq for SpinelPath {
    fn eq(&self, other: &Self) -> bool {
        Rc::ptr_eq(&self.context, &other.context) && Rc::ptr_eq(&self.path, &other.path)
    }
}

#[derive(Debug)]
pub struct SpinelPathBuilder {
    pub(crate) context: Rc<RefCell<InnerContext>>,
    pub(crate) path_builder: Rc<SpnPathBuilder>,
}

impl PathBuilder<Spinel> for SpinelPathBuilder {
    fn move_to(&mut self, point: Point) -> &mut Self {
        unsafe {
            spn!(spn_path_builder_move_to(*self.path_builder, point.x, point.y));
        }
        self
    }

    fn line_to(&mut self, point: Point) -> &mut Self {
        unsafe {
            spn!(spn_path_builder_line_to(*self.path_builder, point.x, point.y));
        }
        self
    }

    fn quad_to(&mut self, p1: Point, p2: Point) -> &mut Self {
        unsafe {
            spn!(spn_path_builder_quad_to(*self.path_builder, p1.x, p1.y, p2.x, p2.y));
        }
        self
    }

    fn cubic_to(&mut self, p1: Point, p2: Point, p3: Point) -> &mut Self {
        unsafe {
            spn!(spn_path_builder_cubic_to(*self.path_builder, p1.x, p1.y, p2.x, p2.y, p3.x, p3.y));
        }
        self
    }

    fn build(self) -> SpinelPath {
        SpinelPath {
            context: Rc::clone(&self.context),
            path: Rc::new(unsafe {
                init(|ptr| spn!(spn_path_builder_end(*self.path_builder, ptr)))
            }),
        }
    }
}
