// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::payload_streamer::PayloadStreamer,
    crate::BootloaderType,
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::Proxy,
    fidl_fuchsia_hardware_block_partition::PartitionProxy,
    fidl_fuchsia_mem::Buffer,
    fidl_fuchsia_paver::{Asset, Configuration, DynamicDataSinkProxy, PayloadStreamMarker},
    fuchsia_async as fasync, fuchsia_zircon as zx, fuchsia_zircon_status as zx_status,
    futures::prelude::*,
    regex,
    std::{fmt, fs, io::Read, path::Path},
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
    size: usize,
    block_size: usize,
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
    /// * `bootloader` - the |BootloaderType| of this device.
    ///
    async fn new(
        src: String,
        part: PartitionProxy,
        bootloader: BootloaderType,
    ) -> Result<Option<Self>, Error> {
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
            // TODO(fxbug.dev/44595) support any other partitions that might be needed
            if string == "storage-sparse" {
                pave_type = Some(PartitionPaveType::Volume);
            } else if bootloader == BootloaderType::Efi {
                pave_type = Partition::get_efi_pave_type(&string);
            } else if bootloader == BootloaderType::Coreboot {
                pave_type = Partition::get_coreboot_pave_type(&string);
            } else {
                pave_type = None;
            }
        } else {
            return Ok(None);
        }

        if let Some(pave_type) = pave_type {
            let (status, info) = part.get_info().await.context("Get info failed")?;
            let info = info.ok_or(Error::new(zx_status::Status::from_raw(status)))?;
            let size = info.block_count * info.block_size as u64;

            Ok(Some(Partition {
                pave_type,
                src,
                size: size as usize,
                block_size: info.block_size as usize,
            }))
        } else {
            Ok(None)
        }
    }

    fn get_efi_pave_type(label: &str) -> Option<PartitionPaveType> {
        if label.starts_with("zircon-") && label.len() == "zircon-x".len() {
            let configuration = Partition::letter_to_configuration(label.chars().last().unwrap());
            Some(PartitionPaveType::Asset { r#type: Asset::Kernel, config: configuration })
        } else if label == "efi" {
            Some(PartitionPaveType::Bootloader)
        } else {
            None
        }
    }

    fn get_coreboot_pave_type(label: &str) -> Option<PartitionPaveType> {
        if let Ok(re) = regex::Regex::new(r"^zircon-(.)\.signed$") {
            if let Some(captures) = re.captures(label) {
                let config = Partition::letter_to_configuration(
                    captures.get(1).unwrap().as_str().chars().last().unwrap(),
                );
                Some(PartitionPaveType::Asset { r#type: Asset::Kernel, config: config })
            } else {
                None
            }
        } else {
            None
        }
    }

    /// Gather all partitions that are children of the given block device,
    /// and return them.
    ///
    /// # Arguments
    /// * `block_device` - the topological path of the block device (must not be
    ///     the /dev/class/block path!)
    /// * `bootloader` - the |BootloaderType| of this device.
    pub async fn get_partitions(
        block_device: &str,
        bootloader: BootloaderType,
    ) -> Result<Vec<Self>, Error> {
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
            if let Some(partition) =
                Partition::new(block_path.to_string(), proxy, bootloader).await?
            {
                partitions.push(partition);
            }
        }
        Ok(partitions)
    }

    /// Pave this partition to disk, using the given |DynamicDataSinkProxy|.
    pub async fn pave(&self, data_sink: &DynamicDataSinkProxy) -> Result<(), Error> {
        match self.pave_type {
            PartitionPaveType::Asset { r#type: asset, config } => {
                let mut fidl_buf = self.read_data().await?;
                data_sink.write_asset(config, asset, &mut fidl_buf).await?;
            }
            PartitionPaveType::Bootloader => {
                let mut fidl_buf = self.read_data().await?;
                data_sink.write_bootloader(&mut fidl_buf).await?;
            }
            PartitionPaveType::Volume => {
                // Set up a PayloadStream to serve the data sink.
                let file =
                    Box::new(fs::File::open(Path::new(&self.src)).context("Opening partition")?);
                let payload_stream = PayloadStreamer::new(file, self.size);
                let (client_end, server_end) =
                    fidl::endpoints::create_endpoints::<PayloadStreamMarker>()?;
                let mut stream = server_end.into_stream()?;

                fasync::Task::spawn(async move {
                    while let Some(req) = stream.try_next().await.expect("Failed to get request!") {
                        payload_stream
                            .handle_request(req)
                            .await
                            .expect("Failed to handle request!");
                    }
                })
                .detach();
                // Tell the data sink to use our PayloadStream.
                data_sink.write_volumes(client_end).await?;
            }
        };
        Ok(())
    }

    /// Pave this A/B partition to its 'B' slot.
    /// Will return an error if the partition is not an A/B partition.
    pub async fn pave_b(&self, data_sink: &DynamicDataSinkProxy) -> Result<(), Error> {
        if !self.is_ab() {
            return Err(Error::from(zx_status::Status::NOT_SUPPORTED));
        }

        let mut fidl_buf = self.read_data().await?;
        match self.pave_type {
            PartitionPaveType::Asset { r#type: asset, config: _ } => {
                // pave() will always pave to A, so this always paves to B.
                // The A/B config from the partition is not respected because on a fresh
                // install we want A/B to be identical, so we install the same thing to both.
                data_sink.write_asset(Configuration::B, asset, &mut fidl_buf).await?;
                Ok(())
            }
            _ => Err(Error::from(zx_status::Status::NOT_SUPPORTED)),
        }
    }

    /// Returns true if this partition has A/B variants when installed.
    pub fn is_ab(&self) -> bool {
        if let PartitionPaveType::Asset { r#type: _, config } = self.pave_type {
            // We only check against the A configuration because |letter_to_configuration|
            // returns A for 'A' and 'B' configurations.
            return config == Configuration::A;
        }
        return false;
    }

    /// Read this partition into a FIDL buffer.
    async fn read_data(&self) -> Result<Buffer, Error> {
        let mut rounded_size = self.size;
        let page_size = zx::sys::ZX_PAGE_SIZE as usize;
        if rounded_size % page_size != 0 {
            rounded_size += page_size;
            rounded_size -= rounded_size % page_size;
        }

        let vmo = zx::Vmo::create_with_opts(zx::VmoOptions::RESIZABLE, rounded_size as u64)?;
        let mut buf: Vec<u8> = vec![0; 100 * self.block_size];
        let mut file = fs::File::open(Path::new(&self.src)).context("Opening partition")?;
        let mut read = 0;
        while read < self.size {
            let write_pos = read;
            read += file.read(&mut buf).context("Reading data from partition")?;
            vmo.write(&buf, write_pos as u64).context("Writing data to VMO")?;
            if self.size - read < buf.len() {
                buf.truncate(self.size - read);
            }
        }
        Ok(Buffer { vmo: fidl::Vmo::from(vmo), size: self.size as u64 })
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
        fasync::Task::local(
            serve_partition(label, block_size, block_count, guid, stream)
                .unwrap_or_else(|e| panic!("Error while serving fake block device: {}", e)),
        )
        .detach();
        Ok(proxy)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_bad_guid() -> Result<(), Error> {
        let proxy = mock_partition("zircon-a", 512, 1000, [0xaa; 16])?;
        let part = Partition::new("zircon-a".to_string(), proxy, BootloaderType::Efi).await?;
        assert!(part.is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_zircona() -> Result<(), Error> {
        let proxy = mock_partition("zircon-a", 512, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("zircon-a".to_string(), proxy, BootloaderType::Efi).await?;
        assert!(part.is_some());
        let part = part.unwrap();
        assert_eq!(
            part.pave_type,
            PartitionPaveType::Asset { r#type: Asset::Kernel, config: Configuration::A }
        );
        assert_eq!(part.size, 512 * 1000);
        assert_eq!(part.src, "zircon-a");
        assert!(part.is_ab());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_zirconb() -> Result<(), Error> {
        let proxy = mock_partition("zircon-b", 20, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("zircon-b".to_string(), proxy, BootloaderType::Efi).await?;
        assert!(part.is_some());
        let part = part.unwrap();
        assert_eq!(
            part.pave_type,
            PartitionPaveType::Asset { r#type: Asset::Kernel, config: Configuration::A }
        );
        assert_eq!(part.size, 20 * 1000);
        assert_eq!(part.src, "zircon-b");
        assert!(part.is_ab());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_zirconr() -> Result<(), Error> {
        let proxy = mock_partition("zircon-r", 40, 200, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("zircon-r".to_string(), proxy, BootloaderType::Efi).await?;
        assert!(part.is_some());
        let part = part.unwrap();
        assert_eq!(
            part.pave_type,
            PartitionPaveType::Asset { r#type: Asset::Kernel, config: Configuration::Recovery }
        );
        assert_eq!(part.size, 40 * 200);
        assert_eq!(part.src, "zircon-r");
        assert!(!part.is_ab());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_efi() -> Result<(), Error> {
        let proxy = mock_partition("efi", 512, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("efi".to_string(), proxy, BootloaderType::Efi).await?;
        assert!(part.is_some());
        let part = part.unwrap();
        assert_eq!(part.pave_type, PartitionPaveType::Bootloader);
        assert_eq!(part.size, 512 * 1000);
        assert_eq!(part.src, "efi");
        assert!(!part.is_ab());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_fvm() -> Result<(), Error> {
        let proxy = mock_partition("storage-sparse", 2048, 4097, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("storage-sparse".to_string(), proxy, BootloaderType::Efi).await?;
        assert!(part.is_some());
        let part = part.unwrap();
        assert_eq!(part.pave_type, PartitionPaveType::Volume);
        assert_eq!(part.size, 2048 * 4097);
        assert_eq!(part.src, "storage-sparse");
        assert!(!part.is_ab());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_zircona_unsigned_coreboot() -> Result<(), Error> {
        let proxy = mock_partition("zircon-a", 512, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("zircon-a".to_string(), proxy, BootloaderType::Coreboot).await?;
        assert!(part.is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_zircona_signed_coreboot() -> Result<(), Error> {
        let proxy = mock_partition("zircon-a.signed", 512, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part =
            Partition::new("zircon-a.signed".to_string(), proxy, BootloaderType::Coreboot).await?;
        assert!(part.is_some());
        let part = part.unwrap();
        assert_eq!(
            part.pave_type,
            PartitionPaveType::Asset { r#type: Asset::Kernel, config: Configuration::A }
        );
        assert_eq!(part.size, 512 * 1000);
        assert_eq!(part.src, "zircon-a.signed");
        assert!(part.is_ab());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_unknown() -> Result<(), Error> {
        let proxy = mock_partition("unknown-label", 512, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("unknown-label".to_string(), proxy, BootloaderType::Efi).await?;
        assert!(part.is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_partition_zedboot_efi() -> Result<(), Error> {
        let proxy = mock_partition("zedboot-efi", 512, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("zedboot-efi".to_string(), proxy, BootloaderType::Efi).await?;
        assert!(part.is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_partitions_coreboot() -> Result<(), Error> {
        let proxy = mock_partition("zircon-.signed", 512, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part =
            Partition::new("zircon-.signed".to_string(), proxy, BootloaderType::Coreboot).await?;
        assert!(part.is_none());

        let proxy = mock_partition("zircon-aa.signed", 512, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part =
            Partition::new("zircon-aa.signed".to_string(), proxy, BootloaderType::Coreboot).await?;
        assert!(part.is_none());

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_partitions_efi() -> Result<(), Error> {
        let proxy = mock_partition("zircon-", 512, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("zircon-".to_string(), proxy, BootloaderType::Efi).await?;
        assert!(part.is_none());

        let proxy = mock_partition("zircon-aa", 512, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part = Partition::new("zircon-aa".to_string(), proxy, BootloaderType::Efi).await?;
        assert!(part.is_none());

        let proxy = mock_partition("zircon-a.signed", 512, 1000, WORKSTATION_INSTALLER_GPT)?;
        let part =
            Partition::new("zircon-a.signed".to_string(), proxy, BootloaderType::Efi).await?;
        assert!(part.is_none());
        Ok(())
    }
}
