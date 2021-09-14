// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::IdGenerator;
use crate::{
    scene::{facets::FacetId, layout::ArrangerPtr, scene::Scene},
    Size,
};
use std::{any::Any, collections::HashMap};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
/// Identifier for a group.
pub struct GroupId(usize);

impl GroupId {
    /// Create a new group identifier.
    pub(crate) fn new(id_generator: &mut IdGenerator) -> Self {
        let group_id = id_generator.next().expect("group ID");
        Self(group_id)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub(crate) enum GroupMember {
    Facet(FacetId),
    Group(GroupId),
}

/// Dynamic type for data an arranger might want to keep per group
/// member.
pub type GroupMemberData = Box<dyn Any>;

impl GroupMember {
    fn is_facet(&self, facet_id: FacetId) -> bool {
        match self {
            GroupMember::Group(_) => false,
            GroupMember::Facet(member_facet_id) => *member_facet_id == facet_id,
        }
    }
}

#[derive(Debug)]
pub(crate) struct GroupEntry {
    member: GroupMember,
    member_data: Option<GroupMemberData>,
}

#[derive(Debug)]
pub(crate) struct Group {
    #[allow(unused)]
    pub id: GroupId,
    #[allow(unused)]
    pub label: String,
    pub members: Vec<GroupEntry>,
    pub arranger: Option<ArrangerPtr>,
}

#[derive(Debug)]
pub(crate) struct GroupMap {
    root_group_id: Option<GroupId>,
    map: HashMap<GroupId, Group>,
}

impl GroupMap {
    pub fn new() -> Self {
        let map = HashMap::new();
        Self { root_group_id: None, map }
    }

    pub fn get_root_group_id(&self) -> GroupId {
        self.root_group_id.expect("root_group_id")
    }

    pub fn add_facet_to_group(
        &mut self,
        facet_id: FacetId,
        group_id: GroupId,
        member_data: Option<GroupMemberData>,
    ) {
        let group = self.map.get_mut(&group_id).expect("group");

        group.members.push(GroupEntry { member: GroupMember::Facet(facet_id), member_data });
    }

    pub fn remove_facet_from_group(&mut self, facet_id: FacetId, group_id: GroupId) {
        let group = self.map.get_mut(&group_id).expect("group");
        group.members.retain(|entry| entry.member.is_facet(facet_id));
    }

    pub fn group_members(&self, group_id: GroupId) -> Vec<GroupMember> {
        self.map
            .get(&group_id)
            .expect("group_id")
            .members
            .iter()
            .map(|entry| entry.member)
            .collect()
    }

    pub(crate) fn group_member_data(&self, group_id: GroupId) -> Vec<&Option<GroupMemberData>> {
        let member_data = self
            .map
            .get(&group_id)
            .expect("group_id")
            .members
            .iter()
            .map(|entry| &entry.member_data)
            .collect();
        member_data
    }

    pub fn group_arranger(&self, group_id: GroupId) -> Option<&ArrangerPtr> {
        self.map.get(&group_id).and_then(|group| group.arranger.as_ref())
    }

    pub fn set_group_arranger(&mut self, group_id: GroupId, group_arranger: ArrangerPtr) {
        let group = self.map.get_mut(&group_id).expect("group");
        group.arranger = Some(group_arranger);
    }

    pub fn start_group(
        &mut self,
        group_id: GroupId,
        label: &str,
        arranger: ArrangerPtr,
        parent: Option<&GroupId>,
        member_data: Option<GroupMemberData>,
    ) {
        if self.root_group_id.is_none() {
            self.root_group_id = Some(group_id);
        }
        let group = Group {
            id: group_id,
            label: String::from(label),
            members: Vec::new(),
            arranger: Some(arranger),
        };
        self.map.insert(group_id, group);
        if let Some(parent) = parent {
            let group = self.map.get_mut(&parent).expect("group");
            group.members.push(GroupEntry { member: GroupMember::Group(group_id), member_data });
        }
    }

    fn calculate_size_map_internal(
        &self,
        target_size: &Size,
        scene: &Scene,
        group_id: &GroupId,
        size_map: &mut HashMap<GroupMember, Size>,
    ) -> Size {
        let group_members = self.group_members(*group_id);
        let mut member_sizes = Vec::new();
        for member in group_members.iter() {
            match member {
                GroupMember::Group(member_group_id) => {
                    let size = self.calculate_size_map_internal(
                        target_size,
                        scene,
                        &member_group_id,
                        size_map,
                    );
                    size_map.insert(*member, size);
                    member_sizes.push(size);
                }
                GroupMember::Facet(member_facet_id) => {
                    let size = scene.calculate_facet_size(&member_facet_id, *target_size);
                    member_sizes.push(size);
                    size_map.insert(*member, size);
                }
            }
        }
        let optional_arranger = self.group_arranger(*group_id);
        if let Some(arranger) = optional_arranger {
            let member_data = self.group_member_data(*group_id);
            let mut mut_sizes = member_sizes.clone();
            let arranged_size = arranger.calculate_size(*target_size, &mut mut_sizes, &member_data);
            for ((member_id, member_size), new_member_size) in
                group_members.iter().zip(member_sizes.iter()).zip(mut_sizes.iter())
            {
                if member_size != new_member_size {
                    size_map.insert(*member_id, *new_member_size);
                }
            }
            arranged_size
        } else {
            panic!("need an arranger");
        }
    }

    pub fn calculate_size_map(
        &self,
        target_size: Size,
        scene: &Scene,
    ) -> HashMap<GroupMember, Size> {
        let mut size_map = HashMap::new();
        let root_group_id = self.root_group_id.expect("root_group_id");
        let root_size =
            self.calculate_size_map_internal(&target_size, scene, &root_group_id, &mut size_map);
        size_map.insert(GroupMember::Group(root_group_id), root_size);
        size_map
    }
}
