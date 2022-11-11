// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    component_debug::list::{get_all_instances, Instance, InstanceState},
    ffx_component::rcs::{connect_to_realm_explorer, connect_to_realm_query},
    ffx_component_graph_args::{ComponentGraphCommand, GraphOrientation},
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc,
    moniker::AbsoluteMonikerBase,
    url::Url,
};

/// The starting part of our Graphviz graph output. This should be printed before any contents.
static GRAPHVIZ_START: &str = r##"digraph {
    graph [ pad = 0.2 ]
    node [ shape = "box" color = "#2a5b4f" penwidth = 2.25 fontname = "prompt medium" fontsize = 10 target = "_parent" margin = 0.22, ordering = out ];
    edge [ color = "#37474f" penwidth = 1 arrowhead = none target = "_parent" fontname = "roboto mono" fontsize = 10 ]

    splines = "ortho"
"##;

/// The ending part of our Graphviz graph output. This should be printed after `GRAPHVIZ_START` and the
/// contents of the graph.
static GRAPHVIZ_END: &str = "}";

#[ffx_plugin()]
pub async fn graph(rcs_proxy: rc::RemoteControlProxy, cmd: ComponentGraphCommand) -> Result<()> {
    let query_proxy = connect_to_realm_query(&rcs_proxy).await?;
    let explorer_proxy = connect_to_realm_explorer(&rcs_proxy).await?;

    let instances = get_all_instances(&explorer_proxy, &query_proxy, cmd.only).await?;

    graph_impl(instances, cmd.orientation, &mut std::io::stdout()).await
}

fn construct_codesearch_url(component_url: &str) -> String {
    // Extract the last part of the component URL
    let mut name_with_filetype = component_url.rsplit_once("/").unwrap().1.to_string();
    if name_with_filetype.ends_with(".cm") {
        name_with_filetype.push('l');
    }

    // We mix dashes and underscores between the manifest name and the instance name
    // sometimes, so search using both.
    let name_with_underscores = name_with_filetype.replace("-", "_");
    let name_with_dashes = name_with_filetype.replace("_", "-");

    let query = if name_with_underscores == name_with_dashes {
        format!("f:{}", &name_with_underscores)
    } else {
        format!("f:{}|{}", &name_with_underscores, &name_with_dashes)
    };

    let mut code_search_url = Url::parse("https://cs.opensource.google/search").unwrap();
    code_search_url.query_pairs_mut().append_pair("q", &query).append_pair("ss", "fuchsia/fuchsia");

    code_search_url.into_string()
}

