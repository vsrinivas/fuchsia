// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    any::{self, Any, TypeId},
    fmt, hash,
    marker::PhantomData,
    ops::Deref,
    rc::{Rc, Weak},
};

use crate::{
    core::{Cast, Core, CoreContext, OnAdded},
    importers::ImportStack,
    status_code::StatusCode,
};

fn any_type_name<T: Any>() -> &'static str {
    any::type_name::<T>().rsplit("::").next().unwrap()
}

type OnAddedListener = fn(&Rc<dyn Core>, &dyn CoreContext) -> StatusCode;
type Importer = fn(&Rc<dyn Core>, Object, &ImportStack) -> StatusCode;

pub struct Object<T = ()> {
    weak: Weak<dyn Core>,
    on_added_dirty: Option<OnAddedListener>,
    pub on_added_clean: Option<OnAddedListener>,
    import: Option<Importer>,
    _phantom: PhantomData<T>,
}

impl<T> Object<T> {
    pub(crate) fn new(rc: &Rc<dyn Core>) -> Self {
        Self {
            weak: Rc::downgrade(rc),
            on_added_dirty: None,
            on_added_clean: None,
            import: None,
            _phantom: PhantomData,
        }
    }

    fn upgrade_rc(&self) -> Rc<dyn Core> {
        self.weak.upgrade().expect("File dropped while objects still in use")
    }

    pub fn as_ref(&self) -> ObjectRef<'_, T> {
        ObjectRef {
            core_ref: CoreRef::Rc(self.upgrade_rc()),
            on_added_dirty: self.on_added_dirty,
            on_added_clean: self.on_added_clean,
            import: self.import,
        }
    }

    pub fn ptr_eq<U>(&self, other: &Object<U>) -> bool {
        self.weak.ptr_eq(&other.weak)
    }
}

impl<T: Any> Object<T> {
    pub fn try_cast<C: Core>(&self) -> Option<Object<C>> {
        let rc = self.upgrade_rc();
        rc.is::<C>().then(|| Object {
            weak: Rc::downgrade(&rc),
            on_added_dirty: None,
            on_added_clean: None,
            import: None,
            _phantom: PhantomData,
        })
    }

    pub fn cast<C: Core>(&self) -> Object<C> {
        fn type_name<T: Any>() -> String {
            if TypeId::of::<T>() == TypeId::of::<()>() {
                return String::from("Object");
            }

            format!("Object<{}>", any_type_name::<T>())
        }

        self.try_cast().unwrap_or_else(|| {
            panic!("failed cast from {} to {}", type_name::<T>(), type_name::<C>(),)
        })
    }
}

impl<'a, T: Core> From<Object<T>> for Object
where
    ObjectRef<'a, T>: OnAdded,
{
    fn from(object: Object<T>) -> Self {
        fn on_added_dirty<'a, T: Core>(rc: &Rc<dyn Core>, context: &dyn CoreContext) -> StatusCode
        where
            ObjectRef<'a, T>: OnAdded + 'a,
        {
            let object_ref: ObjectRef<'_, T> = ObjectRef {
                core_ref: CoreRef::Rc(Rc::clone(rc)),
                on_added_dirty: None,
                on_added_clean: None,
                import: None,
            };
            object_ref.on_added_dirty(context)
        }

        fn on_added_clean<'a, T: Core>(rc: &Rc<dyn Core>, context: &dyn CoreContext) -> StatusCode
        where
            ObjectRef<'a, T>: OnAdded,
        {
            let object_ref: ObjectRef<'_, T> = ObjectRef {
                core_ref: CoreRef::Rc(Rc::clone(rc)),
                on_added_dirty: None,
                on_added_clean: None,
                import: None,
            };
            object_ref.on_added_clean(context)
        }

        fn import<'a, T: Core>(
            rc: &Rc<dyn Core>,
            object: Object,
            import_stack: &ImportStack,
        ) -> StatusCode
        where
            ObjectRef<'a, T>: OnAdded,
        {
            let object_ref: ObjectRef<'_, T> = ObjectRef {
                core_ref: CoreRef::Rc(Rc::clone(rc)),
                on_added_dirty: None,
                on_added_clean: None,
                import: None,
            };
            object_ref.import(object, import_stack)
        }

        Self {
            weak: object.weak,
            on_added_dirty: Some(on_added_dirty::<T>),
            on_added_clean: Some(on_added_clean::<T>),
            import: Some(import::<T>),
            _phantom: PhantomData,
        }
    }
}

