// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

mod cmd;
mod view_token_pair;
pub use self::view_token_pair::*;

use fidl_fuchsia_images::{ImageInfo, MemoryType, PixelFormat, PresentationInfo, Tiling};
use fidl_fuchsia_ui_gfx::{
    CircleArgs, ColorRgba, EntityNodeArgs, ImageArgs, ImportSpec, MaterialArgs, MemoryArgs,
    RectangleArgs, ResourceArgs, RoundedRectangleArgs, ShapeNodeArgs, Value, ViewArgs2,
    ViewHolderArgs2, ViewProperties,
};
use fidl_fuchsia_ui_scenic::{Command, SessionProxy};
use fidl_fuchsia_ui_views::{ViewHolderToken, ViewToken};
use fuchsia_zircon::{Event, EventPair, HandleBased, Rights, Status, Vmo};
use mapped_vmo::Mapping;
use parking_lot::Mutex;
use std::ops::Deref;
use std::sync::Arc;

pub struct Session {
    session: SessionProxy,
    next_resource_id: u32,
    resource_count: u32,
    commands: Vec<Command>,
    acquire_fences: Vec<Event>,
    release_fences: Vec<Event>,
}

pub type SessionPtr = Arc<Mutex<Session>>;

impl Session {
    pub fn new(session: SessionProxy) -> SessionPtr {
        Arc::new(Mutex::new(Session {
            session,
            next_resource_id: 1,
            resource_count: 0,
            commands: vec![],
            acquire_fences: vec![],
            release_fences: vec![],
        }))
    }

    pub fn enqueue(&mut self, command: Command) {
        self.commands.push(command)
    }

    pub fn flush(&mut self) {
        // A single FIDL message can contain only 64k of serialized
        // data. Chunk the command list into fixed size chunks as a hacky
        // way to work around this limit. This is imperfect, though, as
        // there's no strict relationship between the number of commands
        // and the size of the serialized data.
        const CHUNK_SIZE: usize = 32;
        for chunk in self.commands.chunks_mut(CHUNK_SIZE).into_iter() {
            self.session
                .enqueue(&mut chunk.into_iter())
                .expect("Session failed to enqueue commands");
        }
        self.commands.truncate(0);
    }

    pub fn present(
        &mut self,
        presentation_time: u64,
    ) -> fidl::client::QueryResponseFut<(PresentationInfo)> {
        self.flush();
        self.session.present(
            presentation_time,
            &mut self.acquire_fences.drain(..),
            &mut self.release_fences.drain(..),
        )
    }

    pub fn next_resource_id(&self) -> u32 {
        self.next_resource_id
    }

    fn alloc_resource_id(&mut self) -> u32 {
        let id = self.next_resource_id;
        self.next_resource_id += 1;
        self.resource_count += 1;
        assert!(id != 0);
        id
    }

    fn release_resource(&mut self, id: u32) {
        self.resource_count -= 1;
        self.enqueue(cmd::release_resource(id))
    }

    pub fn add_acquire_fence(&mut self, fence: Event) {
        self.acquire_fences.push(fence)
    }

    pub fn add_release_fence(&mut self, fence: Event) {
        self.release_fences.push(fence)
    }
}

pub struct Resource {
    session: SessionPtr,
    id: u32,
}

impl Resource {
    fn new(session: SessionPtr, resource: ResourceArgs) -> Resource {
        let id = {
            let mut s = session.lock();
            let id = s.alloc_resource_id();
            s.enqueue(cmd::create_resource(id, resource));
            id
        };
        Resource { session, id }
    }

    fn import(session: SessionPtr, token: EventPair, spec: ImportSpec) -> Resource {
        let id = {
            let mut s = session.lock();
            let id = s.alloc_resource_id();
            s.enqueue(cmd::import_resource(id, token, spec));
            id
        };
        Resource { session, id }
    }

    fn enqueue(&self, command: Command) {
        let mut session = self.session.lock();
        session.enqueue(command);
    }

    pub fn set_event_mask(&self, event_mask: u32) {
        self.enqueue(cmd::set_event_mask(self.id, event_mask))
    }
}

impl Drop for Resource {
    fn drop(&mut self) {
        let mut s = self.session.lock();
        s.release_resource(self.id);
    }
}

