// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::device::{ContentFormat, Device},
    anyhow::Error,
    async_trait::async_trait,
    std::path::Path,
};

const BLOBFS_PARTITION_LABEL: &str = "blobfs";
const BLOBFS_TYPE_GUID: [u8; 16] = [
    0x0e, 0x38, 0x67, 0x29, 0x4c, 0x13, 0xbb, 0x4c, 0xb6, 0xda, 0x17, 0xe7, 0xce, 0x1c, 0xa4, 0x5d,
];

// const FVM_DRIVER_PATH: &str = "fvm.so";
const GPT_DRIVER_PATH: &str = "gpt.so";
// const ZXCRYPT_DRIVER_PATH: &str = "mbr.so";
const BOOTPART_DRIVER_PATH: &str = "bootpart.so";
// const BLOCK_VERITY_DRIVER_PATH: &str = "block-verity.so";
const NAND_BROKER_DRIVER_PATH: &str = "nand-broker.so";

const BLOCK_FLAG_BOOTPART: u32 = 4;

/// Environment is a trait that performs actions when a device is matched.
#[async_trait]
pub trait Environment: Sync {
    /// Mounts Blobfs on the given device.
    async fn mount_blobfs(&self, device: &dyn Device) -> Result<(), Error>;

    /// Attaches the specified driver to the device.
    async fn attach_driver(&self, device: &dyn Device, driver_path: &str) -> Result<(), Error>;
}

#[async_trait]
pub trait Matcher: Send {
    async fn match_device(
        &mut self,
        device: &dyn Device,
        env: &dyn Environment,
    ) -> Result<bool, Error>;
}

pub struct Matchers {
    matchers: Vec<Box<dyn Matcher>>,
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
        let mut gpt_matcher =
            Box::new(PartitionMapMatcher::new(ContentFormat::Gpt, false, GPT_DRIVER_PATH, ""));
        if config.blobfs {
            gpt_matcher.child_matchers.push(Box::new(BlobfsMatcher::new()));
        }
        if config.gpt || !gpt_matcher.child_matchers.is_empty() {
            matchers.push(gpt_matcher);
        }
        if config.gpt_all {
            matchers.push(Box::new(PartitionMapMatcher::new(
                ContentFormat::Gpt,
                true,
                GPT_DRIVER_PATH,
                "",
            )));
        }

        Matchers { matchers }
    }

    /// Using the set of matchers we created, figure out if this block device matches any of our
    /// expected partitions. If it does, return the information needed to launch the filesystem,
    /// such as the component url or the shared library to pass to the driver binding.
    pub async fn match_device(
        &mut self,
        device: &dyn Device,
        env: &dyn Environment,
    ) -> Result<bool, Error> {
        for m in &mut self.matchers {
            if m.match_device(device, env).await? {
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
                    for path in &self.device_paths {
                        if head == path {
                            for m in &mut self.child_matchers {
                                if m.match_device(device, env).await? {
                                    return Ok(true);
                                }
                            }
                            return Ok(false);
                        }
                    }
                }
            }
        }

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