impl<T> Clone for Object<T> {
    fn clone(&self) -> Self {
        Self {
            weak: self.weak.clone(),
            on_added_dirty: self.on_added_dirty,
            on_added_clean: self.on_added_clean,
            import: self.import,
            _phantom: PhantomData,
        }
    }
}

impl fmt::Debug for Object {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Object").finish()
    }
}

impl<T: Core + fmt::Debug> fmt::Debug for Object<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Object").finish()
    }
}

impl<T> hash::Hash for Object<T> {
    fn hash<H: hash::Hasher>(&self, state: &mut H) {
        self.weak.upgrade().map(|rc| &*rc as *const _).hash(state);
    }
}

impl<T> Eq for Object<T> {}

impl<T> PartialEq for Object<T> {
    fn eq(&self, other: &Self) -> bool {
        self.weak.ptr_eq(&other.weak)
    }
}

#[derive(Debug)]
enum CoreRef<'a, T = ()> {
    Rc(Rc<dyn Core>),
    Borrow(&'a T),
}

impl<'a> CoreRef<'a> {
    pub fn try_cast<'b, C: Core>(&'b self) -> Option<CoreRef<'a, C>> {
        match self {
            Self::Rc(rc) => rc.is::<C>().then(|| CoreRef::Rc(Rc::clone(rc))),
            _ => unreachable!(),
        }
    }
}

impl<'a, T: Core> CoreRef<'a, T> {
    pub fn try_cast<'b, C: Core>(&'b self) -> Option<CoreRef<'a, C>> {
        match self {
            Self::Rc(rc) => rc.is::<C>().then(|| CoreRef::Rc(Rc::clone(rc))),
            Self::Borrow(borrow) => <dyn Core>::try_cast::<C>(*borrow).map(CoreRef::Borrow),
        }
    }

    pub fn type_name(&self) -> &'static str {
        match self {
            Self::Rc(rc) => rc.type_name(),
            Self::Borrow(borrow) => borrow.type_name(),
        }
    }
}

impl Deref for CoreRef<'_> {
    type Target = dyn Core;

    fn deref(&self) -> &Self::Target {
        if let Self::Rc(rc) = self {
            return &**rc;
        }

        panic!("CoreRef<'_> must contain an Rc pointer");
    }
}

impl<T: Core> Deref for CoreRef<'_, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        match self {
            Self::Rc(rc) => rc.cast(),
            Self::Borrow(borrow) => borrow,
        }
    }
}

impl<T> Clone for CoreRef<'_, T> {
    fn clone(&self) -> Self {
        match self {
            Self::Rc(rc) => Self::Rc(Rc::clone(rc)),
            Self::Borrow(borrow) => Self::Borrow(borrow),
        }
    }
}

pub struct ObjectRef<'a, T = ()> {
    core_ref: CoreRef<'a, T>,
    on_added_dirty: Option<OnAddedListener>,
    on_added_clean: Option<OnAddedListener>,
    import: Option<Importer>,
}

impl<'a, T: Core> From<&'a T> for ObjectRef<'a, T> {
    fn from(borrow: &'a T) -> Self {
        Self {
            core_ref: CoreRef::Borrow(borrow),
            on_added_dirty: None,
            on_added_clean: None,
            import: None,
        }
    }
}

impl<T: Core> From<Rc<T>> for ObjectRef<'_, T> {
    fn from(rc: Rc<T>) -> Self {
        Self { core_ref: CoreRef::Rc(rc), on_added_dirty: None, on_added_clean: None, import: None }
    }
}

