// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]

use failure::{Error, ResultExt};
use fidl::endpoints::{RequestStream, ServerEnd};
use fidl_fuchsia_inspect::{
    InspectMarker, InspectRequest, InspectRequestStream, Metric, Object, Property,
};
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use futures::{TryFutureExt, TryStreamExt};
use parking_lot::Mutex;
use std::collections::HashSet;
use std::sync::Arc;

use self::object::ObjectUtil;
use self::objectwrapper::{MetricValueWrapper, ObjectWrapper, PropertyValueWrapper};

pub mod object;
mod objectwrapper;
mod vmo;

pub use self::objectwrapper::{MetricValueGenerator, PropertyValueGenerator};

/// InspectService handles requests for fuchsia.inspect.Inspect on a given channel.
///
/// Contrived example usage:
///
///     let root_node = ObjectTreeNode::new_root();
///     let services =
///             fuchsia_app::server::ServicesServer::new()
///                 .add_service((InspectMarker::NAME, move |channel| {
///                     InspectService::new(root_node.clone(), channel)
///                 })).start()?;
///     fuchsia_async::Executor::new()?.run_singlethreaded(services)?;
#[derive(Clone)]
pub struct InspectService {
    current_node: Arc<Mutex<ObjectTreeNode>>,
}

impl InspectService {
    /// Creates a handler for the Inspect service on the given channel. Requires a clone of a
    /// Arc<Mutex<ObjectTreeNode>>, which should be created with a call to
    /// ObjectTreeNode::new_root().
    pub fn new(node: Arc<Mutex<ObjectTreeNode>>, chan: fasync::Channel) {
        let service = InspectService { current_node: node };
        InspectService::spawn(service, chan);
    }

    /// Spawns a new async to respond to requests querying inspect_service over chan
    fn spawn(service: InspectService, chan: fasync::Channel) {
        let fut = async move {
            let mut stream = InspectRequestStream::from_channel(chan);
            while let Some(req) =
                await!(stream.try_next()).context("error running inspect service")?
            {
                match req {
                    InspectRequest::ReadData { responder } => {
                        responder.send(&mut service.current_node.lock().evaluate())?;
                    }
                    InspectRequest::ListChildren { responder } => responder.send(Some(
                        &mut service.current_node.lock().get_children_names().iter().map(|x| &**x),
                    ))?,
                    InspectRequest::OpenChild { child_name, child_channel, responder } => {
                        responder.send(service.open_child(&child_name, child_channel)?)?
                    }
                }
            }
            Ok(())
        };
        fasync::spawn(fut.unwrap_or_else(|e: failure::Error| {
            fx_log_err!("error running inspect interface: {:?}", e)
        }))
    }

    // Serves a new connection on the given ServerEnd that can be used to interact with the named
    // child.
    fn open_child(
        &self,
        child_name: &str,
        server_end: ServerEnd<InspectMarker>,
    ) -> Result<bool, Error> {
        match self.current_node.lock().get_child(child_name) {
            None => Ok(false),
            Some(child_node) => {
                let server_chan = fasync::Channel::from_channel(server_end.into_channel())?;
                InspectService::new(child_node, server_chan);
                Ok(true)
            }
        }
    }
}

pub type ChildrenGenerator = Box<FnMut() -> Vec<Arc<Mutex<ObjectTreeNode>>> + Send + 'static>;

/// ObjectTreeNode is a node in the tree exposed by InspectService. Each node holds one Object, and
/// 0 or more children nodes.
pub struct ObjectTreeNode {
    object: ObjectWrapper,
    children: Vec<Arc<Mutex<ObjectTreeNode>>>,
    child_generator: Option<ChildrenGenerator>,
}

impl ObjectTreeNode {
    /// Creates a new root ObjectTreeNode, for passing to new InspectServices
    pub fn new_root() -> Arc<Mutex<ObjectTreeNode>> {
        ObjectTreeNode::new(Object::new("objects".to_string()))
    }

    /// Creates a new ObjectTreeNode with a given object and no children
    pub fn new(obj: Object) -> Arc<Mutex<ObjectTreeNode>> {
        Arc::new(Mutex::new(ObjectTreeNode {
            object: ObjectWrapper::from_object(obj),
            children: Vec::new(),
            child_generator: None,
        }))
    }

