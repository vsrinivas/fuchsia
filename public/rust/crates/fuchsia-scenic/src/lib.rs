// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate fidl;
extern crate fidl_fuchsia_images;
extern crate fidl_fuchsia_ui_gfx;
extern crate fidl_fuchsia_ui_scenic;
extern crate fuchsia_zircon;
extern crate parking_lot;

mod cmd;

use fidl_fuchsia_images::{ImageInfo, MemoryType, PixelFormat, PresentationInfo, Tiling};
use fidl_fuchsia_ui_gfx::{EntityNodeArgs, ImageArgs, MaterialArgs, MemoryArgs, ResourceArgs,
                          ShapeNodeArgs};
use fidl_fuchsia_ui_scenic::{Command, SessionProxy};
use fuchsia_zircon::{Event, HandleBased, Rights, Status, Vmar, VmarFlags, Vmo};
use parking_lot::Mutex;
use std::collections::VecDeque;
use std::mem;
use std::ops::Deref;
use std::sync::Arc;

struct Session {
    session: SessionProxy,
    next_resource_id: u32,
    resource_count: u32,
    commands: Vec<Command>,
    acquire_fences: Vec<Event>,
    release_fences: Vec<Event>,
}

impl Session {
    fn new(session: SessionProxy) -> Session {
        Session {
            session,
            next_resource_id: 1,
            resource_count: 0,
            commands: vec![],
            acquire_fences: vec![],
            release_fences: vec![],
        }
    }

    fn enqueue(&mut self, command: Command) {
        self.commands.push(command)
    }

    fn flush(&mut self) {
        self.session.enqueue(&mut self.commands.iter_mut()).ok();
    }

    fn present(
        &mut self, presentation_time: u64,
    ) -> fidl::client2::QueryResponseFut<(PresentationInfo)> {
        self.session.present(
            presentation_time,
            &mut self.acquire_fences.drain(..),
            &mut self.release_fences.drain(..),
        )
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
}

type SessionPtr = Arc<Mutex<Session>>;

struct Resource {
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
}

impl Drop for Resource {
    fn drop(&mut self) {
        let mut s = self.session.lock();
        s.release_resource(self.id);
    }
}

struct Memory {
    resource: Resource,
}

impl Memory {
    fn new(session: SessionPtr, vmo: Vmo, memory_type: MemoryType) -> Memory {
        let args = MemoryArgs {
            vmo: vmo,
            memory_type,
        };
        Memory {
            resource: Resource::new(session, ResourceArgs::Memory(args)),
        }
    }

    fn id(&self) -> u32 {
        self.resource.id
    }
}

struct Image {
    resource: Resource,
    info: ImageInfo,
}

impl Image {
    fn new(memory: &Memory, memory_offset: u32, info: ImageInfo) -> Image {
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

    fn id(&self) -> u32 {
        self.resource.id
    }
}

struct Material {
    resource: Resource,
}

impl Material {
    fn new(session: SessionPtr) -> Material {
        let args = MaterialArgs { dummy: 0 };
        Material {
            resource: Resource::new(session, ResourceArgs::Material(args)),
        }
    }

    fn id(&self) -> u32 {
        self.resource.id
    }

    fn set_texture(&self, image: &Image) {
        let mut session = self.resource.session.lock();
        session.enqueue(cmd::set_texture(self.id(), image.id()));
    }
}

struct Node {
    resource: Resource,
}

impl Node {
    fn new(session: SessionPtr, resource: ResourceArgs) -> Node {
        Node {
            resource: Resource::new(session, resource),
        }
    }

    fn id(&self) -> u32 {
        self.resource.id
    }

    fn enqueue(&self, command: Command) {
        let mut session = self.resource.session.lock();
        session.enqueue(command);
    }
}

struct ShapeNode(Node);

impl ShapeNode {
    fn new(session: SessionPtr) -> ShapeNode {
        let args = ShapeNodeArgs { unused: 0 };
        ShapeNode(Node::new(session, ResourceArgs::ShapeNode(args)))
    }

    fn set_material(&self, material: &Material) {
        self.enqueue(cmd::set_material(self.id(), material.id()));
    }
}

impl Deref for ShapeNode {
    type Target = Node;

    fn deref(&self) -> &Node {
        &self.0
    }
}

struct ContainerNode(Node);

impl ContainerNode {
    fn new(session: SessionPtr, resource: ResourceArgs) -> ContainerNode {
        ContainerNode(Node::new(session, resource))
    }

    fn add_child(&self, node: &Node) {
        self.enqueue(cmd::add_child(self.id(), node.id()));
    }
}

impl Deref for ContainerNode {
    type Target = Node;

    fn deref(&self) -> &Node {
        &self.0
    }
}

struct EntityNode(ContainerNode);

impl EntityNode {
    fn new(session: SessionPtr) -> EntityNode {
        let args = EntityNodeArgs { unused: 0 };
        EntityNode(ContainerNode::new(session, ResourceArgs::EntityNode(args)))
    }
}

impl Deref for EntityNode {
    type Target = ContainerNode;

