// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::{Debug, Display};

#[derive(Debug)]
pub enum Component {
    V1(Realm), // In this topography the root will always be a realm.
    V2(ComponentVersion2),
}

#[derive(Debug)]
pub struct Realm {
    pub id: u32,
    pub name: String,
    pub realms: Vec<Self>,
    pub components: Vec<ComponentVersion1>,
}

#[derive(Debug)]
pub struct ComponentVersion1 {
    pub url: String,
    pub name: String,
    pub id: u32,
    pub merkleroot: Option<String>,
    pub children: Vec<Self>,
}

#[derive(Debug)]
pub struct ComponentVersion2 {
    pub url: String,
    pub id: String,
    pub component_type: String,
    pub children: Vec<Component>,
}

const INDENT_INCREMENT: usize = 2;

pub trait TreeFormatter {
    fn tree_formatter<'a>(&'a self, indent: usize) -> Box<dyn Display + 'a>;
}

struct ComponentTreeFormatterV2<'a> {
    inner: &'a ComponentVersion2,
    indent: usize,
}

struct ComponentTreeFormatterRealm<'a> {
    inner: &'a Realm,
    indent: usize,
}

struct ComponentTreeFormatterV1<'a> {
    inner: &'a ComponentVersion1,
    indent: usize,
}

impl TreeFormatter for Component {
    fn tree_formatter<'a>(&'a self, indent: usize) -> Box<dyn Display + 'a> {
        match self {
            Component::V2(c) => c.tree_formatter(indent),
            Component::V1(c) => c.tree_formatter(indent),
        }
    }
}

impl TreeFormatter for ComponentVersion1 {
    fn tree_formatter<'a>(&'a self, indent: usize) -> Box<dyn Display + 'a> {
        Box::new(ComponentTreeFormatterV1 { inner: self, indent })
    }
}

impl TreeFormatter for ComponentVersion2 {
    fn tree_formatter<'a>(&'a self, indent: usize) -> Box<dyn Display + 'a> {
        Box::new(ComponentTreeFormatterV2 { inner: self, indent })
    }
}

impl TreeFormatter for Realm {
    fn tree_formatter<'a>(&'a self, indent: usize) -> Box<dyn Display + 'a> {
        Box::new(ComponentTreeFormatterRealm { inner: self, indent })
    }
}

fn pad(amount: usize, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
    // I tried several different formatting strings, and none appeared to
    // work, so here's the next best thing.
    (0..amount).try_for_each(|_| write!(f, " "))
}

impl Display for ComponentTreeFormatterV2<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        pad(self.indent, f)?;
        write!(
            f,
            "{}\n",
            self.inner
                .url
                .split("/")
                .last()
                .ok_or(std::fmt::Error {})?
                .split(".")
                .next()
                .ok_or(std::fmt::Error {})?
        )?;
        self.inner
            .children
            .iter()
            .try_for_each(|c| write!(f, "{}", c.tree_formatter(self.indent + INDENT_INCREMENT)))
    }
}

impl Display for ComponentTreeFormatterRealm<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        pad(self.indent, f)?;
        write!(f, "{} (realm)\n", self.inner.name)?;
        self.inner
            .components
            .iter()
            .try_for_each(|c| write!(f, "{}", c.tree_formatter(self.indent + INDENT_INCREMENT)))?;
        self.inner
            .realms
            .iter()
            .try_for_each(|r| write!(f, "{}", r.tree_formatter(self.indent + INDENT_INCREMENT)))
    }
}

impl Display for ComponentTreeFormatterV1<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        pad(self.indent, f)?;
        write!(f, "{}\n", self.inner.url.split("/").last().ok_or(std::fmt::Error {})?)?;
        self.inner
            .children
            .iter()
            .try_for_each(|c| write!(f, "{}", c.tree_formatter(self.indent + INDENT_INCREMENT)))
    }
}

#[cfg(test)]
mod test {
    use super::*;

    fn test_tree() -> ComponentVersion2 {
        ComponentVersion2 {
            url: "/root.cm".to_owned(),
            id: "0".to_owned(),
            component_type: "static".to_owned(),
            children: vec![
                Component::V2(ComponentVersion2 {
                    url: "/bootstrap.cm".to_owned(),
                    id: "0".to_owned(),
                    component_type: "static".to_owned(),
                    children: vec![
                        Component::V2(ComponentVersion2 {
                            url: "/console.cm".to_owned(),
                            id: "0".to_owned(),
                            component_type: "static".to_owned(),
                            children: vec![],
                        }),
                        Component::V2(ComponentVersion2 {
                            url: "/driver_manager.cm".to_owned(),
                            id: "0".to_owned(),
                            component_type: "static".to_owned(),
                            children: vec![],
                        }),
                        Component::V2(ComponentVersion2 {
                            url: "/fshost.cm".to_owned(),
                            id: "0".to_owned(),
                            component_type: "static".to_owned(),
                            children: vec![],
                        }),
                    ],
                }),
                Component::V2(ComponentVersion2 {
                    url: "/core.cm".to_owned(),
                    id: "0".to_owned(),
                    component_type: "static".to_owned(),
                    children: vec![
                        Component::V2(ComponentVersion2 {
                            url: "/appmgr.cm".to_owned(),
                            id: "0".to_owned(),
                            component_type: "static".to_owned(),
                            children: vec![Component::V1(Realm {
                                id: 16606,
                                name: "app".to_owned(),
                                realms: vec![Realm {
                                    id: 16607,
                                    name: "sys".to_owned(),
                                    realms: vec![],
                                    components: vec![
                                        ComponentVersion1 {
                                            url: "/pkg-resolver.cmx".to_owned(),
                                            name: "pkg-resolver.cmx".to_owned(),
                                            id: 17339,
                                            merkleroot: Some("foobar".to_owned()),
                                            children: vec![],
                                        },
                                        ComponentVersion1 {
                                            url: "/something.cmx".to_owned(),
                                            name: "something.cmx".to_owned(),
                                            id: 12345,
                                            merkleroot: Some("bazmumble".to_owned()),
                                            children: vec![ComponentVersion1 {
                                                url: "/somechild.cmx".to_owned(),
                                                name: "somechild.cmx".to_owned(),
                                                id: 12346,
                                                merkleroot: None,
                                                children: vec![],
                                            }],
                                        },
                                    ],
                                }],
                                components: vec![ComponentVersion1 {
                                    url: "/sysmgr.cmx".to_owned(),
                                    name: "sysmgr.cmx".to_owned(),
                                    id: 9999,
                                    merkleroot: Some("florp".to_owned()),
                                    children: vec![],
                                }],
                            })],
                        }),
                        Component::V2(ComponentVersion2 {
                            url: "/archivist.cm".to_owned(),
                            id: "0".to_owned(),
                            component_type: "static".to_owned(),
                            children: vec![],
                        }),
                        Component::V2(ComponentVersion2 {
                            url: "/fonts.cm".to_owned(),
                            id: "0".to_owned(),
                            component_type: "static".to_owned(),
                            children: vec![],
                        }),
                    ],
                }),
            ],
        }
    }

    #[test]
    fn test_tree_formatter_mixed_components() {
        let tree = test_tree();
        let tree_str = format!("{}", tree.tree_formatter(0));
        #[rustfmt::skip]
        assert_eq!(tree_str,
r"root
  bootstrap
    console
    driver_manager
    fshost
  core
    appmgr
      app (realm)
        sysmgr.cmx
        sys (realm)
          pkg-resolver.cmx
          something.cmx
            somechild.cmx
    archivist
    fonts
");
    }
}