    /// Evaluates this node into an Object. All dynamic properties and metrics are evaluated to
    /// produce static values.
    pub fn evaluate(&mut self) -> Object {
        self.object.evaluate()
    }

    /// Adds a child to this node. The child by default will have no properties or metrics. If a
    /// child with the given name already exists, it will be removed from the tree and returned.
    /// Note that this ignores dynamic children.
    pub fn add_child(&mut self, name: String) -> Option<Arc<Mutex<ObjectTreeNode>>> {
        self.add_child_tree(ObjectTreeNode::new(Object::new(name)))
    }

    /// Adds a child tree to this node. The child will be a static node with the given name and no
    /// properties or metrics. If a child with the given name already exists, it will be removed
    /// from the tree and returned. Note that this ignores dynamic children.
    pub fn add_child_tree(
        &mut self,
        otn: Arc<Mutex<ObjectTreeNode>>,
    ) -> Option<Arc<Mutex<ObjectTreeNode>>> {
        let name = otn.lock().get_name();
        if let Some(i) = self.children.iter().position(|child| child.lock().get_name() == name) {
            self.children.push(otn);
            Some(self.children.swap_remove(i))
        } else {
            self.children.push(otn);
            None
        }
    }

    /// Sets this node to also include a set of children that are generated dynamically via the
    /// given function. This dynamic set of children is generated and appended to the static set of
    /// children whenever this node is being interacted with, except where documented as otherwise.
    /// If a child generator is already set, it is overwritten with the new one.
    pub fn add_dynamic_children(&mut self, child_gen: ChildrenGenerator) {
        self.child_generator = Some(child_gen);
    }

    /// Disables any dynamic generation of children on this node.
    pub fn clear_dynamic_children(&mut self) {
        self.child_generator = None;
    }

    /// Gets the number of children of this node.
    pub fn get_num_children(&mut self) -> usize {
        let mut dyn_child_count = 0;
        if let Some(child_gen) = &mut self.child_generator {
            dyn_child_count = child_gen().len();
        }
        self.children.len() + dyn_child_count
    }

    /// Gets the names of all the children of this node.
    pub fn get_children_names(&mut self) -> Vec<String> {
        let names: HashSet<String> =
            self.children.iter().map(|child| child.lock().get_name()).collect();
        if let Some(child_gen) = &mut self.child_generator {
            let dyn_names: HashSet<String> =
                child_gen().iter_mut().map(|child| child.lock().get_name()).collect();
            return names.union(&dyn_names).cloned().collect();
        }
        names.iter().cloned().collect()
    }

    /// Gets a reference to a child of this node. Will return None if the child doesn't exist.
    pub fn get_child(&mut self, name: &str) -> Option<Arc<Mutex<ObjectTreeNode>>> {
        let optional_child_position = self
            .children
            .iter()
            .map(|child| child.lock().get_name())
            .position(|c_name| c_name == name);

        if let Some(i) = optional_child_position {
            return Some(self.children[i].clone());
        } else if let Some(child_gen) = &mut self.child_generator {
            let dyn_children = child_gen();
            let optional_dynamic_child_position = dyn_children
                .iter()
                .map(|child| child.lock().get_name())
                .position(|c_name| c_name == name);
            if let Some(i) = optional_dynamic_child_position {
                return Some(dyn_children[i].clone());
            }
        }
        None
    }

    /// Removes a child from this node, returning the child node if it existed. Doesn't affect
    /// dynamically generated children.
    pub fn remove_child(&mut self, name: &str) -> Option<Arc<Mutex<ObjectTreeNode>>> {
        self.children
            .iter()
            .position(|child| child.lock().get_name() == name)
            .map(|i| self.children.remove(i))
    }

    /// Returns the name of the current object.
    pub fn get_name(&self) -> String {
        self.object.name.clone()
    }

    /// Adds the given property. If a property with this key already exists, it is replaced.
    pub fn add_property(&mut self, prop: Property) {
        self.object.properties.insert(prop.key, PropertyValueWrapper::Static(prop.value));
    }

    /// Adds a property to the Object held by this node, whose value is lazily generated when
    /// needed. If a property with this key already exists, it is replaced.
    pub fn add_dynamic_property(&mut self, key: String, value_func: PropertyValueGenerator) {
        self.object.properties.insert(key, PropertyValueWrapper::Dynamic(value_func));
    }

