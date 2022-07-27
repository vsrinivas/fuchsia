// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::device::{ContentFormat, Device},
    anyhow::Error,
    async_trait::async_trait,
    std::{
        collections::BTreeMap,
        ops::Bound,
        path::{Path, PathBuf},
    },
};

const BLOBFS_PARTITION_LABEL: &str = "blobfs";
const DATA_PARTITION_LABEL: &str = "data";

const BLOBFS_TYPE_GUID: [u8; 16] = [
    0x0e, 0x38, 0x67, 0x29, 0x4c, 0x13, 0xbb, 0x4c, 0xb6, 0xda, 0x17, 0xe7, 0xce, 0x1c, 0xa4, 0x5d,
];
const DATA_TYPE_GUID: [u8; 16] = [
    0x0c, 0x5f, 0x18, 0x08, 0x2d, 0x89, 0x8a, 0x42, 0xa7, 0x89, 0xdb, 0xee, 0xc8, 0xf5, 0x5e, 0x6a,
];

const FVM_DRIVER_PATH: &str = "fvm.so";
const GPT_DRIVER_PATH: &str = "gpt.so";
// const ZXCRYPT_DRIVER_PATH: &str = "mbr.so";
const BOOTPART_DRIVER_PATH: &str = "bootpart.so";
// const BLOCK_VERITY_DRIVER_PATH: &str = "block-verity.so";
const NAND_BROKER_DRIVER_PATH: &str = "nand-broker.so";

const BLOCK_FLAG_BOOTPART: u32 = 4;

/// Environment is a trait that performs actions when a device is matched.
#[async_trait]
pub trait Environment: Sync {
    /// Attaches the specified driver to the device.
    async fn attach_driver(&self, device: &dyn Device, driver_path: &str) -> Result<(), Error>;

    /// Mounts Blobfs on the given device.
    async fn mount_blobfs(&self, device: &dyn Device) -> Result<(), Error>;

    /// Mounts the data partition on the given device.
    async fn mount_data(&self, device: &dyn Device) -> Result<(), Error>;
}

#[async_trait]
pub trait Matcher: Send {
    /// Tries to match this device against this matcher.
    async fn match_device(
        &mut self,
        device: &dyn Device,
        env: &dyn Environment,
    ) -> Result<bool, Error>;

    /// This is called when a device appears that is a child of an already matched device.  It can
    /// be anywhere within the hierarchy, so not necessarily an immediate child.  Devices will be
    /// matched as children before being matched against global matches.
    async fn match_child(
        &mut self,
        _device: &dyn Device,
        _env: &dyn Environment,
        _parent_path: &Path,
    ) -> Result<bool, Error> {
        // By default, matchers don't match children.
        Ok(false)
    }
}

pub struct Matchers {
    matchers: Vec<Box<dyn Matcher>>,

    matched: BTreeMap<PathBuf, usize>,
}

impl Matchers {
    /// Create a new set of matchers. This essentially describes the expected partition layout for
    /// a device.
    pub fn new(config: fshost_config::Config) -> Self {
        let mut matchers = Vec::<Box<dyn Matcher>>::new();

        if config.bootpart {
            matchers.push(Box::new(BootpartMatcher::new()));
        }
        if config.nand {
            matchers.push(Box::new(NandMatcher::new()));
        }
        let gpt_matcher =
            Box::new(PartitionMapMatcher::new(ContentFormat::Gpt, false, GPT_DRIVER_PATH, ""));
        let mut fvm_matcher =
            Box::new(PartitionMapMatcher::new(ContentFormat::Fvm, false, FVM_DRIVER_PATH, ""));

        if config.blobfs {
            fvm_matcher.child_matchers.push(Box::new(BlobfsMatcher::new()));
        }
        if config.data {
            fvm_matcher.child_matchers.push(Box::new(DataMatcher::new()));
        }
        if config.gpt || !gpt_matcher.child_matchers.is_empty() {
            matchers.push(gpt_matcher);
        }
        if config.fvm || !fvm_matcher.child_matchers.is_empty() {
            matchers.push(fvm_matcher);
        }
        if config.gpt_all {
            matchers.push(Box::new(PartitionMapMatcher::new(
                ContentFormat::Gpt,
                true,
                GPT_DRIVER_PATH,
                "",
            )));
        }

        Matchers { matchers, matched: BTreeMap::new() }
    }

