// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    any::{self, Any, TypeId},
    fmt,
    rc::Rc,
};

use crate::{
    animation::Loop,
    artboard::Artboard,
    draw_target::DrawTargetPlacement,
    importers::ImportStack,
    shapes::{
        paint::{BlendMode, Color32, StrokeCap, StrokeJoin},
        FillRule,
    },
    StatusCode,
};

mod binary_reader;
mod object;
mod property;

pub use binary_reader::BinaryReader;
pub use object::{Object, ObjectRef};
pub use property::{Property, TryFromU64};

macro_rules! types {
    ( $( ( $id:expr , $type:ty ) ),* $( , )? ) => {
        pub fn get_type_id(id: u16) -> Option<TypeId> {
            match id {
                $(
                    $id => Some(TypeId::of::<$type>()),
                )*
                _ => None,
            }
        }

        impl dyn Core {
            pub fn from_type_id(id: TypeId) -> Option<(Rc<dyn Core>, Object)> {
                match id {
                    $(
                        id if id == TypeId::of::<$type>() => {
                            let rc: Rc<dyn Core> = Rc::new(<$type>::default());
                            let object = Object::<$type>::new(&rc);
                            Some((rc, object.into()))
                        }
                    )*
                    _ => None,
                }
            }
        }
    };
}

macro_rules! parent_types {
    ( ( $field_head:ident , $type_head:ty ) $( , ( $field_tail:ident , $type_tail:ty ) )* $( , )? ) => {
        fn ref_of(&self, id: ::std::any::TypeId) -> Option<&dyn Core> {
            if id == ::std::any::TypeId::of::<$type_head>() {
                Some(&self.$field_head)
            }
            $(
                else if id == ::std::any::TypeId::of::<$type_tail>() {
                    Some(&self.$field_tail)
                }
            )*
            else {
                self.$field_head
                    .ref_of(id)
                    $(
                        .or_else(|| self.$field_tail.ref_of(id))
                    )*
            }
        }
    };
}

macro_rules! properties {
    ( $parent:ident ) => {
        fn property_of(&self, key: u16) -> Option<&dyn ::std::any::Any> {
            self.$parent.property_of(key)
        }

        fn animate(&self, object: &ObjectRef<'_>, key: u64, animator: &dyn ::std::any::Any) {
            self.$parent.animate(object, key, animator);
        }
    };

    ( $( ( $key:expr , $field:ident , $setter:ident ) ),* , $parent:ident $( , )? ) => {
        fn property_of(&self, key: u16) -> Option<&dyn ::std::any::Any> {
            match key {
                $(
                    $key => Some(&self.$field),
                )*
                _ => self.$parent.property_of(key),
            }
        }

        fn animate(&self, object: &ObjectRef<'_>, key: u64, animator: &dyn ::std::any::Any) {
            $(
                if key == $key {
                    if let Some(animator) = animator.downcast_ref::<crate::animation::Animator<_>>() {
                        return animator.animate(&object.cast(), ObjectRef::<Self>::$setter);
                    }
                }
            )*

            self.$parent.animate(&object, key, animator);
        }
    };

    ( $( ( $key:expr , $field:ident , $setter:ident ) ),* $( , )? ) => {
        fn property_of(&self, key: u16) -> Option<&dyn ::std::any::Any> {
            match key {
                $(
                    $key => Some(&self.$field),
                )*
                _ => None,
            }
        }

        fn animate(&self, object: &ObjectRef<'_>, key: u64, animator: &dyn ::std::any::Any) {
            $(
                if key == $key {
                    if let Some(animator) = animator.downcast_ref::<crate::animation::Animator<_>>() {
                        return animator.animate(&object.cast(), ObjectRef::<Self>::$setter);
                    }
                }
            )*
        }
    };
}

