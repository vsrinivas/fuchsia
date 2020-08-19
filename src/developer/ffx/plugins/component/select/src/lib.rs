// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    ffx_core::ffx_plugin,
    ffx_select_args::SelectCommand,
    fidl_fuchsia_developer_remotecontrol as rc, selectors,
    std::io::{stdout, Write},
};

pub const SELECTOR_FORMAT_HELP: &str = "Selector format: <component moniker>:(in|out|exposed)[:<service name>]. Wildcards may be used anywhere in the selector.
Example: 'remote-control:out:*' would return all services in 'out' for the component remote-control.

Note that moniker wildcards are not recursive: 'a/*/c' will only match components named 'c' running in some sub-realm directly below 'a', and no further.";

#[ffx_plugin()]
pub async fn select_cmd(remote_proxy: rc::RemoteControlProxy, cmd: SelectCommand) -> Result<()> {
    let writer = Box::new(stdout());
    select(remote_proxy, writer, &cmd.selector).await
}

async fn select<W: Write>(
    remote_proxy: rc::RemoteControlProxy,
    mut write: W,
    selector: &str,
) -> Result<()> {
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
    use {super::*, std::io::BufWriter};

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
            _ => assert!(false, format!("got unexpected request: {:?}", req)),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_select_invalid_selector() -> Result<()> {
        let mut output = String::new();
        let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
        let remote_proxy = setup_fake_remote_server();
        let _response = select(remote_proxy, writer, "a:b:").await.expect("select should not fail");
        assert!(output.contains(SELECTOR_FORMAT_HELP));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_select_formats_rcs_response() -> Result<()> {
        let mut output = String::new();
        let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
        let remote_proxy = setup_fake_remote_server();
        let _response =
            select(remote_proxy, writer, "a:valid:selector").await.expect("select should not fail");
        assert_eq!(output, DEFAULT_MATCH_STR);
        Ok(())
    }

    #[test]
    fn test_format_matches_complex() -> Result<()> {
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
