// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::{HashSet, VecDeque};

use crate::{core::Object, Component};

#[derive(Debug, Default)]
pub struct DependencySorter {
    perm: HashSet<Object<Component>>,
    temp: HashSet<Object<Component>>,
}

impl DependencySorter {
    pub fn sort(&mut self, root: Object<Component>, order: &mut VecDeque<Object<Component>>) {
        order.clear();
        self.visit(root, order);
    }

    fn visit(&mut self, component: Object<Component>, order: &mut VecDeque<Object<Component>>) {
        if self.perm.contains(&component) {
            return;
        }

        if self.temp.contains(&component) {
            panic!("dependency cycle");
        }

        self.temp.insert(component.clone());

        for dependent in component.as_ref().depdenents() {
            self.visit(dependent, order);
        }

        self.perm.insert(component.clone());
        order.push_front(component);
    }
}
