// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_data as fdata;

pub trait DictionaryExt {
    fn find(&self, key: &str) -> Option<&fdata::Value>;
}

impl DictionaryExt for fdata::Dictionary {
    fn find(&self, key: &str) -> Option<&fdata::Value> {
        for entry in self.entries.iter() {
            if entry.key == key {
                return entry.value.as_ref().map(|x| &**x);
            }
        }
        None
    }
}

// TODO: Delete clone functions once the Rust FIDL bindings provide cloning out of the box.
pub fn clone_dictionary(v: &fdata::Dictionary) -> fdata::Dictionary {
    fdata::Dictionary { entries: v.entries.iter().map(|x| clone_entry(x)).collect() }
}

pub fn clone_option_dictionary(v: &Option<fdata::Dictionary>) -> Option<fdata::Dictionary> {
    v.as_ref().map(|x| clone_dictionary(x))
}

pub fn clone_entry(v: &fdata::Entry) -> fdata::Entry {
    fdata::Entry { key: v.key.clone(), value: clone_option_boxed_value(&v.value) }
}

pub fn clone_vector(v: &fdata::Vector) -> fdata::Vector {
    fdata::Vector { values: v.values.iter().map(|x| clone_option_boxed_value(x)).collect() }
}

pub fn clone_option_vector(v: &Option<fdata::Vector>) -> Option<fdata::Vector> {
    v.as_ref().map(|x| clone_vector(x))
}

pub fn clone_value(v: &fdata::Value) -> fdata::Value {
    match v {
        fdata::Value::Bit(x) => fdata::Value::Bit(*x),
        fdata::Value::Inum(x) => fdata::Value::Inum(*x),
        fdata::Value::Fnum(x) => fdata::Value::Fnum(*x),
        fdata::Value::Str(x) => fdata::Value::Str(x.clone()),
        fdata::Value::Vec(x) => fdata::Value::Vec(clone_vector(x)),
        fdata::Value::Dict(x) => fdata::Value::Dict(clone_dictionary(x)),
    }
}

pub fn clone_option_boxed_value(v: &Option<Box<fdata::Value>>) -> Option<Box<fdata::Value>> {
    v.as_ref().map(|x| Box::new(clone_value(&**x)))
}
