// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::{self, Debug};
use std::ops::Deref;
use std::sync::Mutex;

use crate::DropWatch;

/// Vigil is a smart pointer that implements DropWatch for any type.
pub struct Vigil<T: ?Sized> {
    ptr: Box<VigilInner<T>>,
}

impl<T: ?Sized> Vigil<T> {
    fn from_inner(ptr: Box<VigilInner<T>>) -> Self {
        Self { ptr }
    }

    fn inner(&self) -> &VigilInner<T> {
        &self.ptr
    }
}

impl<T> Vigil<T> {
    pub fn new(data: T) -> Vigil<T> {
        let x = Box::new(VigilInner { observers: Mutex::new(Vec::new()), data });
        Self::from_inner(x)
    }
}

impl<T: ?Sized> DropWatch<T> for Vigil<T> {
    fn watch(this: &Self, drop_fn: impl FnOnce(&T) + 'static) {
        this.inner().observers.lock().unwrap().push(Box::new(drop_fn));
    }
}

#[derive(Default)]
struct VigilInner<T: ?Sized> {
    observers: Mutex<Vec<Box<dyn FnOnce(&T)>>>,
    data: T,
}

impl<T: ?Sized> Drop for Vigil<T> {
    fn drop(&mut self) {
        for rite in self.inner().observers.lock().unwrap().drain(..) {
            (rite)(&**self);
        }
    }
}

impl<T: ?Sized> Deref for Vigil<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &self.inner().data
    }
}

impl<T: Debug> Debug for Vigil<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Vigil({:?})", self.inner().data)
    }
}

#[cfg(test)]
mod tests {
    use std::rc::Rc;
    use std::sync::atomic::{AtomicUsize, Ordering};

    use super::*;

    #[test]
    fn rites_upon_drop() {
        let ghosts = Rc::new(Mutex::new(String::new()));

        let v = Vigil::new('👻');

        Vigil::watch(&v, {
            let ghosts = ghosts.clone();
            move |thing| ghosts.lock().unwrap().push(thing.clone())
        });

        Vigil::watch(&v, {
            let ghosts = ghosts.clone();
            move |thing| ghosts.lock().unwrap().push(thing.clone())
        });

        drop(v);

        // Dropping the vigil should have dropped all the Rc clones.
        let unwrapped = Rc::try_unwrap(ghosts).unwrap();

        let ghosts_result = unwrapped.into_inner().unwrap();

        assert_eq!("👻👻", ghosts_result);
    }
    struct DropIncrementer(Rc<AtomicUsize>);

    impl Drop for DropIncrementer {
        fn drop(&mut self) {
            let _ = self.0.fetch_add(1, Ordering::Relaxed);
        }
    }

    #[test]
    fn drops_stored_data() {
        let shared_count = Rc::new(AtomicUsize::new(0));
        let v = Vigil::new(DropIncrementer(shared_count.clone()));
        drop(v);
        assert_eq!(1, shared_count.load(Ordering::Relaxed));
    }
}
