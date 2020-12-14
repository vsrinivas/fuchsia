// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Simple Rust utilities for file I/O that are usable on the host (via overnet
//! FIDL usage) as well as on a Fuchsia target.
//!
//! For now all utilities in here are implemented as read-only.

use {
    anyhow::{anyhow, Context, Result},
    async_trait::async_trait,
    byteorder::NetworkEndian,
    fidl_fuchsia_io as fio,
    fuchsia_zircon_status::Status,
    futures::Future,
    packet::BufferView,
    std::convert::{TryFrom, TryInto},
    std::fmt::Debug,
    zerocopy::ByteSlice,
};

/// Applies a predicate to all children. If all children are known to be of
/// a certain type in advance, this function can be run much simpler. For
/// example, this trait is implemented for `MapChildren<DirectoryProxy, _, _>`,
/// which will automatically open each child in the directory as a directory.
#[async_trait]
pub trait MapChildren<T, E, F>
where
    F: Future<Output = Result<E>>,
{
    /// Attempts to apply the predicate to all children, using the flags for
    /// opening each child.
    async fn map_children(self, flags: u32, f: fn(T) -> F) -> Result<Vec<E>>;
}

#[async_trait]
impl<E, F> MapChildren<fio::DirectoryProxy, E, F> for fio::DirectoryProxy
where
    F: Future<Output = Result<E>> + 'static + Send,
    E: 'static + Send,
{
    async fn map_children(self, flags: u32, func: fn(fio::DirectoryProxy) -> F) -> Result<Vec<E>> {
        // TODO(awdavies): Run this in parallel with some kind of barrier to
        // prevent overspawning channels.
        let mut res = Vec::new();
        for child in self.dirents().await? {
            let child_root = child.to_dir_proxy(&self, flags)?;
            res.push(func(child_root).await?);
        }
        Ok(res)
    }
}

#[async_trait]
trait DirectoryProxyExtPrivate {
    async fn dirent_bytes(&self) -> Result<Vec<u8>>;
}

#[async_trait]
impl DirectoryProxyExtPrivate for fio::DirectoryProxy {
    async fn dirent_bytes(&self) -> Result<Vec<u8>> {
        let (status, buf) = self.read_dirents(fio::MAX_BUF).await.context("read_dirents")?;
        Status::ok(status).context("dirents result")?;
        Ok(buf)
    }
}

#[async_trait]
pub trait DirectoryProxyExt {
    /// Reads all raw dirents, returning a vector of dirents.
    /// Each dirent can be potentially converted into either a file or
    /// directory proxy.
    async fn dirents(&self) -> Result<Vec<Dirent>>;

    /// Attempts to find a dirent in this directory, returning `None` if it
    /// cannot be found.
    async fn dirent(&self, name: &str) -> Result<Option<Dirent>>;

    /// Attempts to open a directory at the given path.
    fn open_dir(&self, path: &str, flags: u32) -> Result<fio::DirectoryProxy>;

    /// Attempts to open a file at the given path.
    fn open_file(&self, path: &str, flags: u32) -> Result<fio::FileProxy>;

    /// Similar to `open_dir` but returns `None` if the file doesn't exist inside
    /// this directory (so avoid using names referring to files under one or more
    /// directories).
    async fn open_dir_checked(&self, name: &str, flags: u32)
        -> Result<Option<fio::DirectoryProxy>>;

    /// Attempts to read the max buffer size of a file for a given path.
    async fn read_file(&self, path: &str) -> Result<String>;
}

