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

use fidl_fuchsia_images::{ImageInfo, MemoryType, PresentationInfo};
use fidl_fuchsia_ui_gfx::{EntityNodeArgs, ImageArgs, MaterialArgs, MemoryArgs, ResourceArgs,
                          ShapeNodeArgs};
use fidl_fuchsia_ui_scenic::{Command, SessionProxy};
use fuchsia_zircon::{Event, HandleBased, Rights, Status, Vmar, VmarFlags, Vmo};
use parking_lot::Mutex;
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
}

impl Image {
    fn new(memory: &Memory, memory_offset: u32, info: ImageInfo) -> Image {
        let args = ImageArgs {
            memory_id: memory.resource.id,
            memory_offset,
            info: info,
        };
        Image {
            resource: Resource::new(memory.resource.session.clone(), ResourceArgs::Image(args)),
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
