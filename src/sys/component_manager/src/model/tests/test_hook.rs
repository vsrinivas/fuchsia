// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    futures::{executor::block_on, future::BoxFuture, lock::Mutex, prelude::*},
    std::{cmp::Eq, collections::HashMap, fmt, ops::Deref, pin::Pin, sync::Arc},
};

struct ComponentInstance {
    pub abs_moniker: AbsoluteMoniker,
    pub children: Mutex<Vec<Arc<ComponentInstance>>>,
}

impl Clone for ComponentInstance {
    // This is used by TestHook. ComponentInstance is immutable so when a change
    // needs to be made, TestHook clones ComponentInstance and makes the change
    // in the new copy.
    fn clone(&self) -> Self {
        let children = block_on(self.children.lock());
        return ComponentInstance {
            abs_moniker: self.abs_moniker.clone(),
            children: Mutex::new(children.clone()),
        };
    }
}

impl PartialEq for ComponentInstance {
    fn eq(&self, other: &Self) -> bool {
        self.abs_moniker == other.abs_moniker
    }
}

impl Eq for ComponentInstance {}

impl ComponentInstance {
    pub async fn print(&self) -> String {
        let mut s: String = format!("{}", self.abs_moniker.name().unwrap_or(String::new()));
        let mut children = await!(self.children.lock());
        if children.is_empty() {
            return s;
        }

        // The position of a child in the children vector is a function of timing.
        // In order to produce stable topology strings across runs, we sort the set
        // of children here by moniker.
        children.sort_by(|a, b| a.abs_moniker.cmp(&b.abs_moniker));

        s.push('(');
        let mut count = 0;
        for child in children.iter() {
            // If we've seen a previous child, then add a comma to separate children.
            if count > 0 {
                s.push(',');
            }
            s.push_str(&await!(child.boxed_print()));
            count += 1;
        }
        s.push(')');
        s
    }

    fn boxed_print<'a>(&'a self) -> Pin<Box<dyn Future<Output = String> + 'a>> {
        Box::pin(self.print())
    }
}

pub struct TestHook {
    instances: Mutex<HashMap<AbsoluteMoniker, Arc<ComponentInstance>>>,
}

impl fmt::Display for TestHook {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        writeln!(f, "{}", self.print())?;
        Ok(())
    }
}

/// TestHook is a Hook that generates a strings representing the component
/// topology.
impl TestHook {
    pub fn new() -> TestHook {
        let abs_moniker = AbsoluteMoniker::root();
        let instance =
            ComponentInstance { abs_moniker: abs_moniker.clone(), children: Mutex::new(vec![]) };
        let mut instances = HashMap::new();
        instances.insert(abs_moniker, Arc::new(instance));
        TestHook { instances: Mutex::new(instances) }
    }

    // Recursively traverse the Instance tree to generate a string representing the component
    // topology.
    pub fn print(&self) -> String {
        let instances = block_on(self.instances.lock());
        let abs_moniker = AbsoluteMoniker::root();
        let root_instance =
            instances.get(&abs_moniker).map(|x| x.clone()).expect("Unable to find root instance.");
        block_on(root_instance.print())
    }

    pub async fn create_instance_if_necessary(
        &self,
        abs_moniker: AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let mut instances = await!(self.instances.lock());
        if let Some(parent_moniker) = abs_moniker.parent() {
            // If the parent isn't available yet then opt_parent_instance will have a value
            // of None.
            let opt_parent_instance = instances.get(&parent_moniker).map(|x| x.clone());
            let new_instance = match instances.get(&abs_moniker) {
                Some(old_instance) => Arc::new((old_instance.deref()).clone()),
                None => Arc::new(ComponentInstance {
                    abs_moniker: abs_moniker.clone(),
                    children: Mutex::new(vec![]),
                }),
            };
            instances.insert(abs_moniker.clone(), new_instance.clone());
            // If the parent is available then add this instance as a child to it.
            if let Some(parent_instance) = opt_parent_instance {
                let mut children = await!(parent_instance.children.lock());
                let opt_index =
                    children.iter().position(|c| c.abs_moniker == new_instance.abs_moniker);
                if let Some(index) = opt_index {
                    children.remove(index);
                }
                children.push(new_instance.clone());
            }
        }
        Ok(())
    }
}

impl Hook for TestHook {
    fn on_bind_instance<'a>(
        &'a self,
        realm: Arc<Realm>,
        _instance_state: &'a InstanceState,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.create_instance_if_necessary(realm.abs_moniker.clone()))
    }

    fn on_resolve_realm(&self, realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.create_instance_if_necessary(realm.abs_moniker.clone()))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_hook_test() {
        let a = AbsoluteMoniker::new(vec![ChildMoniker::new("a".to_string(), None)]);
        let ab = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None),
            ChildMoniker::new("b".to_string(), None),
        ]);
        let ac = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None),
            ChildMoniker::new("c".to_string(), None),
        ]);
        let abd = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None),
            ChildMoniker::new("b".to_string(), None),
            ChildMoniker::new("d".to_string(), None),
        ]);
        let abe = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None),
            ChildMoniker::new("b".to_string(), None),
            ChildMoniker::new("e".to_string(), None),
        ]);
        let acf = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None),
            ChildMoniker::new("c".to_string(), None),
            ChildMoniker::new("f".to_string(), None),
        ]);

        // Try adding parent followed by children then verify the topology string
        // is correct.
        {
            let test_hook = TestHook::new();
            assert!(await!(test_hook.create_instance_if_necessary(a.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(ab.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(ac.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(abd.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(abe.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(acf.clone())).is_ok());
            assert_eq!("(a(b(d,e),c(f)))", test_hook.print());
        }

        // Changing the order of monikers should not affect the output string.
        {
            let test_hook = TestHook::new();
            assert!(await!(test_hook.create_instance_if_necessary(a.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(ac.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(ab.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(abd.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(abe.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(acf.clone())).is_ok());
            assert_eq!("(a(b(d,e),c(f)))", test_hook.print());
        }

        // Submitting children before parents should still succeed.
        {
            let test_hook = TestHook::new();
            assert!(await!(test_hook.create_instance_if_necessary(acf.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(abe.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(abd.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(ab.clone())).is_ok());
            // Model will call create_instance_if_necessary for ab's children again
            // after the call to bind_instance for ab.
            assert!(await!(test_hook.create_instance_if_necessary(abe.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(abd.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(ac.clone())).is_ok());
            // Model will call create_instance_if_necessary for ac's children again
            // after the call to bind_instance for ac.
            assert!(await!(test_hook.create_instance_if_necessary(acf.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(a.clone())).is_ok());
            // Model will call create_instance_if_necessary for a's children again
            // after the call to bind_instance for a.
            assert!(await!(test_hook.create_instance_if_necessary(ab.clone())).is_ok());
            assert!(await!(test_hook.create_instance_if_necessary(ac.clone())).is_ok());
            assert_eq!("(a(b(d,e),c(f)))", test_hook.print());
        }
    }
}