#[async_trait]
impl DirectoryProxyExt for fio::DirectoryProxy {
    fn open_dir(&self, path: &str, flags: u32) -> Result<fio::DirectoryProxy> {
        let (dir_client, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .context("creating fidl proxy")?;
        self.open(
            fio::OPEN_FLAG_DIRECTORY | flags,
            fio::MODE_TYPE_DIRECTORY,
            path,
            fidl::endpoints::ServerEnd::new(dir_server.into_channel()),
        )?;
        Ok(dir_client)
    }

    fn open_file(&self, path: &str, flags: u32) -> Result<fio::FileProxy> {
        let (file_client, file_server) =
            fidl::endpoints::create_proxy::<fio::FileMarker>().context("creating fidl proxy")?;
        self.open(
            fio::OPEN_FLAG_NOT_DIRECTORY | flags,
            fio::MODE_TYPE_FILE,
            path,
            fidl::endpoints::ServerEnd::new(file_server.into_channel()),
        )?;
        Ok(file_client)
    }

    async fn open_dir_checked(
        &self,
        path: &str,
        flags: u32,
    ) -> Result<Option<fio::DirectoryProxy>> {
        if let Some(_) = self.dirent(path).await? {
            Ok(Some(self.open_dir(path, flags)?))
        } else {
            Ok(None)
        }
    }

    async fn read_file(&self, path: &str) -> Result<String> {
        Ok(std::str::from_utf8(
            self.open_file(path, fio::OPEN_RIGHT_READABLE)
                .context(format!("reading file from path: {}", path))?
                .read_max_bytes()
                .await
                .context(format!("reading max buf from file: {}", path))?
                .as_ref(),
        )?
        .to_owned())
    }

    async fn dirents(&self) -> Result<Vec<Dirent>> {
        let mut res = Vec::new();
        loop {
            let buf = self.dirent_bytes().await?;
            if buf.is_empty() {
                break;
            }
            let mut bref: &[u8] = buf.as_ref();
            let mut bref_ref = &mut bref;
            loop {
                let dirent_raw = Dirent::parse::<&[u8], &mut &[u8]>(&mut bref_ref)?;
                if dirent_raw.name != "." {
                    res.push(dirent_raw);
                }
                if is_empty::<&[u8], &mut &[u8]>(&mut bref_ref) {
                    break;
                }
            }
        }
        Ok(res)
    }

    async fn dirent(&self, name: &str) -> Result<Option<Dirent>> {
        Ok(self.dirents().await?.iter().find(|d| d.name == name).map(|d| d.clone()))
    }
}

#[async_trait]
pub trait FileProxyExt {
    /// Attempts to read the max bytes for a given file, returning a u8 buffer.
    async fn read_max_bytes(&self) -> Result<Vec<u8>>;
}

#[async_trait]
impl FileProxyExt for fio::FileProxy {
    async fn read_max_bytes(&self) -> Result<Vec<u8>> {
        let (status, buf) = self.read(fio::MAX_BUF).await?;
        Status::ok(status).context("read file")?;
        Ok(buf)
    }
}

type U64 = zerocopy::U64<NetworkEndian>;

#[derive(Debug, Eq, PartialEq, PartialOrd, Ord, Clone, Copy)]
pub enum DirentType {
    Unknown(u8),
    Directory,
    BlockDevice,
    File,
    Socket,
    Service,
}

impl TryFrom<DirentType> for u32 {
    type Error = anyhow::Error;

