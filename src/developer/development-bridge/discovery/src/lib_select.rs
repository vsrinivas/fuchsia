// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    ffx_core::ffx_plugin,
    ffx_select_args::SelectCommand,
    fidl_fuchsia_developer_remotecontrol::{RemoteControlProxy, ServiceMatch},
    selectors,
    std::io::{stdout, Write},
};

pub const SELECTOR_FORMAT_HELP: &str = "Selector format: <component moniker>:(in|out|exposed)[:<service name>]. Wildcards may be used anywhere in the selector.
Example: 'remote-control:out:*' would return all services in 'out' for the component remote-control.

Note that moniker wildcards are not recursive: 'a/*/c' will only match components named 'c' running in some sub-realm directly below 'a', and no further.";

#[ffx_plugin()]
pub async fn select_cmd(remote_proxy: RemoteControlProxy, cmd: SelectCommand) -> Result<(), Error> {
    let writer = Box::new(stdout());
    select(remote_proxy, writer, &cmd.selector).await
}

async fn select<W: Write>(
    remote_proxy: RemoteControlProxy,
    mut write: W,
    selector: &str,
) -> Result<(), Error> {
    let writer = &mut write;
    let selector = match selectors::parse_selector(selector) {
        Ok(s) => s,
        Err(e) => {
            writeln!(writer, "Failed to parse the provided selector: {:?}", e)?;
            writeln!(writer, "{}", SELECTOR_FORMAT_HELP)?;
            return Ok(());
        }
    };

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

fn format_subdir<W: Write>(writer: &mut W, subdir: &str) -> Result<(), Error> {
    writeln!(writer, "|")?;
    writeln!(writer, "--{}", subdir)?;
    writeln!(writer, "   |")?;
    Ok(())
}

fn format_service<W: Write>(writer: &mut W, service: &str) -> Result<(), Error> {
    writeln!(writer, "   --{}", service)?;
    Ok(())
}

fn format_matches<W: Write>(writer: &mut W, matches: Vec<ServiceMatch>) -> Result<(), Error> {
    let mut sorted_paths = matches.iter().collect::<Vec<&ServiceMatch>>();
    sorted_paths.sort();

    let mut prev_opt: Option<&ServiceMatch> = None;

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
    use {
        super::*,
        fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlRequest},
        futures::TryStreamExt,
        std::io::BufWriter,
    };

    const DEFAULT_MATCH_STR: &str = "\
core/test
|
--out
   |
   --fuchsia.myservice\n";

    fn setup_fake_remote_server() -> RemoteControlProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();
        hoist::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    RemoteControlRequest::Select { selector: _, responder } => {
                        let _ = responder
                            .send(&mut Ok(vec![ServiceMatch {
                                moniker: vec![String::from("core"), String::from("test")],
                                subdir: String::from("out"),
                                service: String::from("fuchsia.myservice"),
                            }]))
                            .unwrap();
                    }
                    _ => assert!(false, format!("got unexpected request: {:?}", req)),
                }
            }
        });

        proxy
    }

    #[test]
    fn test_select_invalid_selector() -> Result<(), Error> {
        let mut output = String::new();
        hoist::run(async move {
            let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            let remote_proxy = setup_fake_remote_server();
            let _response =
                select(remote_proxy, writer, "a:b:").await.expect("select should not fail");
            assert!(output.contains(SELECTOR_FORMAT_HELP));
        });
        Ok(())
    }

    #[test]
    fn test_select_formats_rcs_response() -> Result<(), Error> {
        let mut output = String::new();
        hoist::run(async move {
            let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            let remote_proxy = setup_fake_remote_server();
            let _response = select(remote_proxy, writer, "a:valid:selector")
                .await
                .expect("select should not fail");
            assert_eq!(output, DEFAULT_MATCH_STR)
        });
        Ok(())
    }

    #[test]
    fn test_format_matches_complex() -> Result<(), Error> {
        let mut output = String::new();

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

        {
            let mut writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            format_matches(
                &mut writer,
                vec![
                    ServiceMatch {
                        moniker: vec![String::from("core"), String::from("test")],
                        subdir: String::from("out"),
                        service: String::from("fuchsia.myservice3"),
                    },
                    ServiceMatch {
                        moniker: vec![String::from("core"), String::from("test")],
                        subdir: String::from("in"),
                        service: String::from("fuchsia.myservice"),
                    },
                    ServiceMatch {
                        moniker: vec![String::from("core"), String::from("test")],
                        subdir: String::from("in"),
                        service: String::from("fuchsia.myservice2"),
                    },
                    ServiceMatch {
                        moniker: vec![String::from("test")],
                        subdir: String::from("out"),
                        service: String::from("fuchsia.myservice5"),
                    },
                    ServiceMatch {
                        moniker: vec![String::from("test")],
                        subdir: String::from("in"),
                        service: String::from("fuchsia.myservice4"),
                    },
                ],
            )
            .unwrap();
        }

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
