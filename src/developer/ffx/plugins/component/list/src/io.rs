// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

/// Applies a predicate to all children.
#[async_trait]
pub trait MapChildren<T, E, F>
where
    F: Future<Output = Result<E>>,
{
    async fn map_children(self, f: fn(T) -> F) -> Result<Vec<E>>;
}

#[async_trait]
impl<E, F> MapChildren<fio::DirectoryProxy, E, F> for fio::DirectoryProxy
where
    F: Future<Output = Result<E>> + 'static + Send,
    E: 'static + Send,
{
    async fn map_children(self, func: fn(fio::DirectoryProxy) -> F) -> Result<Vec<E>> {
        // TODO(awdavies): Run this in parallel with some kind of barrier to
        // prevent overspawning channels.
        let mut res = Vec::new();
        for child in self.read_raw_dirents().await? {
            let child_root = child.to_dir_proxy(&self)?;
            res.push(func(child_root).await?);
        }
        Ok(res)
    }
}

#[async_trait]
pub trait DirectoryProxyExt {
    async fn read_file(&self, path: &str) -> Result<String>;

    async fn read_raw_dirents(&self) -> Result<Vec<DirentRaw>>;

    async fn dirents(&self) -> Result<Vec<u8>>;

    async fn get_dirent(&self, name: &str) -> Result<Option<DirentRaw>>;
}

#[async_trait]
impl DirectoryProxyExt for fio::DirectoryProxy {
    async fn read_file(&self, path: &str) -> Result<String> {
        Ok(std::str::from_utf8(
            path.to_file_proxy(self)
                .context(format!("reading file from path: {}", path))?
                .read_max_bytes()
                .await?
                .as_ref(),
        )?
        .to_owned())
    }

