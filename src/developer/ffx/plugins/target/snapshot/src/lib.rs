// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Result},
    chrono::{Datelike, Local, Timelike},
    ffx_core::ffx_plugin,
    ffx_snapshot_args::SnapshotCommand,
    fidl_fuchsia_feedback::{
        Annotation, DataProviderProxy, GetAnnotationsParameters, GetSnapshotParameters,
    },
    fidl_fuchsia_io as fio,
    futures::stream::{FuturesOrdered, StreamExt},
    std::convert::{TryFrom, TryInto},
    std::fs,
    std::io::Write,
    std::path::{Path, PathBuf},
    std::time::Duration,
};

// read_data reads all of the contents of the given file from the current seek
// offset to end of file, returning the content. It errors if the seek pointer
// starts at an offset that results in reading less than the size of the file as
// reported on by the first request made by this function.
//
// The implementation attempts to maintain 8 concurrent in-flight requests so as
// to overcome the BDP that otherwise leads to a performance problem with a
// networked peer and only 8kb buffers in fuchsia.io.
pub async fn read_data(file: &fio::FileProxy) -> Result<Vec<u8>> {
    // Number of concurrent read operations to maintain (aim for a 128kb
    // in-flight buffer, divided by the fuchsia.io chunk size). On a short range
    // network, 64kb should be more than sufficient, but on an LFN such as a
    // work-from-home scenario, having some more space further optimizes
    // performance.
    const CONCURRENCY: u64 = 131072 / fio::MAX_BUF;

    let mut out = Vec::new();

    let (status, attrs) = file
        .get_attr()
        .await
        .context(format!("Error: Failed to get attributes of file (fidl failure)"))?;

    if status != 0 {
        bail!("Error: Failed to get attributes, status: {}", status);
    }

    let mut queue = FuturesOrdered::new();

    for _ in 0..CONCURRENCY {
        queue.push(file.read(fio::MAX_BUF));
    }

    loop {
        let mut bytes: Vec<u8> = queue
            .next()
            .await
            .context("read stream closed prematurely")??
            .map_err(|status: i32| anyhow!("read error: status={}", status))?;

        if bytes.is_empty() {
            break;
        }
        out.append(&mut bytes);

        while queue.len() < CONCURRENCY.try_into().unwrap() {
            queue.push(file.read(fio::MAX_BUF));
        }
    }

    if out.len() != usize::try_from(attrs.content_size).map_err(|e| anyhow!(e))? {
        bail!("Error: Expected {} bytes, but instead read {} bytes", attrs.content_size, out.len());
    }

    Ok(out)
}

// Build a multi-line string that represets the current annotation.
fn format_annotation(previous_key: &String, new_key: &String, new_value: &String) -> String {
    let mut output = String::from("");
    let old_key_vec: Vec<_> = previous_key.split(".").collect();
    let new_key_vec: Vec<_> = new_key.split(".").collect();

    let mut common_root = true;
    for idx in 0..new_key_vec.len() {
        // ignore shared key segments.
        if common_root && idx < old_key_vec.len() {
            if old_key_vec[idx] == new_key_vec[idx] {
                continue;
            }
        }
        common_root = false;

        // Build the formatted line from the key segment and append it to the output.
        let indentation: String = (0..idx).map(|_| "    ").collect();
        let end_of_key = new_key_vec.len() - 1 == idx;
        let line = match end_of_key {
            false => format!("{}{}\n", indentation, &new_key_vec[idx]),
            true => format!("{}{}: {}\n", indentation, &new_key_vec[idx], new_value),
        };
        output.push_str(&line);
    }

    output
}

fn format_annotations(mut annotations: Vec<Annotation>) -> String {
    let mut output = String::from("");

    // make sure annotations are sorted.
    annotations.sort_by(|a, b| a.key.cmp(&b.key));

    let mut previous_key = String::from("");
    for annotation in annotations {
        let segment = format_annotation(&previous_key, &annotation.key, &annotation.value);
        output.push_str(&segment);
        previous_key = annotation.key;
    }

    output
}

pub async fn dump_annotations<W: Write>(
    writer: &mut W,
    data_provider_proxy: DataProviderProxy,
) -> Result<()> {
    // Build parameters
    let params = GetAnnotationsParameters {
        collection_timeout_per_annotation: Some(
            i64::try_from(Duration::from_secs(5 * 60).as_nanos()).map_err(|e| anyhow!(e))?,
        ),
        ..GetAnnotationsParameters::EMPTY
    };

    // Request annotations.
    let annotations = data_provider_proxy
        .get_annotations(params)
        .await
        .map_err(|e| anyhow!("Could not get the annotations from the target: {:?}", e))?
        .annotations
        .ok_or(anyhow!("Received empty annotations."))?;

    writeln!(writer, "{}", format_annotations(annotations))?;

    Ok(())
}

#[ffx_plugin(DataProviderProxy = "core/feedback:expose:fuchsia.feedback.DataProvider")]
pub async fn snapshot(data_provider_proxy: DataProviderProxy, cmd: SnapshotCommand) -> Result<()> {
    snapshot_impl(data_provider_proxy, cmd, &mut std::io::stdout()).await
}