macro_rules! on_added {
    ( @impl import ) => {
        fn import(&self,  _object: crate::core::Object, _import_stack: &crate::importers::ImportStack) -> crate::status_code::StatusCode {
            crate::status_code::StatusCode::Ok
        }
    };

    ( @impl $method:ident ) => {
        fn $method(&self, _context: &dyn crate::core::CoreContext) -> crate::status_code::StatusCode {
            crate::status_code::StatusCode::Ok
        }
    };

    ( @impl import , $type:ty ) => {
        fn import(&self, object: crate::core::Object, import_stack: &crate::importers::ImportStack) -> crate::status_code::StatusCode {
            self.cast::<$type>().import(object, import_stack)
        }
    };

    ( @impl $method:ident , $type:ty ) => {
        fn $method(&self, context: &dyn crate::core::CoreContext) -> crate::status_code::StatusCode {
            self.cast::<$type>().$method(context)
        }
    };

    () => {
        on_added!(@impl on_added_dirty);
        on_added!(@impl on_added_clean);
        on_added!(@impl import);
    };

    ( [ $( $method:ident ),+ ] ) => {
        $(
            on_added!(@impl $method);
        )+
    };

    ( [ $( $method:ident ),+ ] , $type:ty ) => {
        $(
            on_added!(@impl $method, $type);
        )+
    };

    ( $type:ty ) => {
        on_added!(@impl on_added_dirty, $type);
        on_added!(@impl on_added_clean, $type);
        on_added!(@impl import, $type);
    };
}

pub trait AsAny: Any + fmt::Debug {
    fn as_any(&self) -> &dyn Any;
    fn any_type_name(&self) -> &'static str;
}

impl<T: Any + fmt::Debug> AsAny for T {
    #[inline(always)]
    fn as_any(&self) -> &dyn Any {
        self
    }

    #[inline(always)]
    fn any_type_name(&self) -> &'static str {
        core::any::type_name::<T>()
    }
}

trait Cast: Core {
    #[inline]
    fn is<T>(&self) -> bool
    where
        T: Core,
    {
        self.as_any().is::<T>() || self.ref_of(TypeId::of::<T>()).is_some()
    }

    #[inline]
    fn try_cast<T>(&self) -> Option<&T>
    where
        T: Core,
    {
        self.as_any()
            .downcast_ref()
            .or_else(|| self.ref_of(TypeId::of::<T>()).and_then(|any| any.as_any().downcast_ref()))
    }

    #[inline]
    fn cast<T>(&self) -> &T
    where
        T: Core,
    {
        self.try_cast().unwrap_or_else(|| panic!("failed cast to {}", any::type_name::<T>()))
    }
}

impl Cast for dyn Core {}

#[allow(unused_variables)]
pub trait Core: AsAny {
    #[inline]
    fn ref_of(&self, id: TypeId) -> Option<&dyn Core> {
        None
    }

    #[inline]
    fn property_of(&self, key: u16) -> Option<&dyn Any> {
        None
    }

    #[inline]
    fn animate(&self, object: &ObjectRef<'_>, key: u64, animator: &dyn Any) {}

    #[inline]
    fn type_name(&self) -> &'static str {
        self.any_type_name().rsplit("::").next().unwrap()
    }
}

impl dyn Core {
    #[inline]
    pub fn get_property<T: Clone + Default + 'static>(&self, key: u16) -> Option<&Property<T>> {
        self.property_of(key).and_then(<dyn Any>::downcast_ref::<Property<T>>)
    }

    pub(crate) fn write(&self, property_key: u16, reader: &mut BinaryReader<'_>) -> bool {
        if let Some(property) = self.property_of(property_key) {
            macro_rules! write_types {
                ( $property:expr , $reader:expr , [ $( $type:ident ),* $( , )? ] ) => {
                    match $property.type_id() {
                        $(
                            id if id == TypeId::of::<Property<$type>>() => {
                                let property = property.downcast_ref::<Property<$type>>().unwrap();
                                return $reader.write(property);
                            }
                        )*
                        _ => (),
                    }
                };
            }

            write_types!(
                property,
                reader,
                [
                    bool,
                    u32,
                    u64,
                    f32,
                    String,
                    Color32,
                    BlendMode,
                    DrawTargetPlacement,
                    FillRule,
                    Loop,
                    StrokeCap,
                    StrokeJoin,
                ]
            );
        }

        false
    }
}

