// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use {
    fuchsia_hash::Hash,
    futures::TryStreamExt as _,
    std::{collections::HashMap, sync::Arc},
};

static PKGFS_BOOT_ARG_KEY: &str = "zircon.system.pkgfs.cmd";
static PKGFS_BOOT_ARG_VALUE_PREFIX: &str = "bin/pkgsvr+";

/// Serves fuchsia.boot/Arguments.GetString from a supplied arguments map.
/// Panics on unexpected keys or fidl methods.
pub struct MockBootArgumentsService {
    args: HashMap<String, Option<String>>,
}

impl MockBootArgumentsService {
    /// Inserts pkgfs boot arg into arguments map using `system_image` hash.
    pub fn insert_pkgfs_boot_arg(&mut self, system_image: Hash) {
        let system_image = format!("{}{}", PKGFS_BOOT_ARG_VALUE_PREFIX, system_image);
        assert_eq!(self.args.insert(PKGFS_BOOT_ARG_KEY.to_string(), Some(system_image)), None);
    }

    pub fn new(args: HashMap<String, Option<String>>) -> Self {
        Self { args }
    }

    /// Serves fuchsia.boot/Arguments requests on the given request stream.
    pub async fn handle_request_stream(
        self: Arc<Self>,
        mut stream: fidl_fuchsia_boot::ArgumentsRequestStream,
    ) {
        while let Some(event) =
            stream.try_next().await.expect("received fuchsia.boot/Arguments request")
        {
            match event {
                fidl_fuchsia_boot::ArgumentsRequest::GetString { key, responder } => {
                    if let Some(value) = self.args.get(&key) {
                        responder.send(value.as_deref()).unwrap();
                    } else {
                        panic!("unexpected fuchsia.boot/Arguments.GetString key {:?}", key);
                    }
                }
                req => {
                    panic!("unexpected fuchsia.boot/Arguments request {:?}", req);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync, maplit::hashmap};

    #[test]
    fn insert_pkgfs_boot_arg_some() {
        let mut mock = MockBootArgumentsService::new(HashMap::new());
        mock.insert_pkgfs_boot_arg(Hash::from([0; 32]));
        assert_eq!(
            mock.args,
            hashmap! {
                "zircon.system.pkgfs.cmd".to_string() =>
                Some("bin/pkgsvr+0000000000000000000000000000000000000000000000000000000000000000"
                    .to_string()
                )
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn handle_request_stream() {
        let mock = Arc::new(MockBootArgumentsService::new(
            hashmap! {"some-key".to_string() => Some("some-value".to_string())},
        ));
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_boot::ArgumentsMarker>()
                .unwrap();
        fasync::Task::spawn(mock.handle_request_stream(stream)).detach();

        let value = proxy.get_string("some-key").await.unwrap();

        assert_eq!(value, Some("some-value".to_string()));
    }
}