    /// Using the set of matchers we created, figure out if this block device matches any of our
    /// expected partitions. If it does, return the information needed to launch the filesystem,
    /// such as the component url or the shared library to pass to the driver binding.
    pub async fn match_device(
        &mut self,
        device: &dyn Device,
        env: &dyn Environment,
    ) -> Result<bool, Error> {
        let topological_path = device.topological_path().await?;
        if let Some((path, &index)) = self
            .matched
            .range::<Path, _>((Bound::Unbounded, Bound::Excluded(topological_path)))
            .next_back()
        {
            if topological_path.starts_with(path) {
                if self.matchers[index].match_child(device, env, path).await? {
                    self.matched.insert(topological_path.to_path_buf(), index);
                    return Ok(true);
                }
            }
        }
        for (index, m) in self.matchers.iter_mut().enumerate() {
            if m.match_device(device, env).await? {
                self.matched.insert(topological_path.to_path_buf(), index);
                return Ok(true);
            }
        }
        Ok(false)
    }
}

// Matches Bootpart devices.
struct BootpartMatcher();

impl BootpartMatcher {
    fn new() -> Self {
        BootpartMatcher()
    }
}

#[async_trait]
impl Matcher for BootpartMatcher {
    async fn match_device(
        &mut self,
        device: &dyn Device,
        env: &dyn Environment,
    ) -> Result<bool, Error> {
        if device.get_block_info().await?.flags & BLOCK_FLAG_BOOTPART == 0 {
            return Ok(false);
        }
        env.attach_driver(device, BOOTPART_DRIVER_PATH).await?;
        Ok(true)
    }
}

// Matches Nand devices.
struct NandMatcher();

impl NandMatcher {
    fn new() -> Self {
        NandMatcher()
    }
}

#[async_trait]
impl Matcher for NandMatcher {
    async fn match_device(
        &mut self,
        device: &dyn Device,
        env: &dyn Environment,
    ) -> Result<bool, Error> {
        if device.is_nand() {
            env.attach_driver(device, NAND_BROKER_DRIVER_PATH).await?;
            Ok(true)
        } else {
            Ok(false)
        }
    }
}

// Matches partition maps. Matching is done using content sniffing. `child_matchers` contain
// matchers that will match against partitions of partition maps.
struct PartitionMapMatcher {
    // The content format expected.
    content_format: ContentFormat,

    // If true, match against multiple devices. Otherwise, only the first is matched.
    allow_multiple: bool,

    // When matched, this driver is attached to the device.
    driver_path: &'static str,

    // The expected path suffixe used in the topological path. For example, FVM uses an "fvm/"
    // suffix.
    path_suffix: &'static str,

    // The topological paths of all devices matched so far.
    device_paths: Vec<String>,

    // A list of matchers to use against partitions of matched devices.
    child_matchers: Vec<Box<dyn Matcher>>,
}

impl PartitionMapMatcher {
    fn new(
        content_format: ContentFormat,
        allow_multiple: bool,
        driver_path: &'static str,
        path_suffix: &'static str,
    ) -> Self {
        Self {
            content_format,
            allow_multiple,
            driver_path,
            path_suffix,
            device_paths: Vec::new(),
            child_matchers: Vec::new(),
        }
    }
}

#[async_trait]
impl Matcher for PartitionMapMatcher {
    async fn match_device(
        &mut self,
        device: &dyn Device,
        env: &dyn Environment,
    ) -> Result<bool, Error> {
        let topological_path = device.topological_path().await?;
        if !self.allow_multiple && !self.device_paths.is_empty() {
            return Ok(false);
        }
        if device.content_format().await? == self.content_format {
            env.attach_driver(device, self.driver_path).await?;
            self.device_paths.push(topological_path.to_str().unwrap().to_string());
            Ok(true)
        } else {
            Ok(false)
        }
    }

