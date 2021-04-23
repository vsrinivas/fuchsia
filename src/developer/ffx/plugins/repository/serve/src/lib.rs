// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_repository_serve_args::ServeCommand,
    log::info,
    pkg::repository::{FileSystemRepository, RepositoryManager, RepositoryServer},
    std::sync::Arc,
};

#[ffx_plugin()]
pub async fn serve(cmd: ServeCommand) -> Result<()> {
    let repo = FileSystemRepository::new(cmd.name, cmd.repo_path);
    let repo_manager = RepositoryManager::new();
    repo_manager.add(Arc::new(repo));

    let (task, server) =
        RepositoryServer::builder(cmd.listen_address, repo_manager).start().await?;

    info!("starting server on {}", server.local_addr());

    let () = task.await;

    Ok(())
}