pub trait OnAdded {
    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode;

    fn on_added_clean(&self, context: &dyn CoreContext) -> StatusCode;

    fn import(&self, object: Object, import_stack: &ImportStack) -> StatusCode;
}

pub trait CoreContext {
    fn resolve(&self, id: usize) -> Option<Object>;
    fn artboard(&self) -> Object<Artboard> {
        self.resolve(0)
            .and_then(|object| object.try_cast())
            .expect("frist object should always be the Artboard")
    }
}

types![
    (1, crate::Artboard),
    (2, crate::Node),
    (3, crate::shapes::Shape),
    (4, crate::shapes::Ellipse),
    (5, crate::shapes::StraightVertex),
    (6, crate::shapes::CubicDetachedVertex),
    (7, crate::shapes::Rectangle),
    (8, crate::shapes::Triangle),
    (9, crate::shapes::PathComposer),
    (10, crate::Component),
    (11, crate::ContainerComponent),
    (13, crate::Drawable),
    (14, crate::shapes::PathVertex),
    (15, crate::shapes::ParametricPath),
    (16, crate::shapes::PointsPath),
    (17, crate::shapes::paint::RadialGradient),
    (18, crate::shapes::paint::SolidColor),
    (19, crate::shapes::paint::GradientStop),
    (20, crate::shapes::paint::Fill),
    (21, crate::shapes::paint::ShapePaint),
    (22, crate::shapes::paint::LinearGradient),
    (23, crate::Backboard),
    (24, crate::shapes::paint::Stroke),
    (25, crate::animation::KeyedObject),
    (26, crate::animation::KeyedProperty),
    (27, crate::animation::Animation),
    (28, crate::animation::CubicInterpolator),
    (29, crate::animation::KeyFrame),
    (30, crate::animation::KeyFrameDouble),
    (31, crate::animation::LinearAnimation),
    (34, crate::shapes::CubicAsymmetricVertex),
    (35, crate::shapes::CubicMirroredVertex),
    (37, crate::animation::KeyFrameColor),
    (38, crate::TransformComponent),
    (39, crate::bones::SkeletalComponent),
    (40, crate::bones::Bone),
    (41, crate::bones::RootBone),
    (42, crate::shapes::ClippingShape),
    (43, crate::bones::Skin),
    (44, crate::bones::Tendon),
    (45, crate::bones::Weight),
    (46, crate::bones::CubicWeight),
    (47, crate::shapes::paint::TrimPath),
    (48, crate::DrawTarget),
    (49, crate::DrawRules),
    (50, crate::animation::KeyFrameId),
    (51, crate::shapes::Polygon),
    (52, crate::shapes::Star),
    (53, crate::animation::StateMachine),
    (54, crate::animation::StateMachineComponent),
    (55, crate::animation::StateMachineInput),
    (56, crate::animation::StateMachineDouble),
    (57, crate::animation::StateMachineLayer),
    (58, crate::animation::StateMachineTrigger),
    (59, crate::animation::StateMachineBool),
    (60, crate::animation::LayerState),
    (61, crate::animation::AnimationState),
    (62, crate::animation::AnyState),
    (63, crate::animation::EntryState),
    (64, crate::animation::ExitState),
    (65, crate::animation::StateTransition),
    (66, crate::animation::StateMachineLayerComponent),
    (67, crate::animation::TransitionCondition),
    (68, crate::animation::TransitionTriggerCondition),
    (69, crate::animation::TransitionValueCondition),
    (70, crate::animation::TransitionDoubleCondition),
    (71, crate::animation::TransitionBoolCondition),
];
