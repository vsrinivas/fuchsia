// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::{
    collections::{HashMap, HashSet},
    slice,
};

use mold::{
    tile::{Map, Op},
    Raster,
};

use crate::*;

unsafe fn spn_to_tile_ops(cmds: &Vec<u32>) -> Vec<Op> {
    let cmds = slice::from_raw_parts(cmds.as_ptr(), cmds.capacity());
    let mut cmds = cmds.iter().copied();
    let mut ops = vec![];

    while let Some(op) = cmds.next() {
        match op {
            SPN_STYLING_OPCODE_NOOP => (),
            SPN_STYLING_OPCODE_COVER_NONZERO => ops.push(Op::CoverWipNonZero),
            SPN_STYLING_OPCODE_COVER_EVENODD => ops.push(Op::CoverWipEvenOdd),
            SPN_STYLING_OPCODE_COVER_ACCUMULATE => ops.push(Op::CoverAccAccumulate),
            SPN_STYLING_OPCODE_COVER_MASK => ops.push(Op::CoverWipMask),
            SPN_STYLING_OPCODE_COVER_WIP_ZERO => ops.push(Op::CoverWipZero),
            SPN_STYLING_OPCODE_COVER_ACC_ZERO => ops.push(Op::CoverAccZero),
            SPN_STYLING_OPCODE_COVER_MASK_ZERO => ops.push(Op::CoverMaskZero),
            SPN_STYLING_OPCODE_COVER_MASK_ONE => ops.push(Op::CoverMaskOne),
            SPN_STYLING_OPCODE_COVER_MASK_INVERT => ops.push(Op::CoverMaskInvert),
            SPN_STYLING_OPCODE_COLOR_FILL_SOLID => {
                let color = cmds.next().expect(
                    "SPN_STYLING_OPCODE_COLOR_FILL_SOLID must be follow by the color value",
                );
                ops.push(Op::ColorWipFillSolid(color));
                cmds.next();
            }
            SPN_STYLING_OPCODE_COLOR_FILL_GRADIENT_LINEAR => unimplemented!(),
            SPN_STYLING_OPCODE_COLOR_WIP_ZERO => ops.push(Op::ColorWipZero),
            SPN_STYLING_OPCODE_COLOR_ACC_ZERO => ops.push(Op::ColorAccZero),
            SPN_STYLING_OPCODE_BLEND_OVER => ops.push(Op::ColorAccBlendOver),
            SPN_STYLING_OPCODE_BLEND_PLUS => ops.push(Op::ColorAccBlendAdd),
            SPN_STYLING_OPCODE_BLEND_MULTIPLY => ops.push(Op::ColorAccBlendMultiply),
            SPN_STYLING_OPCODE_BLEND_KNOCKOUT => unimplemented!(),
            SPN_STYLING_OPCODE_COVER_WIP_MOVE_TO_MASK => ops.push(Op::CoverMaskCopyFromWip),
            SPN_STYLING_OPCODE_COVER_ACC_MOVE_TO_MASK => ops.push(Op::CoverMaskCopyFromAcc),
            SPN_STYLING_OPCODE_COLOR_ACC_OVER_BACKGROUND => {
                let color = cmds.next().expect(
                    "SPN_STYLING_OPCODE_COLOR_ACC_OVER_BACKGROUND \
                     must be follow by the color value",
                );
                ops.push(Op::ColorAccBackground(color));
                cmds.next();
            }
            SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE => (),
            SPN_STYLING_OPCODE_COLOR_ACC_TEST_OPACITY => (),
            SPN_STYLING_OPCODE_COLOR_ILL_ZERO => unimplemented!(),
            SPN_STYLING_OPCODE_COLOR_ILL_COPY_ACC => unimplemented!(),
            SPN_STYLING_OPCODE_COLOR_ACC_MULTIPLY_ILL => unimplemented!(),
            op => panic!("wrong opcode {}", op),
        }
    }

    ops
}

#[derive(Debug, Default)]
struct Layer {
    parent: u32,
    depth: Option<u32>,
    layer_id: u32,
    cmds: Vec<u32>,
}

#[derive(Debug, Default)]
struct Range {
    parent: Option<u32>,
    depth: Option<u32>,
    range_lo: Option<u32>,
    range_hi: Option<u32>,
    enter_cmds: Option<Vec<u32>>,
    leave_cmds: Option<Vec<u32>>,
}

#[derive(Debug)]
enum Node {
    Layer(Layer),
    Range(Range),
}

pub const COMMANDS_LAYER_ID_MAX: u32 = 1 << 18;
const COMMANDS_TYPE_SHIFT: u32 = 2;
const COMMANDS_DEPTH_SHIFT: u32 = 12;
const COMMANDS_DEPTH_MAX: u32 = 1 << COMMANDS_DEPTH_SHIFT;

#[derive(Debug)]
enum CommandsType {
    Enter,
    Layer,
    Leave,
}

