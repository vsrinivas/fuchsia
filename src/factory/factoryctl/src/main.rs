// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_boot::{FactoryItemsMarker, FactoryItemsProxy},
    fidl_fuchsia_factory::{
        AlphaFactoryStoreProviderMarker, CastCredentialsFactoryStoreProviderMarker,
        MiscFactoryStoreProviderMarker, PlayReadyFactoryStoreProviderMarker,
        WeaveFactoryStoreProviderMarker, WidevineFactoryStoreProviderMarker,
    },
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_fs::directory::DirentKind,
    futures::stream::TryStreamExt,
    nom::HexDisplay,
    std::path::PathBuf,
    structopt::StructOpt,
};

#[derive(Debug, StructOpt)]
#[structopt(
    name = "factoryctl command line tool, version 1.0.0",
    about = "Commands to view factory contents"
)]
pub enum Opt {
    #[structopt(name = "alpha")]
    Alpha(FactoryStoreCmd),
    #[structopt(name = "cast")]
    Cast(FactoryStoreCmd),
    #[structopt(name = "factory-items")]
    FactoryItems(FactoryItemsCmd),
    #[structopt(name = "misc")]
    Misc(FactoryStoreCmd),
    #[structopt(name = "playready")]
    PlayReady(FactoryStoreCmd),
    #[structopt(name = "weave")]
    Weave(FactoryStoreCmd),
    #[structopt(name = "widevine")]
    Widevine(FactoryStoreCmd),
}

#[derive(Debug, StructOpt)]
pub enum FactoryStoreCmd {
    #[structopt(name = "list")]
    List,

    #[structopt(name = "dump")]
    Dump { name: String },
}

#[derive(Debug, StructOpt)]
pub enum FactoryItemsCmd {
    #[structopt(name = "dump")]
    Dump { extra: u32 },
}

const HEX_DISPLAY_CHUNK_SIZE: usize = 16;

/// Walks the given `dir`, appending the full path to every file and returning the list.
async fn list_files(dir_proxy: &fio::DirectoryProxy) -> Result<Vec<String>, Error> {
    let mut stream = fuchsia_fs::directory::readdir_recursive(dir_proxy, /*timeout=*/ None);
    let mut out = vec![];
    while let Some(entry) = stream.try_next().await? {
        if entry.kind == DirentKind::File {
            out.push(entry.name);
        }
    }
    Ok(out)
}

/// Processes a command from the command line.
async fn process_cmd<F>(cmd: FactoryStoreCmd, mut connect_fn: F) -> Result<Vec<String>, Error>
where
    F: FnMut(ServerEnd<fio::DirectoryMarker>) -> (),
{
    let (dir_proxy, dir_server_end) = create_proxy::<fio::DirectoryMarker>()?;
    connect_fn(dir_server_end);

    let out = match cmd {
        FactoryStoreCmd::List => list_files(&dir_proxy).await?,
        FactoryStoreCmd::Dump { name } => {
            let file = fuchsia_fs::open_file(
                &dir_proxy,
                &PathBuf::from(name),
                fio::OpenFlags::RIGHT_READABLE,
            )?;
            let contents = fuchsia_fs::read_file_bytes(&file).await?;

            match std::str::from_utf8(&contents) {
                Ok(value) => {
                    vec![value.to_string()]
                }
                Err(_) => {
                    vec![contents.to_hex(HEX_DISPLAY_CHUNK_SIZE)]
                }
            }
        }
    };
    Ok(out)
}

