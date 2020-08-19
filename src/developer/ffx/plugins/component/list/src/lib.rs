// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::component::*,
    crate::io::*,
    anyhow::{Context, Result},
    async_trait::async_trait,
    ffx_component_list_args::ComponentListCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_io as fio,
    fuchsia_zircon_status::Status,
    std::io::{stdout, Write},
};

mod component;
mod io;

#[ffx_plugin()]
pub async fn list(rcs_proxy: rc::RemoteControlProxy, cmd: ComponentListCommand) -> Result<()> {
    list_impl(rcs_proxy, cmd, Box::new(stdout())).await
}

async fn list_impl<W: Write>(
    rcs_proxy: rc::RemoteControlProxy,
    _cmd: ComponentListCommand,
    mut writer: W,
) -> Result<()> {
    let (root, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .context("creating hub root proxy")?;
    rcs_proxy
        .open_hub(dir_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening hub")?;
    let c = root.to_component_v2().await?;
    writeln!(writer, "{}", c.tree_formatter(0))?;
    Ok(())
}

pub const V1_ROOT_COMPONENT: &'static str = "fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm";

pub const V1_HUB_PATH: &'static str = "exec/out/hub";

#[async_trait]
pub trait DirectoryProxyComponentExt {
    async fn to_component_v2(self) -> Result<ComponentVersion2>;

    async fn to_component_v1(self) -> Result<ComponentVersion1>;

    async fn component_v1_children(&self) -> Result<Vec<ComponentVersion1>>;

    async fn to_realm(self) -> Result<Realm>;
}

#[async_trait]
impl DirectoryProxyComponentExt for fio::DirectoryProxy {
    async fn to_component_v2(self) -> Result<ComponentVersion2> {
        let (url, id, component_type) = futures::try_join!(
            self.read_file("url"),
            self.read_file("id"),
            self.read_file("component_type")
        )?;

        let children = match url.as_ref() {
            V1_ROOT_COMPONENT => {
                vec![Component::V1(V1_HUB_PATH.to_dir_proxy(&self)?.to_realm().await?)]
            }
            _ => "children"
                .to_dir_proxy(&self)?
                .map_children(|c| c.to_component_v2())
                .await?
                .drain(..)
                .map(|c| Component::V2(c))
                .collect(),
        };
        Ok(ComponentVersion2 { url, id, component_type, children })
    }

    async fn to_component_v1(self) -> Result<ComponentVersion1> {
        let (id, name, url, merkleroot, children) = futures::join!(
            self.read_file("job-id"),
            self.read_file("name"),
            self.read_file("url"),
            self.read_file("in/pkg/meta"),
            self.component_v1_children(),
        );
        let (id, name, url, merkleroot, children) =
            (id?.parse::<u32>()?, name?, url?, merkleroot.ok(), children?);
        Ok(ComponentVersion1 { id, url, name, merkleroot, children })
    }

    async fn to_realm(self) -> Result<Realm> {
        let (id, name, mut realms_stacked, components) = futures::try_join!(
            self.read_file("job-id"),
            self.read_file("name"),
            "r".to_dir_proxy(&self)?.map_children(|realm_name| {
                realm_name.map_children(|realm_instance| realm_instance.to_realm())
            }),
            self.component_v1_children(),
        )?;
        let id = id.parse::<u32>()?;
        let realms = realms_stacked.drain(..).flatten().collect();
        Ok(Realm { id, name, realms, components })
    }

    async fn component_v1_children(&self) -> Result<Vec<ComponentVersion1>> {
        if let Some(c) = self.get_dirent("c").await? {
            Ok(c.to_dir_proxy(&self)?
                .map_children(|child_name| {
                    child_name.map_children(|child_instance| child_instance.to_component_v1())
                })
                .await?
                .drain(..)
                .flatten()
                .collect())
        } else {
            // "c" directory doesn't exist, therefore no CFv1 children.
            Ok(vec![])
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use io::test as io_test;
    use io::test::TreeBuilder;

    trait ComponentExt {
        fn as_v2<'a>(&'a self) -> Option<&'a ComponentVersion2>;

        fn as_v1<'a>(&'a self) -> Option<&'a Realm>;
    }

    impl ComponentExt for Component {
        fn as_v2<'a>(&'a self) -> Option<&'a ComponentVersion2> {
            match self {
                Component::V2(c) => Some(c),
                _ => None,
            }
        }

        fn as_v1<'a>(&'a self) -> Option<&'a Realm> {
            match self {
                Component::V1(c) => Some(c),
                _ => None,
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_flat_hub() {
        let mut root = io_test::TestDirentTree::root()
            .add_file("url", "fuchsia-boot:///#meta/root.cm")
            .add_file("id", "1")
            .add_file("component_type", "excellent");
        root.add_dir("children");
        let root = io_test::setup_fake_directory(root);
        let component = root.to_component_v2().await.unwrap();
        assert_eq!(component.url, "fuchsia-boot:///#meta/root.cm");
        assert_eq!(component.id, "1");
        assert_eq!(component.component_type, "excellent");
        assert_eq!(component.children.len(), 0);
    }

    trait ComponentBuilder {
        /// Add's a "leaf" component, meaning subsequent builder chains will append to whatever
        /// directory this component is in.
        fn add_v1_component_leaf(self, name: &str, job_id: &str, merkleroot: Option<&str>) -> Self;

        /// Add's a "leaf" component, meaning subsequent builder chains will append to whatever
        /// directory this component is in.
        fn add_v2_component_leaf(self, name: &str, id: &str, component_type: &str) -> Self;

        fn add_v2_component_chain(self, name: &str, id: &str, component_type: &str) -> Self;
    }

    impl ComponentBuilder for &mut io_test::TestDirentTree {
        fn add_v1_component_leaf(self, name: &str, job_id: &str, merkleroot: Option<&str>) -> Self {
            let base_dir = self
                .add_dir(name.clone())
                .add_dir(job_id.clone())
                .add_file("job-id", job_id.clone())
                .add_file("url", format!("{}.cmx", name).as_ref())
                .add_file("name", name.clone());
            if let Some(merkleroot) = merkleroot {
                base_dir.add_dir("in").add_dir("pkg").add_file("meta", merkleroot);
            }
            self
        }

        fn add_v2_component_leaf(self, name: &str, id: &str, component_type: &str) -> Self {
            self.add_v2_component_chain(name, id, component_type);
            self
        }

        fn add_v2_component_chain(self, name: &str, id: &str, component_type: &str) -> Self {
            self.add_dir(name.clone())
                .add_file("url", format!("{}.cm", name).as_ref())
                .add_file("id", id)
                .add_file("component_type", component_type)
                .add_dir("children")
        }
    }

    fn make_v1_hub_tree(root: &mut io_test::TestDirentTree) {
        root.add_file("job-id", "999")
            .add_file("name", "app")
            .add_dir("c")
            .add_v1_component_leaf("sysmgr", "123", Some("12345"))
            .add_v1_component_leaf("tiles", "1234", None);

        let realm = root.add_dir("r").add_dir("realm").add_dir("456");
        realm.add_dir("r");
        realm
            .add_file("name", "realm")
            .add_file("job-id", "456")
            .add_dir("c")
            .add_v1_component_leaf("foo", "4567", None);
    }

    // Root is special as it has no self-named directory.
    fn hub_root_setup(root: &mut io_test::TestDirentTree) -> &mut io_test::TestDirentTree {
        root.add_file("url", "root.cm").add_file("id", "1").add_file("component_type", "fun");
        root.add_dir("children")
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_hub_tree() {
        // Intended hub topo:
        //
        // root -|
        //       |
        //       |-----> bootstrap ---> appmgr --------|----> sysmgr.cmx (m)
        //       |                                     |
        //       |                                     |----> tiles.cmx
        //       |                                     |
        //       |                                     |----> realm ---> foo.cmx
        //       |-----> console ----> driver_manager
        //
        let mut root = io_test::TestDirentTree::root();
        let root_component = hub_root_setup(&mut root);
        #[rustfmt::skip]
        root_component
            .add_v2_component_chain("console", "3", "fabulous")
            .add_v2_component_leaf("driver_manager", "7", "crunchy");
        let appmgr =
            root_component.add_v2_component_chain("bootstrap", "2", "spicy").add_dir("appmgr");

        // Appmgr is a little special for its setup.
        appmgr.add_dir("children");
        appmgr
            .add_file("id", "8")
            .add_file("url", V1_ROOT_COMPONENT)
            .add_file("component_type", "extra_special");
        let v1_hub_root = appmgr.add_dir("exec").add_dir("out").add_dir("hub");
        make_v1_hub_tree(v1_hub_root);

        let root = io_test::setup_fake_directory(root).to_component_v2().await.unwrap();
        assert_eq!(root.url, "root.cm");
        assert_eq!(root.id, "1");
        assert_eq!(root.component_type, "fun");
        assert_eq!(root.children.len(), 2);
        let bootstrap = root
            .children
            .iter()
            .find(|c| c.as_v2().unwrap().url == "bootstrap.cm")
            .unwrap()
            .as_v2()
            .unwrap();
        assert_eq!(bootstrap.id, "2");
        assert_eq!(bootstrap.component_type, "spicy");
        assert_eq!(bootstrap.children.len(), 1);
        let appmgr = bootstrap.children[0].as_v2().unwrap();
        assert_eq!(appmgr.id, "8");
        assert_eq!(appmgr.url, V1_ROOT_COMPONENT);
        assert_eq!(appmgr.component_type, "extra_special");
        assert_eq!(appmgr.children.len(), 1);
        let app_realm = appmgr.children[0].as_v1().unwrap();
        assert_eq!(app_realm.id, 999);
        assert_eq!(app_realm.name, "app");
        assert_eq!(app_realm.components.len(), 2);
        assert_eq!(app_realm.realms.len(), 1);
        let sysmgr = app_realm.components.iter().find(|c| c.name == "sysmgr").unwrap();
        assert_eq!(sysmgr.url, "sysmgr.cmx");
        assert_eq!(sysmgr.id, 123);
        assert_eq!(sysmgr.merkleroot, Some("12345".to_owned()));
        assert_eq!(sysmgr.children.len(), 0);
        let tiles = app_realm.components.iter().find(|c| c.name == "tiles").unwrap();
        assert_eq!(tiles.url, "tiles.cmx");
        assert_eq!(tiles.id, 1234);
        assert_eq!(tiles.merkleroot, None);
        assert_eq!(tiles.children.len(), 0);
        let subrealm = &app_realm.realms[0];
        assert_eq!(subrealm.name, "realm");
        assert_eq!(subrealm.id, 456);
        assert_eq!(subrealm.realms.len(), 0);
        assert_eq!(subrealm.components.len(), 1);
        let foo = &subrealm.components[0];
        assert_eq!(foo.name, "foo");
        assert_eq!(foo.url, "foo.cmx");
        assert_eq!(foo.id, 4567);
        assert_eq!(foo.merkleroot, None);
        assert_eq!(foo.children.len(), 0);
        let console = root
            .children
            .iter()
            .find(|c| c.as_v2().unwrap().url == "console.cm")
            .unwrap()
            .as_v2()
            .unwrap();
        assert_eq!(console.id, "3");
        assert_eq!(console.component_type, "fabulous");
        assert_eq!(console.children.len(), 1);
        let driver_manager = console.children[0].as_v2().unwrap();
        assert_eq!(driver_manager.id, "7");
        assert_eq!(driver_manager.url, "driver_manager.cm");
        assert_eq!(driver_manager.component_type, "crunchy");
    }
}
