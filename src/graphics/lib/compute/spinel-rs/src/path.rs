// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, rc::Rc};

use crate::{context::ContextInner, spinel_sys::*};

#[derive(Debug)]
pub(crate) struct PathInner {
    context: Rc<RefCell<ContextInner>>,
    pub(crate) spn_path: SpnPath,
}

impl PathInner {
    fn new(context: &Rc<RefCell<ContextInner>>, spn_path: SpnPath) -> Self {
        Self { context: Rc::clone(context), spn_path }
    }
}

impl Drop for PathInner {
    fn drop(&mut self) {
        self.context.borrow_mut().discard_path(self.spn_path);
    }
}

/// Spinel path created by a `PathBuilder`. [spn_path_t]
///
/// [spn_path_t]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#50
#[derive(Clone, Debug)]
pub struct Path {
    pub(crate) inner: Rc<PathInner>,
}

impl Path {
    pub(crate) fn new(context: &Rc<RefCell<ContextInner>>, spn_path: SpnPath) -> Self {
        Self { inner: Rc::new(PathInner::new(context, spn_path)) }
    }
}

impl Eq for Path {}

impl PartialEq for Path {
    fn eq(&self, other: &Self) -> bool {
        Rc::ptr_eq(&self.inner, &other.inner)
    }
}