    /// Removes the property with the given key. Returns true if the removed value existed.
    pub fn remove_property(&mut self, key: &str) -> bool {
        self.object.properties.remove(key).is_some()
    }

    /// Adds a metric to the Object held by this node. If a metric with this key already exists, it
    /// is replaced.
    pub fn add_metric(&mut self, metric: Metric) {
        self.object.metrics.insert(metric.key, MetricValueWrapper::Static(metric.value));
    }

    /// Adds a metric to the Object held by this node, whose value is lazily generated when needed.
    /// If a metric with this key already exists, it is replaced.
    pub fn add_dynamic_metric(&mut self, key: String, value_func: MetricValueGenerator) {
        self.object.metrics.insert(key, MetricValueWrapper::Dynamic(value_func));
    }

    /// Removes the metric with the given key. Returns true if the removed value existed.
    pub fn remove_metric(&mut self, key: &str) -> bool {
        self.object.metrics.remove(key).is_some()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy;
    use fidl_fuchsia_inspect::{MetricValue, PropertyValue};

    mod object_tree_node_tests {
        use super::*;

        // Objects as returned by evaluate() aren't guaranteed to have the same ordering of
        // metrics/properties as was originally supplied.
        fn sort_keys(mut obj: Object) -> Object {
            obj.properties
                .as_mut()
                .map(|properties| properties.sort_unstable_by(|a, b| a.key.cmp(&b.key)));
            obj.metrics.as_mut().map(|metrics| metrics.sort_unstable_by(|a, b| a.key.cmp(&b.key)));
            obj
        }

        fn initialize_children(otn: &mut ObjectTreeNode, names: Vec<&str>) {
            for n in names {
                otn.add_child(n.to_string());
            }
        }

        #[test]
        fn new_root() {
            let root_node = ObjectTreeNode::new_root();
            assert_eq!("objects", root_node.lock().get_name());
            assert_eq!(None, root_node.lock().evaluate().properties);
            assert_eq!(None, root_node.lock().evaluate().metrics);
            assert_eq!(0, root_node.lock().get_num_children());
        }

        #[test]
        fn new() {
            let obj = Object::new("test".to_string());
            let otn = ObjectTreeNode::new(obj.clone());
            let mut otn = otn.lock();
            assert_eq!(obj, otn.evaluate());
            assert_eq!(0, otn.get_num_children());
        }

        #[test]
        fn evaluate() {
            let otn = ObjectTreeNode::new(Object::new("test".to_string()));
            let mut otn = otn.lock();
            otn.add_property(Property {
                key: "1".to_string(),
                value: PropertyValue::Str("1".to_string()),
            });
            otn.add_dynamic_property(
                "2".to_string(),
                Box::new(|| PropertyValue::Str("2".to_string())),
            );

            otn.add_metric(Metric { key: "3".to_string(), value: MetricValue::UintValue(3) });
            otn.add_dynamic_metric("4".to_string(), Box::new(|| MetricValue::IntValue(4)));

            let mut expected_object = Object::new("test".to_string());
            expected_object.add_property(Property {
                key: "1".to_string(),
                value: PropertyValue::Str("1".to_string()),
            });
            expected_object.add_property(Property {
                key: "2".to_string(),
                value: PropertyValue::Str("2".to_string()),
            });
            expected_object
                .add_metric(Metric { key: "3".to_string(), value: MetricValue::UintValue(3) });
            expected_object
                .add_metric(Metric { key: "4".to_string(), value: MetricValue::IntValue(4) });

            assert_eq!(expected_object, sort_keys(otn.evaluate()));
        }

        #[test]
        fn add_child() {
            let otn = ObjectTreeNode::new(Object::new("test".to_string()));
            let mut otn = otn.lock();
            assert!(otn.add_child("test_child_1".to_string()).is_none());
            assert_eq!(1, otn.children.len());
            assert_eq!("test_child_1", otn.children[0].lock().get_name());

            assert!(otn.add_child("test_child_2".to_string()).is_none());
            assert_eq!(2, otn.children.len());
            assert_eq!("test_child_2", otn.children[1].lock().get_name());

            let res = otn.add_child("test_child_1".to_string());
            assert_eq!(res.unwrap().lock().evaluate(), Object::new("test_child_1".to_string()));
        }

        #[test]
        fn add_child_tree() {
            let otn = ObjectTreeNode::new(Object::new("test".to_string()));
            let mut otn = otn.lock();
            assert!(otn
                .add_child_tree(ObjectTreeNode::new(Object::new("test_child_1".to_string())))
                .is_none());
            assert_eq!(1, otn.children.len());
            assert_eq!("test_child_1", otn.children[0].lock().get_name());

            assert!(otn
                .add_child_tree(ObjectTreeNode::new(Object::new("test_child_2".to_string())))
                .is_none());
            assert_eq!(2, otn.children.len());
            assert_eq!("test_child_2", otn.children[1].lock().get_name());

            let mut obj = Object::new("test_child_1".to_string());
            obj.add_metric(Metric {
                key: "metric".to_string(),
                value: MetricValue::DoubleValue(0.0001),
            });

            let res = otn.add_child_tree(ObjectTreeNode::new(obj));
            assert_eq!(res.unwrap().lock().evaluate(), Object::new("test_child_1".to_string()));
        }

        #[test]
        fn add_clear_dynamic_children() {
            let otn = ObjectTreeNode::new(Object::new("test".to_string()));
            let mut otn = otn.lock();
            otn.add_child("test_child_1".to_string());
            otn.add_dynamic_children(Box::new(|| {
                vec![
                    ObjectTreeNode::new(Object::new("test_child_2".to_string())),
                    ObjectTreeNode::new(Object::new("test_child_3".to_string())),
                ]
            }));
            let mut names = otn.get_children_names();
            names.sort_unstable();
            assert_eq!(names, vec!["test_child_1", "test_child_2", "test_child_3"]);

            otn.clear_dynamic_children();
            assert_eq!(otn.get_children_names(), vec!["test_child_1"]);
        }

        #[test]
        fn get_num_children() {
            let otn = ObjectTreeNode::new(Object::new("test".to_string()));
            let mut otn = otn.lock();
            assert_eq!(0, otn.get_num_children());
            otn.add_child("0".to_string());
            assert_eq!(1, otn.get_num_children());
            otn.add_child("1".to_string());
            assert_eq!(2, otn.get_num_children());
            otn.add_child("2".to_string());
            assert_eq!(3, otn.get_num_children());

            otn.add_dynamic_children(Box::new(|| {
                vec![
                    ObjectTreeNode::new(Object::new("3".to_string())),
                    ObjectTreeNode::new(Object::new("4".to_string())),
                ]
            }));
            assert_eq!(5, otn.get_num_children());
        }

        #[test]
        fn get_children_names() {
            let otn = ObjectTreeNode::new(Object::new("test".to_string()));
            let mut otn = otn.lock();
            assert_eq!(vec![] as Vec<String>, otn.get_children_names());

            otn.add_child("0".to_string());
            assert_eq!(vec!["0"], otn.get_children_names());

            otn.add_child("1".to_string());
            let mut names = otn.get_children_names();
            names.sort_unstable();
            assert_eq!(vec!["0", "1"], names);

            otn.add_dynamic_children(Box::new(|| {
                vec![
                    ObjectTreeNode::new(Object::new("2".to_string())),
                    ObjectTreeNode::new(Object::new("3".to_string())),
                ]
            }));

            let mut names = otn.get_children_names();
            names.sort_unstable();
            assert_eq!(vec!["0", "1", "2", "3"], names);
        }

        #[test]
        fn get_child() {
            let otn = ObjectTreeNode::new(Object::new("test".to_string()));
            let mut otn = otn.lock();
            initialize_children(&mut otn, vec!["0", "1", "2"]);
            otn.add_dynamic_children(Box::new(|| {
                vec![
                    ObjectTreeNode::new(Object::new("3".to_string())),
                    ObjectTreeNode::new(Object::new("4".to_string())),
                ]
            }));

            assert!(otn.get_child("-1").is_none());
            assert_eq!(otn.get_child("0").unwrap().lock().object.name, "0".to_string());
            assert_eq!(otn.get_child("1").unwrap().lock().object.name, "1".to_string());
            assert_eq!(otn.get_child("2").unwrap().lock().object.name, "2".to_string());
            assert_eq!(otn.get_child("3").unwrap().lock().object.name, "3".to_string());
            assert_eq!(otn.get_child("4").unwrap().lock().object.name, "4".to_string());

            otn.get_child("1").unwrap().lock().add_child("test_child".to_string());
            assert_eq!(
                otn.get_child("1").unwrap().lock().children[0].lock().evaluate(),
                Object::new("test_child".to_string())
            )
        }

        #[test]
        fn remove_child() {
            let otn = ObjectTreeNode::new(Object::new("test".to_string()));
            let mut otn = otn.lock();
            initialize_children(&mut otn, vec!["0", "1", "2"]);
            assert_eq!(None, otn.remove_child("-1").map(|child| child.lock().evaluate()));
            assert_eq!(
                Some(Object::new("0".to_string())),
                otn.remove_child("0").map(|child| child.lock().evaluate())
            );
            assert_eq!(None, otn.remove_child("0").map(|child| child.lock().evaluate()));
            assert_eq!(
                Some(Object::new("2".to_string())),
                otn.remove_child("2").map(|child| child.lock().evaluate())
            );
            assert_eq!(None, otn.remove_child("2").map(|child| child.lock().evaluate()));
            assert_eq!(vec!["1"], otn.get_children_names());
        }

        #[test]
        fn get_name() {
            assert_eq!(
                "test",
                &ObjectTreeNode::new(Object::new("test".to_string())).lock().get_name()
            );
        }

        #[test]
        fn add_property() {
            let otn = ObjectTreeNode::new(Object::new("test".to_string()));
            let mut otn = otn.lock();
            otn.add_property(Property {
                key: "1".to_string(),
                value: PropertyValue::Str("1".to_string()),
            });

            let mut expected_object = Object::new("test".to_string());
            expected_object.add_property(Property {
                key: "1".to_string(),
                value: PropertyValue::Str("1".to_string()),
            });

            assert_eq!(expected_object, otn.evaluate());
        }

        #[test]
        fn add_dynamic_property() {
            let otn = ObjectTreeNode::new(Object::new("test".to_string()));
            let mut otn = otn.lock();
            otn.add_dynamic_property(
                "1".to_string(),
                Box::new(|| PropertyValue::Str("1".to_string())),
            );

            let mut expected_object = Object::new("test".to_string());
            expected_object.add_property(Property {
                key: "1".to_string(),
                value: PropertyValue::Str("1".to_string()),
            });

            assert_eq!(expected_object, otn.evaluate());
        }

        #[test]
        fn remove_property() {
            let otn = ObjectTreeNode::new(Object::new("test".to_string()));
            let mut otn = otn.lock();
            otn.add_property(Property {
                key: "1".to_string(),
                value: PropertyValue::Str("1".to_string()),
            });
            otn.add_dynamic_property(
                "2".to_string(),
                Box::new(|| PropertyValue::Str("2".to_string())),
            );

            assert!(otn.remove_property("1"));
            assert!(otn.remove_property("2"));

            let expected_object = Object::new("test".to_string());
            assert_eq!(expected_object, otn.evaluate());
        }

        #[test]
        fn add_metric() {
            let otn = ObjectTreeNode::new(Object::new("test".to_string()));
            let mut otn = otn.lock();
            otn.add_metric(Metric { key: "1".to_string(), value: MetricValue::IntValue(1) });

            let mut expected_object = Object::new("test".to_string());
            expected_object
                .add_metric(Metric { key: "1".to_string(), value: MetricValue::IntValue(1) });

            assert_eq!(expected_object, otn.evaluate());
        }

        #[test]
        fn add_dynamic_metric() {
            let otn = ObjectTreeNode::new(Object::new("test".to_string()));
            let mut otn = otn.lock();
            otn.add_dynamic_metric("1".to_string(), Box::new(|| MetricValue::UintValue(1)));

            let mut expected_object = Object::new("test".to_string());
            expected_object
                .add_metric(Metric { key: "1".to_string(), value: MetricValue::UintValue(1) });

            assert_eq!(expected_object, otn.evaluate());
        }

        #[test]
        fn remove_metric() {
            let otn = ObjectTreeNode::new(Object::new("test".to_string()));
            let mut otn = otn.lock();
            otn.add_metric(Metric { key: "1".to_string(), value: MetricValue::IntValue(1) });
            otn.add_dynamic_metric("2".to_string(), Box::new(|| MetricValue::UintValue(2)));

            assert!(otn.remove_metric("1"));
            assert!(otn.remove_metric("2"));

            let expected_object = Object::new("test".to_string());
            assert_eq!(expected_object, otn.evaluate());
        }
    }

    mod inspect_service_tests {
        use super::*;

        #[test]
        fn kitchen_sink() {
            let mut exec = fasync::Executor::new().unwrap();

            // Create a new root node
            let root_node = ObjectTreeNode::new_root();
            {
                let mut root_node = root_node.lock();

                // That root node has a child named `test_child`
                root_node.add_child("test_child".to_string());
                let test_child = root_node.get_child("test_child").unwrap();
                // The node `test_child` has a metric `test_metric=int(4)`
                test_child.lock().add_metric(Metric {
                    key: "test_metric".to_string(),
                    value: MetricValue::IntValue(4),
                });

                // The root node also has a child named `child_with_dyn_property`
                root_node.add_child("child_with_dyn_property".to_string());
                let child_with_dyn_property =
                    root_node.get_child("child_with_dyn_property").unwrap();
                // The node `child_with_dyn_property` has a dynamic property, that always is
                // `test_property=str(test_value`
                child_with_dyn_property.lock().add_dynamic_property(
                    "test_property".to_string(),
                    Box::new(|| PropertyValue::Str("test_value".to_string())),
                );

                // The root node shall also have dynamic children
                root_node.add_dynamic_children(Box::new(|| {
                    // There's only one dynamic child of the root node, named
                    // `dynamic_child`
                    let dynamic_child =
                        ObjectTreeNode::new(Object::new("dynamic_child".to_string()));
                    // The `test_child_child_dyn` node has a dynamic metric, that is always
                    // `test_metric=int(1)`
                    dynamic_child.lock().add_dynamic_metric(
                        "test_metric".to_string(),
                        Box::new(|| MetricValue::IntValue(1)),
                    );

                    // The `dynamic_child` node also has dynamic children
                    dynamic_child.lock().add_dynamic_children(Box::new(|| {
                        // There's only one dynamic child of the `dynamic_child node, named
                        // `sub_dynamic_child`
                        let sub_dynamic_child =
                            ObjectTreeNode::new(Object::new("sub_dynamic_child".to_string()));
                        // The `test_child_child_dyn` node has a dynamic metric, that is always
                        // `test_metric=int(2)`
                        sub_dynamic_child.lock().add_dynamic_metric(
                            "test_metric".to_string(),
                            Box::new(|| MetricValue::IntValue(2)),
                        );
                        vec![sub_dynamic_child]
                    }));
                    vec![dynamic_child]
                }));
            }
            let (inspect_proxy, server_end) = create_proxy::<InspectMarker>().unwrap();
            let server_channel = fasync::Channel::from_channel(server_end.into_channel()).unwrap();
            InspectService::new(root_node.clone(), server_channel);

            // We should be able to call read_data to get the root Object
            let expected_object = &root_node.lock().evaluate().clone();
            assert_eq!(
                expected_object,
                &exec.run_singlethreaded(inspect_proxy.read_data()).unwrap()
            );

            // There should be one child with the name "test_child"
            let mut names =
                exec.run_singlethreaded(inspect_proxy.list_children()).unwrap().unwrap();
            names.sort_unstable();
            assert_eq!(
                vec![
                    "child_with_dyn_property".to_string(),
                    "dynamic_child".to_string(),
                    "test_child".to_string()
                ],
                names
            );

            // We shouldn't be able to open a child that doesn't exist
            let (_, server_end) = create_proxy::<InspectMarker>().unwrap();
            assert_eq!(
                false,
                exec.run_singlethreaded(inspect_proxy.open_child("nonexistent", server_end))
                    .unwrap()
            );

            // We should be able to open a child that exists
            let (child_inspect_proxy, child_server_end) = create_proxy::<InspectMarker>().unwrap();
            assert_eq!(
                true,
                exec.run_singlethreaded(inspect_proxy.open_child("test_child", child_server_end))
                    .unwrap()
            );

            // We should be able to call read_data to get the child's Object
            let expected_object =
                root_node.lock().get_child("test_child").unwrap().lock().evaluate();
            assert_eq!(
                expected_object,
                exec.run_singlethreaded(child_inspect_proxy.read_data()).unwrap()
            );

            // If the child we've opened is removed, we should still be able to read its data
            // because we have an open connection to it
            assert!(root_node.lock().remove_child("test_child").is_some());
            assert_eq!(
                expected_object,
                exec.run_singlethreaded(child_inspect_proxy.read_data()).unwrap()
            );

            // There should be no children of this child
            assert_eq!(
                Some(vec![]),
                exec.run_singlethreaded(child_inspect_proxy.list_children()).unwrap()
            );

            // We shouldn't be able to open any of the nonexistent children
            let (_, server_end) = create_proxy::<InspectMarker>().unwrap();
            assert_eq!(
                false,
                exec.run_singlethreaded(child_inspect_proxy.open_child("nonexistent", server_end))
                    .unwrap()
            );

            // We should be able to open and read the data of a child with a dynamic property
            let (child_inspect_proxy, child_server_end) = create_proxy::<InspectMarker>().unwrap();
            assert_eq!(
                true,
                exec.run_singlethreaded(
                    inspect_proxy.open_child("child_with_dyn_property", child_server_end)
                )
                .unwrap()
            );
            let expected_object = &root_node
                .lock()
                .get_child("child_with_dyn_property")
                .unwrap()
                .lock()
                .evaluate()
                .clone();
            assert_eq!(
                expected_object,
                &exec.run_singlethreaded(child_inspect_proxy.read_data()).unwrap()
            );

            // We should be able to open a dynamic child and read its data, which includes a
            // dynamic property.
            let (dynamic_child_inspect_proxy, dynamic_child_server_end) =
                create_proxy::<InspectMarker>().unwrap();
            assert_eq!(
                true,
                exec.run_singlethreaded(
                    inspect_proxy.open_child("dynamic_child", dynamic_child_server_end)
                )
                .unwrap()
            );
            let expected_object =
                &root_node.lock().get_child("dynamic_child").unwrap().lock().evaluate().clone();
            assert_eq!(
                expected_object,
                &exec.run_singlethreaded(dynamic_child_inspect_proxy.read_data()).unwrap()
            );

            // We should be able to open a dynamic child of that last dynamic child and read its
            // data, which includes a dynamic metric.
            let (sub_dynamic_child_inspect_proxy, sub_dynamic_child_server_end) =
                create_proxy::<InspectMarker>().unwrap();
            assert_eq!(
                true,
                exec.run_singlethreaded(
                    dynamic_child_inspect_proxy
                        .open_child("sub_dynamic_child", sub_dynamic_child_server_end)
                )
                .unwrap()
            );
            let expected_object = &root_node
                .lock()
                .get_child("dynamic_child")
                .unwrap()
                .lock()
                .get_child("sub_dynamic_child")
                .unwrap()
                .lock()
                .evaluate()
                .clone();
            assert_eq!(
                expected_object,
                &exec.run_singlethreaded(sub_dynamic_child_inspect_proxy.read_data()).unwrap()
            );
        }

        #[test]
        fn counter() {
            // As a fun test, we should be able to make a dynamic node that returns one more than
            // before for each time it is called.
            let mut exec = fasync::Executor::new().unwrap();

            let root_node = ObjectTreeNode::new_root();

            let mut counter = 0;
            root_node.lock().add_dynamic_metric(
                "counter".to_string(),
                Box::new(move || {
                    counter += 1;
                    MetricValue::UintValue(counter)
                }),
            );

            let (inspect_proxy, server_end) = create_proxy::<InspectMarker>().unwrap();
            let server_channel = fasync::Channel::from_channel(server_end.into_channel()).unwrap();
            InspectService::new(root_node.clone(), server_channel);

            let mut expected_object = Object::new("objects".to_string());
            expected_object.add_metric(Metric {
                key: "counter".to_string(),
                value: MetricValue::UintValue(1),
            });
            assert_eq!(
                expected_object,
                exec.run_singlethreaded(inspect_proxy.read_data()).unwrap()
            );

            expected_object.add_metric(Metric {
                key: "counter".to_string(),
                value: MetricValue::UintValue(2),
            });
            assert_eq!(
                expected_object,
                exec.run_singlethreaded(inspect_proxy.read_data()).unwrap()
            );

            expected_object.add_metric(Metric {
                key: "counter".to_string(),
                value: MetricValue::UintValue(3),
            });
            assert_eq!(
                expected_object,
                exec.run_singlethreaded(inspect_proxy.read_data()).unwrap()
            );
        }
    }
}