    async fn match_child(
        &mut self,
        device: &dyn Device,
        env: &dyn Environment,
        parent_path: &Path,
    ) -> Result<bool, Error> {
        let topological_path = device.topological_path().await?;
        // Only match against children that are immediate children.
        // Child partitions should have topological paths of the form:
        //   ...<suffix>/<partition-name>/block
        if let Some(file_name) = topological_path.file_name() {
            if file_name == "block" {
                if let Some(head) = topological_path
                    .parent()
                    .and_then(Path::parent)
                    .and_then(Path::to_str)
                    .and_then(|p| p.strip_suffix(self.path_suffix))
                {
                    if Path::new(head) == parent_path {
                        for m in &mut self.child_matchers {
                            if m.match_device(device, env).await? {
                                return Ok(true);
                            }
                        }
                    }
                }
            }
        }
        Ok(false)
    }
}

struct PartitionMatcher {
    label: &'static str,
    type_guid: &'static [u8; 16],
}

impl PartitionMatcher {
    fn new(label: &'static str, type_guid: &'static [u8; 16]) -> Self {
        Self { label, type_guid }
    }
}

#[async_trait]
impl Matcher for PartitionMatcher {
    async fn match_device(
        &mut self,
        device: &dyn Device,
        _env: &dyn Environment,
    ) -> Result<bool, Error> {
        Ok(device.partition_label().await? == self.label
            && device.partition_type().await? == self.type_guid)
    }
}

// Matches against a Blobfs partition (by checking for partition label and type GUID).
struct BlobfsMatcher(PartitionMatcher);

impl BlobfsMatcher {
    fn new() -> Self {
        Self(PartitionMatcher::new(BLOBFS_PARTITION_LABEL, &BLOBFS_TYPE_GUID))
    }
}

#[async_trait]
impl Matcher for BlobfsMatcher {
    async fn match_device(
        &mut self,
        device: &dyn Device,
        env: &dyn Environment,
    ) -> Result<bool, Error> {
        if self.0.match_device(device, env).await? {
            env.mount_blobfs(device).await?;
            Ok(true)
        } else {
            Ok(false)
        }
    }
}

// Matches against a Data partition (by checking for partition label and type GUID).
struct DataMatcher(PartitionMatcher);

impl DataMatcher {
    fn new() -> Self {
        Self(PartitionMatcher::new(DATA_PARTITION_LABEL, &DATA_TYPE_GUID))
    }
}

