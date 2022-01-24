// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::{Cell, Ref, RefCell},
    collections::{HashMap, VecDeque},
    iter,
    rc::Rc,
};

use crate::{
    animation::{LinearAnimation, StateMachine},
    component::Component,
    component_dirt::ComponentDirt,
    container_component::ContainerComponent,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    dependency_sorter::DependencySorter,
    draw_target::{DrawTarget, DrawTargetPlacement},
    drawable::Drawable,
    math::{self, Aabb, Mat},
    option_cell::OptionCell,
    renderer::Renderer,
    shapes::{CommandPath, CommandPathBuilder, ShapePaintContainer},
    status_code::StatusCode,
};

#[derive(Clone, Debug)]
pub struct ObjectsIter<T: Core> {
    objects: Rc<RefCell<Vec<Object<T>>>>,
    head: usize,
    tail: usize,
}

impl<T: Core> ObjectsIter<T> {
    fn new(objects: Rc<RefCell<Vec<Object<T>>>>) -> Self {
        let len = objects.borrow().len();
        Self { objects, head: 0, tail: len }
    }
}

impl<T: Core> Iterator for ObjectsIter<T> {
    type Item = Object<T>;

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        if self.head == self.tail {
            return None;
        }

        let result = self.objects.borrow().iter().cloned().nth(self.head);

        if result.is_some() {
            self.head += 1;
        }

        result
    }

    #[inline]
    fn size_hint(&self) -> (usize, Option<usize>) {
        let len = self.tail - self.head;
        (len, Some(len))
    }

    #[inline]
    fn count(self) -> usize {
        self.tail - self.head
    }

    #[inline]
    fn nth(&mut self, n: usize) -> Option<Self::Item> {
        self.objects.borrow().iter().cloned().nth(self.head + n)
    }
}

impl<T: Core> DoubleEndedIterator for ObjectsIter<T> {
    #[inline]
    fn next_back(&mut self) -> Option<Self::Item> {
        if self.head == self.tail {
            return None;
        }

        let result = self.objects.borrow().iter().cloned().nth(self.tail - 1);

        if result.is_some() {
            self.tail -= 1;
        }

        result
    }

    #[inline]
    fn nth_back(&mut self, n: usize) -> Option<Self::Item> {
        if n >= self.tail {
            return None;
        }

        self.objects.borrow().iter().cloned().nth(self.tail - 1 - n)
    }
}

impl<T: Core> ExactSizeIterator for ObjectsIter<T> {
    #[inline]
    fn len(&self) -> usize {
        self.tail - self.head
    }
}

#[derive(Debug, Default)]
struct ArtboardInner {
    objects: RefCell<Vec<Object>>,
    animations: Rc<RefCell<Vec<Object<LinearAnimation>>>>,
    state_machines: Rc<RefCell<Vec<Object<StateMachine>>>>,
    dependency_order: RefCell<VecDeque<Object<Component>>>,
    drawables: RefCell<Vec<Object<Drawable>>>,
    draw_targets: RefCell<Vec<Object<DrawTarget>>>,
    dirt_depth: Cell<usize>,
    background_path: RefCell<Option<CommandPath>>,
    clip_path: RefCell<Option<CommandPath>>,
    first_drawable: OptionCell<Object<Drawable>>,
    root: OptionCell<Rc<dyn Core>>,
}

fn objects_of<T: Core>(objects: &[Object]) -> impl Iterator<Item = Object<T>> + '_ {
    objects.iter().cloned().filter_map(|object| object.try_cast())
}

