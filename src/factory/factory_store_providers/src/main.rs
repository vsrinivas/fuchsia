// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod config;
mod validators;

use {
    anyhow::{format_err, Error},
    config::{Config, ConfigContext, FactoryConfig},
    fidl::endpoints::{create_proxy, ProtocolMarker, Request, RequestStream, ServerEnd},
    fidl_fuchsia_boot::FactoryItemsMarker,
    fidl_fuchsia_factory::{
        AlphaFactoryStoreProviderMarker, AlphaFactoryStoreProviderRequest,
        AlphaFactoryStoreProviderRequestStream, CastCredentialsFactoryStoreProviderMarker,
        CastCredentialsFactoryStoreProviderRequest,
        CastCredentialsFactoryStoreProviderRequestStream, MiscFactoryStoreProviderMarker,
        MiscFactoryStoreProviderRequest, MiscFactoryStoreProviderRequestStream,
        PlayReadyFactoryStoreProviderMarker, PlayReadyFactoryStoreProviderRequest,
        PlayReadyFactoryStoreProviderRequestStream, WeaveFactoryStoreProviderMarker,
        WeaveFactoryStoreProviderRequest, WeaveFactoryStoreProviderRequestStream,
        WidevineFactoryStoreProviderMarker, WidevineFactoryStoreProviderRequest,
        WidevineFactoryStoreProviderRequestStream,
    },
    fidl_fuchsia_io as fio,
    fidl_fuchsia_mem::Buffer,
    fidl_fuchsia_storage_ext4::{MountVmoResult, Server_Marker},
    fuchsia_async::{self as fasync},
    fuchsia_bootfs::BootfsParser,
    fuchsia_component::server::ServiceFs,
    fuchsia_fs, fuchsia_syslog as syslog, fuchsia_zircon as zx,
    futures::{lock::Mutex, prelude::*, TryStreamExt},
    std::{
        io::{self, Read, Seek},
        path::PathBuf,
        sync::Arc,
    },
    vfs::{
        directory::{self, entry::DirectoryEntry},
        execution_scope::ExecutionScope,
        file::vmo::asynchronous::read_only_const,
        tree_builder::TreeBuilder,
    },
};

const CONCURRENT_LIMIT: usize = 10_000;
const DEFAULT_BOOTFS_FACTORY_ITEM_EXTRA: u32 = 0;
const FACTORY_DEVICE_CONFIG: &'static str = "/config/data/factory.config";

enum IncomingServices {
    AlphaFactoryStoreProvider(AlphaFactoryStoreProviderRequestStream),
    CastCredentialsFactoryStoreProvider(CastCredentialsFactoryStoreProviderRequestStream),
    MiscFactoryStoreProvider(MiscFactoryStoreProviderRequestStream),
    PlayReadyFactoryStoreProvider(PlayReadyFactoryStoreProviderRequestStream),
    WeaveFactoryStoreProvider(WeaveFactoryStoreProviderRequestStream),
    WidevineFactoryStoreProvider(WidevineFactoryStoreProviderRequestStream),
}

async fn find_block_device_filepath(partition_path: &str) -> Result<String, Error> {
    const DEV_CLASS_BLOCK: &str = "/dev/class/block";

    let dir =
        fuchsia_fs::directory::open_in_namespace(DEV_CLASS_BLOCK, fio::OpenFlags::RIGHT_READABLE)?;
    device_watcher::wait_for_device_with(
        &dir,
        |device_watcher::DeviceInfo { filename, topological_path }| {
            (topological_path == partition_path)
                .then(|| format!("{}/{}", DEV_CLASS_BLOCK, filename))
        },
    )
    .await
}

