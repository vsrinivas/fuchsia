// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

mod config;
mod validators;

use {
    config::{Config, ConfigContext},
    failure::{format_err, Error},
    fidl::endpoints::{create_proxy, Request, RequestStream, ServerEnd, ServiceMarker},
    fidl_fuchsia_boot::FactoryItemsMarker,
    fidl_fuchsia_factory::{
        CastCredentialsFactoryStoreProviderMarker, CastCredentialsFactoryStoreProviderRequest,
        CastCredentialsFactoryStoreProviderRequestStream, MiscFactoryStoreProviderMarker,
        MiscFactoryStoreProviderRequest, MiscFactoryStoreProviderRequestStream,
        PlayReadyFactoryStoreProviderMarker, PlayReadyFactoryStoreProviderRequest,
        PlayReadyFactoryStoreProviderRequestStream, WidevineFactoryStoreProviderMarker,
        WidevineFactoryStoreProviderRequest, WidevineFactoryStoreProviderRequestStream,
    },
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, NodeMarker, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE,
    },
    fuchsia_async::{self as fasync},
    fuchsia_bootfs::BootfsParser,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog as syslog,
    fuchsia_vfs_pseudo_fs::{
        directory::{self, entry::DirectoryEntry},
        file::simple::read_only,
    },
    fuchsia_zircon as zx,
    futures::{lock::Mutex, prelude::*, StreamExt, TryStreamExt},
    std::{collections::HashMap, iter, path::PathBuf, sync::Arc},
};

const CONCURRENT_LIMIT: usize = 10_000;
const DEFAULT_BOOTFS_FACTORY_ITEM_EXTRA: u32 = 0;

enum IncomingServices {
    CastCredentialsFactoryStoreProvider(CastCredentialsFactoryStoreProviderRequestStream),
    MiscFactoryStoreProvider(MiscFactoryStoreProviderRequestStream),
    PlayReadyFactoryStoreProvider(PlayReadyFactoryStoreProviderRequestStream),
    WidevineFactoryStoreProvider(WidevineFactoryStoreProviderRequestStream),
}

// TODO(mbrunson): Use implementation from fuchsia_vfs_pseudo_fs when it becomes available:
// https://fuchsia-review.googlesource.com/c/fuchsia/+/305595
/// A "node" within a potential directory tree.
///
/// Unlike the pseudo directory types in the fuchsia-vfs library which only allow adding of direct
/// child directory entries, `DirectoryTreeBuilder::Directory` allows adding files using the full
/// file path, creating extra `DirectoryTreeBuilder` instances as necessary to allow successful
/// conversion of the entire directory tree to a `DirectoryEntry` implementation.
///
/// The `DirectoryTreeBuilder::File` type represents a pseudo file. It can only be a leaf in the
/// directory tree and store file contents unlike `DirectoryTreeBuilder::Directory`.
enum DirectoryTreeBuilder {
    Directory(HashMap<String, DirectoryTreeBuilder>),
    File(Vec<u8>),
}
impl DirectoryTreeBuilder {
    pub fn empty_dir() -> Self {
        DirectoryTreeBuilder::Directory(HashMap::new())
    }

    /// Adds a file to the directory tree.
    ///
    /// An error is returned if either of the following occur:
    /// * This function is called on a `DirectoryTreeBuilder::File` enum.
    /// * A file already exists at the given `path`.
    pub fn add_file(&mut self, path: &[&str], content: Vec<u8>) -> Result<(), Error> {
        self.add_file_impl(path, content, &mut PathBuf::from(""))
    }

    fn add_file_impl(
        &mut self,
        path: &[&str],
        content: Vec<u8>,
        mut full_path: &mut PathBuf,
    ) -> Result<(), Error> {
        match self {
            DirectoryTreeBuilder::File(_) => Err(format_err!(
                "Cannot add a file within a File: path={}, name={}, content={:X?}",
                full_path.to_string_lossy(),
                path[0],
                content
            )),
            DirectoryTreeBuilder::Directory(children) => {
                let name = path[0].to_string();
                let nested = &path[1..];
                full_path.push(&name);

                if nested.is_empty() {
                    if children.insert(name, DirectoryTreeBuilder::File(content)).is_some() {
                        Err(format_err!("Duplicate entry at {}", full_path.to_string_lossy()))
                    } else {
                        Ok(())
                    }
                } else {
                    match children.get_mut(&name) {
                        Some(entry) => entry.add_file_impl(nested, content, &mut full_path),
                        None => {
                            let mut entry = DirectoryTreeBuilder::empty_dir();
                            entry.add_file_impl(nested, content, &mut full_path)?;
                            children.insert(name, entry);
                            Ok(())
                        }
                    }
                }
            }
        }
    }