pub struct Memory {
    resource: Resource,
}

impl Memory {
    pub fn new(
        session: SessionPtr,
        vmo: Vmo,
        allocation_size: u64,
        memory_type: MemoryType,
    ) -> Memory {
        let args = MemoryArgs { vmo: vmo, allocation_size: allocation_size, memory_type };
        Memory { resource: Resource::new(session, ResourceArgs::Memory(args)) }
    }
}

pub struct Image {
    resource: Resource,
    info: ImageInfo,
}

impl Image {
    pub fn new(memory: &Memory, memory_offset: u32, info: ImageInfo) -> Image {
        let args = ImageArgs {
            memory_id: memory.resource.id,
            memory_offset,
            // TODO: Use clone once FIDL generated the proper annotations.
            info: ImageInfo { ..info },
        };
        Image {
            resource: Resource::new(memory.resource.session.clone(), ResourceArgs::Image(args)),
            info: info,
        }
    }

    pub fn id(&self) -> u32 {
        self.resource.id
    }
}

pub struct Shape {
    resource: Resource,
}

impl Shape {
    fn new(resource: Resource) -> Shape {
        Shape { resource }
    }

    pub fn id(&self) -> u32 {
        self.resource.id
    }
}

pub struct Circle(Shape);

impl Circle {
    pub fn new(session: SessionPtr, radius: f32) -> Circle {
        let args = CircleArgs { radius: Value::Vector1(radius) };
        Circle(Shape::new(Resource::new(session, ResourceArgs::Circle(args))))
    }
}

impl Deref for Circle {
    type Target = Shape;

    fn deref(&self) -> &Shape {
        &self.0
    }
}

pub struct Rectangle(Shape);

impl Rectangle {
    pub fn new(session: SessionPtr, width: f32, height: f32) -> Rectangle {
        let args = RectangleArgs { width: Value::Vector1(width), height: Value::Vector1(height) };
        Rectangle(Shape::new(Resource::new(session, ResourceArgs::Rectangle(args))))
    }
}

impl Deref for Rectangle {
    type Target = Shape;

    fn deref(&self) -> &Shape {
        &self.0
    }
}

pub struct RoundedRectangle(Shape);

impl RoundedRectangle {
    pub fn new(
        session: SessionPtr,
        width: f32,
        height: f32,
        top_left_radius: f32,
        top_right_radius: f32,
        bottom_right_radius: f32,
        bottom_left_radius: f32,
    ) -> RoundedRectangle {
        let args = RoundedRectangleArgs {
            width: Value::Vector1(width),
            height: Value::Vector1(height),
            top_left_radius: Value::Vector1(top_left_radius),
            top_right_radius: Value::Vector1(top_right_radius),
            bottom_right_radius: Value::Vector1(bottom_right_radius),
            bottom_left_radius: Value::Vector1(bottom_left_radius),
        };
        RoundedRectangle(Shape::new(Resource::new(session, ResourceArgs::RoundedRectangle(args))))
    }
}

impl Deref for RoundedRectangle {
    type Target = Shape;

    fn deref(&self) -> &Shape {
        &self.0
    }
}

pub struct Material {
    resource: Resource,
}

impl Material {
    pub fn new(session: SessionPtr) -> Material {
        let args = MaterialArgs { dummy: 0 };
        Material { resource: Resource::new(session, ResourceArgs::Material(args)) }
    }

    pub fn id(&self) -> u32 {
        self.resource.id
    }

    pub fn set_color(&self, color: ColorRgba) {
        self.resource.enqueue(cmd::set_color(self.id(), color));
    }

    pub fn set_texture(&self, texture: Option<&Image>) {
        self.resource.enqueue(cmd::set_texture(self.id(), texture.map(|t| t.id()).unwrap_or(0)));
    }
}

pub struct Node {
    resource: Resource,
}

impl Node {
    fn new(resource: Resource) -> Node {
        Node { resource }
    }

    pub fn id(&self) -> u32 {
        self.resource.id
    }

    pub fn resource(&self) -> &Resource {
        &self.resource
    }

    pub fn set_translation(&self, x: f32, y: f32, z: f32) {
        self.resource.enqueue(cmd::set_translation(self.id(), x, y, z))
    }

