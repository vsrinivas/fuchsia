// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::Proxy,
    fidl_fuchsia_hardware_block_partition::PartitionProxy,
    fidl_fuchsia_paver::{Asset, Configuration},
    fuchsia_zircon as zx, fuchsia_zircon_status as zx_status,
    std::{fmt, fs, path::Path},
};

#[derive(Debug, PartialEq)]
pub enum PartitionPaveType {
    Asset { r#type: Asset, config: Configuration },
    Volume,
    Bootloader,
}

/// Represents a partition that will be paved to the disk.
pub struct Partition {
    pave_type: PartitionPaveType,
    src: String,
}

/// This GUID is used by the installer to identify partitions that contain
/// data that will be installed to disk. The `fx mkinstaller` tool generates
/// images containing partitions with this GUID.
static WORKSTATION_INSTALLER_GPT: [u8; 16] = [
    0xce, 0x98, 0xce, 0x4d, 0x7e, 0xe7, 0xc1, 0x45, 0xa8, 0x63, 0xca, 0xf9, 0x2f, 0x13, 0x30, 0xc1,
];

impl Partition {
    /// Creates a new partition. Returns `None` if the partition is not
    /// a partition that should be paved to the disk.
    ///
    /// # Arguments
    /// * `src` - path to a block device that represents this partition.
    /// * `part` - a |PartitionProxy| that is connected to this partition.
    ///
    async fn new(src: String, part: PartitionProxy) -> Result<Option<Self>, Error> {
        let (status, guid) = part.get_type_guid().await.context("Get type guid failed")?;
        if let None = guid {
            return Err(Error::new(zx_status::Status::from_raw(status)));
        }

        let guid = guid.unwrap();
        if guid.value != WORKSTATION_INSTALLER_GPT {
            return Ok(None);
        }

        let (_status, name) = part.get_name().await.context("Get name failed")?;
        let pave_type;
        if let Some(string) = name {
            if string.starts_with("zircon-") && string.len() == "zircon-x".len() {
                let configuration =
                    Partition::letter_to_configuration(string.chars().last().unwrap());
                pave_type =
                    PartitionPaveType::Asset { r#type: Asset::Kernel, config: configuration };
            } else if string == "efi" {
                pave_type = PartitionPaveType::Bootloader;
            } else if string == "storage-sparse" {
                pave_type = PartitionPaveType::Volume;
            } else if string == "zedboot-efi" {
                // silently skip the zedboot partition.
                return Ok(None);
            } else {
                println!("Unknown partition: {}", string);
                return Ok(None);
            }
        // TODO(44595) support any other partitions that might be needed
        } else {
            return Ok(None);
        }

        Ok(Some(Partition { pave_type, src }))
    }

    /// Gather all partitions that are children of the given block device,
    /// and return them.
    ///
    /// # Arguments
    /// * `block_device` - the topological path of the block device (must not be
    ///     the /dev/class/block path!)
    pub async fn get_partitions(block_device: &str) -> Result<Vec<Self>, Error> {
        let mut partitions = Vec::new();

        let block_path = Path::new(&block_device);
        for entry in fs::read_dir(block_path).context("Read dir")? {
            let entry = entry?;
            let mut path = entry.path().to_path_buf();
            path.push("block");
            let path = path.as_path();

            let block_path = path.to_str().ok_or(anyhow!("Invalid path"))?;
            let (local, remote) = zx::Channel::create().context("Creating channel")?;
            fdio::service_connect(&block_path, remote).context("Connecting to partition")?;
            let local = fidl::AsyncChannel::from_channel(local).context("Creating AsyncChannel")?;

            let proxy = PartitionProxy::from_channel(local);
            if let Some(partition) = Partition::new(block_path.to_string(), proxy).await? {
                partitions.push(partition);
            }
        }
        Ok(partitions)
    }

    /// Return the |Configuration| that is represented by the given
    /// character. Returns 'Recovery' for the letters 'R' and 'r', and 'A' for
    /// anything else.
    fn letter_to_configuration(letter: char) -> Configuration {
        // Note that we treat 'A' and 'B' the same, as the installer will install
        // the same image to both A and B.
        match letter {
            'A' | 'a' => Configuration::A,
            'B' | 'b' => Configuration::A,
            'R' | 'r' => Configuration::Recovery,
            _ => Configuration::A,
        }
    }
}

impl fmt::Debug for Partition {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.pave_type {
            PartitionPaveType::Asset { r#type, config } => write!(
                f,
                "Partition[src={}, pave_type={:?}, asset={:?}, config={:?}]",
                self.src, self.pave_type, r#type, config
            ),
            _ => write!(f, "Partition[src={}, pave_type={:?}]", self.src, self.pave_type),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_hardware_block::BlockInfo,
        fidl_fuchsia_hardware_block_partition::{
            Guid, PartitionMarker, PartitionRequest, PartitionRequestStream,
        },
        fuchsia_async as fasync,
        futures::prelude::*,
    };