impl ArtboardInner {
    pub fn initialize(&self) -> StatusCode {
        for object in self.objects.borrow().iter() {
            let code = object.as_ref().on_added_dirty(self);
            if code != StatusCode::Ok {
                return code;
            }
        }

        for object in self.animations.borrow().iter() {
            let code = object.as_ref().on_added_dirty(self);
            if code != StatusCode::Ok {
                return code;
            }
        }

        for object in self.state_machines.borrow().iter() {
            let code = object.as_ref().on_added_dirty(self);
            if code != StatusCode::Ok {
                return code;
            }
        }

        let mut component_draw_rules = HashMap::new();

        for object in self.objects.borrow().iter() {
            let code = object.as_ref().on_added_clean(self);
            if code != StatusCode::Ok {
                return code;
            }

            if let Some(draw_rules) = object.try_cast() {
                if let Some(component) = self
                    .resolve(draw_rules.cast::<Component>().as_ref().parent_id() as usize)
                    .and_then(|core| core.try_cast())
                {
                    component_draw_rules.insert(component, draw_rules);
                } else {
                    return StatusCode::MissingObject;
                }
            }
        }

        for object in self.animations.borrow().iter() {
            let code = object.as_ref().on_added_clean(self);
            if code != StatusCode::Ok {
                return code;
            }
        }

        for object in self.state_machines.borrow().iter() {
            let code = object.as_ref().on_added_clean(self);
            if code != StatusCode::Ok {
                return code;
            }
        }

        for component in objects_of::<Component>(self.objects.borrow().as_slice()) {
            component.as_ref().build_dependencies();
        }

        for drawable in objects_of::<Drawable>(self.objects.borrow().as_slice()) {
            self.drawables.borrow_mut().push(drawable.clone());

            let parents = iter::once(drawable.cast::<ContainerComponent>())
                .chain(drawable.cast::<Component>().as_ref().parents());

            for parent in parents {
                if let Some(draw_rules) = component_draw_rules.get(&parent) {
                    drawable.as_ref().flattened_draw_rules.set(Some(draw_rules.clone()));
                    break;
                }
            }
        }

        self.sort_dependencies();

        let root: Rc<dyn Core> = Rc::new(DrawTarget::default());
        self.root.set(Some(Rc::clone(&root)));
        let root = Object::new(&root);

        for draw_target in objects_of::<DrawTarget>(self.objects.borrow().as_slice()) {
            root.cast::<Component>().as_ref().push_dependent(draw_target.cast());

            if let Some(dependent_rules) = draw_target
                .as_ref()
                .drawable()
                .expect("DrawTarget has no Drawable set")
                .as_ref()
                .flattened_draw_rules
                .get()
            {
                for dependent_target in objects_of::<DrawTarget>(self.objects.borrow().as_slice()) {
                    let dependent_target = dependent_target.cast::<Component>();
                    let dependent_target = dependent_target.as_ref();
                    if let Some(parent) = dependent_target.parent() {
                        if parent.ptr_eq(&dependent_rules) {
                            dependent_target.push_dependent(draw_target.cast());
                        }
                    }
                }
            }
        }

        let mut sorter = DependencySorter::default();
        let mut draw_target_order = VecDeque::new();

        sorter.sort(root, &mut draw_target_order);

        self.draw_targets
            .borrow_mut()
            .extend(draw_target_order.into_iter().map(|component| component.cast()));

        StatusCode::Ok
    }

    pub fn sort_draw_order(&self) {
        for target in &*self.draw_targets.borrow() {
            let target = target.as_ref();
            target.first.set(None);
            target.last.set(None);
        }

        self.first_drawable.set(None);
        let mut last_drawable = None;

        for drawable in &*self.drawables.borrow() {
            let drawable_ref = drawable.as_ref();
            if let Some(target) = drawable_ref
                .flattened_draw_rules
                .get()
                .and_then(|rules| rules.as_ref().active_target())
            {
                let target = target.as_ref();
                if let (Some(_), Some(last)) = (target.first.get(), target.last.get()) {
                    last.as_ref().next.set(Some(drawable.clone()));
                    drawable_ref.prev.set(Some(last));
                    target.last.set(Some(drawable.clone()));
                } else {
                    target.first.set(Some(drawable.clone()));
                    target.last.set(Some(drawable.clone()));
                    drawable_ref.prev.set(None);
                }
                drawable_ref.next.set(None);
            } else {
                drawable_ref.prev.set(last_drawable.clone());
                drawable_ref.next.set(None);

                if let Some(ref last_drawable_ref) = last_drawable {
                    last_drawable_ref.as_ref().next.set(Some(drawable.clone()));
                    last_drawable = Some(drawable.clone());
                } else {
                    last_drawable = Some(drawable.clone());
                    self.first_drawable.set(Some(drawable.clone()));
                }
            }
        }

        for rule in &*self.draw_targets.borrow() {
            let rule = rule.as_ref();
            if let (Some(ref rule_first), Some(ref rule_last)) = (rule.first.get(), rule.last.get())
            {
                let rule_last_ref = rule_last.as_ref();
                if let Some(ref target_drawable) = rule.drawable() {
                    let target_drawable_ref = target_drawable.as_ref();
                    match rule.placement() {
                        DrawTargetPlacement::Before => {
                            if let Some(prev) = target_drawable_ref.prev.get() {
                                prev.as_ref().next.set(Some(rule_first.clone()));
                                rule_first.as_ref().prev.set(Some(prev));
                            }

                            if Some(target_drawable) == self.first_drawable.get().as_ref() {
                                self.first_drawable.set(Some(rule_first.clone()));
                            }

                            target_drawable_ref.prev.set(Some(rule_last.clone()));
                            rule_last_ref.next.set(Some(target_drawable.clone()));
                        }
                        DrawTargetPlacement::After => {
                            if let Some(next) = target_drawable_ref.next.get() {
                                next.as_ref().prev.set(Some(rule_last.clone()));
                                rule_last_ref.next.set(target_drawable_ref.next.get());
                            }

                            if Some(target_drawable) == last_drawable.as_ref() {
                                last_drawable = Some(rule_last.clone());
                            }

                            target_drawable_ref.next.set(Some(rule_first.clone()));
                            rule_last_ref.prev.set(Some(target_drawable.clone()));
                        }
                    }
                }
            }
        }

        self.first_drawable.set(last_drawable);
    }