impl From<u32> for CommandsType {
    fn from(value: u32) -> Self {
        match value {
            0 => CommandsType::Enter,
            1 => CommandsType::Layer,
            2 => CommandsType::Leave,
            _ => unimplemented!(),
        }
    }
}

impl Into<u32> for CommandsType {
    fn into(self) -> u32 {
        match self {
            CommandsType::Enter => 0,
            CommandsType::Layer => 1,
            CommandsType::Leave => 2,
        }
    }
}

#[derive(Debug)]
struct Commands {
    value: u32,
}

impl Commands {
    pub fn new(layer_id: u32, r#type: CommandsType, depth: u32) -> Self {
        let mut value = 0;
        let r#type: u32 = r#type.into();

        value |= layer_id << (COMMANDS_TYPE_SHIFT + COMMANDS_DEPTH_SHIFT);
        value |= r#type << COMMANDS_DEPTH_SHIFT;
        value |= depth;

        Self { value }
    }
}

#[derive(Debug)]
pub struct Styling {
    nodes: Vec<Option<Node>>,
    parents: HashMap<u32, Vec<u32>>,
    cached_unions: HashMap<(u32, u32), Raster>,
}

impl Styling {
    pub fn new() -> Self {
        Styling { nodes: vec![], parents: HashMap::new(), cached_unions: HashMap::new() }
    }

    pub fn group_alloc(&mut self) -> u32 {
        let len = self.nodes.len();

        self.nodes.push(None);

        len as u32
    }

    pub fn group_enter(&mut self, group_id: u32, cmd_count: u32) -> *mut u32 {
        let mut cmds = Vec::with_capacity(cmd_count as usize);
        let ptr = cmds.as_mut_ptr();
        match &mut self.nodes[group_id as usize] {
            Some(Node::Layer(_)) | None => {
                self.nodes[group_id as usize] =
                    Some(Node::Range(Range { enter_cmds: Some(cmds), ..Default::default() }))
            }
            Some(Node::Range(Range { ref mut enter_cmds, .. })) => {
                *enter_cmds = Some(cmds);
            }
        };

        ptr
    }

    pub fn group_leave(&mut self, group_id: u32, cmd_count: u32) -> *mut u32 {
        let mut cmds = Vec::with_capacity(cmd_count as usize);
        let ptr = cmds.as_mut_ptr();
        match &mut self.nodes[group_id as usize] {
            Some(Node::Layer(_)) | None => {
                self.nodes[group_id as usize] =
                    Some(Node::Range(Range { leave_cmds: Some(cmds), ..Default::default() }))
            }
            Some(Node::Range(Range { ref mut leave_cmds, .. })) => {
                *leave_cmds = Some(cmds);
            }
        };

        ptr
    }

    pub fn group_parents(&mut self, group_id: u32, parents_count: u32) -> *mut u32 {
        let mut parents = Vec::with_capacity(parents_count as usize);
        let ptr = parents.as_mut_ptr();

        self.parents.insert(group_id, parents);

        ptr
    }

    pub fn group_range_lo(&mut self, group_id: u32, range_lo: u32) {
        let some_range_lo = range_lo;
        match &mut self.nodes[group_id as usize] {
            Some(Node::Layer(_)) | None => {
                self.nodes[group_id as usize] =
                    Some(Node::Range(Range { range_lo: Some(range_lo), ..Default::default() }))
            }
            Some(Node::Range(Range { ref mut range_lo, .. })) => {
                *range_lo = Some(some_range_lo);
            }
        };
    }

    pub fn group_range_hi(&mut self, group_id: u32, range_hi: u32) {
        let some_range_hi = range_hi;
        match &mut self.nodes[group_id as usize] {
            Some(Node::Layer(_)) | None => {
                self.nodes[group_id as usize] =
                    Some(Node::Range(Range { range_hi: Some(range_hi), ..Default::default() }))
            }
            Some(Node::Range(Range { ref mut range_hi, .. })) => {
                *range_hi = Some(some_range_hi);
            }
        };
    }

    pub fn layer(&mut self, group_id: u32, layer_id: u32, cmd_count: u32) -> *mut u32 {
        if layer_id >= COMMANDS_LAYER_ID_MAX {
            panic!("layer_id overflowed the maximum of {}", COMMANDS_LAYER_ID_MAX);
        }

        let mut cmds = Vec::with_capacity(cmd_count as usize);
        let ptr = cmds.as_mut_ptr();

        self.nodes.push(Some(Node::Layer(Layer {
            parent: group_id,
            layer_id,
            cmds,
            ..Default::default()
        })));

        ptr
    }

