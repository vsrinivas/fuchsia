// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, Fail};
use futures::{task::{Context, Waker},
              Async,
              Future};
use slab::Slab;
use std::{cell::RefCell, cmp::Eq, collections::HashMap, hash::Hash, mem::swap, rc::Rc};

enum MaybeWaiting<T, U> {
    Obj(T),
    Waiting(Slab<Waker>),
    Failed(U),
}

#[derive(Clone)]
pub struct AsyncMap<K, V, F>
where
    K: Eq + Hash + Clone,
    V: Clone,
    F: Clone + Fail,
{
    m: Rc<RefCell<HashMap<K, MaybeWaiting<V, F>>>>,
}

impl<K, V, F> AsyncMap<K, V, F>
where
    K: Eq + Hash + Clone,
    V: Clone,
    F: Clone + Fail,
{
    pub fn lookup(&self, key: K) -> Lookup<K, V, F> {
        Lookup {
            key,
            map: self.m.clone(),
            waker: None,
        }
    }

    pub fn new() -> Self {
        AsyncMap {
            m: Rc::new(RefCell::new(HashMap::new())),
        }
    }

    fn put<EF, E>(&mut self, key: K, mut val: MaybeWaiting<V, F>, ec: EF) -> Result<(), E>
    where
        EF: FnOnce() -> Result<(), E>,
    {
        use async_map::MaybeWaiting::*;
        let mut m = self.m.borrow_mut();
        let v = m.entry(key).or_insert_with(|| Waiting(Slab::new()));
        let mut waiters = Slab::new();
        if let Waiting(w) = v {
            swap(w, &mut waiters);
        } else {
            ec()?;
        }
        swap(v, &mut val);
        for waiter in waiters.iter() {
            waiter.1.wake();
        }
        Ok(())
    }

    fn put_replace(&mut self, key: K, val: MaybeWaiting<V, F>) {
        self.put(key, val, || -> Result<(), ()> { Ok(()) }).unwrap()
    }

    fn put_new<EF, E>(&mut self, key: K, val: MaybeWaiting<V, F>, ec: EF) -> Result<(), E>
    where
        EF: FnOnce() -> E,
    {
        self.put(key, val, || Err(ec()))
    }

    pub fn put_ok(&mut self, key: K, value: V) {
        self.put_replace(key, MaybeWaiting::Obj(value));
    }

    pub fn put_new_ok<EF, E>(&mut self, key: K, value: V, ec: EF) -> Result<(), E>
    where
        EF: FnOnce() -> E,
    {
        self.put_new(key, MaybeWaiting::Obj(value), ec)
    }

    pub fn put_fail(&mut self, key: K, failure: F) {
        self.put_replace(key, MaybeWaiting::Failed(failure));
    }

    pub fn put_result(&mut self, key: K, r: Result<V, F>) {
        match r {
            Ok(ok) => self.put_ok(key, ok),
            Err(err) => self.put_fail(key, err),
        };
    }

    pub fn fail_waiting(&mut self, failure: F) {
        use async_map::MaybeWaiting::*;
        for (_, val) in self.m.borrow_mut().iter_mut() {
            let mut waiters = Slab::new();
            if let &mut Waiting(w) = val {
                swap(w, &mut waiters);
            } else {
                return;
            }
            swap(val, &mut Failed(failure.clone()));
            for waiter in waiters.iter() {
                waiter.1.wake();
            }
        }
    }
}

pub struct Lookup<K, V, F>
where
    K: Eq + Hash + Clone,
    V: Clone,
    F: Clone + Fail,
{
    key: K,
    map: Rc<RefCell<HashMap<K, MaybeWaiting<V, F>>>>,
    waker: Option<usize>,
}

impl<K, V, F> Future for Lookup<K, V, F>
where
    K: Eq + Hash + Clone,
    V: Clone,
    F: Clone + Fail,
{
    type Item = V;
    type Error = Error;

    fn poll(&mut self, cx: &mut Context) -> Result<Async<V>, Self::Error> {
        use async_map::MaybeWaiting::*;
        match self.map
            .borrow_mut()
            .entry(self.key.clone())
            .or_insert_with(|| Waiting(Slab::new()))
        {
            Waiting(w) => {
                match self.waker {
                    None => self.waker = Some(w.insert(cx.waker().clone())),
                    Some(idx) => w[idx] = cx.waker().clone(),
                };
                Ok(Async::Pending)
            }
            Obj(x) => Ok(Async::Ready(x.clone())),
            Failed(f) => Err(f.clone().into()),
        }
    }
}
