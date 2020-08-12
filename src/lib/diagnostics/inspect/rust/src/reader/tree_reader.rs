// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        reader::{MissingValueReason, NodeHierarchy, PartialNodeHierarchy, ReadableTree, Snapshot},
        LinkNodeDisposition,
    },
    anyhow,
    fidl_fuchsia_inspect::TreeProxy,
    futures::{
        future::{self, BoxFuture},
        prelude::*,
    },
    std::{
        collections::BTreeMap,
        convert::{TryFrom, TryInto},
    },
};

/// Contains the snapshot of the hierarchy and snapshots of all the lazy nodes in the hierarchy.
pub struct SnapshotTree {
    snapshot: Snapshot,
    children: SnapshotTreeMap,
}

impl SnapshotTree {
    /// Loads a snapshot tree from the given inspect tree.
    pub async fn try_from(tree: &TreeProxy) -> Result<Self, anyhow::Error> {
        load_snapshot_tree(tree).await
    }
}

type SnapshotTreeMap = BTreeMap<String, Result<SnapshotTree, anyhow::Error>>;

impl TryInto<NodeHierarchy> for SnapshotTree {
    type Error = anyhow::Error;

    fn try_into(mut self) -> Result<NodeHierarchy, Self::Error> {
        let partial = PartialNodeHierarchy::try_from(self.snapshot)?;
        Ok(expand(partial, &mut self.children))
    }
}

fn expand(partial: PartialNodeHierarchy, snapshot_children: &mut SnapshotTreeMap) -> NodeHierarchy {
    // TODO(miguelfrde): remove recursion or limit depth.
    let children =
        partial.children.into_iter().map(|child| expand(child, snapshot_children)).collect();
    let mut hierarchy = NodeHierarchy::new(partial.name, partial.properties, children);
    for link_value in partial.links {
        let result = snapshot_children.remove(&link_value.content);
        if result.is_none() {
            hierarchy.add_missing(MissingValueReason::LinkNotFound, link_value.name);
            continue;
        }
        // TODO(miguelfrde): remove recursion or limit depth.
        let result: Result<NodeHierarchy, anyhow::Error> =
            result.unwrap().and_then(|snapshot_tree| snapshot_tree.try_into());
        match result {
            Err(_) => {
                hierarchy.add_missing(MissingValueReason::LinkParseFailure, link_value.name);
            }
            Ok(mut child_hierarchy) => match link_value.disposition {
                LinkNodeDisposition::Child => {
                    child_hierarchy.name = link_value.name;
                    hierarchy.children.push(child_hierarchy);
                }
                LinkNodeDisposition::Inline => {
                    hierarchy.children.extend(child_hierarchy.children.into_iter());
                    hierarchy.properties.extend(child_hierarchy.properties.into_iter());
                    hierarchy.missing.extend(child_hierarchy.missing.into_iter());
                }
            },
        }
    }
    hierarchy
}

pub async fn read<T>(tree: &T) -> Result<NodeHierarchy, anyhow::Error>
where
    T: ReadableTree + Send + Sync,
{
    load_snapshot_tree(tree).await?.try_into()
}

fn load_snapshot_tree<'a, T>(tree: &T) -> BoxFuture<Result<SnapshotTree, anyhow::Error>>
where
    T: ReadableTree + Send + Sync,
{
    async move {
        let results = future::join(tree.vmo(), tree.tree_names()).await;
        let vmo = results.0?;
        let children_names = results.1?;
        let mut children = BTreeMap::new();
        // TODO(miguelfrde): remove recursion or limit depth.
        for child_name in children_names {
            let result = tree.read_tree(&child_name).and_then(|child_tree| load(child_tree)).await;
            children.insert(child_name, result);
        }
        Ok(SnapshotTree { snapshot: Snapshot::try_from(&vmo)?, children })
    }
    .boxed()
}