fn parse_bootfs<'a>(vmo: zx::Vmo) -> Arc<directory::immutable::Simple> {
    let mut tree_builder = TreeBuilder::empty_dir();

    match BootfsParser::create_from_vmo(vmo) {
        Ok(parser) => parser.iter().for_each(|result| match result {
            Ok(entry) => {
                syslog::fx_log_info!("Found {} in factory bootfs", &entry.name);

                let name = entry.name;
                let path_parts: Vec<&str> = name.split("/").collect();
                let payload = entry.payload;
                tree_builder
                    .add_entry(
                        &path_parts,
                        read_only_const(&payload.unwrap_or_else(|| {
                            syslog::fx_log_err!("Failed to buffer bootfs entry {}", name);
                            Vec::new()
                        })),
                    )
                    .unwrap_or_else(|err| {
                        syslog::fx_log_err!(
                            "Failed to add bootfs entry {} to directory: {}",
                            name,
                            err
                        );
                    });
            }
            Err(err) => syslog::fx_log_err!(tag: "BootfsParser", "{}", err),
        }),
        Err(err) => syslog::fx_log_err!(tag: "BootfsParser", "{}", err),
    };

    tree_builder.build()
}

async fn fetch_new_factory_item() -> Result<zx::Vmo, Error> {
    let factory_items = fuchsia_component::client::connect_to_protocol::<FactoryItemsMarker>()?;
    let (vmo_opt, _) = factory_items.get(DEFAULT_BOOTFS_FACTORY_ITEM_EXTRA).await?;
    vmo_opt.ok_or(format_err!("Failed to get a valid VMO from service"))
}

async fn read_file_from_proxy<'a>(
    dir_proxy: &'a fio::DirectoryProxy,
    file_path: &'a str,
) -> Result<Vec<u8>, Error> {
    let file = fuchsia_fs::open_file(
        &dir_proxy,
        &PathBuf::from(file_path),
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    )?;
    fuchsia_fs::file::read(&file).await.map_err(Into::into)
}

fn load_config_file(path: &str) -> Result<FactoryConfig, Error> {
    FactoryConfig::load(io::BufReader::new(std::fs::File::open(path)?))
}

