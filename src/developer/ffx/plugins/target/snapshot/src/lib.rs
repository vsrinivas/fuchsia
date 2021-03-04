// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Result},
    chrono::{Datelike, Timelike, Utc},
    ffx_core::ffx_plugin,
    ffx_snapshot_args::SnapshotCommand,
    fidl_fuchsia_feedback::{DataProviderProxy, GetSnapshotParameters},
    fidl_fuchsia_io::{FileMarker, MAX_BUF},
    std::convert::TryFrom,
    std::fs,
    std::io::prelude::*,
    std::path::{Path, PathBuf},
    std::time::Duration,
};

pub async fn read_data(file: &fidl_fuchsia_io::FileProxy) -> Result<Vec<u8>> {
    let mut out = Vec::new();

    let (status, attrs) = file
        .get_attr()
        .await
        .context(format!("Error: Failed to get attributes of file (fidl failure)"))?;

    if status != 0 {
        bail!("Error: Failed to get attributes, status: {}", status);
    }

    loop {
        let (status, mut bytes) = file.read(MAX_BUF).await?;

        if status != 0 {
            bail!("Error: Failed to get data, status: {}", status);
        }

        if bytes.is_empty() {
            break;
        }
        out.append(&mut bytes);
    }

    if out.len() != usize::try_from(attrs.content_size).map_err(|e| anyhow!(e))? {
        bail!("Error: Expected {} bytes, but instead read {} bytes", attrs.content_size, out.len());
    }

    Ok(out)
}

#[ffx_plugin(DataProviderProxy = "core/appmgr:out:fuchsia.feedback.DataProvider")]
pub async fn snapshot(data_provider_proxy: DataProviderProxy, cmd: SnapshotCommand) -> Result<()> {
    // Parse CLI args.
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
    let (file_proxy, file_server_end) = fidl::endpoints::create_proxy::<FileMarker>()?;

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

    println!("Exported {}", file_path.to_string_lossy());
    Ok(())
}

fn default_output_dir() -> PathBuf {
    let now = Utc::now();

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
        fidl_fuchsia_feedback::{DataProviderRequest, Snapshot},
        fidl_fuchsia_io::{FileRequest, NodeAttributes},
        futures::TryStreamExt,
    };

    fn serve_fake_file(server: ServerEnd<FileMarker>) {
        fuchsia_async::Task::local(async move {
            let data: [u8; 3] = [1, 2, 3];
            let mut stream =
                server.into_stream().expect("converting fake file server proxy to stream");

            let mut cc: u32 = 0;
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    FileRequest::Read { count: _, responder } => {
                        cc = cc + 1;
                        if cc == 1 {
                            responder
                                .send(/*Status*/ 0, &data)
                                .expect("writing file test response");
                        } else {
                            responder.send(/*Status*/ 0, &[]).expect("writing file test response");
                        }
                    }
                    FileRequest::GetAttr { responder } => {
                        let mut attrs = NodeAttributes {
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

    fn setup_fake_data_provider_server() -> DataProviderProxy {
        setup_fake_data_provider_proxy(move |req| match req {
            DataProviderRequest::GetSnapshot { params, responder } => {
                let channel = params.response_channel.unwrap();
                let server_end = ServerEnd::<FileMarker>::new(channel);

                serve_fake_file(server_end);

                let snapshot = Snapshot { ..Snapshot::EMPTY };
                responder.send(snapshot).unwrap();
            }
            _ => assert!(false),
        })
    }

    async fn run_snapshot_test(cmd: SnapshotCommand) {
        let data_provider_proxy = setup_fake_data_provider_server();

        let result = snapshot(data_provider_proxy, cmd).await.unwrap();
        assert_eq!(result, ());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_error() -> Result<()> {
        run_snapshot_test(SnapshotCommand { output_file: None }).await;
        Ok(())
    }
}
