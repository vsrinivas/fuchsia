// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{story_graph::StoryGraph, utils},
    failure::{format_err, Error, ResultExt},
    fidl::encoding::OutOfLine,
    fidl_fuchsia_ledger::{
        Entry, Error as LedgerError, LedgerMarker, PageMarker, PageProxy, PageSnapshotMarker,
        Priority, Token,
    },
    fidl_fuchsia_mem::Buffer,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::macros::*,
    fuchsia_zircon as zx,
    futures::future::LocalFutureObj,
    std::collections::HashMap,
    std::str::from_utf8,
};

pub type StoryName = String;
pub type StoryTitle = String;
type LedgerKey = String;
type LedgerValue = String;
type LedgerKeyValueEntry = (LedgerKey, LedgerValue);
const TITLE_PREFIX: &str = "title/";
const GRAPH_PREFIX: &str = "graph/";

pub trait StoryStorage: Send + Sync {
    // save a story graph with its name to storage
    fn insert_graph<'a>(
        &'a mut self,
        story_name: StoryName,
        story_graph: StoryGraph,
    ) -> LocalFutureObj<'a, Result<(), Error>>;

    // load a story graph from storage according to its name
    fn get_graph<'a>(&'a self, story_name: &'a str)
        -> LocalFutureObj<'a, Option<StoryGraph>>;

    // return the number of saved stories
    fn get_story_count<'a>(&'a self) -> LocalFutureObj<'a, Result<usize, Error>>;

    // save the name and title of a story
    fn insert_name_title<'a>(
        &'a mut self,
        story_name: StoryName,
        story_title: StoryTitle,
    ) -> LocalFutureObj<'a, Result<(), Error>>;

    // return names and stories of all stories
    fn get_name_titles<'a>(
        &'a self,
    ) -> LocalFutureObj<'a, Result<Vec<(StoryName, StoryTitle)>, Error>>;

    // clear the storage
    fn clear<'a>(&'a mut self) -> LocalFutureObj<'a, Result<(), Error>>;
}

pub struct MemoryStorage {
    graph: HashMap<StoryName, StoryGraph>,
    story_name_index: HashMap<StoryName, StoryTitle>,
}

impl StoryStorage for MemoryStorage {
    fn insert_graph<'a>(
        &'a mut self,
        story_name: StoryName,
        story_graph: StoryGraph,
    ) -> LocalFutureObj<'a, Result<(), Error>> {
        LocalFutureObj::new(Box::new(async move {
            self.graph.insert(story_name, story_graph);
            Ok(())
        }))
    }

    fn get_graph<'a>(
        &'a self,
        story_name: &'a str,
    ) -> LocalFutureObj<'a, Option<StoryGraph>> {
        LocalFutureObj::new(Box::new(async move {
            if self.graph.contains_key(story_name) {
                Some(self.graph[story_name].clone())
            } else {
                None
            }
        }))
    }

    fn get_story_count<'a>(&'a self) -> LocalFutureObj<'a, Result<usize, Error>> {
        LocalFutureObj::new(Box::new(async move { Ok(self.graph.len()) }))
    }

    fn insert_name_title<'a>(
        &'a mut self,
        story_name: StoryName,
        story_title: StoryTitle,
    ) -> LocalFutureObj<'a, Result<(), Error>> {
        LocalFutureObj::new(Box::new(async move {
            self.story_name_index.insert(story_name, story_title);
            Ok(())
        }))
    }

    fn get_name_titles<'a>(
        &'a self,
    ) -> LocalFutureObj<'a, Result<Vec<(StoryName, StoryTitle)>, Error>> {
        LocalFutureObj::new(Box::new(async move {
            Ok(self.story_name_index.clone().into_iter().collect())
        }))
    }

    fn clear<'a>(&'a mut self) -> LocalFutureObj<'a, Result<(), Error>> {
        LocalFutureObj::new(Box::new(async move {
            self.story_name_index.clear();
            self.graph.clear();
            Ok(())
        }))
    }
}