    fn try_from(d: DirentType) -> Result<u32> {
        Ok(match d {
            DirentType::Unknown(t) => return Err(anyhow!("unknown type: {}", t)),
            DirentType::Directory => fio::MODE_TYPE_DIRECTORY,
            DirentType::BlockDevice => fio::MODE_TYPE_BLOCK_DEVICE,
            DirentType::File => fio::MODE_TYPE_FILE,
            DirentType::Socket => fio::MODE_TYPE_SOCKET,
            DirentType::Service => fio::MODE_TYPE_SERVICE,
        })
    }
}

impl From<u8> for DirentType {
    fn from(dirent_type: u8) -> Self {
        match dirent_type {
            fio::DIRENT_TYPE_DIRECTORY => DirentType::Directory,
            fio::DIRENT_TYPE_BLOCK_DEVICE => DirentType::BlockDevice,
            fio::DIRENT_TYPE_FILE => DirentType::File,
            fio::DIRENT_TYPE_SOCKET => DirentType::Socket,
            fio::DIRENT_TYPE_SERVICE => DirentType::Service,
            _ => DirentType::Unknown(dirent_type),
        }
    }
}

trait ToDirProxy {
    /// Attempts to use the root `DirectoryProxy`'s `open` command to open a `DirectoryProxy`.
    ///
    /// Passes `OPEN_FLAG_DIRECTORY` bitwise or'd with `flags` to the `open` command.
    fn to_dir_proxy(&self, root: &fio::DirectoryProxy, flags: u32) -> Result<fio::DirectoryProxy>;
}

trait ToFileProxy {
    /// Attempts to use the root `DirectoryProxy`'s `open` command to open a `FileProxy`.
    ///
    /// Passes `OPEN_FLAG_NOT_DIRECTORY` bitwise or'd with `flags` to the `open` command.
    fn to_file_proxy(&self, root: &fio::DirectoryProxy, flags: u32) -> Result<fio::FileProxy>;
}

#[derive(Debug, Eq, PartialEq, Clone)]
pub struct Dirent {
    pub ino: u64,
    pub size: u8,
    pub dirent_type: DirentType,
    pub name: String,
}

impl ToDirProxy for Dirent {
    fn to_dir_proxy(&self, root: &fio::DirectoryProxy, flags: u32) -> Result<fio::DirectoryProxy> {
        match self.dirent_type {
            DirentType::Directory => {
                let (dir_client, dir_server) =
                    fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
                        .context("creating fidl proxy")?;
                root.open(
                    fio::OPEN_FLAG_DIRECTORY | flags,
                    self.dirent_type.try_into()?,
                    &self.name,
                    fidl::endpoints::ServerEnd::new(dir_server.into_channel()),
                )?;

                Ok(dir_client)
            }
            _ => Err(anyhow!("wrong type for dir proxy: {:?}", self.dirent_type)),
        }
    }
}

impl ToFileProxy for Dirent {
    fn to_file_proxy(&self, root: &fio::DirectoryProxy, flags: u32) -> Result<fio::FileProxy> {
        match self.dirent_type {
            DirentType::Directory | DirentType::Unknown(_) => {
                Err(anyhow!("wrong type for file proxy: {:?}", self.dirent_type))
            }
            _ => {
                let (file_client, file_server) = fidl::endpoints::create_proxy::<fio::FileMarker>()
                    .context("creating fidl proxy")?;
                root.open(
                    fio::OPEN_FLAG_NOT_DIRECTORY | flags,
                    self.dirent_type.try_into()?,
                    &self.name,
                    fidl::endpoints::ServerEnd::new(file_server.into_channel()),
                )?;
                Ok(file_client)
            }
        }
    }
}

impl Dirent {
    fn parse<B: ByteSlice, BV: BufferView<B>>(buffer: &mut BV) -> Result<Self> {
        let ino = buffer.take_obj_front::<U64>().context("reading ino")?;
        let size = buffer.take_byte_front().context("reading size")?;
        let dirent_type: DirentType = buffer.take_byte_front().context("reading type")?.into();
        let name = std::str::from_utf8(&buffer.take_front(size.into()).context("reading name")?)
            .context("dirent utf8 parsing")?
            .to_owned();
        Ok(Self { ino: ino.get(), size, dirent_type, name })
    }
}

// This is just here to satisfy the trait constraints, otherwise the compiler
// will complain that it doesn't know <B> (if done outside of this function).
// This is sort of the limitation of type inference where it's a pointer to
// a pointer to a pointer.
#[inline]
pub fn is_empty<B: ByteSlice, BV: BufferView<B>>(buffer: &mut BV) -> bool {
    buffer.len() == 0
}

/// Contains some fake directories and test harness utilities for constructing
/// unit tests on a fake read-only file system.
#[cfg(test)]
pub mod testing {
    use {
        fidl_fuchsia_io as fio, fuchsia_zircon_status::Status, futures::TryStreamExt,
        std::collections::HashMap,
    };

    const DIRENT_HEADER_SIZE: usize = 10;

    /// The type of a fake dirent.
    #[derive(Clone, PartialEq, Debug, Eq)]
    pub enum TestDirentTreeType {
        /// The file variation of a dirent.
        File,
        /// The directory variation of a dirent.
        Dir,
    }

    impl From<&TestDirentTreeType> for u8 {
        fn from(t: &TestDirentTreeType) -> Self {
            match t {
                TestDirentTreeType::Dir => fio::DIRENT_TYPE_DIRECTORY,
                TestDirentTreeType::File => fio::DIRENT_TYPE_FILE,
            }
        }
    }

