// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_diagnostics::{
        ArchiveMarker, BatchIteratorMarker, Format, FormattedContent, ReaderMarker,
    },
    fidl_fuchsia_examples_inspect::ReverserProxy,
    fidl_fuchsia_mem::Buffer,
    fuchsia_async as fasync,
    fuchsia_component::{client, server::ServiceFs},
    fuchsia_inspect::{assert_inspect_tree, reader::NodeHierarchy},
    fuchsia_inspect_node_hierarchy::serialization::{
        json::RawJsonNodeHierarchySerializer, HierarchyDeserializer,
    },
    fuchsia_zircon::DurationNum,
    futures::StreamExt,
    inspect_codelab_shared::CodelabEnvironment,
    lazy_static::lazy_static,
    serde_json,
    std::sync::atomic::{AtomicUsize, Ordering},
};

lazy_static! {
    static ref SUFFIX: AtomicUsize = AtomicUsize::new(0);
}

struct TestOptions {
    include_fizzbuzz: bool,
}

impl Default for TestOptions {
    fn default() -> Self {
        TestOptions { include_fizzbuzz: true }
    }
}

struct IntegrationTest {
    env: CodelabEnvironment,
    environment_label: String,
}

impl IntegrationTest {
    fn start() -> Result<Self, Error> {
        let mut fs = ServiceFs::new();
        let suffix = SUFFIX.fetch_add(1, Ordering::SeqCst);
        let environment_label = format!("{}_{}", "test", suffix);
        let env = CodelabEnvironment::new(
            fs.create_nested_environment(&environment_label)?,
            "inspect_rust_codelab_integration_tests",
            5,
        );
        fasync::spawn(fs.collect::<()>());
        Ok(Self { env, environment_label })
    }

    fn start_component_and_connect(
        &mut self,
        options: TestOptions,
    ) -> Result<ReverserProxy, Error> {
        if options.include_fizzbuzz {
            self.env.launch_fizzbuzz()?;
        }
        self.env.launch_reverser()
    }

    async fn get_inspect_hierarchy(&self) -> Result<NodeHierarchy, Error> {
        let archive =
            client::connect_to_service::<ArchiveMarker>().context("connect to Archive")?;

        let (reader, server_end) = fidl::endpoints::create_proxy::<ReaderMarker>()?;
        let selectors = Vec::new();
        archive
            .read_inspect(server_end, &mut selectors.into_iter())
            .await
            .context("get Reader")?
            .map_err(|e| format_err!("accessor error: {:?}", e))?;

        loop {
            let (iterator, server_end) = fidl::endpoints::create_proxy::<BatchIteratorMarker>()?;
            reader
                .get_snapshot(Format::Json, server_end)
                .await
                .context("get BatchIterator")?
                .map_err(|e| format_err!("get snapshot: {:?}", e))?;

            if let Ok(result) = iterator.get_next().await? {
                for entry in result {
                    match entry {
                        FormattedContent::FormattedJsonHierarchy(json) => {
                            let json_string =
                                self.vmo_buffer_to_string(json).context("read vmo")?;
                            if json_string.contains(&format!(
                                "{}/inspect_rust_codelab_part_5.cmx",
                                self.environment_label
                            )) {
                                let mut output: serde_json::Value =
                                    serde_json::from_str(&json_string).expect("valid json");
                                let tree_json =
                                    output.get_mut("contents").expect("contents are there").take();
                                return RawJsonNodeHierarchySerializer::deserialize(tree_json);
                            }
                        }
                        _ => unreachable!("response should contain only json"),
                    }
                }
            }

            // Retry with delay to ensure data appears.
            150000.micros().sleep();
        }
    }

    pub fn vmo_buffer_to_string(&self, buffer: Buffer) -> Result<String, Error> {
        let buffer_size = buffer.size;
        let buffer_vmo = buffer.vmo;
        let mut bytes = vec![0; buffer_size as usize];
        buffer_vmo.read(&mut bytes, 0)?;
        Ok(String::from_utf8_lossy(&bytes).to_string())
    }
}

#[fasync::run_singlethreaded(test)]
async fn start_with_fizzbuzz() -> Result<(), Error> {
    let mut test = IntegrationTest::start()?;
    let reverser = test.start_component_and_connect(TestOptions::default())?;
    let result = reverser.reverse("hello").await?;
    assert_eq!(result, "olleh");

    let hierarchy = test.get_inspect_hierarchy().await?;
    assert_inspect_tree!(hierarchy, root: contains {
        "fuchsia.inspect.Health": {
            status: "OK",
        }
    });

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn start_without_fizzbuzz() -> Result<(), Error> {
    let mut test = IntegrationTest::start()?;
    let reverser = test.start_component_and_connect(TestOptions { include_fizzbuzz: false })?;
    let result = reverser.reverse("hello").await?;
    assert_eq!(result, "olleh");

    let hierarchy = test.get_inspect_hierarchy().await?;
    assert_inspect_tree!(hierarchy, root: contains {
        "fuchsia.inspect.Health": {
            status: "UNHEALTHY",
            message: "FizzBuzz connection closed",
        }
    });
    Ok(())
}