#[cfg(test)]
mod tests {
    use {
        super::{
            ContentFormat, Device, Environment, Matchers, BLOBFS_PARTITION_LABEL, BLOBFS_TYPE_GUID,
            BLOCK_FLAG_BOOTPART, BOOTPART_DRIVER_PATH, GPT_DRIVER_PATH, NAND_BROKER_DRIVER_PATH,
        },
        crate::config::default_config,
        anyhow::Error,
        async_trait::async_trait,
        fidl::encoding::Decodable,
        fidl_fuchsia_hardware_block::BlockInfo,
        fuchsia_async as fasync,
        std::{
            path::Path,
            sync::atomic::{AtomicBool, Ordering},
        },
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_bootpart_matcher() {
        struct FakeDevice();

        #[async_trait]
        impl Device for FakeDevice {
            async fn get_block_info(
                &self,
            ) -> Result<fidl_fuchsia_hardware_block::BlockInfo, Error> {
                Ok(BlockInfo { flags: BLOCK_FLAG_BOOTPART, ..BlockInfo::new_empty() })
            }

            fn is_nand(&self) -> bool {
                unreachable!();
            }

            async fn content_format(&self) -> Result<ContentFormat, Error> {
                Ok(ContentFormat::Unknown)
            }

            async fn topological_path(&self) -> Result<&Path, Error> {
                Ok(Path::new("fake_device"))
            }

            async fn partition_label(&self) -> Result<&str, Error> {
                unreachable!();
            }

            async fn partition_type(&self) -> Result<&[u8; 16], Error> {
                unreachable!();
            }
        }

        struct FakeEnv(AtomicBool);

        #[async_trait]
        impl Environment for FakeEnv {
            async fn mount_blobfs(&self, _device: &dyn Device) -> Result<(), Error> {
                unreachable!();
            }
            async fn attach_driver(
                &self,
                _device: &dyn Device,
                driver_path: &str,
            ) -> Result<(), Error> {
                assert_eq!(driver_path, BOOTPART_DRIVER_PATH);
                self.0.store(true, Ordering::SeqCst);
                Ok(())
            }
        }

        let env = FakeEnv(AtomicBool::new(false));

        // Check no match when disabled in config.
        assert!(!Matchers::new(fshost_config::Config { bootpart: false, ..default_config() })
            .match_device(&FakeDevice(), &env)
            .await
            .expect("match_device failed"));
        assert!(!env.0.load(Ordering::SeqCst));

        assert!(Matchers::new(default_config())
            .match_device(&FakeDevice(), &env)
            .await
            .expect("match_device failed"));
        assert!(env.0.load(Ordering::SeqCst));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_nand_matcher() {
        struct FakeDevice();

        #[async_trait]
        impl Device for FakeDevice {
            async fn get_block_info(
                &self,
            ) -> Result<fidl_fuchsia_hardware_block::BlockInfo, Error> {
                Ok(BlockInfo::new_empty())
            }

            fn is_nand(&self) -> bool {
                true
            }

            async fn content_format(&self) -> Result<ContentFormat, Error> {
                Ok(ContentFormat::Unknown)
            }

            async fn topological_path(&self) -> Result<&Path, Error> {
                Ok(Path::new("fake_device"))
            }

            async fn partition_label(&self) -> Result<&str, Error> {
                unreachable!();
            }

            async fn partition_type(&self) -> Result<&[u8; 16], Error> {
                unreachable!();
            }
        }

        struct FakeEnv(AtomicBool);

        #[async_trait]
        impl Environment for FakeEnv {
            async fn mount_blobfs(&self, _device: &dyn Device) -> Result<(), Error> {
                unreachable!();
            }
            async fn attach_driver(
                &self,
                _device: &dyn Device,
                driver_path: &str,
            ) -> Result<(), Error> {
                assert_eq!(driver_path, NAND_BROKER_DRIVER_PATH);
                self.0.store(true, Ordering::SeqCst);
                Ok(())
            }
        }

        let env = FakeEnv(AtomicBool::new(false));

        // Default shouldn't match.
        assert!(!Matchers::new(default_config())
            .match_device(&FakeDevice(), &env)
            .await
            .expect("match_device failed"));
        assert!(!env.0.load(Ordering::SeqCst));

        assert!(Matchers::new(fshost_config::Config { nand: true, ..default_config() })
            .match_device(&FakeDevice(), &env)
            .await
            .expect("match_device failed"));
        assert!(env.0.load(Ordering::SeqCst));
    }

    struct FakeGptDevice(&'static str);

    #[async_trait]
    impl Device for FakeGptDevice {
        async fn get_block_info(&self) -> Result<fidl_fuchsia_hardware_block::BlockInfo, Error> {
            Ok(BlockInfo::new_empty())
        }

        fn is_nand(&self) -> bool {
            false
        }

        async fn content_format(&self) -> Result<ContentFormat, Error> {
            Ok(ContentFormat::Gpt)
        }

        async fn topological_path(&self) -> Result<&Path, Error> {
            Ok(Path::new(self.0))
        }

        async fn partition_label(&self) -> Result<&str, Error> {
            unreachable!();
        }

        async fn partition_type(&self) -> Result<&[u8; 16], Error> {
            unreachable!();
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_partition_map_matcher() {
        struct FakeEnv(AtomicBool);

        #[async_trait]
        impl Environment for FakeEnv {
            async fn mount_blobfs(&self, _device: &dyn Device) -> Result<(), Error> {
                unreachable!();
            }
            async fn attach_driver(
                &self,
                _device: &dyn Device,
                driver_path: &str,
            ) -> Result<(), Error> {
                assert_eq!(driver_path, GPT_DRIVER_PATH);
                self.0.store(true, Ordering::SeqCst);
                Ok(())
            }
        }

        let env = FakeEnv(AtomicBool::new(false));

        // Check no match when disabled in config.
        let device = FakeGptDevice("fake_device");
        assert!(!Matchers::new(fshost_config::Config {
            blobfs: false,
            data: false,
            gpt: false,
            ..default_config()
        })
        .match_device(&device, &env)
        .await
        .expect("match_device failed"));
        assert!(!env.0.load(Ordering::SeqCst));

        let mut matchers = Matchers::new(default_config());
        assert!(matchers.match_device(&device, &env).await.expect("match_device failed"));
        assert!(env.0.load(Ordering::SeqCst));

        // More GPT devices should not get matched.
        assert!(!matchers.match_device(&device, &env).await.expect("match_device failed"));

        // The gpt_all config should allow multiple GPT devices to be matched.
        let mut matchers =
            Matchers::new(fshost_config::Config { gpt_all: true, ..default_config() });
        assert!(matchers.match_device(&device, &env).await.expect("match_device failed"));
        assert!(matchers.match_device(&device, &env).await.expect("match_device failed"));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_blobfs_matcher() {
        struct FakeBlobfsDevice {
            topological_path: &'static str,
            partition_label: &'static str,
            partition_type: &'static [u8; 16],
        }

        impl Default for FakeBlobfsDevice {
            fn default() -> Self {
                Self {
                    topological_path: "fake_device/blobfs-p-1/block",
                    partition_label: BLOBFS_PARTITION_LABEL,
                    partition_type: &BLOBFS_TYPE_GUID,
                }
            }
        }

        #[async_trait]
        impl Device for FakeBlobfsDevice {
            async fn get_block_info(
                &self,
            ) -> Result<fidl_fuchsia_hardware_block::BlockInfo, Error> {
                Ok(BlockInfo::new_empty())
            }

            fn is_nand(&self) -> bool {
                false
            }

            async fn content_format(&self) -> Result<ContentFormat, Error> {
                unreachable!();
            }

            async fn topological_path(&self) -> Result<&Path, Error> {
                Ok(Path::new(self.topological_path))
            }

            async fn partition_label(&self) -> Result<&str, Error> {
                Ok(self.partition_label)
            }

            async fn partition_type(&self) -> Result<&[u8; 16], Error> {
                Ok(self.partition_type)
            }
        }

        struct FakeEnv {
            gpt_attached: AtomicBool,
            blobfs_mounted: AtomicBool,
        }

        #[async_trait]
        impl Environment for FakeEnv {
            async fn mount_blobfs(&self, _device: &dyn Device) -> Result<(), Error> {
                self.blobfs_mounted.store(true, Ordering::SeqCst);
                Ok(())
            }
            async fn attach_driver(
                &self,
                _device: &dyn Device,
                driver_path: &str,
            ) -> Result<(), Error> {
                assert_eq!(driver_path, GPT_DRIVER_PATH);
                self.gpt_attached.store(true, Ordering::SeqCst);
                Ok(())
            }
        }

        let env = FakeEnv {
            gpt_attached: AtomicBool::new(false),
            blobfs_mounted: AtomicBool::new(false),
        };

        let mut matchers = Matchers::new(default_config());

        // Attach the first GPT device.
        assert!(matchers
            .match_device(&FakeGptDevice("fake_device"), &env)
            .await
            .expect("match_device failed"));
        assert!(env.gpt_attached.load(Ordering::SeqCst));
        assert!(!env.blobfs_mounted.load(Ordering::SeqCst));

        // Attaching blobfs with a different path should fail.
        assert!(!matchers
            .match_device(
                &FakeBlobfsDevice {
                    topological_path: "another_device/blobfs-p-1/block",
                    ..Default::default()
                },
                &env
            )
            .await
            .expect("match_device failed"));

        // Attaching blobfs with a different label should fail.
        assert!(!matchers
            .match_device(&FakeBlobfsDevice { partition_label: "data", ..Default::default() }, &env)
            .await
            .expect("match_device failed"));

        // Attaching blobfs with a different type should fail.
        assert!(!matchers
            .match_device(
                &FakeBlobfsDevice { partition_type: &[1; 16], ..Default::default() },
                &env
            )
            .await
            .expect("match_device failed"));

        // Attach blobfs.
        assert!(matchers
            .match_device(&FakeBlobfsDevice::default(), &env)
            .await
            .expect("match_device failed"));
        assert!(env.blobfs_mounted.load(Ordering::SeqCst));

        // If gpt_all is enabled, blobfs should only mount on the first gpt device.
        let env = FakeEnv {
            gpt_attached: AtomicBool::new(false),
            blobfs_mounted: AtomicBool::new(false),
        };
        let mut matchers =
            Matchers::new(fshost_config::Config { gpt_all: true, ..default_config() });

        // Attach the first GPT device.
        assert!(matchers
            .match_device(&FakeGptDevice("fake_device"), &env)
            .await
            .expect("match_device failed"));
        assert!(env.gpt_attached.load(Ordering::SeqCst));
        assert!(!env.blobfs_mounted.load(Ordering::SeqCst));

        // Attach the second GPT device.
        assert!(matchers
            .match_device(&FakeGptDevice("gpt_all_fake_device"), &env)
            .await
            .expect("match_device failed"));

        // Attaching blobfs to the second GPT device should fail.
        assert!(!matchers
            .match_device(
                &FakeBlobfsDevice {
                    topological_path: "gpt_all_fake_device/blobfs-p-1/block",
                    ..Default::default()
                },
                &env
            )
            .await
            .expect("match_device failed"));

        // Attaching blobfs to the first GPT device should succeed.
        assert!(matchers
            .match_device(
                &FakeBlobfsDevice {
                    topological_path: "fake_device/blobfs-p-1/block",
                    ..Default::default()
                },
                &env
            )
            .await
            .expect("match_device failed"));
        assert!(env.blobfs_mounted.load(Ordering::SeqCst));
    }
}
