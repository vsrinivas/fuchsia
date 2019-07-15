use crate::geometry::Rect;
use id_tree::{InsertBehavior::AsRoot, Node, NodeId, Tree};
use std::collections::HashMap;

type FacetId = NodeId;

struct Facet {
    bounds: Rect,
}

impl Default for Facet {
    fn default() -> Self {
        Facet { bounds: Rect::zero() }
    }
}

struct Setting {
    facets: Tree<Facet>,
}

impl Setting {
    pub fn new() -> Setting {
        Setting { facets: Tree::new() }
    }

    pub fn add_facet(&mut self, facet: Facet) -> FacetId {
        let root_id: NodeId =
            self.facets.insert(Node::new(facet), AsRoot).expect("insert should work");
        root_id
    }
}

#[cfg(test)]
mod setting_tests {
    use crate::facet::{Facet, Setting};
    use id_tree::{Tree, TreeBuilder};

    #[test]
    fn test_add_child() {
        let mut setting = Setting::new();
        setting.add_facet(Facet::default());
    }
}