    fn deref(&self) -> &ContainerNode {
        &self.0
    }
}

struct MemoryMapping {
    addr: usize,
    len: usize,
}

impl MemoryMapping {
    fn allocate(size: usize) -> Result<(Vmo, MemoryMapping), Status> {
        let vmo = Vmo::create(size as u64)?;
        let remote = vmo.duplicate_handle(
            Rights::DUPLICATE
                | Rights::TRANSFER
                | Rights::WAIT
                | Rights::INSPECT
                | Rights::READ
                | Rights::MAP,
        )?;
        let memory = Self::new(
            &vmo,
            0,
            size,
            VmarFlags::PERM_READ | VmarFlags::PERM_WRITE | VmarFlags::MAP_RANGE,
        )?;
        Ok((remote, memory))
    }

    fn new(vmo: &Vmo, offset: u64, len: usize, flags: VmarFlags) -> Result<MemoryMapping, Status> {
        let addr = Vmar::root_self().map(0, vmo, offset, len, flags)?;
        Ok(MemoryMapping { addr, len })
    }
}

impl Drop for MemoryMapping {
    fn drop(&mut self) {
        unsafe {
            Vmar::root_self().unmap(self.addr, self.len).unwrap();
        }
    }
}

struct HostMemory {
    memory: Memory,
    mapping: Arc<MemoryMapping>,
}

impl HostMemory {
    fn allocate(session: SessionPtr, size: usize) -> Result<HostMemory, Status> {
        let (vmo, mapping) = MemoryMapping::allocate(size)?;
        Ok(HostMemory {
            memory: Memory::new(session, vmo, MemoryType::HostMemory),
            mapping: Arc::new(mapping),
        })
    }

    fn size(&self) -> usize {
        self.mapping.len
    }
}

struct HostImage {
    image: Image,
    mapping: Arc<MemoryMapping>,
}

impl HostImage {
    fn new(memory: &HostMemory, memory_offset: u32, info: ImageInfo) -> HostImage {
        HostImage {
            image: Image::new(&memory.memory, memory_offset, info),
            mapping: memory.mapping.clone(),
        }
    }
}

fn get_image_size(info: &ImageInfo) -> usize {
    assert!(info.tiling == Tiling::Linear);
    match info.pixel_format {
        PixelFormat::Bgra8 => (info.height * info.stride) as usize,
        PixelFormat::Yuy2 => (info.height * info.stride) as usize,
    }
}

fn can_reuse_memory(memory: &HostMemory, desired_size: usize) -> bool {
    let current_size = memory.size();
    current_size >= desired_size && current_size <= desired_size * 2
}

pub struct HostImageGuard<'a> {
    cycler: &'a mut HostImageCycler,
    memory: Option<HostMemory>,
    image: Option<HostImage>,
}

impl<'a> Drop for HostImageGuard<'a> {
    fn drop(&mut self) {
        self.cycler
            .release(self.memory.take().unwrap(), self.image.take().unwrap());
    }
}

struct HostImageCycler {
    entity: EntityNode,
    content_node: ShapeNode,
    content_material: Material,
    pool: VecDeque<(HostMemory, HostImage)>,
}

impl HostImageCycler {
    fn new(session: SessionPtr) -> HostImageCycler {
        let entity = EntityNode::new(session.clone());
        let content_node = ShapeNode::new(session.clone());
        let content_material = Material::new(session);
        content_node.set_material(&content_material);
        entity.add_child(&content_node);
        HostImageCycler {
            entity,
            content_node,
            content_material,
            pool: VecDeque::with_capacity(2),
        }
    }

    fn id(&self) -> u32 {
        self.entity.id()
    }

    pub fn acquire<'a>(&'a mut self, info: ImageInfo) -> Result<HostImageGuard<'a>, Status> {
        if info.tiling != Tiling::Linear {
            return Err(Status::NOT_SUPPORTED);
        }

        let desired_size = get_image_size(&info);
        self.pool
            .retain(|(memory, _image)| can_reuse_memory(&memory, desired_size));

        match self.pool.pop_front() {
            None => {
                let memory =
                    HostMemory::allocate(self.entity.resource.session.clone(), desired_size)?;
                let image = HostImage::new(&memory, 0, info);
                Ok(HostImageGuard {
                    cycler: self,
                    memory: Some(memory),
                    image: Some(image),
                })
            }
            Some((memory, image)) => {
                let i = if image.image.info == info {
                    image
                } else {
                    HostImage::new(&memory, 0, info)
                };
                Ok(HostImageGuard {
                    cycler: self,
                    memory: Some(memory),
                    image: Some(i),
                })
            }
        }
    }

    fn release(&mut self, memory: HostMemory, image: HostImage) {
        self.content_material.set_texture(&image.image);
        // TODO(abarth): Call set_shape on self.content_node once we have
        // Rectangle.
        self.pool.push_back((memory, image));
    }
}