async fn process_factory_items_cmd(
    cmd: FactoryItemsCmd,
    proxy: FactoryItemsProxy,
) -> Result<Vec<String>, Error> {
    let mut out = vec![];
    match cmd {
        FactoryItemsCmd::Dump { extra } => {
            let (vmo_opt, length) = proxy.get(extra).await.unwrap_or_else(|err| {
                panic!("Failed to get factory item with extra {}: {:?}", extra, err);
            });
            match vmo_opt {
                Some(ref vmo) if length > 0 => {
                    let mut buffer = vec![0; length as usize];
                    vmo.read(&mut buffer, 0)?;
                    out.push(buffer.to_hex(HEX_DISPLAY_CHUNK_SIZE));
                }
                _ => out.push("No valid vmo returned".to_string()),
            };
            out.push(format!("Length={}", length));
        }
    };
    Ok(out)
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opt = Opt::from_args();

    let out = match opt {
        Opt::Alpha(cmd) => {
            let proxy = connect_to_protocol::<AlphaFactoryStoreProviderMarker>()
                .expect("Failed to connect to AlphaFactoryStoreProvider service");
            process_cmd(cmd, move |server_end| proxy.get_factory_store(server_end).unwrap()).await
        }
        Opt::Cast(cmd) => {
            let proxy = connect_to_protocol::<CastCredentialsFactoryStoreProviderMarker>()
                .expect("Failed to connect to CastCredentialsFactoryStoreProvider service");
            process_cmd(cmd, move |server_end| proxy.get_factory_store(server_end).unwrap()).await
        }
        Opt::FactoryItems(cmd) => {
            let proxy = connect_to_protocol::<FactoryItemsMarker>()
                .expect("Failed to connect to FactoryItems service");
            process_factory_items_cmd(cmd, proxy).await
        }
        Opt::Misc(cmd) => {
            let proxy = connect_to_protocol::<MiscFactoryStoreProviderMarker>()
                .expect("Failed to connect to PlayReadyFactoryStoreProvider service");
            process_cmd(cmd, move |server_end| proxy.get_factory_store(server_end).unwrap()).await
        }
        Opt::PlayReady(cmd) => {
            let proxy = connect_to_protocol::<PlayReadyFactoryStoreProviderMarker>()
                .expect("Failed to connect to PlayReadyFactoryStoreProvider service");
            process_cmd(cmd, move |server_end| proxy.get_factory_store(server_end).unwrap()).await
        }
        Opt::Weave(cmd) => {
            let proxy = connect_to_protocol::<WeaveFactoryStoreProviderMarker>()
                .expect("Failed to connect to WeaveFactoryStoreProvider service");
            process_cmd(cmd, move |server_end| proxy.get_factory_store(server_end).unwrap()).await
        }
        Opt::Widevine(cmd) => {
            let proxy = connect_to_protocol::<WidevineFactoryStoreProviderMarker>()
                .expect("Failed to connect to WidevineFactoryStoreProvider service");
            process_cmd(cmd, move |server_end| proxy.get_factory_store(server_end).unwrap()).await
        }
    }?;

    // Write output to stdout.
    for l in out {
        println!("{}", l)
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_boot::{FactoryItemsRequest, FactoryItemsRequestStream},
        fidl_fuchsia_factory::{
            AlphaFactoryStoreProviderRequest, AlphaFactoryStoreProviderRequestStream,
            CastCredentialsFactoryStoreProviderRequest,
            CastCredentialsFactoryStoreProviderRequestStream, MiscFactoryStoreProviderRequest,
            MiscFactoryStoreProviderRequestStream, PlayReadyFactoryStoreProviderRequest,
            PlayReadyFactoryStoreProviderRequestStream, WeaveFactoryStoreProviderRequest,
            WeaveFactoryStoreProviderRequestStream, WidevineFactoryStoreProviderRequest,
            WidevineFactoryStoreProviderRequestStream,
        },
        fuchsia_async as fasync,
        fuchsia_component::server as fserver,
        fuchsia_component_test::{
            Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref,
            Route,
        },
        fuchsia_zircon as zx,
        futures::{StreamExt, TryStreamExt},
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::vmo::read_only_static, tree_builder::TreeBuilder,
        },
    };

    const ALPHA_TXT_FILE_NAME: &str = "txt/alpha.txt";
    const CAST_TXT_FILE_NAME: &str = "txt/cast.txt";
    const MISC_TXT_FILE_NAME: &str = "misc/misc.txt";
    const PLAYREADY_TXT_FILE_NAME: &str = "txt/playready.txt";
    const WEAVE_TXT_FILE_NAME: &str = "txt/weave.txt";
    const WIDEVINE_TXT_FILE_NAME: &str = "widevine.txt";

    const ALPHA_BIN_FILE_NAME: &str = "alpha.bin";
    const CAST_BIN_FILE_NAME: &str = "cast.bin";
    const MISC_BIN_FILE_NAME: &str = "bin/misc.bin";
    const PLAYREADY_BIN_FILE_NAME: &str = "playready/playready.bin";
    const WEAVE_BIN_FILE_NAME: &str = "weave.bin";
    const WIDEVINE_BIN_FILE_NAME: &str = "widevine.bin";

    const ALPHA_TXT_FILE_CONTENTS: &str = "an alpha file";
    const CAST_TXT_FILE_CONTENTS: &str = "a cast file";
    const MISC_TXT_FILE_CONTENTS: &str = "a misc file";
    const PLAYREADY_TXT_FILE_CONTENTS: &str = "a playready file";
    const WEAVE_TXT_FILE_CONTENTS: &str = "a weave file";
    const WIDEVINE_TXT_FILE_CONTENTS: &str = "a widevine file";

    const FACTORY_ITEM_CONTENTS: &[u8] = &[0xf0, 0xe8, 0x65, 0x94];
    const ALPHA_BIN_FILE_CONTENTS: &[u8] = &[0xaa, 0xbb, 0xcc, 0xdd];
    const CAST_BIN_FILE_CONTENTS: &[u8] = &[0x0, 0x18, 0xF1, 0x6d];
    const MISC_BIN_FILE_CONTENTS: &[u8] = &[0x0, 0xf3, 0x17, 0xb6];
    const PLAYREADY_BIN_FILE_CONTENTS: &[u8] = &[0x0e, 0xb8, 0x1a, 0xc6];
    const WEAVE_BIN_FILE_CONTENTS: &[u8] = &[0xab, 0xcd, 0xef, 0x1];
    const WIDEVINE_BIN_FILE_CONTENTS: &[u8] = &[0x0c, 0xee, 0x8a, 0x6f];

    enum ServiceMarkers {
        AlphaFactoryStore,
        CastCredentialsFactoryStore,
        MiscFactoryStore,
        PlayReadyFactoryStore,
        WeaveFactoryStore,
        WidevineFactoryStore,
    }

    fn start_test_dir(
        name: &'static str,
        contents: &'static str,
        name2: &'static str,
        contents2: &'static [u8],
    ) -> Result<fio::DirectoryProxy, Error> {
        let mut tree = TreeBuilder::empty_dir();
        tree.add_entry(&name.split("/").collect::<Vec<&str>>(), read_only_static(contents))
            .unwrap();
        tree.add_entry(&name2.split("/").collect::<Vec<&str>>(), read_only_static(contents2))
            .unwrap();
        let test_dir = tree.build();

        let (test_dir_proxy, test_dir_service) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>()?;
        test_dir.open(
            ExecutionScope::new(),
            fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            vfs::path::Path::dot(),
            test_dir_service.into_channel().into(),
        );

        Ok(test_dir_proxy)
    }

    async fn items_mock(handles: LocalComponentHandles) -> Result<(), Error> {
        let mut fs = fserver::ServiceFs::new();
        let mut tasks = vec![];
        fs.dir("svc").add_fidl_service(move |mut stream: FactoryItemsRequestStream| {
            tasks.push(fasync::Task::local(async move {
                while let Some(FactoryItemsRequest::Get { extra: _, responder }) =
                    stream.try_next().await.expect("failed to serve factory_items service")
                {
                    let vmo = zx::Vmo::create(FACTORY_ITEM_CONTENTS.len() as u64).unwrap();
                    vmo.write(&FACTORY_ITEM_CONTENTS, 0).unwrap();
                    responder.send(Some(vmo), FACTORY_ITEM_CONTENTS.len() as u32).unwrap();
                }
            }));
        });
        fs.serve_connection(handles.outgoing_dir.into_channel())?;
        fs.collect::<()>().await;
        Ok(())
    }

    async fn alpha_factory_store_handler(mut stream: AlphaFactoryStoreProviderRequestStream) {
        while let Some(AlphaFactoryStoreProviderRequest::GetFactoryStore {
            dir,
            control_handle: _,
        }) = stream.try_next().await.expect("failed to serve alpha_factory service")
        {
            let proxy = start_test_dir(
                ALPHA_TXT_FILE_NAME,
                ALPHA_TXT_FILE_CONTENTS,
                ALPHA_BIN_FILE_NAME,
                ALPHA_BIN_FILE_CONTENTS,
            )
            .unwrap();
            proxy.clone(fio::OpenFlags::RIGHT_READABLE, dir.into_channel().into()).unwrap();
        }
    }

    async fn cast_credentials_factory_store_handler(
        mut stream: CastCredentialsFactoryStoreProviderRequestStream,
    ) {
        while let Some(CastCredentialsFactoryStoreProviderRequest::GetFactoryStore {
            dir,
            control_handle: _,
        }) =
            stream.try_next().await.expect("failed to serve cast_credentials_factory service")
        {
            let proxy = start_test_dir(
                CAST_TXT_FILE_NAME,
                CAST_TXT_FILE_CONTENTS,
                CAST_BIN_FILE_NAME,
                CAST_BIN_FILE_CONTENTS,
            )
            .unwrap();
            proxy.clone(fio::OpenFlags::RIGHT_READABLE, dir.into_channel().into()).unwrap();
        }
    }

    async fn misc_factory_store_handler(mut stream: MiscFactoryStoreProviderRequestStream) {
        while let Some(MiscFactoryStoreProviderRequest::GetFactoryStore {
            dir,
            control_handle: _,
        }) = stream.try_next().await.expect("failed to serve misc_factory service")
        {
            let proxy = start_test_dir(
                MISC_TXT_FILE_NAME,
                MISC_TXT_FILE_CONTENTS,
                MISC_BIN_FILE_NAME,
                MISC_BIN_FILE_CONTENTS,
            )
            .unwrap();
            proxy.clone(fio::OpenFlags::RIGHT_READABLE, dir.into_channel().into()).unwrap();
        }
    }

    async fn play_ready_factory_store_handler(
        mut stream: PlayReadyFactoryStoreProviderRequestStream,
    ) {
        while let Some(PlayReadyFactoryStoreProviderRequest::GetFactoryStore {
            dir,
            control_handle: _,
        }) = stream.try_next().await.expect("failed to serve play_ready_factory service")
        {
            let proxy = start_test_dir(
                PLAYREADY_TXT_FILE_NAME,
                PLAYREADY_TXT_FILE_CONTENTS,
                PLAYREADY_BIN_FILE_NAME,
                PLAYREADY_BIN_FILE_CONTENTS,
            )
            .unwrap();
            proxy.clone(fio::OpenFlags::RIGHT_READABLE, dir.into_channel().into()).unwrap();
        }
    }

    async fn weave_factory_store_handler(mut stream: WeaveFactoryStoreProviderRequestStream) {
        while let Some(WeaveFactoryStoreProviderRequest::GetFactoryStore {
            dir,
            control_handle: _,
        }) = stream.try_next().await.expect("failed to serve weave_factory service")
        {
            let proxy = start_test_dir(
                WEAVE_TXT_FILE_NAME,
                WEAVE_TXT_FILE_CONTENTS,
                WEAVE_BIN_FILE_NAME,
                WEAVE_BIN_FILE_CONTENTS,
            )
            .unwrap();
            proxy.clone(fio::OpenFlags::RIGHT_READABLE, dir.into_channel().into()).unwrap();
        }
    }

    async fn widevine_factory_store_handler(mut stream: WidevineFactoryStoreProviderRequestStream) {
        while let Some(WidevineFactoryStoreProviderRequest::GetFactoryStore {
            dir,
            control_handle: _,
        }) = stream.try_next().await.expect("failed to serve widevine_factory service")
        {
            let proxy = start_test_dir(
                WIDEVINE_TXT_FILE_NAME,
                WIDEVINE_TXT_FILE_CONTENTS,
                WIDEVINE_BIN_FILE_NAME,
                WIDEVINE_BIN_FILE_CONTENTS,
            )
            .unwrap();
            proxy.clone(fio::OpenFlags::RIGHT_READABLE, dir.into_channel().into()).unwrap();
        }
    }

    // The server_mock! macro creates a ServiceFs to run a given mock under a given name.
    // Call like:
    //   server_mock!(foo_mock, FooProviderRequestStream, foo_handler);
    //
    //  It will create service called `foo_mock` that handles `FooProviderRequestStream` requests
    //  via the `foo_handler` function.
    macro_rules! server_mock {
        ( $func_name:ident, $strm:ty, $hndlr:ident ) => {
            async fn $func_name(handles: LocalComponentHandles) -> Result<(), Error> {
                let mut fs = fserver::ServiceFs::new();
                fs.dir("svc").add_fidl_service(move |stream: $strm| {
                    fasync::Task::spawn($hndlr(stream)).detach();
                });
                fs.serve_connection(handles.outgoing_dir.into_channel())?;
                fs.collect::<()>().await;
                Ok(())
            }
        };
    }

    //----------------------------------------------------------------------------------------------
    // Create the mocks.
    //
    server_mock!(alpha_mock, AlphaFactoryStoreProviderRequestStream, alpha_factory_store_handler);

    server_mock!(
        cast_mock,
        CastCredentialsFactoryStoreProviderRequestStream,
        cast_credentials_factory_store_handler
    );

    server_mock!(misc_mock, MiscFactoryStoreProviderRequestStream, misc_factory_store_handler);

    server_mock!(
        play_ready_mock,
        PlayReadyFactoryStoreProviderRequestStream,
        play_ready_factory_store_handler
    );

    server_mock!(weave_mock, WeaveFactoryStoreProviderRequestStream, weave_factory_store_handler);

    server_mock!(
        widevine_mock,
        WidevineFactoryStoreProviderRequestStream,
        widevine_factory_store_handler
    );

    //
    // End creating the mocks.
    //----------------------------------------------------------------------------------------------

    async fn get_factory_items_output(
        realm: &RealmInstance,
        cmd: FactoryItemsCmd,
    ) -> Result<Vec<String>, Error> {
        let factory_items_proxy =
            realm.root.connect_to_protocol_at_exposed_dir::<FactoryItemsMarker>()?;
        process_factory_items_cmd(cmd, factory_items_proxy).await
    }

    async fn get_factory_store_output(
        realm: &RealmInstance,
        marker: ServiceMarkers,
        cmd: FactoryStoreCmd,
    ) -> Result<Vec<String>, Error> {
        match marker {
            ServiceMarkers::AlphaFactoryStore => {
                let proxy = realm
                    .root
                    .connect_to_protocol_at_exposed_dir::<AlphaFactoryStoreProviderMarker>()?;
                process_cmd(cmd, |server_end| proxy.get_factory_store(server_end).unwrap()).await
            }
            ServiceMarkers::CastCredentialsFactoryStore => {
                let proxy = realm
                        .root
                        .connect_to_protocol_at_exposed_dir::<CastCredentialsFactoryStoreProviderMarker>()?;
                process_cmd(cmd, |server_end| proxy.get_factory_store(server_end).unwrap()).await
            }
            ServiceMarkers::MiscFactoryStore => {
                let proxy = realm
                    .root
                    .connect_to_protocol_at_exposed_dir::<MiscFactoryStoreProviderMarker>()?;
                process_cmd(cmd, |server_end| proxy.get_factory_store(server_end).unwrap()).await
            }
            ServiceMarkers::PlayReadyFactoryStore => {
                let proxy = realm
                    .root
                    .connect_to_protocol_at_exposed_dir::<PlayReadyFactoryStoreProviderMarker>()?;
                process_cmd(cmd, |server_end| proxy.get_factory_store(server_end).unwrap()).await
            }
            ServiceMarkers::WeaveFactoryStore => {
                let proxy = realm
                    .root
                    .connect_to_protocol_at_exposed_dir::<WeaveFactoryStoreProviderMarker>()?;
                process_cmd(cmd, |server_end| proxy.get_factory_store(server_end).unwrap()).await
            }
            ServiceMarkers::WidevineFactoryStore => {
                let proxy = realm
                    .root
                    .connect_to_protocol_at_exposed_dir::<WidevineFactoryStoreProviderMarker>()?;
                process_cmd(cmd, |server_end| proxy.get_factory_store(server_end).unwrap()).await
            }
        }
    }

    // The add_mock! macro adds a given mock component as a local child to the given BuildRealm
    // with the given name.
    macro_rules! add_mock {
        ( $builder:ident, $name:literal, $mock:ident ) => {
            $builder
                .add_local_child(
                    $name,
                    move |handles: LocalComponentHandles| Box::pin($mock(handles)),
                    ChildOptions::new(),
                )
                .await?
        };
    }

    // The add_route! macro routes the named capability from the given component to the parent.
    macro_rules! add_route {
        ( $builder:ident, $name:literal, $comp:ident ) => {
            $builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name($name))
                        .from(&$comp)
                        .to(Ref::parent()),
                )
                .await?
        };
    }

    async fn build_realm() -> Result<RealmInstance, fuchsia_component_test::error::Error> {
        let builder = RealmBuilder::new().await?;

        // Add the mock components.
        let factory_items = add_mock!(builder, "factory_items", items_mock);
        let alpha_store = add_mock!(builder, "alpha", alpha_mock);
        let cast_store = add_mock!(builder, "cast", cast_mock);
        let misc_store = add_mock!(builder, "misc", misc_mock);
        let play_ready_store = add_mock!(builder, "play_ready", play_ready_mock);
        let weave_store = add_mock!(builder, "weave", weave_mock);
        let widevine_store = add_mock!(builder, "widevine", widevine_mock);

        // Add the capabilities.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&factory_items)
                    .to(&alpha_store)
                    .to(&cast_store)
                    .to(&misc_store)
                    .to(&play_ready_store)
                    .to(&weave_store)
                    .to(&widevine_store),
            )
            .await?;

        // Route these capabilities to the parent.
        add_route!(builder, "fuchsia.boot.FactoryItems", factory_items);
        add_route!(builder, "fuchsia.factory.AlphaFactoryStoreProvider", alpha_store);
        add_route!(builder, "fuchsia.factory.CastCredentialsFactoryStoreProvider", cast_store);
        add_route!(builder, "fuchsia.factory.MiscFactoryStoreProvider", misc_store);
        add_route!(builder, "fuchsia.factory.PlayReadyFactoryStoreProvider", play_ready_store);
        add_route!(builder, "fuchsia.factory.WeaveFactoryStoreProvider", weave_store);
        add_route!(builder, "fuchsia.factory.WidevineFactoryStoreProvider", widevine_store);

        builder.build().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn list_files() -> Result<(), Error> {
        let realm = build_realm().await?;
        for (marker, bin_name, txt_name) in vec![
            (ServiceMarkers::AlphaFactoryStore, ALPHA_BIN_FILE_NAME, ALPHA_TXT_FILE_NAME),
            (ServiceMarkers::CastCredentialsFactoryStore, CAST_BIN_FILE_NAME, CAST_TXT_FILE_NAME),
            (ServiceMarkers::MiscFactoryStore, MISC_BIN_FILE_NAME, MISC_TXT_FILE_NAME),
            (
                ServiceMarkers::PlayReadyFactoryStore,
                PLAYREADY_BIN_FILE_NAME,
                PLAYREADY_TXT_FILE_NAME,
            ),
            (ServiceMarkers::WeaveFactoryStore, WEAVE_BIN_FILE_NAME, WEAVE_TXT_FILE_NAME),
            (ServiceMarkers::WidevineFactoryStore, WIDEVINE_BIN_FILE_NAME, WIDEVINE_TXT_FILE_NAME),
        ] {
            let out = get_factory_store_output(&realm, marker, FactoryStoreCmd::List {}).await?;
            assert_eq!(out.len(), 2);
            assert_eq!(out[0], bin_name.to_string());
            assert_eq!(out[1], txt_name.to_string());
        }

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn dump_text_files() -> Result<(), Error> {
        let realm = build_realm().await?;
        for (marker, name, contents) in vec![
            (ServiceMarkers::AlphaFactoryStore, ALPHA_TXT_FILE_NAME, ALPHA_TXT_FILE_CONTENTS),
            (
                ServiceMarkers::CastCredentialsFactoryStore,
                CAST_TXT_FILE_NAME,
                CAST_TXT_FILE_CONTENTS,
            ),
            (ServiceMarkers::MiscFactoryStore, MISC_TXT_FILE_NAME, MISC_TXT_FILE_CONTENTS),
            (
                ServiceMarkers::PlayReadyFactoryStore,
                PLAYREADY_TXT_FILE_NAME,
                PLAYREADY_TXT_FILE_CONTENTS,
            ),
            (ServiceMarkers::WeaveFactoryStore, WEAVE_TXT_FILE_NAME, WEAVE_TXT_FILE_CONTENTS),
            (
                ServiceMarkers::WidevineFactoryStore,
                WIDEVINE_TXT_FILE_NAME,
                WIDEVINE_TXT_FILE_CONTENTS,
            ),
        ] {
            let out = get_factory_store_output(
                &realm,
                marker,
                FactoryStoreCmd::Dump { name: name.to_string() },
            )
            .await?;
            assert_eq!(out.len(), 1);
            assert!(out[0].contains(contents));
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn dump_binary_files() -> Result<(), Error> {
        let realm = build_realm().await?;
        for (marker, name, contents) in vec![
            (ServiceMarkers::AlphaFactoryStore, ALPHA_BIN_FILE_NAME, ALPHA_BIN_FILE_CONTENTS),
            (
                ServiceMarkers::CastCredentialsFactoryStore,
                CAST_BIN_FILE_NAME,
                CAST_BIN_FILE_CONTENTS,
            ),
            (ServiceMarkers::MiscFactoryStore, MISC_BIN_FILE_NAME, MISC_BIN_FILE_CONTENTS),
            (
                ServiceMarkers::PlayReadyFactoryStore,
                PLAYREADY_BIN_FILE_NAME,
                PLAYREADY_BIN_FILE_CONTENTS,
            ),
            (ServiceMarkers::WeaveFactoryStore, WEAVE_BIN_FILE_NAME, WEAVE_BIN_FILE_CONTENTS),
            (
                ServiceMarkers::WidevineFactoryStore,
                WIDEVINE_BIN_FILE_NAME,
                WIDEVINE_BIN_FILE_CONTENTS,
            ),
        ] {
            let out = get_factory_store_output(
                &realm,
                marker,
                FactoryStoreCmd::Dump { name: name.to_string() },
            )
            .await?;
            assert_eq!(out.len(), 1);
            assert!(out[0].contains("00000000"));
            assert!(out[0].contains(&contents.to_hex(HEX_DISPLAY_CHUNK_SIZE)));
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn dump_factory_item() -> Result<(), Error> {
        let realm = build_realm().await?;
        let out = get_factory_items_output(&realm, FactoryItemsCmd::Dump { extra: 0 }).await?;
        assert_eq!(out.len(), 2);
        assert!(out[0].contains("00000000"));
        assert!(out[0].contains(&FACTORY_ITEM_CONTENTS.to_hex(HEX_DISPLAY_CHUNK_SIZE)));
        assert!(out[1].contains("Length=4"));
        Ok(())
    }
}