impl MemoryStorage {
    pub fn new() -> Self {
        MemoryStorage { graph: HashMap::new(), story_name_index: HashMap::new() }
    }
}

pub struct LedgerStorage {
    page: PageProxy,
}

impl LedgerStorage {
    pub fn new() -> Result<Self, Error> {
        let ledger =
            connect_to_service::<LedgerMarker>().context("[ledger] failed to connect to ledger")?;
        let (page, server_end) = fidl::endpoints::create_proxy::<PageMarker>()
            .context("[ledger] failed to create page proxy")?;
        ledger.get_root_page(server_end)?;
        Ok(LedgerStorage { page })
    }

    async fn write(&self, key: &str, value: &str) -> Result<(), Error> {
        let data_to_write = value.as_bytes();
        let vmo = zx::Vmo::create(data_to_write.len() as u64)?;
        vmo.write(&data_to_write, 0)?;
        let data_ref = self
            .page
            .create_reference_from_buffer(&mut Buffer { vmo, size: data_to_write.len() as u64 })
            .await?;
        match data_ref {
            Ok(mut data) => {
                self.page.put_reference(&mut key.bytes(), &mut data, Priority::Eager)?;
                Ok(())
            }
            Err(e) => {
                fx_log_err!("Unable to create data reference with error code: {}", e);
                Err(format_err!("Unable to create data reference with error code: {}", e))
            }
        }
    }

    async fn read(&self, prefix: &str, key: &str) -> Result<Option<String>, Error> {
        let (snapshot, server_end) = fidl::endpoints::create_proxy::<PageSnapshotMarker>()?;
        self.page.get_snapshot(server_end, &mut prefix.bytes(), None)?;
        let entry = snapshot.get(&mut key.bytes()).await?;
        entry.and_then(|e| Ok(utils::vmo_buffer_to_string(Box::new(e)).ok())).or_else(|e| match e {
            LedgerError::KeyNotFound => Ok(None), // handle the not-found error
            _ => Err(format_err!("Unable to read with error code: {:?}", e)),
        })
    }

    async fn read_entries(&self, prefix: &str) -> Result<Vec<LedgerKeyValueEntry>, Error> {
        let (snapshot, server_end) = fidl::endpoints::create_proxy::<PageSnapshotMarker>()?;
        self.page.get_snapshot(server_end, &mut prefix.bytes(), None)?;
        let mut results = snapshot.get_entries(&mut "".bytes(), None).await?;
        let mut entries: Vec<Entry> = vec![];
        entries.append(&mut results.0);
        let mut token: Option<Token> = results.1.and_then(|boxed_token| Some(*boxed_token));
        while let Some(mut unwrap_token) = token.take()
        // keep reading until token is none
        {
            results =
                snapshot.get_entries(&mut "".bytes(), Some(OutOfLine(&mut unwrap_token))).await?;
            entries.append(&mut results.0);
            token = results.1.and_then(|boxed_token| Some(*boxed_token));
        }
        Ok(entries
            .into_iter()
            .filter_map(|e| {
                from_utf8(&e.key.clone()).ok().and_then(|k| {
                    e.value
                        .and_then(|buf| utils::vmo_buffer_to_string(buf).ok())
                        .and_then(|v| Some((k.to_string(), v)))
                })
            })
            .collect())
    }

    async fn read_keys(&self, prefix: &str) -> Result<Vec<LedgerKey>, Error> {
        let (snapshot, server_end) = fidl::endpoints::create_proxy::<PageSnapshotMarker>()?;
        self.page.get_snapshot(server_end, &mut prefix.bytes(), None)?;
        let mut results = snapshot.get_keys(&mut "".bytes(), None).await?;
        let mut keys = vec![];
        keys.append(&mut results.0);
        let mut token: Option<Token> = results.1.and_then(|boxed_token| Some(*boxed_token));
        while let Some(mut unwrap_token) = token.take()
        // keep reading until token is none
        {
            results =
                snapshot.get_keys(&mut "".bytes(), Some(OutOfLine(&mut unwrap_token))).await?;
            keys.append(&mut results.0);
            token = results.1.and_then(|boxed_token| Some(*boxed_token));
        }
        Ok(keys.into_iter().filter_map(|e| from_utf8(&e).map(|k| k.to_string()).ok()).collect())
    }
}

