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
        pkg::repository::{Error, Repository, Resource},
        std::sync::Arc,
    };

    #[derive(Debug)]
    struct DummyRepository {
        name: String,
    }

    #[async_trait]
    impl Repository for DummyRepository {
        fn name(&self) -> &str {
            &self.name
        }

        async fn fetch(&self, _: &str) -> Result<Resource, Error> {
            unimplemented!()
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn list() {
        let mut out = Vec::<u8>::new();
        let manager = RepositoryManager::new();
        manager.add(Arc::new(DummyRepository { name: "Test1".to_owned() }));
        manager.add(Arc::new(DummyRepository { name: "Test2".to_owned() }));
        list_impl(&manager, &mut out).await.unwrap();

        assert_eq!(&String::from_utf8_lossy(&out), "Test1\nTest2\n");
    }
}
