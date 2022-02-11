// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    component_hub::{io::Directory, select::find_components, select::MatchingComponents},
    errors::ffx_error,
    ffx_component::SELECTOR_FORMAT_HELP,
    ffx_component_select_args::{
        CapabilityStruct, ComponentSelectCommand, MonikerStruct, SubcommandEnum,
    },
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_io as fio,
    fuchsia_zircon_status::Status,
    selectors::{self, VerboseError},
    std::io::{stdout, Write},
};

#[ffx_plugin()]
pub async fn select_cmd(
    remote_proxy: rc::RemoteControlProxy,
    cmd: ComponentSelectCommand,
) -> Result<()> {
    let writer = Box::new(stdout());
    match &cmd.nested {
        SubcommandEnum::Capability(CapabilityStruct { capability: c }) => {
            select_capability(remote_proxy, c).await
        }
        SubcommandEnum::Moniker(MonikerStruct { moniker: m }) => {
            select_moniker(remote_proxy, writer, m).await
        }
    }
}

// TODO (72749): Allow reverse lookup for incoming/outgoing capabilities.
async fn select_capability(remote_proxy: rc::RemoteControlProxy, capability: &str) -> Result<()> {
    let (root, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .context("creating hub root proxy")?;
    remote_proxy
        .open_hub(dir_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening hub")?;
    let hub_dir = Directory::from_proxy(root);
    let MatchingComponents { exposed, used } =
        find_components(capability.to_string(), hub_dir).await?;

    if !exposed.is_empty() {
        println!("Exposed:");
        for component in exposed {
            println!("  {}", component);
        }
    }
    if !used.is_empty() {
        println!("Used:");
        for component in used {
            println!("  {}", component);
        }
    }

    Ok(())
}

async fn select_moniker<W: Write>(
    remote_proxy: rc::RemoteControlProxy,
    mut write: W,
    selector: &str,
) -> Result<()> {
    let writer = &mut write;
    let selector = selectors::parse_selector::<VerboseError>(selector).map_err(|e| {
        ffx_error!("Invalid selector '{}': {}\n{}", selector, e, SELECTOR_FORMAT_HELP)
    })?;

    match remote_proxy.select(selector).await.context("awaiting select call")? {
        Ok(paths) => {
            if paths.is_empty() {
                writeln!(writer, "No matching paths.")?;
                return Ok(());
            }
            format_matches(writer, paths)
        }
        Err(e) => {
            eprintln!("Failed to execute selector: {:?}", e);
            Ok(())
        }
    }
}

fn format_subdir<W: Write>(writer: &mut W, subdir: &str) -> Result<()> {
    writeln!(writer, "|")?;
    writeln!(writer, "--{}", subdir)?;
    writeln!(writer, "   |")?;
    Ok(())
}

fn format_service<W: Write>(writer: &mut W, service: &str) -> Result<()> {
    writeln!(writer, "   --{}", service)?;
    Ok(())
}

fn format_matches<W: Write>(writer: &mut W, matches: Vec<rc::ServiceMatch>) -> Result<()> {
    let mut sorted_paths = matches.iter().collect::<Vec<&rc::ServiceMatch>>();
    sorted_paths.sort();

    let mut prev_opt: Option<&rc::ServiceMatch> = None;

    for m in sorted_paths.iter() {
        if let Some(prev) = prev_opt {
            if prev.moniker == m.moniker {
                if prev.subdir == m.subdir {
                    format_service(writer, &m.service)?;
                } else {
                    format_subdir(writer, &m.subdir)?;
                    format_service(writer, &m.service)?;
                }
                prev_opt = Some(m);
                continue;
            } else {
                write!(writer, "\n")?;
            }
        }

        writeln!(writer, "{}", m.moniker.join("/"))?;
        format_subdir(writer, &m.subdir)?;
        format_service(writer, &m.service)?;
        prev_opt = Some(m);
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;

    const DEFAULT_MATCH_STR: &str = "\
core/test
|
--out
   |
   --fuchsia.myservice\n";

    fn setup_fake_remote_server() -> rc::RemoteControlProxy {
        setup_fake_remote_proxy(|req| match req {
            rc::RemoteControlRequest::Select { selector: _, responder } => {
                let _ = responder
                    .send(&mut Ok(vec![rc::ServiceMatch {
                        moniker: vec![String::from("core"), String::from("test")],
                        subdir: String::from("out"),
                        service: String::from("fuchsia.myservice"),
                    }]))
                    .unwrap();
            }
            _ => assert!(false, "got unexpected request: {:?}", req),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_select_invalid_selector() -> Result<()> {
        let mut output = Vec::new();
        let remote_proxy = setup_fake_remote_server();
        let response = select_moniker(remote_proxy, &mut output, "a:b:").await;
        let e = response.unwrap_err();
        assert!(e.to_string().contains(SELECTOR_FORMAT_HELP));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_select_formats_rcs_response() -> Result<()> {
        let mut output = Vec::new();
        let remote_proxy = setup_fake_remote_server();
        let _response = select_moniker(remote_proxy, &mut output, "a:valid:selector")
            .await
            .expect("select should not fail");
        let output = String::from_utf8(output).expect("Invalid UTF-8 bytes");
        assert_eq!(output, DEFAULT_MATCH_STR);
        Ok(())
    }

    #[test]
    fn test_format_matches_complex() -> Result<()> {
        let expected = "\
core/test
|
--in
   |
   --fuchsia.myservice
   --fuchsia.myservice2
|
--out
   |
   --fuchsia.myservice3

test
|
--in
   |
   --fuchsia.myservice4
|
--out
   |
   --fuchsia.myservice5\n";

        let mut output_utf8 = Vec::new();
        format_matches(
            &mut output_utf8,
            vec![
                rc::ServiceMatch {
                    moniker: vec![String::from("core"), String::from("test")],
                    subdir: String::from("out"),
                    service: String::from("fuchsia.myservice3"),
                },
                rc::ServiceMatch {
                    moniker: vec![String::from("core"), String::from("test")],
                    subdir: String::from("in"),
                    service: String::from("fuchsia.myservice"),
                },
                rc::ServiceMatch {
                    moniker: vec![String::from("core"), String::from("test")],
                    subdir: String::from("in"),
                    service: String::from("fuchsia.myservice2"),
                },
                rc::ServiceMatch {
                    moniker: vec![String::from("test")],
                    subdir: String::from("out"),
                    service: String::from("fuchsia.myservice5"),
                },
                rc::ServiceMatch {
                    moniker: vec![String::from("test")],
                    subdir: String::from("in"),
                    service: String::from("fuchsia.myservice4"),
                },
            ],
        )
        .unwrap();

        let output = String::from_utf8(output_utf8).expect("Invalid UTF-8 bytes");

        // assert won't expand newlines, which makes failures hard to read.
        if output != expected {
            println!("actual output: ");
            println!("{}", output);
            println!("expected: ");
            println!("{}", expected);
            assert_eq!(output, expected);
        }

        Ok(())
    }
}
