// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_sys2 as fsys;

pub trait ObjectExt {
    fn find(&self, key: &str) -> Option<&fsys::Value>;
}

impl ObjectExt for fsys::Object {
    fn find(&self, key: &str) -> Option<&fsys::Value> {
        for entry in self.entries.iter() {
            if entry.key == key {
                return entry.value.as_ref().map(|x| &**x);
            }
        }
        None
    }
}

// TODO: Delete clone functions once the Rust FIDL bindings provide cloning out of the box.
pub fn clone_object(v: &fsys::Object) -> fsys::Object {
    fsys::Object { entries: v.entries.iter().map(|x| clone_entry(x)).collect() }
}

pub fn clone_option_object(v: &Option<fsys::Object>) -> Option<fsys::Object> {
    v.as_ref().map(|x| clone_object(x))
}

pub fn clone_entry(v: &fsys::Entry) -> fsys::Entry {
    fsys::Entry { key: v.key.clone(), value: clone_option_boxed_value(&v.value) }
}

pub fn clone_vector(v: &fsys::Vector) -> fsys::Vector {
    fsys::Vector { values: v.values.iter().map(|x| clone_option_boxed_value(x)).collect() }
}

pub fn clone_option_vector(v: &Option<fsys::Vector>) -> Option<fsys::Vector> {
    v.as_ref().map(|x| clone_vector(x))
}

pub fn clone_value(v: &fsys::Value) -> fsys::Value {
    match v {
        fsys::Value::Bit(x) => fsys::Value::Bit(*x),
        fsys::Value::Inum(x) => fsys::Value::Inum(*x),
        fsys::Value::Fnum(x) => fsys::Value::Fnum(*x),
        fsys::Value::Str(x) => fsys::Value::Str(x.clone()),
        fsys::Value::Vec(x) => fsys::Value::Vec(clone_vector(x)),
        fsys::Value::Obj(x) => fsys::Value::Obj(clone_object(x)),
    }
}

pub fn clone_option_boxed_value(v: &Option<Box<fsys::Value>>) -> Option<Box<fsys::Value>> {
    v.as_ref().map(|x| Box::new(clone_value(&**x)))
}