    pub fn set_scale(&self, x: f32, y: f32, z: f32) {
        self.resource.enqueue(cmd::set_scale(self.id(), x, y, z))
    }

    pub fn set_rotation(&self, x: f32, y: f32, z: f32, w: f32) {
        self.resource.enqueue(cmd::set_rotation(self.id(), x, y, z, w))
    }

    pub fn set_anchor(&self, x: f32, y: f32, z: f32) {
        self.resource.enqueue(cmd::set_anchor(self.id(), x, y, z))
    }

    pub fn detach(&self) {
        self.resource.enqueue(cmd::detach(self.id()))
    }

    fn enqueue(&self, command: Command) {
        self.resource.enqueue(command);
    }
}

pub struct View {
    resource: Resource,
}

impl View {
    pub fn new(session: SessionPtr, token: ViewToken, debug_name: Option<String>) -> View {
        let args = ViewArgs2 { token: token, debug_name: debug_name };
        View { resource: Resource::new(session, ResourceArgs::View2(args)) }
    }

    pub fn id(&self) -> u32 {
        self.resource.id
    }

    pub fn add_child(&self, child: &Node) {
        self.resource.enqueue(cmd::add_child(self.id(), child.id()))
    }

    pub fn detach_child(&self, child: &Node) {
        self.resource.enqueue(cmd::detach(child.id()))
    }
}

pub struct ViewHolder {
    resource: Resource,
}

impl ViewHolder {
    pub fn new(
        session: SessionPtr,
        token: ViewHolderToken,
        debug_name: Option<String>,
    ) -> ViewHolder {
        let args = ViewHolderArgs2 { token: token, debug_name: debug_name };
        ViewHolder { resource: Resource::new(session, ResourceArgs::ViewHolder2(args)) }
    }

    pub fn id(&self) -> u32 {
        self.resource.id
    }

    pub fn set_view_properties(&self, view_properties: ViewProperties) {
        self.resource.enqueue(cmd::set_view_properties(self.id(), view_properties))
    }
}

pub struct ShapeNode(Node);

impl ShapeNode {
    pub fn new(session: SessionPtr) -> ShapeNode {
        let args = ShapeNodeArgs { unused: 0 };
        ShapeNode(Node::new(Resource::new(session, ResourceArgs::ShapeNode(args))))
    }

    pub fn set_shape(&self, shape: &Shape) {
        self.enqueue(cmd::set_shape(self.id(), shape.id()));
    }

    pub fn set_material(&self, material: &Material) {
        self.enqueue(cmd::set_material(self.id(), material.id()));
    }
}

impl Deref for ShapeNode {
    type Target = Node;

    fn deref(&self) -> &Node {
        &self.0
    }
}

pub struct ContainerNode(Node);

impl ContainerNode {
    fn new(resource: Resource) -> ContainerNode {
        ContainerNode(Node::new(resource))
    }

    pub fn add_child(&self, node: &Node) {
        self.enqueue(cmd::add_child(self.id(), node.id()));
    }

    pub fn add_part(&self, node: &Node) {
        self.enqueue(cmd::add_part(self.id(), node.id()));
    }
}

impl Deref for ContainerNode {
    type Target = Node;

    fn deref(&self) -> &Node {
        &self.0
    }
}

pub struct ImportNode(ContainerNode);

impl ImportNode {
    pub fn new(session: SessionPtr, token: EventPair) -> ImportNode {
        ImportNode(ContainerNode::new(Resource::import(session, token, ImportSpec::Node)))
    }
}

impl Deref for ImportNode {
    type Target = ContainerNode;

    fn deref(&self) -> &ContainerNode {
        &self.0
    }
}

pub struct EntityNode(ContainerNode);

impl EntityNode {
    pub fn new(session: SessionPtr) -> EntityNode {
        let args = EntityNodeArgs { unused: 0 };
        EntityNode(ContainerNode::new(Resource::new(session, ResourceArgs::EntityNode(args))))
    }

    pub fn set_clip(&self, clip_id: u32, clip_to_self: bool) {
        self.enqueue(cmd::set_clip(self.id(), clip_id, clip_to_self));
    }