    /// Represents a whole faked file system.
    #[derive(Clone, Debug)]
    pub struct TestDirentTree {
        /// The name fo the entity.
        pub name: String,
        /// The dirent type of this entity.
        pub ttype: TestDirentTreeType,
        /// If this is a file, the contents of the file.
        pub contents: String,
        /// If this is a directory, the children of this directory.
        pub children: Option<HashMap<String, Self>>,
    }

    /// A builder trait for adding files to a faked file tree.
    pub trait TreeBuilder {
        /// Adds a file while returning an instance of the builder, allowing
        /// for this function to be chained.
        fn add_file(self, name: &str, contents: &str) -> Self;
    }

    impl TreeBuilder for TestDirentTree {
        fn add_file(mut self, name: &str, contents: &str) -> Self {
            (&mut self).add_file(name, contents);
            self
        }
    }

    impl TreeBuilder for &mut TestDirentTree {
        fn add_file(self, name: &str, contents: &str) -> Self {
            let name = name.to_owned();
            let contents = contents.to_owned();
            self.children.as_mut().unwrap().insert(
                name.clone(),
                TestDirentTree { name, ttype: TestDirentTreeType::File, contents, children: None },
            );
            self
        }
    }

    impl TestDirentTree {
        /// Creates a root directory with no name and no children.
        pub fn root() -> Self {
            Self {
                name: "".to_string(),
                ttype: TestDirentTreeType::Dir,
                contents: "".to_string(),
                children: Some(HashMap::new()),
            }
        }

        /// Adds a directory to the tree, and then returns an instance of `Self`
        /// so that subsequent commands can be chained to this tree.
        pub fn add_dir<'a>(&'a mut self, name: &str) -> &'a mut Self {
            let name = name.to_owned();
            self.children.as_mut().unwrap().insert(
                name.clone(),
                TestDirentTree {
                    name: name.clone(),
                    ttype: TestDirentTreeType::Dir,
                    contents: "".to_owned(),
                    children: Some(HashMap::new()),
                },
            );

            self.children.as_mut().unwrap().get_mut(&name).unwrap()
        }
    }

    fn launch_fake_file(
        entity: TestDirentTree,
        server: fidl::endpoints::ServerEnd<fio::FileMarker>,
    ) {
        fuchsia_async::Task::local(async move {
            let mut stream =
                server.into_stream().expect("converting fake file server proxy to stream");
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    fio::FileRequest::Read { count: _, responder } => {
                        responder
                            .send(Status::OK.into_raw(), entity.contents.clone().as_ref())
                            .expect("writing file test response");
                    }
                    _ => panic!("not supported"),
                }
            }
        })
        .detach();
    }

    fn launch_fake_directory(
        root: TestDirentTree,
        server: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
    ) {
        fuchsia_async::Task::local(async move {
            let mut finished_reading_dirents = false;
            let mut stream =
                server.into_stream().expect("converting fake dir server proxy to stream");
            'stream_loop: while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    fio::DirectoryRequest::Open {
                        flags: _,
                        mode,
                        path,
                        object,
                        control_handle: _,
                    } => {
                        let mut iter = path.split("/");
                        let mut child =
                            match root.children.as_ref().unwrap().get(iter.next().unwrap()) {
                                Some(child) => child.clone(),
                                None => {
                                    object.close_with_epitaph(Status::NOT_FOUND).unwrap();
                                    continue;
                                }
                            };
                        for entry in iter {
                            child = match child.children.as_ref().unwrap().get(entry) {
                                Some(child) => child.clone(),
                                None => {
                                    object.close_with_epitaph(Status::NOT_FOUND).unwrap();
                                    continue 'stream_loop;
                                }
                            }
                        }
                        match child.ttype {
                            TestDirentTreeType::Dir => {
                                assert_eq!(mode, fio::MODE_TYPE_DIRECTORY);
                                launch_fake_directory(
                                    child,
                                    fidl::endpoints::ServerEnd::new(object.into_channel()),
                                );
                            }
                            TestDirentTreeType::File => {
                                assert_eq!(mode, fio::MODE_TYPE_FILE);
                                launch_fake_file(
                                    child,
                                    fidl::endpoints::ServerEnd::new(object.into_channel()),
                                );
                            }
                        }
                    }
                    fio::DirectoryRequest::ReadDirents { max_bytes: _, responder } => {
                        let res = if !finished_reading_dirents {
                            let children = root.children.as_ref().unwrap();
                            children.values().fold(Vec::with_capacity(256), |mut acc, child| {
                                let mut buf =
                                    Vec::with_capacity(DIRENT_HEADER_SIZE + child.name.len() + 100);
                                buf.append(&mut vec![0, 0, 0, 0, 0, 0, 0, 0]);
                                buf.push(child.name.len() as u8);
                                buf.push(u8::from(&child.ttype));
                                // unsafe: this is a test, plus the UTF-8 value is checked later.
                                buf.append(unsafe { child.name.clone().as_mut_vec() });
                                acc.append(&mut buf);
                                acc
                            })
                        } else {
                            vec![]
                        };
                        finished_reading_dirents = !finished_reading_dirents;
                        responder
                            .send(Status::OK.into_raw(), res.as_ref())
                            .expect("dirents read test response");
                    }
                    _ => panic!("unsupported"),
                }
            }
        })
        .detach();
    }

    /// Launches a fake file system based off of the passed tree.
    ///
    /// Currently the file system is intended to be read-only.
    pub fn setup_fake_directory(root: TestDirentTree) -> fio::DirectoryProxy {
        let (proxy, server) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().expect("making fake dir proxy");
        launch_fake_directory(root, server);
        proxy
    }
}

