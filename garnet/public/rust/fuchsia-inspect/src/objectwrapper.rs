// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::object::ObjectUtil;
use fidl_fuchsia_inspect::{Metric, MetricValue, Object, Property, PropertyValue};
use std::collections::HashMap;

pub struct ObjectWrapper {
    pub name: String,
    pub properties: HashMap<String, PropertyValueWrapper>,
    pub metrics: HashMap<String, MetricValueWrapper>,
}

pub type PropertyValueGenerator = Box<FnMut() -> PropertyValue + Send + 'static>;

pub enum PropertyValueWrapper {
    Static(PropertyValue),
    Dynamic(PropertyValueGenerator),
}

pub type MetricValueGenerator = Box<FnMut() -> MetricValue + Send + 'static>;

pub enum MetricValueWrapper {
    Static(MetricValue),
    Dynamic(MetricValueGenerator),
}

impl ObjectWrapper {
    pub fn from_object(mut obj: Object) -> ObjectWrapper {
        ObjectWrapper {
            name: obj.name,
            properties: obj
                .properties
                .get_or_insert(vec![])
                .into_iter()
                .map(|p| (p.key.clone(), PropertyValueWrapper::Static(p.value.clone())))
                .collect(),
            metrics: obj
                .metrics
                .get_or_insert(vec![])
                .into_iter()
                .map(|m| (m.key.clone(), MetricValueWrapper::Static(m.value.clone())))
                .collect(),
        }
    }

    pub fn evaluate(&mut self) -> Object {
        let mut res = Object::new(self.name.clone());
        for (key, mut val_wrapper) in &mut self.properties {
            res.properties.get_or_insert(vec![]).push(Property {
                key: key.clone(),
                value: match &mut val_wrapper {
                    PropertyValueWrapper::Static(val) => val.clone(),
                    PropertyValueWrapper::Dynamic(f) => f(),
                },
            });
        }
        for (key, mut val_wrapper) in &mut self.metrics {
            res.metrics.get_or_insert(vec![]).push(Metric {
                key: key.clone(),
                value: match &mut val_wrapper {
                    MetricValueWrapper::Static(val) => val.clone(),
                    MetricValueWrapper::Dynamic(f) => f(),
                },
            });
        }
        res
    }
}
