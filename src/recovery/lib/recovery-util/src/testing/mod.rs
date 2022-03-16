// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

use {
    anyhow::Error,
    fuchsia_async as fasync,
    futures::prelude::*,
    std::{cell::RefCell, fmt::Debug, sync::RwLock},
};
pub struct SimpleCallRecorder<T>(RwLock<Vec<T>>);
impl<T: Clone> SimpleCallRecorder<T> {
    pub fn new() -> Self {
        Self(RwLock::new(Vec::new()))
    }
    pub fn record(&self, args: T) {
        self.0.try_write().map(|mut v| v.push(args)).unwrap();
    }
    pub fn recorded(&self) -> Vec<T> {
        self.0.try_read().unwrap().clone()
    }
    pub fn count(&self) -> usize {
        self.0.try_read().unwrap().len()
    }
}

pub struct CallRecorder<T: Sized + Debug + PartialEq> {
    calls: RefCell<Vec<T>>,
}

impl<T: Sized + Debug + PartialEq> CallRecorder<T> {
    pub fn new() -> Self {
        Self { calls: RefCell::new(vec![]) }
    }

    pub fn record(&self, call: T) {
        self.calls.borrow_mut().push(call);
    }

    pub fn take(&self) -> Vec<T> {
        self.calls.borrow_mut().drain(..).collect()
    }

    pub fn expect(&self, test_name: &str, expected: Vec<T>) {
        let observed = self.take();
        if observed.len() != expected.len() {
            eprintln!(
                "for test {} observed {} calls, expected {} calls.",
                test_name,
                observed.len(),
                expected.len()
            );
            eprintln!("observed: ");
            for (i, call) in observed.into_iter().enumerate() {
                let info = format!("{:?}", call);
                eprintln!("{}: {:.250}", i, info);
            }
            eprintln!("expected: ");
            for (i, call) in expected.into_iter().enumerate() {
                let info = format!("{:?}", call);
                eprintln!("{}: {:.250}", i, info);
            }
            panic!("call counts don't match for {}", test_name);
        }
        for (i, (o, e)) in observed.into_iter().zip(expected).enumerate() {
            assert_eq!(o, e, "call {} for test: {}", i, test_name);
        }
    }
}

pub struct MockServer<M: fidl::endpoints::ProtocolMarker>(std::marker::PhantomData<M>);

impl<M: fidl::endpoints::ProtocolMarker> MockServer<M> {
    pub fn start<F, H>(handler: H) -> Result<M::Proxy, Error>
    where
        F: 'static + Future<Output = ()>,
        H: Fn(M::RequestStream) -> F,
    {
        let (proxy, request_stream) = fidl::endpoints::create_proxy_and_stream::<M>()?;
        fasync::Task::local((handler)(request_stream)).detach();
        Ok(proxy)
    }
}
