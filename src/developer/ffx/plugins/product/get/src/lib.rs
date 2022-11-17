// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A tool to:
//! - acquire and display product bundle information (metadata)
//! - acquire related data files, such as disk partition images (data)

use {
    ::gcs::client::{ProgressResponse, ProgressState},
    anyhow::{Context, Result},
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_product_get_args::GetCommand,
    pbms::{make_way_for_output, transfer_download},
    std::io::{stderr, stdin, stdout},
    structured_ui,
};

/// `ffx product get` sub-command.
#[ffx_plugin("product.experimental")]
pub async fn pb_get(cmd: GetCommand) -> Result<()> {
    let mut input = stdin();
    let mut output = stdout();
    let mut err_out = stderr();
    let mut ui = structured_ui::TextUi::new(&mut input, &mut output, &mut err_out);
    pb_get_impl(&cmd, &mut ui).await
}

async fn pb_get_impl<I: structured_ui::Interface + Sync>(
    cmd: &GetCommand,
    ui: &mut I,
) -> Result<()> {
    let start = std::time::Instant::now();
    tracing::info!("---------------------- Begin ----------------------------");
    tracing::debug!("transfer_manifest_url Url::parse");
    let transfer_manifest_url = match url::Url::parse(&cmd.transfer_manifest_url) {
        Ok(p) => p,
        _ => ffx_bail!(
            "The source location must be a URL, failed to parse {:?}",
            cmd.transfer_manifest_url
        ),
    };
    let local_dir = &cmd.out_dir;
    make_way_for_output(&local_dir, cmd.force).await.context("make_way_for_output")?;

    tracing::debug!("transfer_manifest, transfer_manifest_url {:?}", transfer_manifest_url);
    transfer_download(
        &transfer_manifest_url,
        local_dir,
        cmd.auth,
        &|layers| {
            let mut progress = structured_ui::Progress::builder();
            progress.title("Transfer download");
            progress.entry("Transfer manifest", /*at=*/ 1, /*of=*/ 2, "steps");
            for layer in layers {
                progress.entry(&layer.name, layer.at, layer.of, layer.units);
            }
            ui.present(&structured_ui::Presentation::Progress(progress))?;
            Ok(ProgressResponse::Continue)
        },
        ui,
    )
    .await
    .context("downloading via transfer manifest")?;

    let layers = vec![ProgressState { name: "complete", at: 2, of: 2, units: "steps" }];
    let mut progress = structured_ui::Progress::builder();
    progress.title("Transfer download");
    for layer in layers {
        progress.entry(&layer.name, layer.at, layer.of, layer.units);
    }
    ui.present(&structured_ui::Presentation::Progress(progress))?;

    tracing::debug!(
        "Total fx product-bundle get runtime {} seconds.",
        start.elapsed().as_secs_f32()
    );
    tracing::debug!("End");
    Ok(())
}

#[cfg(test)]
mod test {
    use {super::*, tempfile};

    #[should_panic(expected = "downloading via transfer manifest")]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_pb_get_impl() {
        let test_dir = tempfile::TempDir::new().expect("temp dir");
        let cmd = GetCommand {
            auth: pbms::AuthFlowChoice::Default,
            force: false,
            out_dir: test_dir.path().to_path_buf(),
            repository: None,
            transfer_manifest_url: "gs://example/fake/transfer.json".to_string(),
        };
        let mut ui = structured_ui::MockUi::new();
        pb_get_impl(&cmd, &mut ui).await.expect("testing get");
    }
}