    /// Converts this `DirectoryTreeBuilder` into a `DirectoryEntry`.
    ///
    /// On successful creation of the `DirectoryEntry`, any payloads owned by this node or its
    /// children are moved into closures called by an associated pseudo file implementation.
    ///
    /// Errors are propogated from `Controllable::add_boxed_entry()` but are converted to
    /// `zx::Status` before being wrapped in an `Error`.
    pub fn build<'a>(self) -> Result<Box<dyn DirectoryEntry>, Error> {
        self.build_impl(&mut PathBuf::from(""))
    }

    fn build_impl<'a>(self, mut full_path: &mut PathBuf) -> Result<Box<dyn DirectoryEntry>, Error> {
        match self {
            DirectoryTreeBuilder::File(content) => {
                syslog::fx_log_info!("Adding content at {}", full_path.to_string_lossy());
                Ok(Box::new(read_only(move || Ok(content.to_vec()))))
            }
            DirectoryTreeBuilder::Directory(children) => {
                let mut dir = directory::simple::empty();

                for (name, child) in children.into_iter() {
                    full_path.push(&name);
                    let entry = child.build_impl(&mut full_path)?;
                    full_path.pop();

                    dir.add_boxed_entry(&name, entry).map_err(|err| Error::from(err.0))?;
                }

                Ok(Box::new(dir))
            }
        }
    }
}

fn parse_bootfs<'a>(vmo: zx::Vmo) -> HashMap<String, Vec<u8>> {
    let mut items = HashMap::new();

    match BootfsParser::create_from_vmo(vmo) {
        Ok(parser) => parser.iter().for_each(|result| match result {
            Ok(entry) => {
                syslog::fx_log_info!("Found {} in factory bootfs", &entry.name);
                items.insert(entry.name, entry.payload);
            }
            Err(err) => syslog::fx_log_err!(tag: "BootfsParser", "{}", err),
        }),
        Err(err) => syslog::fx_log_err!(tag: "BootfsParser", "{}", err),
    };

    items
}

async fn fetch_new_factory_item() -> Result<zx::Vmo, Error> {
    let factory_items = fuchsia_component::client::connect_to_service::<FactoryItemsMarker>()?;
    let (vmo_opt, _) = factory_items.get(DEFAULT_BOOTFS_FACTORY_ITEM_EXTRA).await?;
    vmo_opt.ok_or(format_err!("Failed to get a valid VMO from service"))
}

async fn create_dir_from_context<'a>(
    context: &'a ConfigContext,
    items: &'a HashMap<String, Vec<u8>>,
) -> Result<Box<dyn DirectoryEntry>, Error> {
    let mut dir_builder = DirectoryTreeBuilder::empty_dir();

    for (path, dest) in &context.file_path_map {
        let contents = match items.get(path) {
            Some(contents) => contents,
            None => {
                syslog::fx_log_err!("Failed to find {}, skipping", &path);
                continue;
            }
        };

        let mut failed_validation = false;
        let mut validated = false;

        for validator_context in &context.validator_contexts {
            if validator_context.paths_to_validate.contains(path) {
                syslog::fx_log_info!(
                    "Validating {} with {} validator",
                    &path,
                    &validator_context.name
                );
                if let Err(err) = validator_context.validator.validate(&path, &contents) {
                    syslog::fx_log_err!("{}", err);
                    failed_validation = true;
                    break;
                }
                validated = true;
            }
        }

        // Do not allow files that failed validation or have not been validated at all.
        if !failed_validation && validated {
            let path_parts: Vec<&str> = dest.split("/").collect();
            dir_builder.add_file(&path_parts, contents.to_vec()).unwrap_or_else(|err| {
                syslog::fx_log_err!("Failed to add file {} to directory: {}", dest, err);
            });
        } else if !validated {
            syslog::fx_log_err!("{} was never validated, ignored", &path);
        }
    }

    dir_builder.build()
}

fn apply_config(config: Config, items: Arc<Mutex<HashMap<String, Vec<u8>>>>) -> DirectoryProxy {
    let (directory_proxy, directory_server_end) = create_proxy::<DirectoryMarker>().unwrap();

    fasync::spawn(async move {
        let items_mtx = items.clone();

        // We only want to hold this lock to create `dir` so limit the scope of `items_ref`.
        let mut dir = {
            let items_ref = items_mtx.lock().await;
            let context = config.into_context().expect("Failed to convert config into context");
            create_dir_from_context(&context, &*items_ref).await.unwrap_or_else(|err| {
                syslog::fx_log_err!(
                    "Failed to create directory from config: {}, {:?}",
                    err,
                    context
                );
                Box::new(directory::simple::empty())
            })
        };

        dir.open(
            OPEN_RIGHT_READABLE,
            MODE_TYPE_DIRECTORY,
            &mut iter::empty(),
            ServerEnd::<NodeMarker>::new(directory_server_end.into_channel()),
        );

        dir.await;
    });

    directory_proxy
}