#[cfg(test)]
pub mod test {
    use super::testing::*;
    use super::*;

    impl Dirent {
        /// Creates a default (everything zeroed out) file with the given name.
        fn anonymous_file(name: &str) -> Self {
            Self { ino: 0, size: 0, dirent_type: DirentType::File, name: name.to_owned() }
        }

        /// Creates a default (everything zeroed out) directory with the given name.
        fn anonymous_dir(name: &str) -> Self {
            Self { ino: 0, size: 0, dirent_type: DirentType::Directory, name: name.to_owned() }
        }
    }

    fn take_byte<B: ByteSlice, BV: BufferView<B>>(buffer: &mut BV) {
        let _ = buffer.take_byte_front().unwrap();
    }

    #[test]
    fn test_buffer_is_empty() {
        let buf: [u8; 1] = [2];
        let mut bref = &buf[..];
        let mut bref_ref = &mut bref;
        assert!(!is_empty::<&[u8], &mut &[u8]>(&mut bref_ref));
        take_byte::<&[u8], &mut &[u8]>(&mut bref_ref);
        assert!(is_empty::<&[u8], &mut &[u8]>(&mut bref_ref));
    }

    #[test]
    fn test_dirent_to_mode_type() {
        let dtype = DirentType::Unknown(252u8);
        assert!(u32::try_from(dtype).is_err());

        struct Test {
            dtype: DirentType,
            expected: u32,
        }
        for test in vec![
            Test { dtype: DirentType::Directory, expected: fio::MODE_TYPE_DIRECTORY },
            Test { dtype: DirentType::BlockDevice, expected: fio::MODE_TYPE_BLOCK_DEVICE },
            Test { dtype: DirentType::File, expected: fio::MODE_TYPE_FILE },
            Test { dtype: DirentType::Socket, expected: fio::MODE_TYPE_SOCKET },
            Test { dtype: DirentType::Service, expected: fio::MODE_TYPE_SERVICE },
        ]
        .iter()
        {
            assert_eq!(u32::try_from(test.dtype).unwrap(), test.expected);
        }
    }

    #[test]
    fn test_dirent_u8_to_dirent_type() {
        struct Test {
            u8type: u8,
            expected: DirentType,
        }
        for test in vec![
            Test { u8type: fio::DIRENT_TYPE_DIRECTORY, expected: DirentType::Directory },
            Test { u8type: fio::DIRENT_TYPE_BLOCK_DEVICE, expected: DirentType::BlockDevice },
            Test { u8type: fio::DIRENT_TYPE_FILE, expected: DirentType::File },
            Test { u8type: fio::DIRENT_TYPE_SOCKET, expected: DirentType::Socket },
            Test { u8type: fio::DIRENT_TYPE_SERVICE, expected: DirentType::Service },
            Test { u8type: 253u8, expected: DirentType::Unknown(253u8) },
        ]
        .iter()
        {
            assert_eq!(DirentType::from(test.u8type), test.expected);
        }
    }

