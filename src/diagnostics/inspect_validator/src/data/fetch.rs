// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_inspect::{TreeMarker, TreeNameIteratorMarker, TreeProxy},
    fuchsia_zircon::Vmo,
    std::{collections::HashMap, future::Future, pin::Pin},
};

/// A tree representation of an Inspect-formatted VMO.
/// It contains the root VMO and all loaded child VMOs.
#[allow(dead_code)]
#[derive(Debug)]
pub struct LazyNode {
    vmo: Vmo,
    children: Option<HashMap<String, LazyNode>>,
}

#[allow(dead_code)]
impl LazyNode {
    /// Creates a VMO tree using the channel given as root and the children trees as read from `channel.open_child`.
    pub fn new(channel: TreeProxy) -> Pin<Box<dyn Future<Output = Result<LazyNode, Error>>>> {
        Box::pin(async move {
            let fetcher = LazyNodeFetcher::new(channel);
            let vmo = fetcher.get_vmo().await?;
            let mut children = HashMap::new();
            for child_name in fetcher.get_child_names().await?.iter() {
                let child_channel = fetcher.get_child_tree_channel(child_name).await?;
                let lazy_node = LazyNode::new(child_channel).await?;
                children.insert(child_name.to_string(), lazy_node);
            }
            return Ok(LazyNode { vmo, children: Some(children) });
        })
    }

    /// Returns VMO ref held by this node.
    pub fn vmo(&self) -> &Vmo {
        &self.vmo
    }

    /// Returns the children nodes held by this node.
    /// This is a move-operation wherein doing this will result in the underlying child tree being set to None.
    /// Subsequent calls to this method will yield None.
    pub fn take_children(&mut self) -> Option<HashMap<String, LazyNode>> {
        self.children.take()
    }
}

// Utility class that wraps around the methods specified Tree protocol.
// FIDL API is defined here: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/sdk/fidl/fuchsia.inspect/tree.fidl#43
struct LazyNodeFetcher {
    channel: TreeProxy,
}

impl LazyNodeFetcher {
    fn new(channel: TreeProxy) -> LazyNodeFetcher {
        LazyNodeFetcher { channel }
    }

    async fn get_vmo(&self) -> Result<Vmo, Error> {
        let tree_content = self.channel.get_content().await?;
        return tree_content.buffer.map(|b| b.vmo).ok_or(format_err!("Failed to fetch VMO."));
    }

    async fn get_child_names(&self) -> Result<Vec<String>, Error> {
        let (name_iterator, server_end) = create_proxy::<TreeNameIteratorMarker>()?;
        self.channel.list_child_names(server_end)?;
        let mut names = vec![];
        loop {
            let subset = name_iterator.get_next().await?;
            if subset.is_empty() {
                return Ok(names);
            }
            names.extend(subset.into_iter());
        }
    }

    async fn get_child_tree_channel(&self, name: &String) -> Result<TreeProxy, Error> {
        let (child_channel, server_end) = create_proxy::<TreeMarker>()?;
        self.channel.open_child(name, server_end)?;
        Ok(child_channel)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{Context, Error},
        fidl_fuchsia_inspect::{
            TreeContent, TreeMarker, TreeNameIteratorRequest, TreeNameIteratorRequestStream,
            TreeProxy, TreeRequest, TreeRequestStream,
        },
        fidl_fuchsia_mem::Buffer,
        fuchsia_async as fasync,
        fuchsia_syslog::macros::fx_log_err,
        fuchsia_zircon::{self as zx, HandleBased},
        futures::{TryFutureExt, TryStreamExt},
        std::convert::TryInto,
        std::sync::Arc,
    };

    const MAX_TREE_NAME_LIST_SIZE: usize = 1;
    const SHARED_VMO: &str = "SHARED";
    const SINGLE_CHILD_ROOT: &str = "SINGLE_CHILD_ROOT";
    const NAMED_CHILD_NODE: &str = "NAMED_CHILD_NODE";
    const NAMED_GRANDCHILD_NODE: &str = "NAMED_GRANDCHILD_NODE";
    const MULTI_CHILD_ROOT: &str = "MULTI_CHILD_ROOT";
    const ALL_NODES: [&str; 4] =
        [SINGLE_CHILD_ROOT, NAMED_CHILD_NODE, NAMED_GRANDCHILD_NODE, MULTI_CHILD_ROOT];