    pub fn attach(&self, view_holder: &ViewHolder) {
        self.enqueue(cmd::add_child(self.id(), view_holder.id()))
    }

    pub fn export_as_request(&self) -> EventPair {
        let (mine, theirs) = EventPair::create().unwrap();
        self.enqueue(cmd::export_resource(self.id(), mine));
        theirs
    }
}

impl Deref for EntityNode {
    type Target = ContainerNode;

    fn deref(&self) -> &ContainerNode {
        &self.0
    }
}

pub struct HostMemory {
    memory: Memory,
    mapping: Arc<Mapping>,
}

impl HostMemory {
    pub fn allocate(session: SessionPtr, size: usize) -> Result<HostMemory, Status> {
        let (mapping, vmo) = Mapping::allocate(size)?;
        let remote = vmo.duplicate_handle(
            Rights::DUPLICATE
                | Rights::TRANSFER
                | Rights::WAIT
                | Rights::INSPECT
                | Rights::READ
                | Rights::MAP,
        )?;
        Ok(HostMemory {
            memory: Memory::new(session, remote, size as u64, MemoryType::HostMemory),
            mapping: Arc::new(mapping),
        })
    }

    pub fn len(&self) -> usize {
        self.mapping.len()
    }
}

pub struct HostImage {
    image: Image,
    mapping: Arc<Mapping>,
}

impl HostImage {
    pub fn new(memory: &HostMemory, memory_offset: u32, info: ImageInfo) -> HostImage {
        HostImage {
            image: Image::new(&memory.memory, memory_offset, info),
            mapping: memory.mapping.clone(),
        }
    }

    pub fn mapping(&self) -> &Arc<Mapping> {
        &self.mapping
    }
}

impl Deref for HostImage {
    type Target = Image;

    fn deref(&self) -> &Image {
        &self.image
    }
}

fn get_image_size(info: &ImageInfo) -> usize {
    assert!(info.tiling == Tiling::Linear);
    match info.pixel_format {
        PixelFormat::Bgra8 => (info.height * info.stride) as usize,
        PixelFormat::Yuy2 => (info.height * info.stride) as usize,
        PixelFormat::Nv12 => (info.height * info.stride * 3 / 2) as usize,
        PixelFormat::Yv12 => (info.height * info.stride * 3 / 2) as usize,
    }
}

pub struct HostImageGuard<'a> {
    cycler: &'a mut HostImageCycler,
    image: Option<HostImage>,
}

impl<'a> HostImageGuard<'a> {
    pub fn image(&self) -> &HostImage {
        self.image.as_ref().unwrap()
    }
}

impl<'a> Drop for HostImageGuard<'a> {
    fn drop(&mut self) {
        self.cycler.release(self.image.take().unwrap());
    }
}

pub struct HostImageCycler {
    node: EntityNode,
    content_node: ShapeNode,
    content_material: Material,
    content_shape: Option<Rectangle>,
}

impl HostImageCycler {
    pub fn new(session: SessionPtr) -> HostImageCycler {
        let node = EntityNode::new(session.clone());
        let content_node = ShapeNode::new(session.clone());
        let content_material = Material::new(session);
        content_node.set_material(&content_material);
        node.add_child(&content_node);
        HostImageCycler { node, content_node, content_material, content_shape: None }
    }

    pub fn node(&self) -> &EntityNode {
        &self.node
    }

    pub fn acquire<'a>(&'a mut self, info: ImageInfo) -> Result<HostImageGuard<'a>, Status> {
        if info.tiling != Tiling::Linear {
            return Err(Status::NOT_SUPPORTED);
        }

        let desired_size = get_image_size(&info);

        let memory = HostMemory::allocate(self.node.resource.session.clone(), desired_size)?;
        let image = HostImage::new(&memory, 0, info);
        Ok(HostImageGuard { cycler: self, image: Some(image) })
    }

    fn release(&mut self, image: HostImage) {
        self.content_material.set_texture(Some(&image));
        let rectangle = Rectangle::new(
            self.node.resource.session.clone(),
            image.info.width as f32,
            image.info.height as f32,
        );
        self.content_node.set_shape(&rectangle);
        self.content_shape = Some(rectangle);
    }
}