async fn create_dir_from_context<'a>(
    context: &'a ConfigContext,
    dir: &'a fio::DirectoryProxy,
) -> Arc<directory::immutable::Simple> {
    let mut tree_builder = TreeBuilder::empty_dir();

    for (path, dest) in &context.file_path_map {
        let contents = match read_file_from_proxy(dir, path).await {
            Ok(contents) => contents,
            Err(_) => {
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
                if let Err(err) = validator_context.validator.validate(&path, &contents[..]) {
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
            let file = read_only_const(&content);
            tree_builder.add_entry(&path_parts, file).unwrap_or_else(|err| {
                syslog::fx_log_err!("Failed to add file {} to directory: {}", dest, err);
            });
        } else if !validated {
            syslog::fx_log_err!("{} was never validated, ignored", &path);
        }
    }

    tree_builder.build()
}

async fn apply_config(config: Config, dir: Arc<Mutex<fio::DirectoryProxy>>) -> fio::DirectoryProxy {
    let (directory_proxy, directory_server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();

    let dir_mtx = dir.clone();

    // We only want to hold this lock to create `dir` so limit the scope of `dir_ref`.
    let dir = {
        let dir_ref = dir_mtx.lock().await;
        let context = config.into_context().expect("Failed to convert config into context");
        create_dir_from_context(&context, &*dir_ref).await
    };

    dir.open(
        ExecutionScope::new(),
        fio::OpenFlags::RIGHT_READABLE,
        fio::MODE_TYPE_DIRECTORY,
        vfs::path::Path::dot(),
        ServerEnd::<fio::NodeMarker>::new(directory_server_end.into_channel()),
    );

    directory_proxy
}

async fn handle_request_stream<RS, G>(
    mut stream: RS,
    directory_mutex: Arc<Mutex<fio::DirectoryProxy>>,
    mut get_directory_request_fn: G,
) -> Result<(), Error>
where
    RS: RequestStream,
    G: FnMut(Request<RS::Protocol>) -> Option<fidl::endpoints::ServerEnd<fio::DirectoryMarker>>,
{
    while let Some(request) = stream.try_next().await? {
        if let Some(directory_request) = get_directory_request_fn(request) {
            if let Err(err) = directory_mutex.lock().await.clone(
                fio::OpenFlags::RIGHT_READABLE,
                ServerEnd::<fio::NodeMarker>::new(directory_request.into_channel()),
            ) {
                syslog::fx_log_err!(
                    "Failed to clone directory connection for {}: {:?}",
                    RS::Protocol::DEBUG_NAME,
                    err
                );
            }
        }
    }
    Ok(())
}

async fn open_factory_source(factory_config: FactoryConfig) -> Result<fio::DirectoryProxy, Error> {
    let (directory_proxy, directory_server_end) = create_proxy::<fio::DirectoryMarker>()?;
    match factory_config {
        FactoryConfig::FactoryItems => {
            syslog::fx_log_info!("{}", "Reading from FactoryItems service");
            let factory_items_directory =
                fetch_new_factory_item().await.map(|vmo| parse_bootfs(vmo)).unwrap_or_else(|err| {
                    syslog::fx_log_err!(
                        "Failed to get factory item, returning empty item list: {}",
                        err
                    );
                    directory::immutable::simple()
                });

            factory_items_directory.open(
                ExecutionScope::new(),
                fio::OpenFlags::RIGHT_READABLE,
                fio::MODE_TYPE_DIRECTORY,
                vfs::path::Path::dot(),
                ServerEnd::<fio::NodeMarker>::new(directory_server_end.into_channel()),
            );

            Ok(directory_proxy)
        }
        FactoryConfig::Ext4(partition_path) => {
            syslog::fx_log_info!("Reading from EXT4-formatted source: {}", partition_path);
            let block_path = find_block_device_filepath(&partition_path).await?;
            syslog::fx_log_info!("found the block path {}", block_path);
            let mut reader = io::BufReader::new(std::fs::File::open(block_path)?);
            let size = reader.seek(io::SeekFrom::End(0))?;
            reader.seek(io::SeekFrom::Start(0))?;

            let mut reader_buf = vec![0u8; size as usize];
            reader.read_exact(&mut reader_buf)?;

            let vmo = zx::Vmo::create(size)?;
            vmo.write(&reader_buf, 0)?;
            let mut buf = Buffer { vmo, size };

            let ext4_server = fuchsia_component::client::connect_to_protocol::<Server_Marker>()?;

            syslog::fx_log_info!("Mounting EXT4 VMO");
            match ext4_server
                .mount_vmo(&mut buf, fio::OpenFlags::RIGHT_READABLE, directory_server_end)
                .await
            {
                Ok(MountVmoResult::Success(_)) => Ok(directory_proxy),
                Ok(MountVmoResult::VmoReadFailure(status)) => {
                    Err(format_err!("Failed to read ext4 vmo: {}", status))
                }
                Ok(MountVmoResult::ParseError(parse_error)) => {
                    Err(format_err!("Failed to parse ext4 data: {:?}", parse_error))
                }
                Err(err) => Err(Error::from(err)),
                _ => Err(format_err!("Unknown error while mounting ext4 vmo")),
            }
        }
        FactoryConfig::FactoryVerity => {
            syslog::fx_log_info!("reading from factory verity");
            fdio::open(
                "/factory",
                fio::OpenFlags::RIGHT_READABLE,
                directory_server_end.into_channel(),
            )?;
            Ok(directory_proxy)
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["factory_store_providers"]).expect("Can't init logger");
    syslog::fx_log_info!("{}", "Starting factory_store_providers");

    let factory_config = load_config_file(FACTORY_DEVICE_CONFIG).unwrap_or_default();
    let directory_proxy = open_factory_source(factory_config)
        .await
        .map_err(|e| {
            syslog::fx_log_err!("{:?}", e);
            e
        })
        .unwrap();

    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(IncomingServices::AlphaFactoryStoreProvider)
        .add_fidl_service(IncomingServices::CastCredentialsFactoryStoreProvider)
        .add_fidl_service(IncomingServices::MiscFactoryStoreProvider)
        .add_fidl_service(IncomingServices::PlayReadyFactoryStoreProvider)
        .add_fidl_service(IncomingServices::WeaveFactoryStoreProvider)
        .add_fidl_service(IncomingServices::WidevineFactoryStoreProvider);
    fs.take_and_serve_directory_handle().expect("Failed to serve factory providers");

    syslog::fx_log_info!("{}", "Setting up factory directories");
    let dir_mtx = Arc::new(Mutex::new(directory_proxy));
    let alpha_config = Config::load::<AlphaFactoryStoreProviderMarker>().unwrap_or_default();
    let alpha_directory = Arc::new(Mutex::new(apply_config(alpha_config, dir_mtx.clone()).await));

    let cast_credentials_config = Config::load::<CastCredentialsFactoryStoreProviderMarker>()?;
    let cast_directory =
        Arc::new(Mutex::new(apply_config(cast_credentials_config, dir_mtx.clone()).await));

    let misc_config = Config::load::<MiscFactoryStoreProviderMarker>()?;
    let misc_directory = Arc::new(Mutex::new(apply_config(misc_config, dir_mtx.clone()).await));

    let playready_config = Config::load::<PlayReadyFactoryStoreProviderMarker>()?;
    let playready_directory =
        Arc::new(Mutex::new(apply_config(playready_config, dir_mtx.clone()).await));

    let widevine_config = Config::load::<WidevineFactoryStoreProviderMarker>()?;
    let widevine_directory =
        Arc::new(Mutex::new(apply_config(widevine_config, dir_mtx.clone()).await));

    // The weave config may or may not be present.
    let weave_config = Config::load::<WeaveFactoryStoreProviderMarker>().unwrap_or_default();
    let weave_directory = Arc::new(Mutex::new(apply_config(weave_config, dir_mtx.clone()).await));

    fs.for_each_concurrent(CONCURRENT_LIMIT, move |incoming_service| {
        let alpha_directory_clone = alpha_directory.clone();
        let cast_directory_clone = cast_directory.clone();
        let misc_directory_clone = misc_directory.clone();
        let playready_directory_clone = playready_directory.clone();
        let weave_directory_clone = weave_directory.clone();
        let widevine_directory_clone = widevine_directory.clone();

        async move {
            match incoming_service {
                IncomingServices::AlphaFactoryStoreProvider(stream) => {
                    let alpha_directory_clone = alpha_directory_clone.clone();
                    handle_request_stream(
                        stream,
                        alpha_directory_clone,
                        |req: AlphaFactoryStoreProviderRequest| {
                            req.into_get_factory_store().map(|item| item.0)
                        },
                    )
                    .await
                }
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_endpoints,
        vfs::{file::vmo::read_only_static, pseudo_directory},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_open_factory_verity() {
        // Bind a vfs to /factory.
        let dir = pseudo_directory! {
            "a" => read_only_static("a content"),
            "b" => pseudo_directory! {
                "c" => read_only_static("c content"),
            },
        };
        let (dir_client, dir_server) = create_endpoints().unwrap();
        let scope = ExecutionScope::new();
        dir.open(
            scope,
            fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            vfs::path::Path::dot(),
            ServerEnd::new(dir_server.into_channel()),
        );
        let ns = fdio::Namespace::installed().unwrap();
        ns.bind("/factory", dir_client).unwrap();

        let factory_proxy = open_factory_source(FactoryConfig::FactoryVerity).await.unwrap();

        assert_eq!(
            read_file_from_proxy(&factory_proxy, "a").await.unwrap(),
            "a content".as_bytes()
        );
        assert_eq!(
            read_file_from_proxy(&factory_proxy, "b/c").await.unwrap(),
            "c content".as_bytes()
        );
    }
}