impl<'a, T: Core> ObjectRef<'a, T> {
    pub fn try_cast<'b, C: Core>(&'b self) -> Option<ObjectRef<'a, C>> {
        self.core_ref.try_cast().map(|core_ref| ObjectRef {
            core_ref,
            on_added_dirty: self.on_added_dirty,
            on_added_clean: self.on_added_clean,
            import: self.import,
        })
    }

    pub fn cast<'b, C: Core>(&'b self) -> ObjectRef<'a, C> {
        fn type_name<T: Any>() -> String {
            format!("ObjectRef<'_, {}>", any_type_name::<T>())
        }

        self.try_cast().unwrap_or_else(|| {
            panic!("failed cast from {} to {}", type_name::<T>(), type_name::<C>(),)
        })
    }

    pub fn type_name(&self) -> &'static str {
        self.core_ref.type_name()
    }
}

impl<'a> ObjectRef<'a> {
    pub fn try_cast<'b, C: Core>(&'b self) -> Option<ObjectRef<'a, C>> {
        self.core_ref.try_cast().map(|core_ref| ObjectRef {
            core_ref,
            on_added_dirty: self.on_added_dirty,
            on_added_clean: self.on_added_clean,
            import: self.import,
        })
    }

    pub fn cast<'b, C: Core>(&'b self) -> ObjectRef<'a, C> {
        self.try_cast().unwrap_or_else(|| {
            panic!("failed cast from ObjectRef<'_> to {}", any_type_name::<C>(),)
        })
    }

    pub fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        if let CoreRef::Rc(rc) = &self.core_ref {
            self.on_added_dirty.map(|f| f(rc, context)).unwrap_or(StatusCode::Ok)
        } else {
            panic!("on_added_dirty should not be called on ObjectRef from borrows");
        }
    }

    pub fn on_added_clean(&self, context: &dyn CoreContext) -> StatusCode {
        if let CoreRef::Rc(rc) = &self.core_ref {
            self.on_added_clean.map(|f| f(rc, context)).unwrap_or(StatusCode::Ok)
        } else {
            panic!("on_added_clean should not be called on ObjectRef from borrows");
        }
    }

    pub fn import(&self, object: Object, import_stack: &ImportStack) -> StatusCode {
        if let CoreRef::Rc(rc) = &self.core_ref {
            self.import.map(|f| f(rc, object, import_stack)).unwrap_or(StatusCode::Ok)
        } else {
            panic!("import should not be called on ObjectRef from borrows");
        }
    }
}

impl<T> ObjectRef<'_, T> {
    pub fn as_object(&self) -> Object<T> {
        if let CoreRef::Rc(rc) = &self.core_ref {
            Object {
                weak: Rc::downgrade(rc),
                on_added_dirty: self.on_added_dirty,
                on_added_clean: self.on_added_clean,
                import: self.import,
                _phantom: PhantomData,
            }
        } else {
            panic!("as_object should not be called on ObjectRef from borrows")
        }
    }
}

impl Deref for ObjectRef<'_> {
    type Target = dyn Core;

    fn deref(&self) -> &Self::Target {
        &*self.core_ref
    }
}

impl<T: Core> Deref for ObjectRef<'_, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &*self.core_ref
    }
}

impl<T> Clone for ObjectRef<'_, T> {
    fn clone(&self) -> Self {
        Self {
            core_ref: self.core_ref.clone(),
            on_added_dirty: self.on_added_dirty,
            on_added_clean: self.on_added_clean,
            import: self.import,
        }
    }
}

impl fmt::Debug for ObjectRef<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Debug::fmt(&*self.core_ref, f)
    }
}

impl<T: Core + fmt::Debug> fmt::Debug for ObjectRef<'_, T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Debug::fmt(&*self.core_ref, f)
    }
}

impl<T: Eq> Eq for ObjectRef<'_, T> {}

impl<T: PartialEq> PartialEq for ObjectRef<'_, T> {
    fn eq(&self, other: &Self) -> bool {
        self.as_object() == other.as_object()
    }
}
