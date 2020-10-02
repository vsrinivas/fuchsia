// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod cmd;
mod view_ref_pair;
mod view_token_pair;
pub use self::view_ref_pair::*;
pub use self::view_token_pair::*;

use fidl::endpoints::ClientEnd;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_images::{
    ImageInfo, ImagePipe2Marker, MemoryType, PixelFormat, PresentationInfo, Tiling,
};
use fidl_fuchsia_scenic_scheduling::FuturePresentationTimes;
use fidl_fuchsia_sysmem::BufferCollectionTokenMarker;
use fidl_fuchsia_ui_gfx::{
    AmbientLightArgs, CameraArgs, CircleArgs, ColorRgb, ColorRgba, DirectionalLightArgs,
    DisplayCompositorArgs, EntityNodeArgs, ImageArgs, ImageArgs2, ImagePipe2Args, LayerArgs,
    LayerStackArgs, MaterialArgs, MemoryArgs, PointLightArgs, RectangleArgs, RendererArgs,
    ResourceArgs, RoundedRectangleArgs, SceneArgs, ShapeNodeArgs, Value, ViewArgs, ViewArgs3,
    ViewHolderArgs, ViewProperties,
};
use fidl_fuchsia_ui_scenic::{Command, Present2Args, SessionEventStream, SessionProxy};
use fidl_fuchsia_ui_views::{ViewHolderToken, ViewRef, ViewRefControl, ViewToken};
use fuchsia_zircon::{Event, HandleBased, Rights, Status, Vmo};
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
    ) -> fidl::client::QueryResponseFut<PresentationInfo> {
        self.flush();
        self.session.present(
            presentation_time,
            &mut self.acquire_fences.drain(..),
            &mut self.release_fences.drain(..),
        )
    }

    pub fn present2(
        &mut self,
        requested_prediction_span: i64,
        requested_presentation_time: i64,
    ) -> fidl::client::QueryResponseFut<FuturePresentationTimes> {
        self.flush();
        let args = Present2Args {
            requested_presentation_time: Some(requested_presentation_time),
            requested_prediction_span: Some(requested_prediction_span),
            acquire_fences: Some(self.acquire_fences.drain(..).collect()),
            release_fences: Some(self.release_fences.drain(..).collect()),
        };
        self.session.present2(args)
    }

    pub fn take_event_stream(&self) -> SessionEventStream {
        self.session.take_event_stream()
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

    pub fn register_buffer_collection(
        &self,
        buffer_id: u32,
        token: ClientEnd<BufferCollectionTokenMarker>,
    ) -> Result<(), fidl::Error> {
        self.session.register_buffer_collection(buffer_id, token)
    }

    pub fn deregister_buffer_collection(&self, buffer_id: u32) -> Result<(), fidl::Error> {
        self.session.deregister_buffer_collection(buffer_id)
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

pub struct Image2 {
    resource: Resource,
}

impl Image2 {
    pub fn new(
        session: &SessionPtr,
        width: u32,
        height: u32,
        buffer_collection_id: u32,
        buffer_collection_index: u32,
    ) -> Image2 {
        let args = ImageArgs2 { width, height, buffer_collection_id, buffer_collection_index };
        Image2 { resource: Resource::new(session.clone(), ResourceArgs::Image2(args)) }
    }

    pub fn id(&self) -> u32 {
        self.resource.id
    }
}

impl Deref for Image2 {
    type Target = Resource;

    fn deref(&self) -> &Resource {
        &self.resource
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

    pub fn set_texture_resource(&self, texture: Option<&Resource>) {
        self.resource.enqueue(cmd::set_texture(self.id(), texture.map(|t| t.id).unwrap_or(0)));
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

pub struct AmbientLight {
    resource: Resource,
}

impl AmbientLight {
    pub fn new(session: SessionPtr) -> AmbientLight {
        let args = AmbientLightArgs { dummy: 0 };
        AmbientLight { resource: Resource::new(session, ResourceArgs::AmbientLight(args)) }
    }
    pub fn id(&self) -> u32 {
        self.resource.id
    }

    pub fn set_color(&self, color: ColorRgb) {
        self.resource.enqueue(cmd::set_light_color(self.id(), color));
    }
    pub fn detach_light(&self) {
        self.resource.enqueue(cmd::detach_light(self.id()));
    }
}
pub struct PointLight {
    resource: Resource,
}

impl PointLight {
    pub fn new(session: SessionPtr) -> PointLight {
        let args = PointLightArgs { dummy: 0 };
        PointLight { resource: Resource::new(session, ResourceArgs::PointLight(args)) }
    }
    pub fn id(&self) -> u32 {
        self.resource.id
    }
    pub fn set_color(&self, color: ColorRgb) {
        self.resource.enqueue(cmd::set_light_color(self.id(), color));
    }
    pub fn detach_light(&self) {
        self.resource.enqueue(cmd::detach_light(self.id()));
    }
}
pub struct DirectionalLight {
    resource: Resource,
}

impl DirectionalLight {
    pub fn new(session: SessionPtr) -> DirectionalLight {
        let args = DirectionalLightArgs { dummy: 0 };
        DirectionalLight { resource: Resource::new(session, ResourceArgs::DirectionalLight(args)) }
    }
    pub fn id(&self) -> u32 {
        self.resource.id
    }
    pub fn set_color(&self, color: ColorRgb) {
        self.resource.enqueue(cmd::set_light_color(self.id(), color));
    }
    pub fn set_direction(&self, x: f32, y: f32, z: f32) {
        self.resource.enqueue(cmd::set_light_direction(self.id(), x, y, z));
    }

    pub fn detach_light(&self) {
        self.resource.enqueue(cmd::detach_light(self.id()));
    }
}

pub struct Scene {
    resource: Resource,
}

impl Scene {
    pub fn new(session: SessionPtr) -> Scene {
        let args = SceneArgs { dummy: 0 };
        Scene { resource: Resource::new(session, ResourceArgs::Scene(args)) }
    }
    pub fn id(&self) -> u32 {
        self.resource.id
    }
    pub fn add_child(&self, child: &Node) {
        self.resource.enqueue(cmd::add_child(self.id(), child.id()))
    }

    pub fn add_directional_light(&self, light: &DirectionalLight) {
        self.resource.enqueue(cmd::scene_add_directional_light(self.id(), light.id()));
    }

    pub fn add_ambient_light(&self, light: &AmbientLight) {
        self.resource.enqueue(cmd::scene_add_ambient_light(self.id(), light.id()));
    }

    pub fn add_point_light(&self, light: &PointLight) {
        self.resource.enqueue(cmd::scene_add_point_light(self.id(), light.id()));
    }

    pub fn detach_lights(&self) {
        self.resource.enqueue(cmd::detach_lights(self.id()));
    }

    pub fn set_scale(&self, x: f32, y: f32, z: f32) {
        self.resource.enqueue(cmd::set_scale(self.id(), x, y, z))
    }
}

pub struct Camera {
    resource: Resource,
}

impl Camera {
    pub fn new(session: SessionPtr, scene: &Scene) -> Camera {
        let args = CameraArgs { scene_id: scene.id() };
        Camera { resource: Resource::new(session, ResourceArgs::Camera(args)) }
    }
    pub fn id(&self) -> u32 {
        self.resource.id
    }
}

pub struct Renderer {
    resource: Resource,
}

impl Renderer {
    pub fn new(session: SessionPtr) -> Renderer {
        let args = RendererArgs { dummy: 0 };
        Renderer { resource: Resource::new(session, ResourceArgs::Renderer(args)) }
    }

    pub fn id(&self) -> u32 {
        self.resource.id
    }

    pub fn set_camera(&self, camera: &Camera) {
        self.resource.enqueue(cmd::set_camera(self.id(), camera.id()))
    }
}

pub struct Layer {
    resource: Resource,
}

impl Layer {
    pub fn new(session: SessionPtr) -> Layer {
        let args = LayerArgs { dummy: 0 };
        Layer { resource: Resource::new(session, ResourceArgs::Layer(args)) }
    }

    pub fn id(&self) -> u32 {
        self.resource.id
    }

    pub fn set_renderer(&self, renderer: &Renderer) {
        self.resource.enqueue(cmd::set_renderer(self.id(), renderer.id()))
    }

    pub fn set_size(&self, x: f32, y: f32) {
        self.resource.enqueue(cmd::set_size(self.id(), x, y))
    }
}

pub struct LayerStack {
    resource: Resource,
}

impl LayerStack {
    pub fn new(session: SessionPtr) -> LayerStack {
        let args = LayerStackArgs { dummy: 0 };
        LayerStack { resource: Resource::new(session, ResourceArgs::LayerStack(args)) }
    }

    pub fn id(&self) -> u32 {
        self.resource.id
    }

    pub fn add_layer(&self, layer: &Layer) {
        self.resource.enqueue(cmd::add_layer(self.id(), layer.id()))
    }

    pub fn remove_layer(&self, layer: &Layer) {
        self.resource.enqueue(cmd::remove_layer(self.id(), layer.id()))
    }

    pub fn remove_all_layers(&self) {
        self.resource.enqueue(cmd::remove_all_layers(self.id()))
    }
}

pub struct DisplayCompositor {
    resource: Resource,
}

#[derive(Copy, Clone, PartialEq, Debug)]
pub enum DisplayRotation {
    None = 0,
    By90Degrees = 90,
    By180Degrees = 180,
    By270Degrees = 270,
}

impl DisplayCompositor {
    pub fn new(session: SessionPtr) -> DisplayCompositor {
        let args = DisplayCompositorArgs { dummy: 0 };
        DisplayCompositor {
            resource: Resource::new(session, ResourceArgs::DisplayCompositor(args)),
        }
    }

    pub fn id(&self) -> u32 {
        self.resource.id
    }

    pub fn set_display_rotation(&self, rotation: DisplayRotation) {
        self.resource.enqueue(cmd::set_display_rotation(self.id(), rotation as u32))
    }

    pub fn set_layer_stack(&self, layer_stack: &LayerStack) {
        self.resource.enqueue(cmd::set_layer_stack(self.id(), layer_stack.id()))
    }
}

pub struct View {
    resource: Resource,
}

impl View {
    pub fn new(session: SessionPtr, token: ViewToken, debug_name: Option<String>) -> View {
        let args = ViewArgs { token: token, debug_name: debug_name };
        View { resource: Resource::new(session, ResourceArgs::View(args)) }
    }

    /// Creates a new view using the ViewArgs3 resource which allows the user to
    /// use view_refs.
    pub fn new3(
        session: SessionPtr,
        token: ViewToken,
        control_ref: ViewRefControl,
        view_ref: ViewRef,
        debug_name: Option<String>,
    ) -> View {
        let args = ViewArgs3 { token, control_ref, view_ref, debug_name };
        View { resource: Resource::new(session, ResourceArgs::View3(args)) }
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

pub struct ViewHolder(Node);

impl ViewHolder {
    pub fn new(
        session: SessionPtr,
        token: ViewHolderToken,
        debug_name: Option<String>,
    ) -> ViewHolder {
        let args = ViewHolderArgs { token: token, debug_name: debug_name };
        ViewHolder(Node::new(Resource::new(session, ResourceArgs::ViewHolder(args))))
    }

    pub fn set_view_properties(&self, view_properties: ViewProperties) {
        self.enqueue(cmd::set_view_properties(self.id(), view_properties))
    }
}

impl Deref for ViewHolder {
    type Target = Node;

    fn deref(&self) -> &Node {
        &self.0
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

pub struct ImagePipe2(Resource);

impl ImagePipe2 {
    pub fn new(session: SessionPtr, image_pipe_request: ServerEnd<ImagePipe2Marker>) -> ImagePipe2 {
        let args = ImagePipe2Args { image_pipe_request };
        ImagePipe2(Resource::new(session, ResourceArgs::ImagePipe2(args)))
    }
}

impl Deref for ImagePipe2 {
    type Target = Resource;

    fn deref(&self) -> &Resource {
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

    pub fn remove_child(&self, node: &Node) {
        self.enqueue(cmd::detach(node.id()));
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
        self.mapping.len() as usize
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
        PixelFormat::Bgra8 | PixelFormat::R8G8B8A8 => (info.height * info.stride) as usize,
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

#[cfg(test)]
mod tests {
    use {
        super::*, fidl::endpoints::create_proxy_and_stream, fidl_fuchsia_ui_gfx,
        fidl_fuchsia_ui_scenic, fuchsia_async as fasync, futures::prelude::*,
    };

    /// Returns `true` if the received session command matches the test expectation.
    ///
    /// If the received request is not a SessionRequest::Enqueue, the function will return
    /// `false`.
    ///
    /// Parameters:
    /// - `request`: The request the test case received.
    /// - `check_command`: The function which is applied to the received request.
    ///
    /// Returns:
    /// `true` iff the request is enqueuing a command, and that command passes the provided
    /// check.
    fn verify_session_command<F>(
        request: fidl_fuchsia_ui_scenic::SessionRequest,
        check_command: F,
    ) -> bool
    where
        F: Fn(&fidl_fuchsia_ui_gfx::Command) -> bool,
    {
        match request {
            fidl_fuchsia_ui_scenic::SessionRequest::Enqueue { cmds, control_handle: _ } => {
                let cmd = cmds.first();
                match cmd {
                    Some(fidl_fuchsia_ui_scenic::Command::Gfx(gfx_command)) => {
                        assert!(check_command(gfx_command));
                        true
                    }
                    _ => false,
                }
            }
            _ => false,
        }
    }

    /// Verifies that the session's next resource id is given to a newly created layer.
    #[fasync::run_singlethreaded(test)]
    async fn test_resource_id() {
        let (session_proxy, mut session_server) =
            create_proxy_and_stream::<fidl_fuchsia_ui_scenic::SessionMarker>()
                .expect("Failed to create Session FIDL.");

        let session = Session::new(session_proxy);
        let layer_id = session.lock().next_resource_id();

        let _ = Layer::new(session.clone());

        fasync::Task::spawn(async move {
            let fut = session.lock().present(0);
            let _ = fut.await;
        })
        .detach();

        if let Some(session_request) = session_server.try_next().await.unwrap() {
            assert!(verify_session_command(session_request, |gfx_command| {
                match gfx_command {
                    fidl_fuchsia_ui_gfx::Command::CreateResource(create_command) => {
                        // Verify that the layer that was created got the expected layer id.
                        create_command.id == layer_id
                    }
                    _ => false,
                }
            }));
        }
    }

    /// Print a more detailed message about the failure, and return false.
    ///
    /// Example:
    ///   fidl_fuchsia_ui_gfx::Command::$command_type(command_struct) => {
    ///       command_struct.$member_name == $expected_value
    ///           || fail!("{}.{} = '{}' (expected: '{}')",
    ///                    stringify!($command_type), stringify!($member_name),
    ///                    command_struct.$member_name, $expected_value)
    ///   }
    ///   unexpected_type => {
    ///       fail!("{} type is '{:?}' (expected '{}')",
    ///               stringify!($command), unexpected_type, stringify!($command_type))
    ///   }
    ///
    /// Note:
    ///   * Do not end the macro call with a semicolon. The result is a bool "false".
    ///   * Note the "||" (or) expression in the first example returns true if the test passes,
    ///     and othewise executes the fail!() macro and returns false.
    macro_rules! fail {
        (
            $format_string:literal,
            $($args:expr),*
        ) => {
            {
                println!(concat!("FAILED({}:{}:{}): ", $format_string),
                         file!(), line!(), column!() $(,$args)*);
                false
            }
        };
    }

    /// Tests that the appropriate resource type is created.
    macro_rules! test_resource_creation {
        (
            session: $session:expr,
            session_server: $session_server:expr,
            resource_type: $resource_type:ident
        ) => {
            let _ = $resource_type::new($session.clone());

            fasync::Task::spawn(async move {
                let fut = $session.lock().present(0);
                let _ = fut.await;
            })
            .detach();

            while let Some(session_request) = $session_server.try_next().await.unwrap() {
                let passed = verify_session_command(session_request, |gfx_command| {
                    match gfx_command {
                        fidl_fuchsia_ui_gfx::Command::CreateResource(create_command) => {
                            // Verify that the resource that was created got the expected resource
                            // id.
                            match create_command.resource {
                                fidl_fuchsia_ui_gfx::ResourceArgs::$resource_type(_resource) => {
                                    true
                                }
                                _ => false,
                            }
                        }
                        _ => false,
                    }
                });

                if passed {
                    break;
                }
            }
        };
    }

    /// Asserts that the incoming command is the type we expect.
    macro_rules! assert_command_type {
        (
            command: $command:expr,
            command_type: $command_type:ident
        ) => {
            assert!({
                match $command {
                    fidl_fuchsia_ui_scenic::Command::Gfx(gfx_command) => match gfx_command {
                        fidl_fuchsia_ui_gfx::Command::$command_type(_) => true,
                        unexpected_type => fail!(
                            "{} type is '{:?}' (expected '{}')",
                            stringify!($command),
                            unexpected_type,
                            stringify!($command_type)
                        ),
                    },
                    unexpected_command => fail!(
                        "{} = '{:?}' (expected fidl_fuchsia_ui_gfx::Command::Gfx)",
                        stringify!($command),
                        unexpected_command
                    ),
                }
            })
        };
    }

    /// Asserts that the incoming command is the type we expect.
    ///
    /// Example:
    ///   assert_command_value!(command: &commands[0], command_type: SetDisplayRotation,
    ///                               rotation_degrees, 270.0));
    macro_rules! assert_command_value {
        (
            command: $command:expr,
            command_type: $command_type:ident,
            $member_name:ident,
            $expected_value:expr
        ) => {
            assert!({
                match $command {
                    fidl_fuchsia_ui_scenic::Command::Gfx(gfx_command) => match gfx_command {
                        fidl_fuchsia_ui_gfx::Command::$command_type(command_struct) => {
                            command_struct.$member_name == $expected_value
                                || fail!(
                                    "{}.{} = '{:?}' (expected: '{:?}')",
                                    stringify!($command_type),
                                    stringify!($member_name),
                                    command_struct.$member_name,
                                    $expected_value
                                )
                        }
                        unexpected_type => fail!(
                            "{} type is '{:?}' (expected '{}')",
                            stringify!($command),
                            unexpected_type,
                            stringify!($command_type)
                        ),
                    },
                    unexpected_command => fail!(
                        "{} = '{:?}' (expected fidl_fuchsia_ui_gfx::Command::Gfx)",
                        stringify!($command),
                        unexpected_command
                    ),
                }
            })
        };
    }

    /// Verifies that a new resource is created for a layer.
    #[fasync::run_singlethreaded(test)]
    async fn test_add_layer() {
        let (session_proxy, mut session_server) =
            create_proxy_and_stream::<fidl_fuchsia_ui_scenic::SessionMarker>()
                .expect("Failed to create Session FIDL.");

        let session = Session::new(session_proxy);
        test_resource_creation!(
            session: session,
            session_server: session_server,
            resource_type: Layer
        );
    }

    /// Verifies that a new resource is created for a layer stack.
    #[fasync::run_singlethreaded(test)]
    async fn test_add_layer_stack() {
        let (session_proxy, mut session_server) =
            create_proxy_and_stream::<fidl_fuchsia_ui_scenic::SessionMarker>()
                .expect("Failed to create Session FIDL.");

        let session = Session::new(session_proxy);
        test_resource_creation!(
            session: session,
            session_server: session_server,
            resource_type: LayerStack
        );
    }

    /// Verifies that a new resource is created for a renderer.
    #[fasync::run_singlethreaded(test)]
    async fn test_add_renderer() {
        let (session_proxy, mut session_server) =
            create_proxy_and_stream::<fidl_fuchsia_ui_scenic::SessionMarker>()
                .expect("Failed to create Session FIDL.");

        let session = Session::new(session_proxy);
        test_resource_creation!(
            session: session,
            session_server: session_server,
            resource_type: Renderer
        );
    }

    /// Verifies that a new resource is created for a scene.
    #[fasync::run_singlethreaded(test)]
    async fn test_add_scene() {
        let (session_proxy, mut session_server) =
            create_proxy_and_stream::<fidl_fuchsia_ui_scenic::SessionMarker>()
                .expect("Failed to create Session FIDL.");

        let session = Session::new(session_proxy);
        test_resource_creation!(
            session: session,
            session_server: session_server,
            resource_type: Scene
        );
    }

    /// Verifies that a new resource is created for an ambient light.
    #[fasync::run_singlethreaded(test)]
    async fn test_add_ambient() {
        let (session_proxy, mut session_server) =
            create_proxy_and_stream::<fidl_fuchsia_ui_scenic::SessionMarker>()
                .expect("Failed to create Session FIDL.");

        let session = Session::new(session_proxy);
        test_resource_creation!(
            session: session,
            session_server: session_server,
            resource_type: AmbientLight
        );
    }

    /// Verifies that a new resource is created for a directional light.
    #[fasync::run_singlethreaded(test)]
    async fn test_add_directional() {
        let (session_proxy, mut session_server) =
            create_proxy_and_stream::<fidl_fuchsia_ui_scenic::SessionMarker>()
                .expect("Failed to create Session FIDL.");

        let session = Session::new(session_proxy);
        test_resource_creation!(
            session: session,
            session_server: session_server,
            resource_type: DirectionalLight
        );
    }

    /// Verifies that a new resource is created for a point light.
    #[fasync::run_singlethreaded(test)]
    async fn test_add_point() {
        let (session_proxy, mut session_server) =
            create_proxy_and_stream::<fidl_fuchsia_ui_scenic::SessionMarker>()
                .expect("Failed to create Session FIDL.");

        let session = Session::new(session_proxy);
        test_resource_creation!(
            session: session,
            session_server: session_server,
            resource_type: PointLight
        );
    }

    /// Verifies that a new resource is created for a display compositor.
    #[fasync::run_singlethreaded(test)]
    async fn test_add_display_compositor() {
        let (session_proxy, mut session_server) =
            create_proxy_and_stream::<fidl_fuchsia_ui_scenic::SessionMarker>()
                .expect("Failed to create Session FIDL.");

        let session = Session::new(session_proxy);
        test_resource_creation!(
            session: session,
            session_server: session_server,
            resource_type: DisplayCompositor
        );
    }

    /// Verifies that adding a child to a node creates the correct command
    #[fasync::run_singlethreaded(test)]
    async fn test_add_child() {
        let (session_proxy, mut session_server) =
            create_proxy_and_stream::<fidl_fuchsia_ui_scenic::SessionMarker>()
                .expect("Failed to create Session FIDL.");

        let session = Session::new(session_proxy);
        let _ = session.lock();
        let parent_node = EntityNode::new(session.clone());
        let child_node = EntityNode::new(session.clone());
        parent_node.add_child(&child_node);

        fasync::Task::spawn(async move {
            let fut = session.lock().present(0);
            let _ = fut.await;
        })
        .detach();

        let mut commands = vec![];

        if let Some(session_request) = session_server.try_next().await.unwrap() {
            if let fidl_fuchsia_ui_scenic::SessionRequest::Enqueue { mut cmds, .. } =
                session_request
            {
                commands.append(&mut cmds);
            }
        }

        assert_command_type!(command: &commands[0], command_type: CreateResource);
        assert_command_type!(command: &commands[1], command_type: CreateResource);
        assert_command_type!(command: &commands[2], command_type: AddChild);
    }

    /// Verifies that removing a child from a node creates the correct command.
    #[fasync::run_singlethreaded(test)]
    async fn test_remove_child() {
        let (session_proxy, mut session_server) =
            create_proxy_and_stream::<fidl_fuchsia_ui_scenic::SessionMarker>()
                .expect("Failed to create Session FIDL.");

        let session = Session::new(session_proxy);
        let _ = session.lock();
        let parent_node = EntityNode::new(session.clone());
        let child_node = EntityNode::new(session.clone());
        parent_node.add_child(&child_node);
        parent_node.remove_child(&child_node);

        fasync::Task::spawn(async move {
            let fut = session.lock().present(0);
            let _ = fut.await;
        })
        .detach();

        let mut commands = vec![];

        if let Some(session_request) = session_server.try_next().await.unwrap() {
            if let fidl_fuchsia_ui_scenic::SessionRequest::Enqueue { mut cmds, .. } =
                session_request
            {
                commands.append(&mut cmds);
            }
        }

        assert_command_type!(command: &commands[0], command_type: CreateResource);
        assert_command_type!(command: &commands[1], command_type: CreateResource);
        assert_command_type!(command: &commands[2], command_type: AddChild);
        assert_command_type!(command: &commands[3], command_type: Detach);
    }

    /// Verifies that setting display rotation by enum creates a command with the correct u32 value
    #[fasync::run_singlethreaded(test)]
    async fn test_set_display_rotation() {
        let (session_proxy, mut session_server) =
            create_proxy_and_stream::<fidl_fuchsia_ui_scenic::SessionMarker>()
                .expect("Failed to create Session FIDL.");

        let session = Session::new(session_proxy);
        let _ = session.lock();
        let compositor = DisplayCompositor::new(session.clone());
        compositor.set_display_rotation(DisplayRotation::By270Degrees);
        compositor.set_display_rotation(DisplayRotation::By180Degrees);
        compositor.set_display_rotation(DisplayRotation::By90Degrees);
        compositor.set_display_rotation(DisplayRotation::None);

        fasync::Task::spawn(async move {
            let fut = session.lock().present(0);
            let _ = fut.await;
        })
        .detach();

        let mut commands = vec![];

        if let Some(session_request) = session_server.try_next().await.unwrap() {
            if let fidl_fuchsia_ui_scenic::SessionRequest::Enqueue { mut cmds, .. } =
                session_request
            {
                commands.append(&mut cmds);
            }
        }

        assert_command_type!(command: &commands[0], command_type: CreateResource);
        assert_command_value!(command: &commands[1], command_type: SetDisplayRotation,
                              rotation_degrees, 270);
        assert_command_value!(command: &commands[2], command_type: SetDisplayRotation,
                              rotation_degrees, 180);
        assert_command_value!(command: &commands[3], command_type: SetDisplayRotation,
                              rotation_degrees, 90);
        assert_command_value!(command: &commands[4], command_type: SetDisplayRotation,
                              rotation_degrees, 0);
    }

    /// Verifies that adding a child to a node creates the correct command
    #[fasync::run_singlethreaded(test)]
    async fn test_view_holder() {
        let (session_proxy, mut session_server) =
            create_proxy_and_stream::<fidl_fuchsia_ui_scenic::SessionMarker>()
                .expect("Failed to create Session FIDL.");

        let session = Session::new(session_proxy);
        let _ = session.lock();

        let ViewTokenPair { view_token: _, view_holder_token } = ViewTokenPair::new().unwrap();
        let view_holder = ViewHolder::new(session.clone(), view_holder_token, None);

        let view_properties = fidl_fuchsia_ui_gfx::ViewProperties {
            bounding_box: fidl_fuchsia_ui_gfx::BoundingBox {
                min: fidl_fuchsia_ui_gfx::Vec3 { x: 100.0, y: 200.0, z: 300.0 },
                max: fidl_fuchsia_ui_gfx::Vec3 { x: 400.0, y: 500.0, z: 600.0 },
            },
            downward_input: false,
            focus_change: true,
            inset_from_min: fidl_fuchsia_ui_gfx::Vec3 { x: 10.0, y: 20.0, z: 30.0 },
            inset_from_max: fidl_fuchsia_ui_gfx::Vec3 { x: 40.0, y: 50.0, z: 60.0 },
        };
        view_holder.set_view_properties(view_properties);

        fasync::Task::spawn(async move {
            let fut = session.lock().present(0);
            let _ = fut.await;
        })
        .detach();

        let mut commands = vec![];

        if let Some(session_request) = session_server.try_next().await.unwrap() {
            if let fidl_fuchsia_ui_scenic::SessionRequest::Enqueue { mut cmds, .. } =
                session_request
            {
                commands.append(&mut cmds);
            }
        }

        assert_command_type!(command: &commands[0], command_type: CreateResource);
        assert_command_value!(command: &commands[1], command_type: SetViewProperties,
                              view_holder_id, view_holder.id());

        if let fidl_fuchsia_ui_scenic::Command::Gfx(gfx_command) = &commands[1] {
            if let fidl_fuchsia_ui_gfx::Command::SetViewProperties(command_struct) = gfx_command {
                assert_eq!(command_struct.properties.bounding_box.min.x, 100.0);
                assert_eq!(command_struct.properties.bounding_box.min.y, 200.0);
                assert_eq!(command_struct.properties.bounding_box.min.z, 300.0);
                assert_eq!(command_struct.properties.bounding_box.max.x, 400.0);
                assert_eq!(command_struct.properties.bounding_box.max.y, 500.0);
                assert_eq!(command_struct.properties.bounding_box.max.z, 600.0);
                assert_eq!(command_struct.properties.downward_input, false);
                assert_eq!(command_struct.properties.focus_change, true);
                assert_eq!(command_struct.properties.inset_from_min.x, 10.0);
                assert_eq!(command_struct.properties.inset_from_min.y, 20.0);
                assert_eq!(command_struct.properties.inset_from_min.z, 30.0);
                assert_eq!(command_struct.properties.inset_from_max.x, 40.0);
                assert_eq!(command_struct.properties.inset_from_max.y, 50.0);
                assert_eq!(command_struct.properties.inset_from_max.z, 60.0);
            }
        }
    }
}
