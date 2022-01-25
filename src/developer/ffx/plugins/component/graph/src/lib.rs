// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    component_hub::{
        io::Directory,
        list::{Component, ListFilter},
    },
    ffx_component_graph_args::{ComponentGraphCommand, GraphOrientation},
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_io as fio,
    fuchsia_zircon_status::Status,
    url::Url,
};

/// The number of times listing the components from the component hub should be retried
/// before assuming failure.
const NUM_COMPONENT_LIST_ATTEMPTS: u64 = 3;

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

// Attempt to get the component list `NUM_COMPONENT_LIST_ATTEMPTS` times. If all attempts fail,
// return the last error encountered.
//
// This fixes an issue (fxbug.dev/84805) where the component topology may be mutating while the
// hub is being traversed, resulting in failures.
async fn try_get_component_list(hub_dir: Directory) -> Result<Component> {
    let mut attempt_number = 1;
    loop {
        match Component::parse("/".to_string(), hub_dir.clone()?).await {
            Ok(component) => return Ok(component),
            Err(e) => {
                if attempt_number > NUM_COMPONENT_LIST_ATTEMPTS {
                    return Err(e);
                } else {
                    eprintln!("Retrying. Attempt #{} failed: {}", attempt_number, e);
                }
            }
        }
        attempt_number += 1;
    }
}

