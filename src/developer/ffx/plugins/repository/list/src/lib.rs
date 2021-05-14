// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_repository_list_args::ListCommand,
    pkg::repository::RepositoryManager,
    std::io::{stdout, Write},
};

#[ffx_plugin()]
pub async fn list(_cmd: ListCommand) -> Result<()> {
    list_impl(&RepositoryManager::new(), stdout()).await
}

async fn list_impl<W: Write>(manager: &RepositoryManager, mut writer: W) -> Result<()> {
    for repo in manager.repositories() {
        writeln!(writer, "{}", repo.name())?;
    }

    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use {
        async_trait::async_trait,
        fuchsia_async as fasync,
        pkg::repository::{Error, Repository, RepositoryBackend, RepositoryMetadata, Resource},
        std::sync::Arc,
        tuf::{interchange::Json, repository::RepositoryProvider},
    };

    #[derive(Debug)]
    struct DummyBackend {}

    #[async_trait]
    impl RepositoryBackend for DummyBackend {
        async fn fetch(&self, _: &str) -> Result<Resource, Error> {
            unimplemented!()
        }

        fn get_tuf_repo(&self) -> Result<Box<dyn RepositoryProvider<Json>>, Error> {
            unimplemented!();
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn list() {
        let metadata = RepositoryMetadata::new(vec![], 1);

        let mut out = Vec::<u8>::new();
        let manager = RepositoryManager::new();
        manager.add(Arc::new(Repository::new_with_metadata(
            "Test1",
            Box::new(DummyBackend {}),
            metadata.clone(),
        )));
        manager.add(Arc::new(Repository::new_with_metadata(
            "Test2",
            Box::new(DummyBackend {}),
            metadata,
        )));
        list_impl(&manager, &mut out).await.unwrap();

        assert_eq!(&String::from_utf8_lossy(&out), "Test1\nTest2\n");
    }
}