async fn handle_request_stream<RS, G>(
    mut stream: RS,
    directory_mutex: Arc<Mutex<DirectoryProxy>>,
    mut get_directory_request_fn: G,
) -> Result<(), Error>
where
    RS: RequestStream,
    G: FnMut(
        Request<RS::Service>,
    ) -> Option<fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>>,
{
    while let Some(request) = stream.try_next().await? {
        if let Some(directory_request) = get_directory_request_fn(request) {
            if let Err(err) = directory_mutex.lock().await.clone(
                OPEN_RIGHT_READABLE,
                ServerEnd::<NodeMarker>::new(directory_request.into_channel()),
            ) {
                syslog::fx_log_err!(
                    "Failed to clone directory connection for {}: {:?}",
                    RS::Service::DEBUG_NAME,
                    err
                );
            }
        }
    }
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["factory_store_providers"]).expect("Can't init logger");
    syslog::fx_log_info!("{}", "Starting factory_store_providers");

    let directory_items =
        fetch_new_factory_item().await.map(|vmo| parse_bootfs(vmo)).unwrap_or_else(|err| {
            syslog::fx_log_err!("Failed to get factory item, returning empty item list: {}", err);
            HashMap::new()
        });

    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(IncomingServices::CastCredentialsFactoryStoreProvider)
        .add_fidl_service(IncomingServices::MiscFactoryStoreProvider)
        .add_fidl_service(IncomingServices::PlayReadyFactoryStoreProvider)
        .add_fidl_service(IncomingServices::WidevineFactoryStoreProvider);
    fs.take_and_serve_directory_handle().expect("Failed to serve factory providers");

    let items_mtx = Arc::new(Mutex::new(directory_items));
    let cast_credentials_config = Config::load::<CastCredentialsFactoryStoreProviderMarker>()?;
    let cast_directory =
        Arc::new(Mutex::new(apply_config(cast_credentials_config, items_mtx.clone())));

    let misc_config = Config::load::<MiscFactoryStoreProviderMarker>()?;
    let misc_directory = Arc::new(Mutex::new(apply_config(misc_config, items_mtx.clone())));

    let playready_config = Config::load::<PlayReadyFactoryStoreProviderMarker>()?;
    let playready_directory =
        Arc::new(Mutex::new(apply_config(playready_config, items_mtx.clone())));

    let widevine_config = Config::load::<WidevineFactoryStoreProviderMarker>()?;
    let widevine_directory = Arc::new(Mutex::new(apply_config(widevine_config, items_mtx.clone())));

    fs.for_each_concurrent(CONCURRENT_LIMIT, move |incoming_service| {
        let cast_directory_clone = cast_directory.clone();
        let misc_directory_clone = misc_directory.clone();
        let playready_directory_clone = playready_directory.clone();
        let widevine_directory_clone = widevine_directory.clone();

        async move {
            match incoming_service {
                IncomingServices::CastCredentialsFactoryStoreProvider(stream) => {
                    let cast_directory_clone = cast_directory_clone.clone();
                    handle_request_stream(
                        stream,
                        cast_directory_clone,
                        |req: CastCredentialsFactoryStoreProviderRequest| {
                            req.into_get_factory_store().map(|item| item.0)
                        },
                    )
                    .await
                }
                IncomingServices::MiscFactoryStoreProvider(stream) => {
                    let misc_directory_clone = misc_directory_clone.clone();
                    handle_request_stream(
                        stream,
                        misc_directory_clone,
                        |req: MiscFactoryStoreProviderRequest| {
                            req.into_get_factory_store().map(|item| item.0)
                        },
                    )
                    .await
                }
                IncomingServices::PlayReadyFactoryStoreProvider(stream) => {
                    let playready_directory_clone = playready_directory_clone.clone();
                    handle_request_stream(
                        stream,
                        playready_directory_clone,
                        |req: PlayReadyFactoryStoreProviderRequest| {
                            req.into_get_factory_store().map(|item| item.0)
                        },
                    )
                    .await
                }
                IncomingServices::WidevineFactoryStoreProvider(stream) => {
                    let widevine_directory_clone = widevine_directory_clone.clone();
                    handle_request_stream(
                        stream,
                        widevine_directory_clone,
                        |req: WidevineFactoryStoreProviderRequest| {
                            req.into_get_factory_store().map(|item| item.0)
                        },
                    )
                    .await
                }
            }
        }
            .unwrap_or_else(|err| syslog::fx_log_err!("Failed to handle incoming service: {}", err))
    })
    .await;
    Ok(())
}