async fn load<'a, T>(tree: T) -> Result<SnapshotTree, anyhow::Error>
where
    T: ReadableTree + Send + Sync,
{
    load_snapshot_tree(&tree).await
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{assert_inspect_tree, reader, Inspector},
        fuchsia_async as fasync,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_read() -> Result<(), anyhow::Error> {
        let inspector = test_inspector();
        let hierarchy = read(&inspector).await?;
        assert_inspect_tree!(hierarchy, root: {
            int: 3i64,
            "lazy-node": {
                a: "test",
                child: {
                    double: 3.14,
                },
            }
        });
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_snapshot_tree() -> Result<(), anyhow::Error> {
        let inspector = test_inspector();
        let mut snapshot_tree = load_snapshot_tree(&inspector).await?;

        let root_hierarchy: NodeHierarchy =
            PartialNodeHierarchy::try_from(snapshot_tree.snapshot)?.into();
        assert_eq!(snapshot_tree.children.keys().collect::<Vec<&String>>(), vec!["lazy-node-0"]);
        assert_inspect_tree!(root_hierarchy, root: {
            int: 3i64,
        });

        let mut lazy_node = snapshot_tree.children.remove("lazy-node-0").unwrap().unwrap();
        let lazy_node_hierarchy: NodeHierarchy =
            PartialNodeHierarchy::try_from(lazy_node.snapshot)?.into();
        assert_eq!(lazy_node.children.keys().collect::<Vec<&String>>(), vec!["lazy-values-0"]);
        assert_inspect_tree!(lazy_node_hierarchy, root: {
            a: "test",
            child: {},
        });

        let lazy_values = lazy_node.children.remove("lazy-values-0").unwrap().unwrap();
        let lazy_values_hierarchy = PartialNodeHierarchy::try_from(lazy_values.snapshot)?;
        assert_eq!(lazy_values.children.keys().len(), 0);
        assert_inspect_tree!(lazy_values_hierarchy, root: {
            double: 3.14,
        });

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn missing_value_parse_failure() -> Result<(), anyhow::Error> {
        let inspector = Inspector::new();
        let _lazy_child = inspector.root().create_lazy_child("lazy", || {
            async move {
                // For the sake of the test, force an invalid vmo.
                Ok(Inspector::new_no_op())
            }
            .boxed()
        });
        let hierarchy = reader::read_from_inspector(&inspector).await?;
        assert_eq!(hierarchy.missing.len(), 1);
        assert_eq!(hierarchy.missing[0].reason, MissingValueReason::LinkParseFailure);
        assert_inspect_tree!(hierarchy, root: {});
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn missing_value_not_found() -> Result<(), anyhow::Error> {
        let inspector = Inspector::new();
        inspector.state().map(|state| {
            let mut state = state.lock();
            state.allocate_link("missing", "missing-404", LinkNodeDisposition::Child, 0).unwrap();
        });
        let hierarchy = reader::read_from_inspector(&inspector).await?;
        assert_eq!(hierarchy.missing.len(), 1);
        assert_eq!(hierarchy.missing[0].reason, MissingValueReason::LinkNotFound);
        assert_eq!(hierarchy.missing[0].name, "missing");
        assert_inspect_tree!(hierarchy, root: {});
        Ok(())
    }

    fn test_inspector() -> Inspector {
        let inspector = Inspector::new();
        let root = inspector.root();
        root.record_int("int", 3);
        root.record_lazy_child("lazy-node", || {
            async move {
                let inspector = Inspector::new();
                inspector.root().record_string("a", "test");
                let child = inspector.root().create_child("child");
                child.record_lazy_values("lazy-values", || {
                    async move {
                        let inspector = Inspector::new();
                        inspector.root().record_double("double", 3.14);
                        Ok(inspector)
                    }
                    .boxed()
                });
                inspector.root().record(child);
                Ok(inspector)
            }
            .boxed()
        });
        inspector
    }
}
