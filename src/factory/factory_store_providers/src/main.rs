// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
        PlayReadyFactoryStoreProviderRequestStream, WeaveFactoryStoreProviderMarker,
        WeaveFactoryStoreProviderRequest, WeaveFactoryStoreProviderRequestStream,
        WidevineFactoryStoreProviderMarker, WidevineFactoryStoreProviderRequest,
        WidevineFactoryStoreProviderRequestStream,
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
        tree_builder::TreeBuilder,
    },
    fuchsia_zircon as zx,
    futures::{lock::Mutex, prelude::*, TryStreamExt},
    std::{collections::HashMap, iter, sync::Arc},
};

const CONCURRENT_LIMIT: usize = 10_000;
const DEFAULT_BOOTFS_FACTORY_ITEM_EXTRA: u32 = 0;

enum IncomingServices {
    CastCredentialsFactoryStoreProvider(CastCredentialsFactoryStoreProviderRequestStream),
    MiscFactoryStoreProvider(MiscFactoryStoreProviderRequestStream),
    PlayReadyFactoryStoreProvider(PlayReadyFactoryStoreProviderRequestStream),
    WidevineFactoryStoreProvider(WidevineFactoryStoreProviderRequestStream),
    WeaveFactoryStoreProvider(WeaveFactoryStoreProviderRequestStream),
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

fn create_dir_from_context<'a>(
    context: &'a ConfigContext,
    items: &'a HashMap<String, Vec<u8>>,
) -> directory::simple::Simple<'static> {
    let mut tree_builder = TreeBuilder::empty_dir();

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
            let content = contents.to_vec();
            let file = read_only(move || Ok(content.clone()));
            tree_builder.add_entry(&path_parts, file).unwrap_or_else(|err| {
                syslog::fx_log_err!("Failed to add file {} to directory: {}", dest, err);
            });
        } else if !validated {
            syslog::fx_log_err!("{} was never validated, ignored", &path);
        }
    }

    tree_builder.build()
}

fn apply_config(config: Config, items: Arc<Mutex<HashMap<String, Vec<u8>>>>) -> DirectoryProxy {
    let (directory_proxy, directory_server_end) = create_proxy::<DirectoryMarker>().unwrap();

    fasync::spawn(async move {
        let items_mtx = items.clone();

        // We only want to hold this lock to create `dir` so limit the scope of `items_ref`.
        let mut dir = {
            let items_ref = items_mtx.lock().await;
            let context = config.into_context().expect("Failed to convert config into context");
            create_dir_from_context(&context, &*items_ref)
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
        .add_fidl_service(IncomingServices::WidevineFactoryStoreProvider)
        .add_fidl_service(IncomingServices::WeaveFactoryStoreProvider);
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

    // The weave config may or may not be present.
    let weave_config = Config::load::<WeaveFactoryStoreProviderMarker>().unwrap_or_default();
    let weave_directory = Arc::new(Mutex::new(apply_config(weave_config, items_mtx.clone())));

    fs.for_each_concurrent(CONCURRENT_LIMIT, move |incoming_service| {
        let cast_directory_clone = cast_directory.clone();
        let misc_directory_clone = misc_directory.clone();
        let playready_directory_clone = playready_directory.clone();
        let widevine_directory_clone = widevine_directory.clone();
        let weave_directory_clone = weave_directory.clone();

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
                IncomingServices::WeaveFactoryStoreProvider(stream) => {
                    let weave_directory_clone = weave_directory_clone.clone();
                    handle_request_stream(
                        stream,
                        weave_directory_clone,
                        |req: WeaveFactoryStoreProviderRequest| {
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