#[ffx_plugin("component.experimental")]
pub async fn graph(rcs_proxy: rc::RemoteControlProxy, cmd: ComponentGraphCommand) -> Result<()> {
    let (root, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .context("creating hub root proxy")?;
    rcs_proxy
        .open_hub(dir_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening hub")?;
    let hub_dir = Directory::from_proxy(root);
    let component = try_get_component_list(hub_dir).await?;

    graph_impl(component, cmd.only, cmd.orientation, &mut std::io::stdout()).await
}

async fn graph_impl<W: std::io::Write>(
    component: Component,
    list_filter: Option<ListFilter>,
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

    print_graph_helper(component, &list_filter.unwrap_or(ListFilter::None), writer, None)?;

    writeln!(writer, "{}", GRAPHVIZ_END)?;
    Ok(())
}

/// Recursive function to print a dot graph for the component.
fn print_graph_helper<W: std::io::Write>(
    component: Component,
    list_filter: &ListFilter,
    writer: &mut W,
    parent_node_name: Option<String>,
) -> Result<()> {
    // The component's node's name is <parent_node_name>/<component.name>.
    let node_name = match &parent_node_name {
        Some(parent) => {
            format!("{}/{}", if parent == "/" { "" } else { parent }, &component.name)
        }
        None => component.name.clone(),
    };

    let included = component.should_include(&list_filter);
    if included {
        // CMX components are shaded red.
        let cmx_attrs = if component.is_cmx { r##"color = "#8f3024""## } else { "" };

        // Running components are filled.
        let running_attrs =
            if component.is_running { r##"style = "filled" fontcolor = "#ffffff""## } else { "" };

        // Components can be clicked to search for them on Code Search.
        let url_attrs = match component.name.as_str() {
            "/" => "".to_string(),
            name => {
                let name_with_filetype = if name.ends_with("cmx") || name.ends_with("cml") {
                    name.to_string()
                } else if component.is_cmx {
                    format!("{}.cmx", name.to_string())
                } else {
                    format!("{}.cml", name.to_string())
                };

                // We mix dashes and underscores between the manifest name and the instance name
                // sometimes, so search using both.
                let name_with_underscores = name_with_filetype.replace("-", "_");
                let name_with_dashes = name_with_filetype.replace("_", "-");

                let mut code_search_url = Url::parse("https://cs.opensource.google/search")?;
                code_search_url
                    .query_pairs_mut()
                    .append_pair(
                        "q",
                        &format!("f:{}|{}", &name_with_underscores, &name_with_dashes),
                    )
                    .append_pair("ss", "fuchsia/fuchsia");

                format!(r#"href = "{}""#, code_search_url.as_str())
            }
        };

        // Draw the component.
        writeln!(
            writer,
            r#"    "{}" [ label = "{}" {} {} {} ]"#,
            &node_name, &component.name, &cmx_attrs, &running_attrs, &url_attrs
        )?;

        // Connect the component to its parent.
        if let Some(parent) = &parent_node_name {
            writeln!(writer, r#"    "{}" -> "{}""#, &parent, &node_name)?;
        }
    }

    // If we didn't include this component, we don't want this component to be
    // the parent of its children in the graph.
    let parent_node_for_children = if included {
        Some(node_name.clone())
    } else {
        match &parent_node_name {
            Some(parent) => Some(parent.clone()),
            None => None,
        }
    };

    // Recursively draw the component's children.
    for child in component.children {
        print_graph_helper(child, &list_filter, writer, parent_node_for_children.clone())?;
    }

    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;

    fn component_for_test() -> Component {
        Component {
            name: "/".to_owned(),
            is_cmx: false,
            is_running: false,
            ancestors: vec![],
            children: vec![
                Component {
                    name: "appmgr".to_owned(),
                    is_cmx: false,
                    is_running: true,
                    ancestors: vec!["/".to_owned()],
                    children: vec![
                        Component {
                            name: "foo.cmx".to_owned(),
                            is_cmx: true,
                            is_running: true,
                            ancestors: vec!["/".to_owned(), "appmgr".to_owned()],
                            children: vec![],
                        },
                        Component {
                            name: "bar.cmx".to_owned(),
                            is_cmx: true,
                            is_running: true,
                            ancestors: vec!["/".to_owned(), "appmgr".to_owned()],
                            children: vec![],
                        },
                    ],
                },
                Component {
                    name: "sys".to_owned(),
                    is_cmx: false,
                    is_running: false,
                    ancestors: vec!["/".to_owned()],
                    children: vec![
                        Component {
                            name: "baz".to_owned(),
                            is_cmx: false,
                            is_running: true,
                            ancestors: vec!["/".to_owned(), "sys".to_owned()],
                            children: vec![],
                        },
                        Component {
                            name: "fuzz".to_owned(),
                            is_cmx: false,
                            is_running: false,
                            ancestors: vec!["/".to_owned(), "sys".to_owned()],
                            children: vec![Component {
                                name: "hello".to_owned(),
                                is_cmx: false,
                                is_running: false,
                                ancestors: vec![
                                    "/".to_owned(),
                                    "sys".to_owned(),
                                    "fuzz".to_owned(),
                                ],
                                children: vec![],
                            }],
                        },
                    ],
                },
            ],
        }
    }

    // The tests in this file are change-detectors because they will fail on
    // any style changes to the graph. This isn't great, but it makes it easy
    // to view the changes in a Graphviz visualizer.

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_graph_no_filter() {
        let component = component_for_test();

        let mut output = Vec::new();
        graph_impl(
            component,
            /* list_filter */ None,
            /* orientation */ None,
            &mut output,
        )
        .await
        .unwrap();
        pretty_assertions::assert_eq!(String::from_utf8(output).unwrap(), r##"digraph {
    graph [ pad = 0.2 ]
    node [ shape = "box" color = "#2a5b4f" penwidth = 2.25 fontname = "prompt medium" fontsize = 10 target = "_parent" margin = 0.22, ordering = out ];
    edge [ color = "#37474f" penwidth = 1 arrowhead = none target = "_parent" fontname = "roboto mono" fontsize = 10 ]

    splines = "ortho"

    "/" [ label = "/"    ]
    "/appmgr" [ label = "appmgr"  style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Aappmgr.cml%7Cappmgr.cml&ss=fuchsia%2Ffuchsia" ]
    "/" -> "/appmgr"
    "/appmgr/foo.cmx" [ label = "foo.cmx" color = "#8f3024" style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Afoo.cmx%7Cfoo.cmx&ss=fuchsia%2Ffuchsia" ]
    "/appmgr" -> "/appmgr/foo.cmx"
    "/appmgr/bar.cmx" [ label = "bar.cmx" color = "#8f3024" style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Abar.cmx%7Cbar.cmx&ss=fuchsia%2Ffuchsia" ]
    "/appmgr" -> "/appmgr/bar.cmx"
    "/sys" [ label = "sys"   href = "https://cs.opensource.google/search?q=f%3Asys.cml%7Csys.cml&ss=fuchsia%2Ffuchsia" ]
    "/" -> "/sys"
    "/sys/baz" [ label = "baz"  style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Abaz.cml%7Cbaz.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys" -> "/sys/baz"
    "/sys/fuzz" [ label = "fuzz"   href = "https://cs.opensource.google/search?q=f%3Afuzz.cml%7Cfuzz.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys" -> "/sys/fuzz"
    "/sys/fuzz/hello" [ label = "hello"   href = "https://cs.opensource.google/search?q=f%3Ahello.cml%7Chello.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys/fuzz" -> "/sys/fuzz/hello"
}
"##.to_string());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_graph_cmx_only() {
        let component = component_for_test();

        let mut output = Vec::new();
        graph_impl(component, Some(ListFilter::CMX), /* orientation */ None, &mut output)
            .await
            .unwrap();
        pretty_assertions::assert_eq!(String::from_utf8(output).unwrap(), r##"digraph {
    graph [ pad = 0.2 ]
    node [ shape = "box" color = "#2a5b4f" penwidth = 2.25 fontname = "prompt medium" fontsize = 10 target = "_parent" margin = 0.22, ordering = out ];
    edge [ color = "#37474f" penwidth = 1 arrowhead = none target = "_parent" fontname = "roboto mono" fontsize = 10 ]

    splines = "ortho"

    "foo.cmx" [ label = "foo.cmx" color = "#8f3024" style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Afoo.cmx%7Cfoo.cmx&ss=fuchsia%2Ffuchsia" ]
    "bar.cmx" [ label = "bar.cmx" color = "#8f3024" style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Abar.cmx%7Cbar.cmx&ss=fuchsia%2Ffuchsia" ]
}
"##.to_string());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_graph_cml_only() {
        let component = component_for_test();

        let mut output = Vec::new();
        graph_impl(component, Some(ListFilter::CML), /* orientation */ None, &mut output)
            .await
            .unwrap();
        pretty_assertions::assert_eq!(String::from_utf8(output).unwrap(), r##"digraph {
    graph [ pad = 0.2 ]
    node [ shape = "box" color = "#2a5b4f" penwidth = 2.25 fontname = "prompt medium" fontsize = 10 target = "_parent" margin = 0.22, ordering = out ];
    edge [ color = "#37474f" penwidth = 1 arrowhead = none target = "_parent" fontname = "roboto mono" fontsize = 10 ]

    splines = "ortho"

    "/" [ label = "/"    ]
    "/appmgr" [ label = "appmgr"  style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Aappmgr.cml%7Cappmgr.cml&ss=fuchsia%2Ffuchsia" ]
    "/" -> "/appmgr"
    "/sys" [ label = "sys"   href = "https://cs.opensource.google/search?q=f%3Asys.cml%7Csys.cml&ss=fuchsia%2Ffuchsia" ]
    "/" -> "/sys"
    "/sys/baz" [ label = "baz"  style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Abaz.cml%7Cbaz.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys" -> "/sys/baz"
    "/sys/fuzz" [ label = "fuzz"   href = "https://cs.opensource.google/search?q=f%3Afuzz.cml%7Cfuzz.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys" -> "/sys/fuzz"
    "/sys/fuzz/hello" [ label = "hello"   href = "https://cs.opensource.google/search?q=f%3Ahello.cml%7Chello.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys/fuzz" -> "/sys/fuzz/hello"
}
"##.to_string());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_graph_running_only() {
        let component = component_for_test();

        let mut output = Vec::new();
        graph_impl(component, Some(ListFilter::Running), /* orientation */ None, &mut output)
            .await
            .unwrap();
        pretty_assertions::assert_eq!(String::from_utf8(output).unwrap(), r##"digraph {
    graph [ pad = 0.2 ]
    node [ shape = "box" color = "#2a5b4f" penwidth = 2.25 fontname = "prompt medium" fontsize = 10 target = "_parent" margin = 0.22, ordering = out ];
    edge [ color = "#37474f" penwidth = 1 arrowhead = none target = "_parent" fontname = "roboto mono" fontsize = 10 ]

    splines = "ortho"

    "appmgr" [ label = "appmgr"  style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Aappmgr.cml%7Cappmgr.cml&ss=fuchsia%2Ffuchsia" ]
    "appmgr/foo.cmx" [ label = "foo.cmx" color = "#8f3024" style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Afoo.cmx%7Cfoo.cmx&ss=fuchsia%2Ffuchsia" ]
    "appmgr" -> "appmgr/foo.cmx"
    "appmgr/bar.cmx" [ label = "bar.cmx" color = "#8f3024" style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Abar.cmx%7Cbar.cmx&ss=fuchsia%2Ffuchsia" ]
    "appmgr" -> "appmgr/bar.cmx"
    "baz" [ label = "baz"  style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Abaz.cml%7Cbaz.cml&ss=fuchsia%2Ffuchsia" ]
}
"##.to_string());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_graph_stopped_only() {
        let component = component_for_test();

        let mut output = Vec::new();
        graph_impl(component, Some(ListFilter::Stopped), /* orientation */ None, &mut output)
            .await
            .unwrap();
        pretty_assertions::assert_eq!(String::from_utf8(output).unwrap(), r##"digraph {
    graph [ pad = 0.2 ]
    node [ shape = "box" color = "#2a5b4f" penwidth = 2.25 fontname = "prompt medium" fontsize = 10 target = "_parent" margin = 0.22, ordering = out ];
    edge [ color = "#37474f" penwidth = 1 arrowhead = none target = "_parent" fontname = "roboto mono" fontsize = 10 ]

    splines = "ortho"

    "/" [ label = "/"    ]
    "/sys" [ label = "sys"   href = "https://cs.opensource.google/search?q=f%3Asys.cml%7Csys.cml&ss=fuchsia%2Ffuchsia" ]
    "/" -> "/sys"
    "/sys/fuzz" [ label = "fuzz"   href = "https://cs.opensource.google/search?q=f%3Afuzz.cml%7Cfuzz.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys" -> "/sys/fuzz"
    "/sys/fuzz/hello" [ label = "hello"   href = "https://cs.opensource.google/search?q=f%3Ahello.cml%7Chello.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys/fuzz" -> "/sys/fuzz/hello"
}
"##.to_string());
    }

    async fn test_graph_orientation(orientation: GraphOrientation, expected_rankdir: &str) {
        let component = component_for_test();

        let mut output = Vec::new();
        graph_impl(component, /* list_filter */ None, Some(orientation), &mut output)
            .await
            .unwrap();
        pretty_assertions::assert_eq!(
            String::from_utf8(output).unwrap(),
            format!(
                r##"digraph {{
    graph [ pad = 0.2 ]
    node [ shape = "box" color = "#2a5b4f" penwidth = 2.25 fontname = "prompt medium" fontsize = 10 target = "_parent" margin = 0.22, ordering = out ];
    edge [ color = "#37474f" penwidth = 1 arrowhead = none target = "_parent" fontname = "roboto mono" fontsize = 10 ]

    splines = "ortho"

    rankdir = "{}"
    "/" [ label = "/"    ]
    "/appmgr" [ label = "appmgr"  style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Aappmgr.cml%7Cappmgr.cml&ss=fuchsia%2Ffuchsia" ]
    "/" -> "/appmgr"
    "/appmgr/foo.cmx" [ label = "foo.cmx" color = "#8f3024" style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Afoo.cmx%7Cfoo.cmx&ss=fuchsia%2Ffuchsia" ]
    "/appmgr" -> "/appmgr/foo.cmx"
    "/appmgr/bar.cmx" [ label = "bar.cmx" color = "#8f3024" style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Abar.cmx%7Cbar.cmx&ss=fuchsia%2Ffuchsia" ]
    "/appmgr" -> "/appmgr/bar.cmx"
    "/sys" [ label = "sys"   href = "https://cs.opensource.google/search?q=f%3Asys.cml%7Csys.cml&ss=fuchsia%2Ffuchsia" ]
    "/" -> "/sys"
    "/sys/baz" [ label = "baz"  style = "filled" fontcolor = "#ffffff" href = "https://cs.opensource.google/search?q=f%3Abaz.cml%7Cbaz.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys" -> "/sys/baz"
    "/sys/fuzz" [ label = "fuzz"   href = "https://cs.opensource.google/search?q=f%3Afuzz.cml%7Cfuzz.cml&ss=fuchsia%2Ffuchsia" ]
    "/sys" -> "/sys/fuzz"
    "/sys/fuzz/hello" [ label = "hello"   href = "https://cs.opensource.google/search?q=f%3Ahello.cml%7Chello.cml&ss=fuchsia%2Ffuchsia" ]
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