impl StoryStorage for LedgerStorage {
    fn insert_graph<'a>(
        &'a mut self,
        story_name: StoryName,
        story_graph: StoryGraph,
    ) -> LocalFutureObj<'a, Result<(), Error>> {
        LocalFutureObj::new(Box::new(async move {
            let key = format!("{}{}", GRAPH_PREFIX, story_name);
            let value = serde_json::to_string(&story_graph)?;
            self.write(&key, &value).await?;
            Ok(())
        }))
    }

    fn get_graph<'a>(
        &'a self,
        story_name: &'a str,
    ) -> LocalFutureObj<'a, Option<StoryGraph>> {
        LocalFutureObj::new(Box::new(async move {
            let key = format!("{}{}", GRAPH_PREFIX, story_name);
            self.read(&key, &key).await.ok().and_then(|optional_graph_string| {
                optional_graph_string
                    .and_then(|graph_string| serde_json::from_str(&graph_string).ok())
            })
        }))
    }

    fn get_story_count<'a>(&'a self) -> LocalFutureObj<'a, Result<usize, Error>> {
        LocalFutureObj::new(Box::new(async move { Ok(self.read_keys(TITLE_PREFIX).await?.len()) }))
    }

    fn insert_name_title<'a>(
        &'a mut self,
        story_name: StoryName,
        story_title: StoryTitle,
    ) -> LocalFutureObj<'a, Result<(), Error>> {
        LocalFutureObj::new(Box::new(async move {
            let key = format!("{}{}", TITLE_PREFIX, story_name);
            let value = story_title.as_str();
            self.write(&key, value).await?;
            Ok(())
        }))
    }

    fn get_name_titles<'a>(
        &'a self,
    ) -> LocalFutureObj<'a, Result<Vec<(StoryName, StoryTitle)>, Error>> {
        LocalFutureObj::new(Box::new(async move {
            let entries = self.read_entries(TITLE_PREFIX).await?;
            let results = entries
                .into_iter()
                .map(|(name, title)| (name.split_at(TITLE_PREFIX.len()).1.to_string(), title))
                .collect();
            Ok(results)
        }))
    }

    fn clear<'a>(&'a mut self) -> LocalFutureObj<'a, Result<(), Error>> {
        LocalFutureObj::new(Box::new(async move {
            self.page.clear()?;
            Ok(())
        }))
    }
}

#[cfg(test)]
mod tests {
    use {super::*, failure::Error, fuchsia_async as fasync};

    #[fasync::run_singlethreaded(test)]
    async fn memory_storage() -> Result<(), Error> {
        let mut memory_storage = MemoryStorage::new();
        memory_storage
            .insert_name_title("story_name".to_string(), "story_title".to_string())
            .await?;
        memory_storage.insert_graph("story_name".to_string(), StoryGraph::new()).await?;

        let mut name_titles = memory_storage.get_name_titles().await?;
        assert_eq!(name_titles.len(), 1);
        assert_eq!(memory_storage.get_story_count().await?, 1);
        assert_eq!(name_titles[0].1, "story_title".to_string());

        // update the story title of saved story
        memory_storage
            .insert_name_title("story_name".to_string(), "story_title_new".to_string())
            .await?;
        name_titles = memory_storage.get_name_titles().await?;
        assert_eq!(name_titles[0].1, "story_title_new".to_string());

        memory_storage.clear().await?;
        assert_eq!(memory_storage.get_story_count().await?, 0);
        Ok(())
    }
}