#[async_trait]
impl Matcher for DataMatcher {
    async fn match_device(
        &mut self,
        device: &dyn Device,
        env: &dyn Environment,
    ) -> Result<bool, Error> {
        if self.0.match_device(device, env).await? {
            env.mount_data(device).await?;
            Ok(true)
        } else {
            Ok(false)
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{
            ContentFormat, Device, Environment, Matchers, BLOBFS_PARTITION_LABEL, BLOBFS_TYPE_GUID,
            BLOCK_FLAG_BOOTPART, BOOTPART_DRIVER_PATH, DATA_PARTITION_LABEL, DATA_TYPE_GUID,
            FVM_DRIVER_PATH, GPT_DRIVER_PATH, NAND_BROKER_DRIVER_PATH,
        },
        crate::config::default_config,
        anyhow::Error,
        async_trait::async_trait,
        fidl::encoding::Decodable,
        fidl_fuchsia_hardware_block::BlockInfo,
        fuchsia_async as fasync,
        std::{
            path::{Path, PathBuf},
            sync::Mutex,
        },
    };

    struct MockDevice {
        block_flags: u32,
        is_nand: bool,
        content_format: ContentFormat,
        topological_path: PathBuf,
        partition_label: Option<String>,
        partition_type: Option<[u8; 16]>,
    }

    impl MockDevice {
        fn new() -> Self {
            MockDevice {
                block_flags: 0,
                is_nand: false,
                content_format: ContentFormat::Unknown,
                topological_path: Path::new("mock_device").to_path_buf(),
                partition_label: None,
                partition_type: None,
            }
        }
        fn set_block_flags(mut self, flags: u32) -> Self {
            self.block_flags = flags;
            self
        }
        fn set_nand(mut self, v: bool) -> Self {
            self.is_nand = v;
            self
        }
        fn set_content_format(mut self, format: ContentFormat) -> Self {
            self.content_format = format;
            self
        }
        fn set_topological_path(mut self, path: impl ToString) -> Self {
            self.topological_path = path.to_string().into();
            self
        }
        fn set_partition_label(mut self, label: impl ToString) -> Self {
            self.partition_label = Some(label.to_string());
            self
        }
        fn set_partition_type(mut self, partition_type: &[u8; 16]) -> Self {
            self.partition_type = Some(partition_type.clone());
            self
        }
    }

    #[async_trait]
    impl Device for MockDevice {
        async fn get_block_info(&self) -> Result<fidl_fuchsia_hardware_block::BlockInfo, Error> {
            Ok(BlockInfo { flags: self.block_flags, ..BlockInfo::new_empty() })
        }
        fn is_nand(&self) -> bool {
            self.is_nand
        }
        async fn content_format(&self) -> Result<ContentFormat, Error> {
            Ok(self.content_format)
        }
        async fn topological_path<'a>(&'a self) -> Result<&'a Path, Error> {
            Ok(&self.topological_path)
        }
        async fn partition_label(&self) -> Result<&str, Error> {
            Ok(self
                .partition_label
                .as_ref()
                .unwrap_or_else(|| panic!("Unexpected call to partition_label")))
        }
        async fn partition_type<'a>(&'a self) -> Result<&'a [u8; 16], Error> {
            Ok(self
                .partition_type
                .as_ref()
                .unwrap_or_else(|| panic!("Unexpected call to partition_type")))
        }
    }

    struct MockEnv {
        expected_driver_path: Mutex<Option<String>>,
        expect_mount_blobfs: Mutex<bool>,
        expect_mount_data: Mutex<bool>,
    }

    impl MockEnv {
        fn new() -> Self {
            MockEnv {
                expected_driver_path: Mutex::new(None),
                expect_mount_blobfs: Mutex::new(false),
                expect_mount_data: Mutex::new(false),
            }
        }
        fn expect_attach_driver(mut self, path: impl ToString) -> Self {
            *self.expected_driver_path.get_mut().unwrap() = Some(path.to_string());
            self
        }
        fn expect_mount_blobfs(mut self) -> Self {
            *self.expect_mount_blobfs.get_mut().unwrap() = true;
            self
        }
        fn expect_mount_data(mut self) -> Self {
            *self.expect_mount_data.get_mut().unwrap() = true;
            self
        }
    }

    #[async_trait]
    impl Environment for MockEnv {
        async fn attach_driver(
            &self,
            _device: &dyn Device,
            driver_path: &str,
        ) -> Result<(), Error> {
            assert_eq!(
                driver_path,
                self.expected_driver_path
                    .lock()
                    .unwrap()
                    .take()
                    .expect("Unexpected call to attach_driver")
            );
            Ok(())
        }

        async fn mount_blobfs(&self, _device: &dyn Device) -> Result<(), Error> {
            assert_eq!(
                std::mem::take(&mut *self.expect_mount_blobfs.lock().unwrap()),
                true,
                "Unexpected call to mount_blobfs"
            );
            Ok(())
        }

        async fn mount_data(&self, _device: &dyn Device) -> Result<(), Error> {
            assert_eq!(
                std::mem::take(&mut *self.expect_mount_data.lock().unwrap()),
                true,
                "Unexpected call to mount_data"
            );
            Ok(())
        }
    }

    impl Drop for MockEnv {
        fn drop(&mut self) {
            assert!(self.expected_driver_path.get_mut().unwrap().is_none());
            assert!(!*self.expect_mount_blobfs.lock().unwrap());
            assert!(!*self.expect_mount_data.lock().unwrap());
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_bootpart_matcher() {
        let mock_device = MockDevice::new().set_block_flags(BLOCK_FLAG_BOOTPART);

        // Check no match when disabled in config.
        assert!(!Matchers::new(fshost_config::Config { bootpart: false, ..default_config() })
            .match_device(&mock_device, &MockEnv::new())
            .await
            .expect("match_device failed"));

        assert!(Matchers::new(default_config())
            .match_device(&mock_device, &MockEnv::new().expect_attach_driver(BOOTPART_DRIVER_PATH))
            .await
            .expect("match_device failed"));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_nand_matcher() {
        let device = MockDevice::new().set_nand(true);
        let env = MockEnv::new().expect_attach_driver(NAND_BROKER_DRIVER_PATH);

        // Default shouldn't match.
        assert!(!Matchers::new(default_config())
            .match_device(&device, &env)
            .await
            .expect("match_device failed"));

        assert!(Matchers::new(fshost_config::Config { nand: true, ..default_config() })
            .match_device(&device, &env)
            .await
            .expect("match_device failed"));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_partition_map_matcher() {
        let env = MockEnv::new().expect_attach_driver(GPT_DRIVER_PATH);

        // Check no match when disabled in config.
        let device = MockDevice::new().set_content_format(ContentFormat::Gpt);
        assert!(!Matchers::new(fshost_config::Config {
            blobfs: false,
            data: false,
            gpt: false,
            ..default_config()
        })
        .match_device(&device, &env)
        .await
        .expect("match_device failed"));

        let mut matchers = Matchers::new(default_config());
        assert!(matchers.match_device(&device, &env).await.expect("match_device failed"));

        // More GPT devices should not get matched.
        assert!(!matchers.match_device(&device, &env).await.expect("match_device failed"));

        // The gpt_all config should allow multiple GPT devices to be matched.
        let mut matchers =
            Matchers::new(fshost_config::Config { gpt_all: true, ..default_config() });
        let env = MockEnv::new().expect_attach_driver(GPT_DRIVER_PATH);
        assert!(matchers.match_device(&device, &env).await.expect("match_device failed"));
        let env = MockEnv::new().expect_attach_driver(GPT_DRIVER_PATH);
        assert!(matchers.match_device(&device, &env).await.expect("match_device failed"));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_blobfs_matcher() {
        fn fake_blobfs_device() -> MockDevice {
            MockDevice::new()
                .set_topological_path("mock_device/blobfs-p-1/block")
                .set_partition_label(BLOBFS_PARTITION_LABEL)
                .set_partition_type(&BLOBFS_TYPE_GUID)
        }

        let fvm_device = MockDevice::new().set_content_format(ContentFormat::Fvm);
        let env = MockEnv::new().expect_attach_driver(FVM_DRIVER_PATH).expect_mount_blobfs();

        let mut matchers = Matchers::new(default_config());

        // Attach the first GPT device.
        assert!(matchers.match_device(&fvm_device, &env).await.expect("match_device failed"));

        // Attaching blobfs with a different path should fail.
        assert!(!matchers
            .match_device(
                &fake_blobfs_device().set_topological_path("another_device/blobfs-p-1/block"),
                &env
            )
            .await
            .expect("match_device failed"));

        // Attaching blobfs with a different label should fail.
        assert!(!matchers
            .match_device(&fake_blobfs_device().set_partition_label("data"), &env)
            .await
            .expect("match_device failed"));

        // Attaching blobfs with a different type should fail.
        assert!(!matchers
            .match_device(&fake_blobfs_device().set_partition_type(&[1; 16]), &env)
            .await
            .expect("match_device failed"));

        // Attach blobfs.
        assert!(matchers
            .match_device(&fake_blobfs_device(), &env)
            .await
            .expect("match_device failed"));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_data_matcher() {
        let mut matchers = Matchers::new(default_config());

        // Attach FVM device.
        assert!(matchers
            .match_device(
                &MockDevice::new().set_content_format(ContentFormat::Fvm),
                &MockEnv::new().expect_attach_driver(FVM_DRIVER_PATH)
            )
            .await
            .expect("match_device failed"));

        // Check that data is mounted.
        assert!(matchers
            .match_device(
                &MockDevice::new()
                    .set_topological_path("mock_device/data-p-2/block")
                    .set_partition_label(DATA_PARTITION_LABEL)
                    .set_partition_type(&DATA_TYPE_GUID),
                &MockEnv::new().expect_mount_data()
            )
            .await
            .expect("match_device failed"));
    }
}
