// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_factory::{
        CastCredentialsFactoryStoreProviderRequest,
        CastCredentialsFactoryStoreProviderRequestStream, MiscFactoryStoreProviderRequest,
        MiscFactoryStoreProviderRequestStream, PlayReadyFactoryStoreProviderRequest,
        PlayReadyFactoryStoreProviderRequestStream, WidevineFactoryStoreProviderRequest,
        WidevineFactoryStoreProviderRequestStream,
    },
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, macros::*},
    fuchsia_vfs_pseudo_fs::{
        directory::entry::DirectoryEntry, file::simple::read_only_str, tree_builder::TreeBuilder,
    },
    futures::{lock::Mutex, prelude::*},
    serde_json::from_reader,
    std::{collections::HashMap, fs::File, iter, str::FromStr, sync::Arc},
    structopt::StructOpt,
};

type LockedDirectoryProxy = Arc<Mutex<DirectoryProxy>>;

enum IncomingServices {
    CastCredentialsFactoryStoreProvider(CastCredentialsFactoryStoreProviderRequestStream),
    MiscFactoryStoreProvider(MiscFactoryStoreProviderRequestStream),
    PlayReadyFactoryStoreProvider(PlayReadyFactoryStoreProviderRequestStream),
    WidevineFactoryStoreProvider(WidevineFactoryStoreProviderRequestStream),
}

fn start_test_dir(config_path: &str) -> Result<DirectoryProxy, Error> {
    let files: HashMap<String, String> = match File::open(&config_path) {
        Ok(file) => from_reader(file)?,
        Err(err) => {
            fx_log_warn!("publishing empty directory for {} due to error: {:?}", &config_path, err);
            HashMap::new()
        }
    };

    fx_log_info!("Files from {}: {:?}", &config_path, files);

    let mut tree = TreeBuilder::empty_dir();

    for (name, contents) in files.into_iter() {
        tree.add_entry(
            &name.split("/").collect::<Vec<&str>>(),
            read_only_str(move || Ok(contents.to_string())),
        )
        .unwrap();
    }

    let mut test_dir = tree.build();

    let (test_dir_proxy, test_dir_service) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;
    test_dir.open(
        OPEN_RIGHT_READABLE,
        MODE_TYPE_DIRECTORY,
        &mut iter::empty(),
        test_dir_service.into_channel().into(),
    );

    fasync::spawn(async move {
        let _ = test_dir.await;
    });

    Ok(test_dir_proxy)
}

async fn run_server(req: IncomingServices, dir_mtx: LockedDirectoryProxy) -> Result<(), Error> {
    match req {
        IncomingServices::CastCredentialsFactoryStoreProvider(mut stream) => {
            while let Some(request) = stream.try_next().await? {
                let CastCredentialsFactoryStoreProviderRequest::GetFactoryStore {
                    dir,
                    control_handle: _,
                } = request;
                dir_mtx.lock().await.clone(OPEN_RIGHT_READABLE, dir.into_channel().into())?;
            }
        }
        IncomingServices::MiscFactoryStoreProvider(mut stream) => {
            while let Some(request) = stream.try_next().await? {
                let MiscFactoryStoreProviderRequest::GetFactoryStore { dir, control_handle: _ } =
                    request;
                dir_mtx.lock().await.clone(OPEN_RIGHT_READABLE, dir.into_channel().into())?;
            }
        }
        IncomingServices::PlayReadyFactoryStoreProvider(mut stream) => {
            while let Some(request) = stream.try_next().await? {
                let PlayReadyFactoryStoreProviderRequest::GetFactoryStore {
                    dir,
                    control_handle: _,
                } = request;
                dir_mtx.lock().await.clone(OPEN_RIGHT_READABLE, dir.into_channel().into())?;
            }
        }
        IncomingServices::WidevineFactoryStoreProvider(mut stream) => {
            while let Some(request) = stream.try_next().await? {
                let WidevineFactoryStoreProviderRequest::GetFactoryStore { dir, control_handle: _ } =
                    request;
                dir_mtx.lock().await.clone(OPEN_RIGHT_READABLE, dir.into_channel().into())?;
            }
        }
    }
    Ok(())
}

#[derive(Debug, StructOpt)]
enum Provider {
    Cast,
    Misc,
    Playready,
    Widevine,
}
impl FromStr for Provider {
    type Err = Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let formatted_str = s.trim().to_lowercase();
        match formatted_str.as_ref() {
            "cast" => Ok(Provider::Cast),
            "misc" => Ok(Provider::Misc),
            "playready" => Ok(Provider::Playready),
            "widevine" => Ok(Provider::Widevine),
            _ => Err(format_err!("Could not find '{}' provider", formatted_str)),
        }
    }
}

#[derive(Debug, StructOpt)]
struct Flags {
    /// The factory store provider to fake.
    #[structopt(short, long)]
    provider: Provider,

    /// The path to the config file for the provider.
    #[structopt(short, long)]
    config: String,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["fake_factory_store_providers"])?;
    let flags = Flags::from_args();
    let dir = Arc::new(Mutex::new(start_test_dir(&flags.config)?));

    let mut fs = ServiceFs::new_local();
    let mut fs_dir = fs.dir("svc");

    match flags.provider {
        Provider::Cast => {
            fs_dir.add_fidl_service(IncomingServices::CastCredentialsFactoryStoreProvider)
        }
        Provider::Misc => fs_dir.add_fidl_service(IncomingServices::MiscFactoryStoreProvider),
        Provider::Playready => {
            fs_dir.add_fidl_service(IncomingServices::PlayReadyFactoryStoreProvider)
        }
        Provider::Widevine => {
            fs_dir.add_fidl_service(IncomingServices::WidevineFactoryStoreProvider)
        }
    };

    fs.take_and_serve_directory_handle()?;
    fs.for_each_concurrent(10, |req| {
        run_server(req, dir.clone()).unwrap_or_else(|e| fx_log_err!("{:?}", e))
    })
    .await;
    Ok(())
}