    async fn read_raw_dirents(&self) -> Result<Vec<DirentRaw>> {
        let mut res = Vec::new();
        loop {
            let buf = self.dirents().await?;
            if buf.is_empty() {
                break;
            }
            let mut bref: &[u8] = buf.as_ref();
            let mut bref_ref = &mut bref;
            loop {
                let dirent_raw = DirentRaw::parse::<&[u8], &mut &[u8]>(&mut bref_ref)?;
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

    async fn dirents(&self) -> Result<Vec<u8>> {
        let (status, buf) = self.read_dirents(fio::MAX_BUF).await.context("read_dirents")?;
        Status::ok(status).context("dirents result")?;
        Ok(buf)
    }

    async fn get_dirent(&self, name: &str) -> Result<Option<DirentRaw>> {
        Ok(self.read_raw_dirents().await?.iter().find(|d| d.name == name).map(|d| d.clone()))
    }
}

#[async_trait]
pub trait FileProxyExt {
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

pub trait ToDirProxy {
    fn to_dir_proxy(&self, root: &fio::DirectoryProxy) -> Result<fio::DirectoryProxy>;
}

pub trait ToFileProxy {
    fn to_file_proxy(&self, root: &fio::DirectoryProxy) -> Result<fio::FileProxy>;
}

#[derive(Debug, Eq, PartialEq, Clone)]
pub struct DirentRaw {
    pub ino: u64,
    pub size: u8,
    pub dirent_type: DirentType,
    pub name: String,
}

impl DirentRaw {
    pub fn anonymous_file(name: &str) -> Self {
        Self { ino: 0, size: 0, dirent_type: DirentType::File, name: name.to_owned() }
    }

    pub fn anonymous_dir(name: &str) -> Self {
        Self { ino: 0, size: 0, dirent_type: DirentType::Directory, name: name.to_owned() }
    }
}

impl ToDirProxy for DirentRaw {
    fn to_dir_proxy(&self, root: &fio::DirectoryProxy) -> Result<fio::DirectoryProxy> {
        match self.dirent_type {
            DirentType::Directory => {
                let (dir_client, dir_server) =
                    fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
                        .context("creating fidl proxy")?;
                root.open(
                    fio::OPEN_FLAG_DIRECTORY | fio::OPEN_RIGHT_READABLE,
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

impl ToFileProxy for DirentRaw {
    fn to_file_proxy(&self, root: &fio::DirectoryProxy) -> Result<fio::FileProxy> {
        match self.dirent_type {
            DirentType::Directory | DirentType::Unknown(_) => {
                Err(anyhow!("wrong type for file proxy: {:?}", self.dirent_type))
            }
            _ => {
                let (file_client, file_server) = fidl::endpoints::create_proxy::<fio::FileMarker>()
                    .context("creating fidl proxy")?;
                root.open(
                    fio::OPEN_FLAG_NOT_DIRECTORY | fio::OPEN_RIGHT_READABLE,
                    self.dirent_type.try_into()?,
                    &self.name,
                    fidl::endpoints::ServerEnd::new(file_server.into_channel()),
                )?;
                Ok(file_client)
            }
        }
    }
}

impl ToFileProxy for &str {
    fn to_file_proxy(&self, root: &fio::DirectoryProxy) -> Result<fio::FileProxy> {
        let dirent = DirentRaw::anonymous_file(self);
        dirent.to_file_proxy(root)
    }
}

impl ToDirProxy for &str {
    fn to_dir_proxy(&self, root: &fio::DirectoryProxy) -> Result<fio::DirectoryProxy> {
        let dirent = DirentRaw::anonymous_dir(self);
        dirent.to_dir_proxy(root)
    }
}

impl DirentRaw {
    pub fn parse<B: ByteSlice, BV: BufferView<B>>(buffer: &mut BV) -> Result<Self> {
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

#[cfg(test)]
pub(crate) mod test {
    use super::*;
    use futures::TryStreamExt;
    use std::collections::HashMap;

    const DIRENT_HEADER_SIZE: usize = 10;

    #[derive(Clone, PartialEq, Debug, Eq)]
    pub enum TestDirentTreeType {
        File,
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

    #[derive(Clone, Debug)]
    pub struct TestDirentTree {
        pub name: String,
        pub ttype: TestDirentTreeType,
        pub contents: String,
        pub children: Option<HashMap<String, Self>>,
    }

    pub trait TreeBuilder {
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
        pub fn root() -> Self {
            Self {
                name: "".to_string(),
                ttype: TestDirentTreeType::Dir,
                contents: "".to_string(),
                children: Some(HashMap::new()),
            }
        }

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
        let dirent_raw = DirentRaw::parse::<&[u8], &mut &[u8]>(&mut bref_ref)?;
        assert_eq!(dirent_raw.size, 4);
        assert_eq!(dirent_raw.ino, 0x2a00000000000000);
        assert_eq!(dirent_raw.dirent_type, DirentType::File);
        assert_eq!(dirent_raw.name, "test".to_owned());
        let dirent_raw = DirentRaw::parse::<&[u8], &mut &[u8]>(&mut bref_ref)?;
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
        assert!(DirentRaw::parse::<&[u8], &mut &[u8]>(&mut bref_ref).is_err());

        let buf = [1, 2, 3, 4];
        let mut bref = &buf[..];
        let mut bref_ref = &mut bref;
        assert!(DirentRaw::parse::<&[u8], &mut &[u8]>(&mut bref_ref).is_err());
    }

    #[test]
    fn test_to_proxy_fails() {
        let (root, _dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("creating fake root proxy");
        let dirent = DirentRaw::anonymous_dir("foo");
        assert!(dirent.to_file_proxy(&root).is_err());
        let dirent = DirentRaw::anonymous_file("foo");
        assert!(dirent.to_dir_proxy(&root).is_err());
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

    pub fn setup_fake_directory(root: TestDirentTree) -> fio::DirectoryProxy {
        let (proxy, server) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().expect("making fake dir proxy");
        launch_fake_directory(root, server);
        proxy
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_read_file_from_str() {
        let mut root = TestDirentTree::root().add_file("foo", "test").add_file("bar", "other_test");
        root.add_dir("baz").add_file("mumble", "testarossa");
        let root = setup_fake_directory(root);

        let buf = "foo".to_file_proxy(&root).unwrap().read_max_bytes().await.unwrap();
        assert_eq!("test", std::str::from_utf8(&buf[..]).unwrap());
        let buf = "bar".to_file_proxy(&root).unwrap().read_max_bytes().await.unwrap();
        assert_eq!("other_test", std::str::from_utf8(&buf[..]).unwrap());
        let buf = "baz/mumble".to_file_proxy(&root).unwrap().read_max_bytes().await.unwrap();
        assert_eq!("testarossa", std::str::from_utf8(&buf[..]).unwrap());
        assert!("blorp".to_file_proxy(&root).unwrap().read_max_bytes().await.is_err());
        assert!("blorp/blip/bloop".to_file_proxy(&root).unwrap().read_max_bytes().await.is_err());
    }
}