    fn component(&self) -> Object<Component> {
        self.objects.borrow()[0].try_cast().expect("first object in Artboard must be itself")
    }

    fn sort_dependencies(&self) {
        let mut sorter = DependencySorter::default();

        let component = self.component();
        sorter.sort(component.clone(), &mut *self.dependency_order.borrow_mut());

        for (component, graph_order) in self.dependency_order.borrow().iter().zip(0..) {
            component.as_ref().graph_order.set(graph_order);
        }

        component.as_ref().dirt.set(component.as_ref().dirt.get() | ComponentDirt::COMPONENTS);
    }

    pub fn push_object(&self, object: Object) {
        self.objects.borrow_mut().push(object);
    }

    pub fn push_animation(&self, animation: Object<LinearAnimation>) {
        self.animations.borrow_mut().push(animation);
    }

    pub fn push_state_machine(&self, state_machine: Object<StateMachine>) {
        self.state_machines.borrow_mut().push(state_machine);
    }

    pub fn on_component_dirty(&self, component: &Component) {
        let this_component = self.component();
        let this_component = this_component.as_ref();
        this_component.dirt.set(this_component.dirt.get() | ComponentDirt::COMPONENTS);

        self.dirt_depth.set(self.dirt_depth.get().min(component.graph_order.get()));
    }

    pub fn update_components(&self, component: ObjectRef<'_, Component>) -> bool {
        let mut step = 0;
        while component.has_dirt(ComponentDirt::COMPONENTS) && step < 100 {
            for (i, component) in self.dependency_order.borrow().iter().enumerate() {
                self.dirt_depth.set(i);

                let component = component.as_ref();
                let dirt = component.dirt.get();
                if dirt.is_empty() {
                    continue;
                }

                component.dirt.set(ComponentDirt::empty());
                component.update(dirt);

                if self.dirt_depth.get() < i {
                    break;
                }
            }

            step += 1;
        }

        false
    }
}

#[derive(Debug, Default)]
pub struct Artboard {
    container_component: ContainerComponent,
    shape_paint_container: ShapePaintContainer,
    width: Property<f32>,
    height: Property<f32>,
    x: Property<f32>,
    y: Property<f32>,
    origin_x: Property<f32>,
    origin_y: Property<f32>,
    inner: ArtboardInner,
}

impl ObjectRef<'_, Artboard> {
    pub fn width(&self) -> f32 {
        self.width.get()
    }

    pub fn set_width(&self, width: f32) {
        self.width.set(width);
    }

    pub fn height(&self) -> f32 {
        self.height.get()
    }

    pub fn set_height(&self, height: f32) {
        self.height.set(height);
    }

    pub fn x(&self) -> f32 {
        self.x.get()
    }

    pub fn set_x(&self, x: f32) {
        self.x.set(x);
    }

    pub fn y(&self) -> f32 {
        self.y.get()
    }

    pub fn set_y(&self, y: f32) {
        self.y.set(y);
    }

    pub fn origin_x(&self) -> f32 {
        self.origin_x.get()
    }

    pub fn set_origin_x(&self, origin_x: f32) {
        self.origin_x.set(origin_x);
    }

    pub fn origin_y(&self) -> f32 {
        self.origin_y.get()
    }

    pub fn set_origin_y(&self, origin_y: f32) {
        self.origin_y.set(origin_y);
    }
}