    #[fasync::run_singlethreaded(test)]
    async fn complete_vmo_tree_gets_read() -> Result<(), Error> {
        let vmos = Arc::new(TestVmos::new());
        let tree = spawn_root_tree_server(SINGLE_CHILD_ROOT, Arc::clone(&vmos))?;

        let root_tree = LazyNode::new(tree).await?;
        let child_tree = root_tree.children.as_ref().unwrap().get(NAMED_CHILD_NODE).unwrap();
        let grandchild_tree =
            child_tree.children.as_ref().unwrap().get(NAMED_GRANDCHILD_NODE).unwrap();

        assert_has_size(&root_tree.children.as_ref().unwrap(), 1);
        assert_has_size(&child_tree.children.as_ref().unwrap(), 1);
        assert_has_size(&grandchild_tree.children.as_ref().unwrap(), 0);
        assert_has_vmo(&vmos, SINGLE_CHILD_ROOT, &root_tree);
        assert_has_vmo(&vmos, NAMED_CHILD_NODE, &child_tree);
        assert_has_vmo(&vmos, NAMED_GRANDCHILD_NODE, &grandchild_tree);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn repeatedly_calls_list_children_until_all_names_are_fetched() -> Result<(), Error> {
        let vmos = Arc::new(TestVmos::new());
        let tree = spawn_root_tree_server(MULTI_CHILD_ROOT, Arc::clone(&vmos))?;
        let mut root_tree = LazyNode::new(tree).await?;

        let children = root_tree.take_children().unwrap();

        assert_eq!(root_tree.children.is_none(), true);
        assert_has_size(&children, child_names(MULTI_CHILD_ROOT).len());
        for name in child_names(MULTI_CHILD_ROOT).iter() {
            assert_eq!(children.contains_key(name), true);
        }

        Ok(())
    }

    fn spawn_root_tree_server(tree_name: &str, vmos: Arc<TestVmos>) -> Result<TreeProxy, Error> {
        let (tree, request_stream) = fidl::endpoints::create_proxy_and_stream::<TreeMarker>()?;
        spawn_tree_server(tree_name.to_string(), vmos, request_stream);
        Ok(tree)
    }

    fn spawn_tree_server(name: String, vmos: Arc<TestVmos>, stream: TreeRequestStream) {
        fasync::spawn(async move {
            handle_request_stream(name, vmos, stream)
                .await
                .unwrap_or_else(|e: Error| fx_log_err!("Couldn't run tree server: {:?}", e));
        });
    }

    async fn handle_request_stream(
        name: String,
        vmos: Arc<TestVmos>,
        mut stream: TreeRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await.context("Error running tree server")? {
            match request {
                TreeRequest::GetContent { responder } => {
                    let vmo = vmos.get(&name);
                    let size = vmo.get_size()?;
                    let content = TreeContent { buffer: Some(Buffer { vmo, size }) };
                    responder.send(content)?;
                }
                TreeRequest::ListChildNames { tree_iterator, .. } => {
                    let request_stream = tree_iterator.into_stream()?;
                    spawn_tree_name_iterator_server(child_names(&name), request_stream)
                }
                TreeRequest::OpenChild { child_name, tree, .. } => {
                    spawn_tree_server(child_name, Arc::clone(&vmos), tree.into_stream()?)
                }
            }
        }
        Ok(())
    }

    fn spawn_tree_name_iterator_server(
        values: Vec<String>,
        mut stream: TreeNameIteratorRequestStream,
    ) {
        fasync::spawn(
            async move {
                let mut values_iter = values.into_iter();
                while let Some(request) =
                    stream.try_next().await.context("Error running tree iterator server")?
                {
                    match request {
                        TreeNameIteratorRequest::GetNext { responder } => {
                            let result = values_iter
                                .by_ref()
                                .take(MAX_TREE_NAME_LIST_SIZE)
                                .collect::<Vec<String>>();
                            if result.is_empty() {
                                responder.send(&mut vec![].into_iter())?;
                                return Ok(());
                            }
                            responder.send(&mut result.iter().map(|s| s.as_ref()))?;
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| {
                fx_log_err!("Failed to running tree name iterator server: {:?}", e)
            }),
        )
    }

    struct TestVmos {
        vmos: HashMap<String, zx::Vmo>,
        default: zx::Vmo,
    }

    impl TestVmos {
        fn new() -> TestVmos {
            let mut vmos = HashMap::new();
            vmos.insert(SHARED_VMO.to_string(), create_vmo(SHARED_VMO));
            for value in ALL_NODES.iter() {
                let vmo = create_vmo(value);
                vmos.insert(value.to_string(), vmo);
            }
            TestVmos { vmos, default: create_vmo(SHARED_VMO) }
        }

        fn get(&self, value: &str) -> zx::Vmo {
            duplicate_vmo(self.vmos.get(&value.to_string()).unwrap_or(&self.default))
        }
    }

    fn assert_has_vmo(vmos: &TestVmos, name: &str, node: &LazyNode) {
        let actual = get_vmo_as_buf(node.vmo()).unwrap();
        let expected = get_vmo_as_buf(&vmos.get(&name.to_string())).unwrap();
        assert_eq!(actual, expected);
    }

    fn assert_has_size(map: &HashMap<String, LazyNode>, size: usize) {
        assert_eq!(map.len(), size);
    }

    fn get_vmo_as_buf(vmo: &Vmo) -> Result<Vec<u8>, Error> {
        let size = vmo.get_size()?;
        let mut buffer = vec![0u8; size as usize];
        vmo.read(&mut buffer[..], 0)?;
        Ok(buffer)
    }

    fn create_vmo(value: &str) -> zx::Vmo {
        zx::Vmo::create(value.len().try_into().unwrap()).unwrap()
    }

    fn duplicate_vmo(vmo: &Vmo) -> Vmo {
        vmo.duplicate_handle(zx::Rights::BASIC | zx::Rights::READ | zx::Rights::MAP).ok().unwrap()
    }

    fn child_names(value: &str) -> Vec<String> {
        match value {
            SINGLE_CHILD_ROOT => vec![NAMED_CHILD_NODE.to_string()],
            NAMED_CHILD_NODE => vec![NAMED_GRANDCHILD_NODE.to_string()],
            MULTI_CHILD_ROOT => vec!["a".to_string(), "b".to_string(), "c".to_string()],
            _ => vec![],
        }
    }
}
