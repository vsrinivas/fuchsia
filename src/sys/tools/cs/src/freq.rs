// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect_contrib::reader::{ArchiveReader, Inspect, NodeHierarchy},
    std::fmt::{Display, Formatter, Result},
};

struct BlobFrequency {
    pub hash: String,
    pub frequencies: Vec<(u64, u64)>,
}

impl Display for BlobFrequency {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result {
        write!(f, "{}", self.hash)?;
        for (index, frequency) in &self.frequencies {
            write!(f, ",{}:{}", index, frequency)?;
        }
        Ok(())
    }
}

async fn get_blobfs_tree(reader: ArchiveReader) -> Option<NodeHierarchy<String>> {
    let mut response = reader.snapshot::<Inspect>().await.unwrap();

    for data in response.drain(..) {
        if data.metadata.filename == "blobfs/fuchsia.inspect.Tree" {
            return data.payload;
        }
    }

    panic!("Could not find blobfs inspect tree!");
}

pub struct BlobFrequencies {
    blobs: Vec<BlobFrequency>,
}

impl BlobFrequencies {
    pub async fn collect() -> BlobFrequencies {
        let mut blobs = vec![];
        let reader =
            ArchiveReader::new().add_selector("bootstrap/fshost:root/page_in_frequency_stats");
        let result = get_blobfs_tree(reader).await;

        if let Some(hierarchy) = result {
            let blob_nodes = hierarchy.get_child("page_in_frequency_stats").unwrap();

            for blob_node in &blob_nodes.children {
                let mut frequencies = vec![];
                for property in &blob_node.properties {
                    let index = property.name().parse().unwrap();
                    let frequency = property.uint().unwrap();
                    frequencies.push((index, *frequency));
                }

                blobs.push(BlobFrequency { hash: blob_node.name.clone(), frequencies });
            }
        }

        BlobFrequencies { blobs }
    }
}

impl Display for BlobFrequencies {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result {
        if self.blobs.is_empty() {
            writeln!(
                f,
                "There is no data in the Inspect tree. Check that page-in metrics \
                         recording is enabled for blobfs. Enable the page-in metrics flag in \
                         src/storage/blobfs/BUILD.gn"
            )?;
        } else {
            for blob in &self.blobs {
                writeln!(f, "{}", blob)?;
            }
        }
        Ok(())
    }
}