impl ObjectRef<'_, Artboard> {
    pub fn initialize(&self) -> StatusCode {
        self.inner.initialize()
    }

    pub fn push_object(&self, object: Object) {
        self.inner.push_object(object);
    }

    pub(crate) fn objects(&self) -> Ref<'_, [Object]> {
        Ref::map(self.inner.objects.borrow(), |objects| objects.as_slice())
    }

    pub fn push_animation(&self, animation: Object<LinearAnimation>) {
        self.inner.push_animation(animation);
    }

    pub fn push_state_machine(&self, state_machine: Object<StateMachine>) {
        self.inner.push_state_machine(state_machine);
    }

    pub fn on_component_dirty(&self, component: &Component) {
        self.inner.on_component_dirty(component);
    }

    fn as_component(&self) -> ObjectRef<'_, Component> {
        self.cast()
    }

    pub fn on_dirty(&self, _dirt: ComponentDirt) {
        let dirt = &self.as_component().dirt;
        dirt.set(dirt.get() | ComponentDirt::COMPONENTS);
    }

    pub fn update(&self, value: ComponentDirt) {
        if Component::value_has_dirt(value, ComponentDirt::DRAW_ORDER) {
            self.inner.sort_draw_order();
        }

        if Component::value_has_dirt(value, ComponentDirt::PATH) {
            let mut builder = CommandPathBuilder::new();

            builder.rect(math::Vec::new(0.0, 0.0), math::Vec::new(self.width(), self.height()));

            *self.inner.clip_path.borrow_mut() = Some(builder.build());

            let mut builder = CommandPathBuilder::new();

            builder.rect(
                math::Vec::new(-self.width() * self.origin_x(), -self.height() * self.origin_y()),
                math::Vec::new(self.width(), self.height()),
            );

            *self.inner.background_path.borrow_mut() = Some(builder.build());
        }
    }

    pub fn update_components(&self) -> bool {
        let component = self.as_component();
        if component.has_dirt(ComponentDirt::COMPONENTS) {
            return self.inner.update_components(component);
        }

        false
    }

    pub fn advance(&self, _elapsed_seconds: f32) -> bool {
        self.update_components()
    }

    pub fn draw(&self, renderer: &mut impl Renderer, transform: Mat) {
        let mut artboard_transform = Mat {
            translate_x: self.width() * self.origin_x(),
            translate_y: self.height() * self.origin_y(),
            ..Default::default()
        };

        artboard_transform = artboard_transform * transform;

        for shape_paint in self.cast::<ShapePaintContainer>().shape_paints() {
            shape_paint.as_ref().draw(
                renderer,
                self.inner
                    .background_path
                    .borrow()
                    .as_ref()
                    .expect("background_path should already be set in Artboard"),
                artboard_transform,
            );
        }

        let drawables = iter::successors(self.inner.first_drawable.get(), |drawable| {
            drawable.as_ref().prev.get()
        });

        for drawable in drawables {
            drawable.as_ref().draw(renderer, artboard_transform);
        }
    }

    pub fn bounds(&self) -> Aabb {
        Aabb::new(0.0, 0.0, self.width(), self.height())
    }

    pub fn animations(&self) -> ObjectsIter<LinearAnimation> {
        ObjectsIter::new(self.inner.animations.clone())
    }

    pub fn state_machines(&self) -> ObjectsIter<StateMachine> {
        ObjectsIter::new(self.inner.state_machines.clone())
    }
}

impl Core for Artboard {
    parent_types![
        (container_component, ContainerComponent),
        (shape_paint_container, ShapePaintContainer),
    ];

    properties![
        (7, width, set_width),
        (8, height, set_height),
        (9, x, set_x),
        (10, y, set_y),
        (11, origin_x, set_origin_x),
        (12, origin_y, set_origin_y),
        container_component,
    ];
}

impl OnAdded for ObjectRef<'_, Artboard> {
    on_added!(ContainerComponent);
}

impl CoreContext for ArtboardInner {
    fn resolve(&self, id: usize) -> Option<Object> {
        self.objects.borrow().get(id).cloned()
    }
}

impl CoreContext for Artboard {
    fn resolve(&self, id: usize) -> Option<Object> {
        self.inner.resolve(id)
    }
}