    async fn serve_partition(
        label: &str,
        block_size: usize,
        block_count: usize,
        guid: [u8; 16],
        mut stream: PartitionRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                PartitionRequest::GetName { responder } => responder.send(0, Some(label))?,
                PartitionRequest::GetInfo { responder } => responder.send(
                    0,
                    Some(&mut BlockInfo {
                        block_count: block_count as u64,
                        block_size: block_size as u32,
                        max_transfer_size: 0,
                        flags: 0,
                        reserved: 0,
                    }),
                )?,
                PartitionRequest::GetTypeGuid { responder } => {
                    responder.send(0, Some(&mut Guid { value: guid }))?
                }
                _ => panic!("Expected a GetInfo/GetName request, but did not get one."),
            }
        }
        Ok(())
    }

    fn mock_partition(
        label: &'static str,
        block_size: usize,
        block_count: usize,
        guid: [u8; 16],
    ) -> Result<PartitionProxy, Error> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<PartitionMarker>()?;
        fasync::spawn_local(
            serve_partition(label, block_size, block_count, guid, stream)
                .unwrap_or_else(|e| panic!("Error while serving fake block device: {}", e)),
        );
        Ok(proxy)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_bad_guid() -> Result<(), Error> {
        let proxy = mock_partition("zircon-a", 512, 1000, [0xaa; 16])?;
        let part = Partition::new("zircon-a".to_string(), proxy).await?;
        assert!(part.is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_zircona() -> Result<(), Error> {
        let proxy = mock_partition("zircon-a", 512, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("zircon-a".to_string(), proxy).await?;
        assert!(part.is_some());
        let part = part.unwrap();
        assert_eq!(
            part.pave_type,
            PartitionPaveType::Asset { r#type: Asset::Kernel, config: Configuration::A }
        );
        assert_eq!(part.src, "zircon-a");
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_zirconb() -> Result<(), Error> {
        let proxy = mock_partition("zircon-b", 20, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("zircon-b".to_string(), proxy).await?;
        assert!(part.is_some());
        let part = part.unwrap();
        assert_eq!(
            part.pave_type,
            PartitionPaveType::Asset { r#type: Asset::Kernel, config: Configuration::A }
        );
        assert_eq!(part.src, "zircon-b");
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_zirconr() -> Result<(), Error> {
        let proxy = mock_partition("zircon-r", 40, 200, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("zircon-r".to_string(), proxy).await?;
        assert!(part.is_some());
        let part = part.unwrap();
        assert_eq!(
            part.pave_type,
            PartitionPaveType::Asset { r#type: Asset::Kernel, config: Configuration::Recovery }
        );
        assert_eq!(part.src, "zircon-r");
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_efi() -> Result<(), Error> {
        let proxy = mock_partition("efi", 512, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("efi".to_string(), proxy).await?;
        assert!(part.is_some());
        let part = part.unwrap();
        assert_eq!(part.pave_type, PartitionPaveType::Bootloader);
        assert_eq!(part.src, "efi");
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_fvm() -> Result<(), Error> {
        let proxy = mock_partition("storage-sparse", 2048, 4097, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("storage-sparse".to_string(), proxy).await?;
        assert!(part.is_some());
        let part = part.unwrap();
        assert_eq!(part.pave_type, PartitionPaveType::Volume);
        assert_eq!(part.src, "storage-sparse");
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_unknown() -> Result<(), Error> {
        let proxy = mock_partition("unknown-label", 512, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("unknown-label".to_string(), proxy).await?;
        assert!(part.is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_zedboot_efi() -> Result<(), Error> {
        let proxy = mock_partition("zedboot-efi", 512, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("zedboot-efi".to_string(), proxy).await?;
        assert!(part.is_none());
        Ok(())
    }
}
