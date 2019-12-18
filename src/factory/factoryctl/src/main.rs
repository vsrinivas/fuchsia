// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_boot::FactoryItemsMarker,
    fidl_fuchsia_factory::{
        CastCredentialsFactoryStoreProviderMarker, MiscFactoryStoreProviderMarker,
        PlayReadyFactoryStoreProviderMarker, WidevineFactoryStoreProviderMarker,
    },
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    files_async::{self, DirentKind},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    io_util,
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
    #[structopt(name = "cast")]
    Cast(FactoryStoreCmd),
    #[structopt(name = "factory-items")]
    FactoryItems(FactoryItemsCmd),
    #[structopt(name = "misc")]
    Misc(FactoryStoreCmd),
    #[structopt(name = "playready")]
    PlayReady(FactoryStoreCmd),
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

/// Prints the hexdump of `data` to stdout.
fn hexdump(data: &[u8]) {
    println!("{}", data.to_hex(HEX_DISPLAY_CHUNK_SIZE));
}

/// Walks the given `dir`, printing the full path to every file.
async fn print_files(dir_proxy: &DirectoryProxy) -> Result<(), Error> {
    let dir_entries = files_async::readdir_recursive(dir_proxy).await?;
    for entry in dir_entries.iter() {
        if entry.kind == DirentKind::File {
            println!("{}", entry.name);
        }
    }
    Ok(())
}

