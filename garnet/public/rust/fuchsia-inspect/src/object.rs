// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_inspect::{Metric, MetricValue, Object, Property, PropertyValue};

pub trait ObjectUtil {
    fn new(name: String) -> Self;
    fn clone(&self) -> Self;

    fn add_property(&mut self, prop: Property) -> Option<Property>;
    fn get_property(&self, key: &str) -> Option<&Property>;
    fn get_property_mut(&mut self, key: &str) -> Option<&mut Property>;
    fn remove_property(&mut self, key: &str) -> Option<Property>;

    fn add_metric(&mut self, metric: Metric) -> Option<Metric>;
    fn get_metric(&self, key: &str) -> Option<&Metric>;
    fn get_metric_mut(&mut self, key: &str) -> Option<&mut Metric>;
    fn remove_metric(&mut self, key: &str) -> Option<Metric>;
}

impl ObjectUtil for Object {
    /// Creates a new Object with the given name and no properties or metrics.
    fn new(name: String) -> Object {
        Object { name, properties: None, metrics: None }
    }

    /// clone will create a copy of the given Object. This is necessary because
    /// fidl-generated structs don't derive the Clone trait.
    fn clone(&self) -> Object {
        let name = self.name.clone();
        let mut properties = None;
        let mut metrics = None;
        if let Some(ps) = &self.properties {
            let mut pvec = Vec::new();
            for p in ps {
                pvec.push(Property {
                    key: p.key.clone(),
                    value: match &p.value {
                        PropertyValue::Str(s) => PropertyValue::Str(s.clone()),
                        PropertyValue::Bytes(b) => PropertyValue::Bytes(b.clone()),
                    },
                });
            }
            properties = Some(pvec);
        }
        if let Some(ms) = &self.metrics {
            let mut mvec = Vec::new();
            for m in ms {
                mvec.push(Metric {
                    key: m.key.clone(),
                    value: match m.value {
                        MetricValue::IntValue(n) => MetricValue::IntValue(n),
                        MetricValue::UintValue(n) => MetricValue::UintValue(n),
                        MetricValue::DoubleValue(n) => MetricValue::DoubleValue(n),
                    },
                });
            }
            metrics = Some(mvec);
        }
        Object { name, properties, metrics }
    }

    /// Adds the given property to the given Object. If a property with this key already exists, it is
    /// replaced and the old value is returned.
    fn add_property(&mut self, prop: Property) -> Option<Property> {
        let properties = self.properties.get_or_insert(Vec::new());
        if let Some(i) = properties.iter().position(|old_prop| old_prop.key == prop.key) {
            properties.push(prop);
            Some(properties.swap_remove(i))
        } else {
            properties.push(prop);
            None
        }
    }

    /// Gets a reference to the property with the given key on the given Object.
    fn get_property(&self, key: &str) -> Option<&Property> {
        if let Some(properties) = &self.properties {
            if let Some(i) = properties.iter().position(|prop| prop.key == key) {
                return Some(&properties[i]);
            }
        }
        None
    }

    /// Gets a mutable reference to the property with the given key on the given Object.
    fn get_property_mut(&mut self, key: &str) -> Option<&mut Property> {
        if let Some(properties) = &mut self.properties {
            if let Some(i) = properties.iter().position(|prop| prop.key == key) {
                return Some(&mut properties[i]);
            }
        }
        None
    }

    /// Removes the property with the given key from the given Object. Returns the removed value if it
    /// existed.
    fn remove_property(&mut self, key: &str) -> Option<Property> {
        if let Some(properties) = &mut self.properties {
            if let Some(i) = properties.iter().position(|prop| prop.key == key) {
                return Some(properties.remove(i));
            }
        }
        None
    }

    /// Adds the given metric to the given Object. If a metric with this key already exists, it is
    /// replaced and the old value is returned.
    fn add_metric(&mut self, metric: Metric) -> Option<Metric> {
        let metrics = self.metrics.get_or_insert(Vec::new());
        if let Some(i) = metrics.iter().position(|old_metric| old_metric.key == metric.key) {
            metrics.push(metric);
            Some(metrics.swap_remove(i))
        } else {
            metrics.push(metric);
            None
        }
    }

    /// Gets a reference to the metric with the given key on the given Object
    fn get_metric(&self, key: &str) -> Option<&Metric> {
        if let Some(metrics) = &self.metrics {
            if let Some(i) = metrics.iter().position(|metric| metric.key == key) {
                return Some(&metrics[i]);
            }
        }
        None
    }