    fn depth(&mut self, groupd_id: u32) -> u32 {
        let mut node = self.nodes[groupd_id as usize].take();
        let depth = match node {
            Some(Node::Layer(Layer { depth: Some(depth), .. }))
            | Some(Node::Range(Range { depth: Some(depth), .. })) => depth,
            Some(Node::Layer(Layer { parent: parent_id, ref mut depth, .. }))
            | Some(Node::Range(Range { parent: Some(parent_id), ref mut depth, .. })) => {
                let parent_depth = self.depth(parent_id) + 1;
                *depth = Some(parent_depth);
                parent_depth
            }
            Some(Node::Range(Range { parent: None, ref mut depth, .. })) => {
                *depth = Some(0);
                0
            }
            _ => panic!("parent_ids either not set up or set up incorrectly"),
        };

        if depth >= COMMANDS_DEPTH_MAX {
            panic!("group tree height overflowed the maximum of {}", COMMANDS_DEPTH_MAX);
        }

        self.nodes[groupd_id as usize] = node;
        depth
    }

    pub fn reset(&mut self) {
        self.nodes.clear();
        self.parents.clear();
    }

    fn process(&mut self) {
        for (group_id, parents) in self.parents.drain() {
            let mut node = &mut self.nodes[group_id as usize];

            for &parent_id in parents.iter().rev() {
                match node {
                    Some(Node::Range(Range { ref mut parent, .. })) => {
                        *parent = Some(parent_id);
                    }
                    _ => panic!("wrong parent_id passed to spn_styling_group_parents"),
                }

                node = &mut self.nodes[parent_id as usize]
            }
        }

        for group_id in (0..self.nodes.len()).rev() {
            self.depth(group_id as u32);
        }
    }

    pub fn prints(
        &mut self,
        composition: &mut Composition,
        map: &mut Map,
        prints: &mut HashSet<u32>,
    ) {
        self.process();
        let layers = composition.layers();
        let cached_unions = &mut self.cached_unions;

        for node in &self.nodes {
            if let Some(node) = node {
                match node {
                    Node::Layer(Layer { depth: Some(depth), layer_id, cmds, .. }) => {
                        let id = Commands::new(*layer_id, CommandsType::Layer, *depth).value;
                        let raster = layers
                            .get(layer_id)
                            .map(|raster| raster.clone())
                            .unwrap_or_else(|| Raster::empty());
                        let layer = mold::Layer::new(raster, unsafe { spn_to_tile_ops(cmds) });

                        prints.insert(id);
                        map.print(id, layer);
                    }
                    Node::Range(Range {
                        parent,
                        depth: Some(depth),
                        range_lo: Some(range_lo),
                        range_hi: Some(range_hi),
                        enter_cmds,
                        leave_cmds,
                        ..
                    }) => {
                        if parent.is_some() {
                            let raster =
                                cached_unions.entry((*range_lo, *range_hi)).or_insert_with(|| {
                                    Raster::union_without_segments(
                                        (*range_lo..=*range_hi)
                                            .flat_map(|layer_id| layers.get(&layer_id)),
                                    )
                                });

                            if let Some(cmds) = enter_cmds {
                                let id =
                                    Commands::new(*range_lo, CommandsType::Enter, *depth).value;
                                let layer = mold::Layer::new(raster.clone(), unsafe {
                                    spn_to_tile_ops(cmds)
                                });

                                prints.insert(id);
                                map.print(id, layer);
                            }
                            if let Some(cmds) = leave_cmds {
                                let id =
                                    Commands::new(*range_hi, CommandsType::Leave, *depth).value;
                                let layer = mold::Layer::new(raster.clone(), unsafe {
                                    spn_to_tile_ops(cmds)
                                });

                                prints.insert(id);
                                map.print(id, layer);
                            }
                        } else {
                            if let Some(cmds) = enter_cmds {
                                let id =
                                    Commands::new(*range_lo, CommandsType::Enter, *depth).value;

                                prints.insert(id);
                                map.global(id, unsafe { spn_to_tile_ops(cmds) });
                            }
                            if let Some(cmds) = leave_cmds {
                                let id =
                                    Commands::new(*range_hi, CommandsType::Leave, *depth).value;

                                prints.insert(id);
                                map.global(id, unsafe { spn_to_tile_ops(cmds) });
                            }
                        };
                    }
                    Node::Range(Range { parent, .. }) => {
                        panic!("group with parent {:?} is missing range_lo/range_hi values", parent,)
                    }
                    _ => unreachable!(),
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    #[cfg(feature = "lib")]
    use super::*;

    #[cfg(feature = "lib")]
    use crate::composition::Composition;

    #[cfg(feature = "lib")]
    #[test]
    fn print_empty() {
        let mut map = Map::new(1, 1);
        let mut composition = Composition::new();
        let mut styling = Styling::new();
        let group_id_parent = styling.group_alloc();

        styling.group_range_lo(group_id_parent, 1);
        styling.group_range_hi(group_id_parent, 1);

        styling.layer(group_id_parent, 0, 0);
        unsafe {
            styling.group_parents(1, 1).write(group_id_parent);
        }

        styling.prints(&mut composition, &mut map, &mut HashSet::new());
    }
}