/// Processes a command from the command line.
async fn process_cmd<F>(cmd: FactoryStoreCmd, mut connect_fn: F) -> Result<(), Error>
where
    F: FnMut(ServerEnd<DirectoryMarker>) -> (),
{
    let (dir_proxy, dir_server_end) = create_proxy::<DirectoryMarker>()?;
    connect_fn(dir_server_end);

    match cmd {
        FactoryStoreCmd::List => print_files(&dir_proxy).await?,
        FactoryStoreCmd::Dump { name } => {
            let file =
                io_util::open_file(&dir_proxy, &PathBuf::from(name), io_util::OPEN_RIGHT_READABLE)?;
            let contents = io_util::read_file_bytes(&file).await?;

            match std::str::from_utf8(&contents) {
                Ok(value) => {
                    println!("{}", value);
                }
                Err(_) => {
                    hexdump(&contents);
                }
            };
        }
    };
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opt = Opt::from_args();

    match opt {
        Opt::Cast(cmd) => {
            let proxy = connect_to_service::<CastCredentialsFactoryStoreProviderMarker>()
                .expect("Failed to connect to CastCredentialsFactoryStoreProvider service");
            process_cmd(cmd, move |server_end| proxy.get_factory_store(server_end).unwrap()).await
        }
        Opt::FactoryItems(cmd) => {
            let proxy = connect_to_service::<FactoryItemsMarker>()
                .expect("Failed to connect to FactoryItems service");

            match cmd {
                FactoryItemsCmd::Dump { extra } => {
                    let (vmo_opt, length) = proxy.get(extra).await.unwrap_or_else(|err| {
                        panic!("Failed to get factory item with extra {}: {:?}", extra, err);
                    });
                    match vmo_opt {
                        Some(ref vmo) if length > 0 => {
                            let mut buffer = vec![0; length as usize];
                            vmo.read(&mut buffer, 0)?;
                            hexdump(&buffer);
                        }
                        _ => eprintln!("No valid vmo returned"),
                    };
                    println!("Length={}", length);
                }
            };
            Ok(())
        }
        Opt::Misc(cmd) => {
            let proxy = connect_to_service::<MiscFactoryStoreProviderMarker>()
                .expect("Failed to connect to PlayReadyFactoryStoreProvider service");
            process_cmd(cmd, move |server_end| proxy.get_factory_store(server_end).unwrap()).await
        }
        Opt::PlayReady(cmd) => {
            let proxy = connect_to_service::<PlayReadyFactoryStoreProviderMarker>()
                .expect("Failed to connect to PlayReadyFactoryStoreProvider service");
            process_cmd(cmd, move |server_end| proxy.get_factory_store(server_end).unwrap()).await
        }
        Opt::Widevine(cmd) => {
            let proxy = connect_to_service::<WidevineFactoryStoreProviderMarker>()
                .expect("Failed to connect to WidevineFactoryStoreProvider service");
            process_cmd(cmd, move |server_end| proxy.get_factory_store(server_end).unwrap()).await
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_boot::{FactoryItemsRequest, FactoryItemsRequestStream},
        fidl_fuchsia_factory::{
            CastCredentialsFactoryStoreProviderRequest,
            CastCredentialsFactoryStoreProviderRequestStream, MiscFactoryStoreProviderRequest,
            MiscFactoryStoreProviderRequestStream, PlayReadyFactoryStoreProviderRequest,
            PlayReadyFactoryStoreProviderRequestStream, WidevineFactoryStoreProviderRequest,
            WidevineFactoryStoreProviderRequestStream,
        },
        fidl_fuchsia_io::{DirectoryProxy, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE},
        fuchsia_component::{
            client::AppBuilder,
            server::{NestedEnvironment, ServiceFs},
        },
        fuchsia_vfs_pseudo_fs::{
            directory::entry::DirectoryEntry,
            file::simple::{read_only, read_only_str},
            tree_builder::TreeBuilder,
        },
        fuchsia_zircon as zx,
        futures::{StreamExt, TryStreamExt},
        std::{iter, str::from_utf8},
    };

    const FACTORYCTL_PKG_URL: &str =
        "fuchsia-pkg://fuchsia.com/factoryctl_tests#meta/factoryctl.cmx";

    const CAST_TXT_FILE_NAME: &str = "txt/cast.txt";
    const MISC_TXT_FILE_NAME: &str = "misc/misc.txt";
    const PLAYREADY_TXT_FILE_NAME: &str = "txt/playready.txt";
    const WIDEVINE_TXT_FILE_NAME: &str = "widevine.txt";

    const CAST_BIN_FILE_NAME: &str = "cast.bin";
    const MISC_BIN_FILE_NAME: &str = "bin/misc.bin";
    const PLAYREADY_BIN_FILE_NAME: &str = "playready/playready.bin";
    const WIDEVINE_BIN_FILE_NAME: &str = "widevine.bin";

    const CAST_TXT_FILE_CONTENTS: &str = "a cast file";
    const MISC_TXT_FILE_CONTENTS: &str = "a misc file";
    const PLAYREADY_TXT_FILE_CONTENTS: &str = "a playready file";
    const WIDEVINE_TXT_FILE_CONTENTS: &str = "a widevine file";

    const FACTORY_ITEM_CONTENTS: &[u8] = &[0xf0, 0xe8, 0x65, 0x94];
    const CAST_BIN_FILE_CONTENTS: &[u8] = &[0x0, 0x18, 0xF1, 0x6d];
    const MISC_BIN_FILE_CONTENTS: &[u8] = &[0x0, 0xf3, 0x17, 0xb6];
    const PLAYREADY_BIN_FILE_CONTENTS: &[u8] = &[0x0e, 0xb8, 0x1a, 0xc6];
    const WIDEVINE_BIN_FILE_CONTENTS: &[u8] = &[0x0c, 0xee, 0x8a, 0x6f];

    enum IncomingServices {
        FactoryItems(FactoryItemsRequestStream),
        CastCredentialsFactoryStoreProvider(CastCredentialsFactoryStoreProviderRequestStream),
        MiscFactoryStoreProvider(MiscFactoryStoreProviderRequestStream),
        PlayReadyFactoryStoreProvider(PlayReadyFactoryStoreProviderRequestStream),
        WidevineFactoryStoreProvider(WidevineFactoryStoreProviderRequestStream),
    }

    fn start_test_dir(
        name: &'static str,
        contents: &'static str,
        name2: &'static str,
        contents2: &'static [u8],
    ) -> Result<DirectoryProxy, Error> {
        let mut tree = TreeBuilder::empty_dir();
        tree.add_entry(
            &name.split("/").collect::<Vec<&str>>(),
            read_only_str(move || Ok(contents.to_owned())),
        )
        .unwrap();
        tree.add_entry(
            &name2.split("/").collect::<Vec<&str>>(),
            read_only(move || Ok(contents2.to_vec())),
        )
        .unwrap();
        let mut test_dir = tree.build();

        let (test_dir_proxy, test_dir_service) =
            fidl::endpoints::create_proxy::<DirectoryMarker>()?;
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

    fn run_test_services() -> Result<NestedEnvironment, Error> {
        let mut fs = ServiceFs::new();
        fs.add_fidl_service(IncomingServices::FactoryItems)
            .add_fidl_service(IncomingServices::CastCredentialsFactoryStoreProvider)
            .add_fidl_service(IncomingServices::MiscFactoryStoreProvider)
            .add_fidl_service(IncomingServices::PlayReadyFactoryStoreProvider)
            .add_fidl_service(IncomingServices::WidevineFactoryStoreProvider);

        let env = fs.create_salted_nested_environment("factoryctl_env");

        fasync::spawn(fs.for_each_concurrent(None, |req| async {
            match req {
                IncomingServices::FactoryItems(stream) => {
                    stream
                        .err_into::<Error>()
                        .try_for_each(
                            |FactoryItemsRequest::Get { extra: _, responder }| async move {
                                let vmo = zx::Vmo::create(FACTORY_ITEM_CONTENTS.len() as u64)?;
                                vmo.write(&FACTORY_ITEM_CONTENTS, 0)?;
                                responder.send(Some(vmo), FACTORY_ITEM_CONTENTS.len() as u32)?;
                                Ok(())
                            },
                        )
                        .await
                        .unwrap();
                }
                IncomingServices::CastCredentialsFactoryStoreProvider(stream) => {
                    stream
                        .err_into::<Error>()
                        .try_for_each(
                            |CastCredentialsFactoryStoreProviderRequest::GetFactoryStore {
                                 dir,
                                 control_handle: _,
                             }| {
                                async move {
                                    let cast_proxy = start_test_dir(
                                        CAST_TXT_FILE_NAME,
                                        CAST_TXT_FILE_CONTENTS,
                                        CAST_BIN_FILE_NAME,
                                        CAST_BIN_FILE_CONTENTS,
                                    )?;
                                    cast_proxy
                                        .clone(OPEN_RIGHT_READABLE, dir.into_channel().into())?;
                                    Ok(())
                                }
                            },
                        )
                        .await
                        .unwrap();
                }
                IncomingServices::MiscFactoryStoreProvider(stream) => {
                    stream
                        .err_into::<Error>()
                        .try_for_each(
                            |MiscFactoryStoreProviderRequest::GetFactoryStore {
                                 dir,
                                 control_handle: _,
                             }| {
                                async move {
                                    let misc_proxy = start_test_dir(
                                        MISC_TXT_FILE_NAME,
                                        MISC_TXT_FILE_CONTENTS,
                                        MISC_BIN_FILE_NAME,
                                        MISC_BIN_FILE_CONTENTS,
                                    )?;
                                    misc_proxy
                                        .clone(OPEN_RIGHT_READABLE, dir.into_channel().into())?;
                                    Ok(())
                                }
                            },
                        )
                        .await
                        .unwrap();
                }
                IncomingServices::PlayReadyFactoryStoreProvider(stream) => {
                    stream
                        .err_into::<Error>()
                        .try_for_each(
                            |PlayReadyFactoryStoreProviderRequest::GetFactoryStore {
                                 dir,
                                 control_handle: _,
                             }| {
                                async move {
                                    let playready_proxy = start_test_dir(
                                        PLAYREADY_TXT_FILE_NAME,
                                        PLAYREADY_TXT_FILE_CONTENTS,
                                        PLAYREADY_BIN_FILE_NAME,
                                        PLAYREADY_BIN_FILE_CONTENTS,
                                    )?;
                                    playready_proxy
                                        .clone(OPEN_RIGHT_READABLE, dir.into_channel().into())?;
                                    Ok(())
                                }
                            },
                        )
                        .await
                        .unwrap();
                }
                IncomingServices::WidevineFactoryStoreProvider(stream) => {
                    stream
                        .err_into::<Error>()
                        .try_for_each(
                            |WidevineFactoryStoreProviderRequest::GetFactoryStore {
                                 dir,
                                 control_handle: _,
                             }| {
                                async move {
                                    let widevine_proxy = start_test_dir(
                                        WIDEVINE_TXT_FILE_NAME,
                                        WIDEVINE_TXT_FILE_CONTENTS,
                                        WIDEVINE_BIN_FILE_NAME,
                                        WIDEVINE_BIN_FILE_CONTENTS,
                                    )?;
                                    widevine_proxy
                                        .clone(OPEN_RIGHT_READABLE, dir.into_channel().into())?;
                                    Ok(())
                                }
                            },
                        )
                        .await
                        .unwrap();
                }
            }
        }));

        env
    }

    #[fasync::run_singlethreaded(test)]
    async fn list_files() -> Result<(), Error> {
        let env = run_test_services()?;

        for (store, action, bin_file_name, txt_file_name) in vec![
            ("cast", "list", CAST_BIN_FILE_NAME, CAST_TXT_FILE_NAME),
            ("misc", "list", MISC_BIN_FILE_NAME, MISC_TXT_FILE_NAME),
            ("playready", "list", PLAYREADY_BIN_FILE_NAME, PLAYREADY_TXT_FILE_NAME),
            ("widevine", "list", WIDEVINE_BIN_FILE_NAME, WIDEVINE_TXT_FILE_NAME),
        ] {
            let output = AppBuilder::new(FACTORYCTL_PKG_URL)
                .arg(store)
                .arg(action)
                .output(&env.launcher())
                .unwrap()
                .await
                .unwrap();

            let expected_output = format!("{}\n{}\n", bin_file_name, txt_file_name);
            assert_eq!(expected_output, from_utf8(&output.stdout).unwrap());
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn dump_text_files() -> Result<(), Error> {
        let env = run_test_services()?;

        for (store, action, file_name, contents) in vec![
            ("cast", "dump", CAST_TXT_FILE_NAME, CAST_TXT_FILE_CONTENTS),
            ("misc", "dump", MISC_TXT_FILE_NAME, MISC_TXT_FILE_CONTENTS),
            ("playready", "dump", PLAYREADY_TXT_FILE_NAME, PLAYREADY_TXT_FILE_CONTENTS),
            ("widevine", "dump", WIDEVINE_TXT_FILE_NAME, WIDEVINE_TXT_FILE_CONTENTS),
        ] {
            let output = AppBuilder::new(FACTORYCTL_PKG_URL)
                .arg(store)
                .arg(action)
                .arg(file_name)
                .output(&env.launcher())
                .unwrap()
                .await
                .unwrap();

            let expected_output = format!("{}\n", contents);
            assert_eq!(expected_output, from_utf8(&output.stdout).unwrap());
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn dump_binary_files() -> Result<(), Error> {
        let env = run_test_services()?;

        for (store, action, file_name, contents) in vec![
            (
                "cast",
                "dump",
                CAST_BIN_FILE_NAME,
                CAST_BIN_FILE_CONTENTS.to_hex(HEX_DISPLAY_CHUNK_SIZE),
            ),
            (
                "misc",
                "dump",
                MISC_BIN_FILE_NAME,
                MISC_BIN_FILE_CONTENTS.to_hex(HEX_DISPLAY_CHUNK_SIZE),
            ),
            (
                "playready",
                "dump",
                PLAYREADY_BIN_FILE_NAME,
                PLAYREADY_BIN_FILE_CONTENTS.to_hex(HEX_DISPLAY_CHUNK_SIZE),
            ),
            (
                "widevine",
                "dump",
                WIDEVINE_BIN_FILE_NAME,
                WIDEVINE_BIN_FILE_CONTENTS.to_hex(HEX_DISPLAY_CHUNK_SIZE),
            ),
        ] {
            let output = AppBuilder::new(FACTORYCTL_PKG_URL)
                .arg(store)
                .arg(action)
                .arg(file_name)
                .output(&env.launcher())
                .unwrap()
                .await
                .unwrap();

            let expected_output = format!("{}\n", contents);
            assert_eq!(expected_output, from_utf8(&output.stdout).unwrap());
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn dump_factory_item() -> Result<(), Error> {
        let env = run_test_services()?;

        let output = AppBuilder::new(FACTORYCTL_PKG_URL)
            .args(vec!["factory-items", "dump", "0"])
            .output(&env.launcher())
            .unwrap()
            .await
            .unwrap();

        let expected_output = format!(
            "{}\nLength={}\n",
            FACTORY_ITEM_CONTENTS.to_hex(HEX_DISPLAY_CHUNK_SIZE),
            FACTORY_ITEM_CONTENTS.len()
        );
        assert_eq!(expected_output, from_utf8(&output.stdout).unwrap());
        Ok(())
    }
}