    #[test]
    fn test_parse_dirent_raw() -> Result<()> {
        #[rustfmt::skip]
        let buf = [
            // ino
            42, 0, 0, 0, 0, 0, 0, 0,
            // name length
            4,
            // type
            fio::DIRENT_TYPE_FILE,
            // name
            't' as u8, 'e' as u8, 's' as u8, 't' as u8,
            // ino
            41, 0, 0, 0, 0, 0, 0, 1,
            // name length
            5,
            // type
            fio::DIRENT_TYPE_SERVICE,
            // name
            'f' as u8, 'o' as u8, 'o' as u8, 'o' as u8, 'o' as u8,
        ];

        let mut bref = &buf[..];
        let mut bref_ref = &mut bref;
        let dirent_raw = Dirent::parse::<&[u8], &mut &[u8]>(&mut bref_ref)?;
        assert_eq!(dirent_raw.size, 4);
        assert_eq!(dirent_raw.ino, 0x2a00000000000000);
        assert_eq!(dirent_raw.dirent_type, DirentType::File);
        assert_eq!(dirent_raw.name, "test".to_owned());
        let dirent_raw = Dirent::parse::<&[u8], &mut &[u8]>(&mut bref_ref)?;
        assert_eq!(dirent_raw.size, 5);
        assert_eq!(dirent_raw.ino, 0x2900000000000001);
        assert_eq!(dirent_raw.dirent_type, DirentType::Service);
        assert_eq!(dirent_raw.name, "foooo".to_owned());
        Ok(())
    }

    #[test]
    fn test_parse_dirent_raw_malformed() {
        #[rustfmt::skip]
        let buf = [
            // ino
            1, 0, 0, 0, 0, 0, 0, 0,
            // name length
            12,
            // type
            fio::DIRENT_TYPE_FILE,
            // name
            'w' as u8, 'o' as u8, 'o' as u8, 'p' as u8, 's' as u8,
        ];

        let mut bref = &buf[..];
        let mut bref_ref = &mut bref;
        assert!(Dirent::parse::<&[u8], &mut &[u8]>(&mut bref_ref).is_err());

        let buf = [1, 2, 3, 4];
        let mut bref = &buf[..];
        let mut bref_ref = &mut bref;
        assert!(Dirent::parse::<&[u8], &mut &[u8]>(&mut bref_ref).is_err());
    }

    #[test]
    fn test_to_proxy_fails() {
        let (root, _dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("creating fake root proxy");
        let dirent = Dirent::anonymous_dir("foo");
        assert!(dirent.to_file_proxy(&root, fio::OPEN_RIGHT_READABLE).is_err());
        let dirent = Dirent::anonymous_file("foo");
        assert!(dirent.to_dir_proxy(&root, fio::OPEN_RIGHT_READABLE).is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_read_file_from_str() {
        let mut root = TestDirentTree::root().add_file("foo", "test").add_file("bar", "other_test");
        root.add_dir("baz").add_file("mumble", "testarossa");
        let root = setup_fake_directory(root);

        let buf = root
            .open_file("foo", fio::OPEN_RIGHT_READABLE)
            .unwrap()
            .read_max_bytes()
            .await
            .unwrap();
        assert_eq!("test", std::str::from_utf8(&buf[..]).unwrap());
        let buf = root
            .open_file("bar", fio::OPEN_RIGHT_READABLE)
            .unwrap()
            .read_max_bytes()
            .await
            .unwrap();
        assert_eq!("other_test", std::str::from_utf8(&buf[..]).unwrap());
        let buf = root
            .open_file("baz/mumble", fio::OPEN_RIGHT_READABLE)
            .unwrap()
            .read_max_bytes()
            .await
            .unwrap();
        assert_eq!("testarossa", std::str::from_utf8(&buf[..]).unwrap());
        assert!(root
            .open_file("blorp", fio::OPEN_RIGHT_READABLE)
            .unwrap()
            .read_max_bytes()
            .await
            .is_err());
        assert!(root
            .open_file("blorp/blip/bloop", fio::OPEN_RIGHT_READABLE)
            .unwrap()
            .read_max_bytes()
            .await
            .is_err());
    }
}