async fn graph_impl<W: std::io::Write>(
    instances: Vec<Instance>,
    orientation: Option<GraphOrientation>,
    writer: &mut W,
) -> Result<()> {
    // TODO(fxb/90084): Use `dot` crate for printing this graph.
    writeln!(writer, "{}", GRAPHVIZ_START)?;

    // Switch the orientation of the graph.
    match orientation.unwrap_or(GraphOrientation::Default) {
        GraphOrientation::TopToBottom => writeln!(writer, r#"    rankdir = "TB""#),
        GraphOrientation::LeftToRight => writeln!(writer, r#"    rankdir = "LR""#),
        GraphOrientation::Default => Ok({}),
    }?;

    for instance in &instances {
        let moniker = instance.moniker.to_string();
        let label = if let Some(leaf) = instance.moniker.leaf() {
            leaf.to_string()
        } else {
            "/".to_string()
        };

        // CMX components are shaded red.
        let cmx_attrs = if instance.is_cmx { r##"color = "#8f3024""## } else { "" };

        // Running components are filled.
        let running_attrs = if instance.state == InstanceState::Started {
            r##"style = "filled" fontcolor = "#ffffff""##
        } else {
            ""
        };

        // Components can be clicked to search for them on Code Search.
        let url_attrs = if let Some(url) = &instance.url {
            let code_search_url = construct_codesearch_url(url);
            format!(r#"href = "{}""#, code_search_url.as_str())
        } else {
            String::new()
        };

        // Draw the component.
        writeln!(
            writer,
            r#"    "{}" [ label = "{}" {} {} {} ]"#,
            &moniker, &label, &cmx_attrs, &running_attrs, &url_attrs
        )?;

        // Component has a parent and the parent is also in the list of components
        if let Some(parent_moniker) = instance.moniker.parent() {
            if let Some(parent) = instances.iter().find(|i| i.moniker == parent_moniker) {
                // Connect parent to component
                writeln!(writer, r#"    "{}" -> "{}""#, &parent.moniker.to_string(), &moniker)?;
            }
        }
    }

    writeln!(writer, "{}", GRAPHVIZ_END)?;
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use moniker::AbsoluteMoniker;

    fn instances_for_test() -> Vec<Instance> {
        vec![
            Instance {
                moniker: AbsoluteMoniker::root(),
                url: Some("fuchsia-boot:///#meta/root.cm".to_owned()),
                instance_id: None,
                is_cmx: false,
                state: InstanceState::Resolved,
                hub_dir: None,
            },
            Instance {
                moniker: AbsoluteMoniker::parse_str("/appmgr").unwrap(),
                url: Some("fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".to_owned()),
                instance_id: None,
                is_cmx: false,
                state: InstanceState::Started,
                hub_dir: None,
            },
            Instance {
                moniker: AbsoluteMoniker::parse_str("/appmgr/foo.cmx").unwrap(),
                url: Some("fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx".to_owned()),
                instance_id: None,
                is_cmx: true,
                state: InstanceState::Started,
                hub_dir: None,
            },
            Instance {
                moniker: AbsoluteMoniker::parse_str("/appmgr/bar_baz.cmx").unwrap(),
                url: Some("fuchsia-pkg://fuchsia.com/bar#meta/bar_baz.cmx".to_owned()),
                instance_id: None,
                is_cmx: true,
                state: InstanceState::Started,
                hub_dir: None,
            },
            Instance {
                moniker: AbsoluteMoniker::parse_str("/sys").unwrap(),
                url: Some("fuchsia-pkg://fuchsia.com/sys#meta/sys.cm".to_owned()),
                instance_id: None,
                is_cmx: false,
                state: InstanceState::Resolved,
                hub_dir: None,
            },
            Instance {
                moniker: AbsoluteMoniker::parse_str("/sys/baz").unwrap(),
                url: Some("fuchsia-pkg://fuchsia.com/baz#meta/baz.cm".to_owned()),
                instance_id: None,
                is_cmx: false,
                state: InstanceState::Started,
                hub_dir: None,
            },
            Instance {
                moniker: AbsoluteMoniker::parse_str("/sys/fuzz").unwrap(),
                url: Some("fuchsia-pkg://fuchsia.com/fuzz#meta/fuzz.cm".to_owned()),
                instance_id: None,
                is_cmx: false,
                state: InstanceState::Resolved,
                hub_dir: None,
            },
            Instance {
                moniker: AbsoluteMoniker::parse_str("/sys/fuzz/hello").unwrap(),
                url: Some("fuchsia-pkg://fuchsia.com/hello#meta/hello.cm".to_owned()),
                instance_id: None,
                is_cmx: false,
                state: InstanceState::Resolved,
                hub_dir: None,
            },
        ]
    }

    // The tests in this file are change-detectors because they will fail on
    // any style changes to the graph. This isn't great, but it makes it easy
    // to view the changes in a Graphviz visualizer.

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_graph() {
        let instances = instances_for_test();

        let mut output = Vec::new();
        graph_impl(instances, /* orientation */ None, &mut output).await.unwrap();
        pretty_assertions::assert_eq!(String::from_utf8(output).unwrap(), r##"digraph {
    graph [ pad = 0.2 ]
    node [ shape = "box" color = "#2a5b4f" penwidth = 2.25 fontname = "prompt medium" fontsize = 10 target = "_parent" margin = 0.22, ordering = out ];
    edge [ color = "#37474f" penwidth = 1 arrowhead = none target = "_parent" fontname = "roboto mono" fontsize = 10 ]

    splines = "ortho"

    "/" [ label = "/"   href = "https://cs.opensource.google/search?q=f%3Aroot.cml&ss=fuchsia%2Ffuchsia" ]
    "/appmgr" [ label = "appmgr"  style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Aappmgr.cml&ss=fuchsia%2Ffuchsia" ]
    "/" -> "/appmgr"
    "/appmgr/foo.cmx" [ label = "foo.cmx" color = "#8f3024" style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Afoo.cmx&ss=fuchsia%2Ffuchsia" ]
    "/appmgr" -> "/appmgr/foo.cmx"
    "/appmgr/bar_baz.cmx" [ label = "bar_baz.cmx" color = "#8f3024" style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Abar_baz.cmx%7Cbar-baz.cmx&ss=fuchsia%2Ffuchsia" ]
    "/appmgr" -> "/appmgr/bar_baz.cmx"
    "/sys" [ label = "sys"   href = "https://cs.opensource.google/search?q=f%3Asys.cml&ss=fuchsia%2Ffuchsia" ]
    "/" -> "/sys"
    "/sys/baz" [ label = "baz"  style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Abaz.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys" -> "/sys/baz"
    "/sys/fuzz" [ label = "fuzz"   href = "https://cs.opensource.google/search?q=f%3Afuzz.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys" -> "/sys/fuzz"
    "/sys/fuzz/hello" [ label = "hello"   href = "https://cs.opensource.google/search?q=f%3Ahello.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys/fuzz" -> "/sys/fuzz/hello"
}
"##.to_string());
    }

    async fn test_graph_orientation(orientation: GraphOrientation, expected_rankdir: &str) {
        let component = instances_for_test();

        let mut output = Vec::new();
        graph_impl(component, Some(orientation), &mut output).await.unwrap();
        pretty_assertions::assert_eq!(
            String::from_utf8(output).unwrap(),
            format!(
                r##"digraph {{
    graph [ pad = 0.2 ]
    node [ shape = "box" color = "#2a5b4f" penwidth = 2.25 fontname = "prompt medium" fontsize = 10 target = "_parent" margin = 0.22, ordering = out ];
    edge [ color = "#37474f" penwidth = 1 arrowhead = none target = "_parent" fontname = "roboto mono" fontsize = 10 ]

    splines = "ortho"

    rankdir = "{}"
    "/" [ label = "/"   href = "https://cs.opensource.google/search?q=f%3Aroot.cml&ss=fuchsia%2Ffuchsia" ]
    "/appmgr" [ label = "appmgr"  style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Aappmgr.cml&ss=fuchsia%2Ffuchsia" ]
    "/" -> "/appmgr"
    "/appmgr/foo.cmx" [ label = "foo.cmx" color = "#8f3024" style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Afoo.cmx&ss=fuchsia%2Ffuchsia" ]
    "/appmgr" -> "/appmgr/foo.cmx"
    "/appmgr/bar_baz.cmx" [ label = "bar_baz.cmx" color = "#8f3024" style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Abar_baz.cmx%7Cbar-baz.cmx&ss=fuchsia%2Ffuchsia" ]
    "/appmgr" -> "/appmgr/bar_baz.cmx"
    "/sys" [ label = "sys"   href = "https://cs.opensource.google/search?q=f%3Asys.cml&ss=fuchsia%2Ffuchsia" ]
    "/" -> "/sys"
    "/sys/baz" [ label = "baz"  style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Abaz.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys" -> "/sys/baz"
    "/sys/fuzz" [ label = "fuzz"   href = "https://cs.opensource.google/search?q=f%3Afuzz.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys" -> "/sys/fuzz"
    "/sys/fuzz/hello" [ label = "hello"   href = "https://cs.opensource.google/search?q=f%3Ahello.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys/fuzz" -> "/sys/fuzz/hello"
}}
"##,
                expected_rankdir
            )
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_graph_top_to_bottom_orientation() {
        test_graph_orientation(GraphOrientation::TopToBottom, "TB").await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_graph_left_to_right_orientation() {
        test_graph_orientation(GraphOrientation::LeftToRight, "LR").await;
    }
}