    /// Gets a mutable reference to the metric with the given key on the given Object.
    fn get_metric_mut(&mut self, key: &str) -> Option<&mut Metric> {
        if let Some(metrics) = &mut self.metrics {
            if let Some(i) = metrics.iter().position(|metric| metric.key == key) {
                return Some(&mut metrics[i]);
            }
        }
        None
    }

    /// Removes the metric with the given key on the given Object. Returns the removed value if it
    /// existed.
    fn remove_metric(&mut self, key: &str) -> Option<Metric> {
        if let Some(metrics) = &mut self.metrics {
            if let Some(i) = metrics.iter().position(|metric| metric.key == key) {
                return Some(metrics.remove(i));
            }
        }
        None
    }
}

pub trait PropertyValueUtil {
    fn clone(&self) -> Self;
}

impl PropertyValueUtil for PropertyValue {
    fn clone(&self) -> PropertyValue {
        match &self {
            PropertyValue::Str(s) => PropertyValue::Str(s.clone()),
            PropertyValue::Bytes(b) => PropertyValue::Bytes(b.clone()),
        }
    }
}

pub trait MetricValueUtil {
    fn clone(&self) -> Self;
}

impl MetricValueUtil for MetricValue {
    fn clone(&self) -> MetricValue {
        match &self {
            MetricValue::IntValue(n) => MetricValue::IntValue(*n),
            MetricValue::UintValue(n) => MetricValue::UintValue(*n),
            MetricValue::DoubleValue(n) => MetricValue::DoubleValue(*n),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn new_test() {
        assert_eq!(
            Object::new("test".to_string()),
            Object { name: "test".to_string(), properties: None, metrics: None }
        );
    }

    #[test]
    fn clone_test() {
        let original = Object {
            name: "test".to_string(),
            properties: Some(vec![
                Property {
                    key: "prop0".to_string(),
                    value: PropertyValue::Str("string value".to_string()),
                },
                Property {
                    key: "prop1".to_string(),
                    value: PropertyValue::Bytes(vec![0x00, 0x01, 0x02]),
                },
            ]),
            metrics: Some(vec![
                Metric { key: "metric0".to_string(), value: MetricValue::IntValue(0) },
                Metric { key: "metric1".to_string(), value: MetricValue::UintValue(1) },
                Metric { key: "metric2".to_string(), value: MetricValue::DoubleValue(2.0) },
            ]),
        };
        let cloned = original.clone();
        assert_eq!(original, cloned);
    }

    #[test]
    fn add_property_test() {
        let mut obj = Object::new("test".to_string());
        obj.add_property(Property {
            key: "prop0".to_string(),
            value: PropertyValue::Str("string value".to_string()),
        });
        assert_eq!(
            obj.properties.as_ref().unwrap(),
            &vec![Property {
                key: "prop0".to_string(),
                value: PropertyValue::Str("string value".to_string()),
            },]
        );
        obj.add_property(Property {
            key: "prop1".to_string(),
            value: PropertyValue::Str("another string value".to_string()),
        });
        assert_eq!(
            obj.properties.unwrap(),
            vec![
                Property {
                    key: "prop0".to_string(),
                    value: PropertyValue::Str("string value".to_string()),
                },
                Property {
                    key: "prop1".to_string(),
                    value: PropertyValue::Str("another string value".to_string()),
                },
            ]
        );
    }

    #[test]
    fn get_property_test() {
        let mut obj = Object::new("test".to_string());
        obj.add_property(Property {
            key: "prop0".to_string(),
            value: PropertyValue::Str("string value".to_string()),
        });
        obj.add_property(Property {
            key: "prop1".to_string(),
            value: PropertyValue::Str("another string value".to_string()),
        });

        assert!(obj.get_property("prop0").is_some());
        assert_eq!(
            obj.get_property("prop0").unwrap(),
            &Property {
                key: "prop0".to_string(),
                value: PropertyValue::Str("string value".to_string()),
            }
        );

        assert!(obj.get_property("prop1").is_some());
        assert_eq!(
            obj.get_property("prop1").unwrap(),
            &Property {
                key: "prop1".to_string(),
                value: PropertyValue::Str("another string value".to_string()),
            }
        );
    }

    #[test]
    fn get_property_mut_test() {
        let mut obj = Object::new("test".to_string());
        obj.add_property(Property {
            key: "prop0".to_string(),
            value: PropertyValue::Str("string value".to_string()),
        });

        assert!(obj.get_property_mut("prop0").is_some());
        obj.get_property_mut("prop0").unwrap().key = "prop1".to_string();

        assert_eq!(
            obj.get_property("prop1").unwrap(),
            &Property {
                key: "prop1".to_string(),
                value: PropertyValue::Str("string value".to_string()),
            }
        );
    }

    #[test]
    fn remove_property_test() {
        let mut obj = Object::new("test".to_string());
        obj.add_property(Property {
            key: "prop0".to_string(),
            value: PropertyValue::Str("string value".to_string()),
        });
        obj.add_property(Property {
            key: "prop1".to_string(),
            value: PropertyValue::Str("another string value".to_string()),
        });
        assert_eq!(None, obj.remove_property("prop-1"));
        assert_eq!(
            Some(Property {
                key: "prop0".to_string(),
                value: PropertyValue::Str("string value".to_string()),
            }),
            obj.remove_property("prop0")
        );
        assert_eq!(None, obj.remove_property("prop0"));
        assert_eq!(
            Some(Property {
                key: "prop1".to_string(),
                value: PropertyValue::Str("another string value".to_string()),
            }),
            obj.remove_property("prop1")
        );
        assert_eq!(None, obj.remove_property("prop1"));
    }

    #[test]
    fn add_metric_test() {
        let mut obj = Object::new("test".to_string());
        obj.add_metric(Metric { key: "metric0".to_string(), value: MetricValue::IntValue(0) });
        assert_eq!(
            obj.metrics.as_ref().unwrap(),
            &vec![Metric { key: "metric0".to_string(), value: MetricValue::IntValue(0) },]
        );
        obj.add_metric(Metric {
            key: "metric1".to_string(),
            value: MetricValue::DoubleValue(3.14),
        });
        assert_eq!(
            obj.metrics.unwrap(),
            vec![
                Metric { key: "metric0".to_string(), value: MetricValue::IntValue(0) },
                Metric { key: "metric1".to_string(), value: MetricValue::DoubleValue(3.14) },
            ]
        );
    }

    #[test]
    fn get_metric_test() {
        let mut obj = Object::new("test".to_string());
        obj.add_metric(Metric { key: "metric0".to_string(), value: MetricValue::IntValue(0) });
        obj.add_metric(Metric {
            key: "metric1".to_string(),
            value: MetricValue::DoubleValue(3.14),
        });

        assert!(obj.get_metric("metric0").is_some());
        assert_eq!(
            obj.get_metric("metric0").unwrap(),
            &Metric { key: "metric0".to_string(), value: MetricValue::IntValue(0) }
        );

        assert!(obj.get_metric("metric1").is_some());
        assert_eq!(
            obj.get_metric("metric1").unwrap(),
            &Metric { key: "metric1".to_string(), value: MetricValue::DoubleValue(3.14) }
        );
    }

    #[test]
    fn get_metric_mut_test() {
        let mut obj = Object::new("test".to_string());
        obj.add_metric(Metric { key: "metric0".to_string(), value: MetricValue::IntValue(0) });

        assert!(obj.get_metric_mut("metric0").is_some());
        obj.get_metric_mut("metric0").unwrap().key = "metric1".to_string();

        assert_eq!(
            obj.get_metric("metric1").unwrap(),
            &Metric { key: "metric1".to_string(), value: MetricValue::IntValue(0) }
        );
    }

    #[test]
    fn remove_metric_test() {
        let mut obj = Object::new("test".to_string());
        obj.add_metric(Metric { key: "metric0".to_string(), value: MetricValue::UintValue(0) });
        obj.add_metric(Metric { key: "metric1".to_string(), value: MetricValue::IntValue(-1) });
        assert_eq!(None, obj.remove_metric("metric-1"));
        assert_eq!(
            Some(Metric { key: "metric0".to_string(), value: MetricValue::UintValue(0) }),
            obj.remove_metric("metric0")
        );
        assert_eq!(None, obj.remove_metric("metric0"));
        assert_eq!(
            Some(Metric { key: "metric1".to_string(), value: MetricValue::IntValue(-1) }),
            obj.remove_metric("metric1")
        );
        assert_eq!(None, obj.remove_metric("metric1"));
    }
}