pub async fn snapshot_impl<W: Write>(
    data_provider_proxy: DataProviderProxy,
    cmd: SnapshotCommand,
    writer: &mut W,
) -> Result<()> {
    // Dump annotations doesn't capture the snapshot.
    if cmd.dump_annotations {
        dump_annotations(writer, data_provider_proxy).await?;
    } else {
        let output_dir = match cmd.output_file {
            None => {
                let dir = default_output_dir();
                fs::create_dir_all(&dir)?;
                dir
            }
            Some(file_dir) => {
                let dir = Path::new(&file_dir);
                if !dir.is_dir() {
                    bail!("ERROR: Path provided is not a directory");
                }
                dir.to_path_buf()
            }
        };

        // Make file proxy and channel for snapshot
        let (file_proxy, file_server_end) = fidl::endpoints::create_proxy::<fio::FileMarker>()?;

        // Build parameters
        let params = GetSnapshotParameters {
            collection_timeout_per_data: Some(
                i64::try_from(Duration::from_secs(5 * 60).as_nanos()).map_err(|e| anyhow!(e))?,
            ),
            response_channel: Some(file_server_end.into_channel()),
            ..GetSnapshotParameters::EMPTY
        };

        // Request snapshot & send channel.
        let _snapshot = data_provider_proxy
            .get_snapshot(params)
            .await
            .map_err(|e| anyhow!("Error: Could not get the snapshot from the target: {:?}", e))?;

        // Read archive
        let data = read_data(&file_proxy).await?;

        // Write archive to file.
        let file_path = output_dir.join("snapshot.zip");
        let mut file = fs::File::create(&file_path)?;
        file.write_all(&data)?;

        writeln!(writer, "Exported {}", file_path.to_string_lossy())?;
    }

    Ok(())
}

fn default_output_dir() -> PathBuf {
    let now = Local::now();

    Path::new("/tmp").join("snapshots").join(format!(
        "{}{:02}{:02}_{:02}{:02}{:02}",
        now.year(),
        now.month(),
        now.day(),
        now.hour(),
        now.minute(),
        now.second()
    ))
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_feedback::{Annotations, DataProviderRequest, Snapshot},
        futures::TryStreamExt,
    };

    fn serve_fake_file(server: ServerEnd<fio::FileMarker>) {
        fuchsia_async::Task::local(async move {
            let data: [u8; 3] = [1, 2, 3];
            let mut stream =
                server.into_stream().expect("converting fake file server proxy to stream");

            let mut cc: u32 = 0;
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    fio::FileRequest::Read { count: _, responder } => {
                        cc = cc + 1;
                        if cc == 1 {
                            responder
                                .send(&mut Ok(data.to_vec()))
                                .expect("writing file test response");
                        } else {
                            responder.send(&mut Ok(vec![])).expect("writing file test response");
                        }
                    }
                    fio::FileRequest::GetAttr { responder } => {
                        let mut attrs = fio::NodeAttributes {
                            mode: 0,
                            id: 0,
                            content_size: data.len() as u64,
                            storage_size: data.len() as u64,
                            link_count: 1,
                            creation_time: 0,
                            modification_time: 0,
                        };
                        responder.send(0, &mut attrs).expect("sending attributes");
                    }
                    e => panic!("not supported {:?}", e),
                }
            }
        })
        .detach();
    }

    macro_rules! annotation {
        ($val_1:expr, $val_2:expr) => {
            Annotation { key: $val_1.to_string(), value: $val_2.to_string() }
        };
    }

    fn setup_fake_data_provider_server(annotations: Annotations) -> DataProviderProxy {
        setup_fake_data_provider_proxy(move |req| match req {
            DataProviderRequest::GetSnapshot { params, responder } => {
                let channel = params.response_channel.unwrap();
                let server_end = ServerEnd::<fio::FileMarker>::new(channel);

                serve_fake_file(server_end);

                let snapshot = Snapshot { ..Snapshot::EMPTY };
                responder.send(snapshot).unwrap();
            }
            DataProviderRequest::GetAnnotations { params, responder } => {
                let _ignore = params;
                responder.send(annotations.clone()).unwrap();
            }
            _ => assert!(false),
        })
    }

    async fn run_snapshot_test(cmd: SnapshotCommand) {
        let annotations = Annotations { ..Annotations::EMPTY };
        let data_provider_proxy = setup_fake_data_provider_server(annotations);

        let mut writer = Vec::new();
        let result = snapshot_impl(data_provider_proxy, cmd, &mut writer).await;
        assert!(result.is_ok());

        let output = String::from_utf8(writer).unwrap();
        assert!(output.starts_with("Exported"));
        assert!(output.ends_with("snapshot.zip\n"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_error() -> Result<()> {
        run_snapshot_test(SnapshotCommand { output_file: None, dump_annotations: false }).await;
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_annotations() -> Result<()> {
        let annotation_vec: Vec<Annotation> = vec![
            annotation!("build.board", "x64"),
            annotation!("hardware.board.name", "default-board"),
            annotation!("build.is_debug", "false"),
        ];
        let annotations = Annotations { annotations: Some(annotation_vec), ..Annotations::EMPTY };
        let data_provider_proxy = setup_fake_data_provider_server(annotations);

        let mut writer = Vec::new();
        dump_annotations(&mut writer, data_provider_proxy).await?;

        let output = String::from_utf8(writer).unwrap();
        assert_eq!(
            output,
            "build\n\
        \x20   board: x64\n\
        \x20   is_debug: false\n\
        hardware\n\
        \x20   board\n\
        \x20       name: default-board\n\n"
        );
        Ok(())
    }
}
