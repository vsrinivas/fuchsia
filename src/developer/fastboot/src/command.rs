// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::anyhow, std::convert::TryFrom};

#[derive(PartialEq, Debug)]
pub enum ClientVariable {
    // Version of FastBoot protocol supported. It should be "0.4".
    Version,
    // Version string for the Bootloader.
    VersionBootLoader,
    // Version string of the Baseband Software.
    VersionBaseBand,
    // Name of the product.
    Product,
    // Product serial number.
    SerialNumber,
    // If the value is "yes", this is a secure bootloader requiring a signature before
    // it will install or boot images.
    Secure,
    // If the value is "yes", the device is running fastbootd. Otherwise, it is running fastboot
    // in the bootloader.
    IsUserSpace,
}

#[derive(PartialEq, Debug)]
pub enum Command {
    // Read a config/version variable from the bootloader.  The variable contents will be returned
    // after the OKAY response. If the variable is unknown, the bootloader should return a FAIL
    // response, optionally with an error message.
    GetVar(ClientVariable),
    // Write data to memory which will be later used by "boot", "ramdisk", "flash", etc.  The
    // client will reply with "DATA%08x" if it has enough space in RAM or "FAIL" if not.  The size
    // of the download is remembered.
    Download(u32),
    // Read data from memory which was staged by the last command, e.g. an oem command.  The client
    // will reply with "DATA%08x" if it is ready to send %08x bytes of data.  If no data was staged
    // in the last command, the client must reply with "FAIL".  After the client successfully sends
    // %08x bytes, the client shall send a single packet starting with "OKAY".  Clients should not
    // support "upload" unless it supports an oem command that requires "upload" capabilities.
    Upload,
    // Write the previously downloaded image to the named partition (if possible).
    Flash(String),
    // Erase the indicated partition (clear to 0xFFs).
    Erase(String),
    // The previously downloaded data is a boot.img and should be booted according to the normal
    // procedure for a boot.img.
    Boot,
    // Continue booting as normal (if possible).
    Continue,
    // Reboot the device.
    Reboot,
    // Reboot back into the bootloader. Useful for upgrade processes that require upgrading
    // the bootloader and then upgrading other partitions using the new bootloader.
    RebootBootLoader,

    ////////////// Logical Partition Commands ////////////////////////////////////////////
    //
    // Write the previously downloaded image to a super partition. Unlike the "flash" command, this
    // has special rules. The image must have been created by the lpmake command, and must not be a
    // sparse image.  If the last argument is "wipe", then all existing logical partitions are
    // deleted. If no final argument is specified, the partition tables are merged. Any partition
    // in the new image that does not exist in the old image is created with a zero size.
    //
    // In all cases, this will cause the temporary "scratch" partition to be deleted if it exists.
    UpdateSuper(String, String),
    // Create a logical partition with the given name and size, in the super partition.
    CreateLogicalPartition(String, usize),
    // Delete a logical partition with the given name.
    DeleteLogicalPartition(String),
    // Change the size of the named logical partition.
    ResizeLogicalPartition(String, usize),
    // If the value is "yes", the partition is logical. Otherwise the partition is physical.
    IsLogical(String),
}

impl From<ClientVariable> for Vec<u8> {
    fn from(var: ClientVariable) -> Self {
        match var {
            ClientVariable::Version => b"version".to_vec(),
            ClientVariable::VersionBootLoader => b"version-bootloader".to_vec(),
            ClientVariable::VersionBaseBand => b"version-baseband".to_vec(),
            ClientVariable::Product => b"product".to_vec(),
            ClientVariable::SerialNumber => b"serialno".to_vec(),
            ClientVariable::Secure => b"secure".to_vec(),
            ClientVariable::IsUserSpace => b"is-userspace".to_vec(),
        }
    }
}

const MAX_COMMAND_LENGTH: usize = 64;

fn concat_message(cmd: &[u8], s: String) -> Result<Vec<u8>, anyhow::Error> {
    let bytes = s.into_bytes();
    if MAX_COMMAND_LENGTH - cmd.len() < bytes.len() {
        return Err(anyhow!("Message name is too long for command."));
    }
    Ok([cmd, &bytes[..]].concat())
}

impl TryFrom<Command> for Vec<u8> {
    type Error = anyhow::Error;

    fn try_from(command: Command) -> Result<Self, Self::Error> {
        match command {
            Command::GetVar(v) => Ok([b"getvar:", &Vec::<u8>::from(v)[..]].concat()),
            Command::Download(s) => {
                Ok([b"download:", &format!("{:08X}", s).into_bytes()[..]].concat())
            }
            Command::Upload => Ok(b"upload".to_vec()),
            Command::Flash(s) => concat_message(b"flash:", s),
            Command::Erase(s) => concat_message(b"erase:", s),
            Command::Boot => Ok(b"boot".to_vec()),
            Command::Continue => Ok(b"continue".to_vec()),
            Command::Reboot => Ok(b"reboot".to_vec()),
            Command::RebootBootLoader => Ok(b"reboot-bootloader".to_vec()),
            Command::UpdateSuper(partition_name, arg) => {
                concat_message(b"update-super:", format!("{}:{}", partition_name, arg))
            }
            Command::CreateLogicalPartition(partition_name, size) => {
                concat_message(b"create-logical-partition:", format!("{}:{}", partition_name, size))
            }
            Command::DeleteLogicalPartition(s) => concat_message(b"delete-logical-partition:", s),
            Command::ResizeLogicalPartition(partition_name, size) => {
                concat_message(b"resize-logical-partition:", format!("{}:{}", partition_name, size))
            }
            Command::IsLogical(s) => concat_message(b"is-logical:", s),
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_get_var() {
        let version = Vec::<u8>::try_from(Command::GetVar(ClientVariable::Version));
        assert!(!version.is_err());
        assert_eq!(version.unwrap(), b"getvar:version".to_vec());

        let version_boot_loader =
            Vec::<u8>::try_from(Command::GetVar(ClientVariable::VersionBootLoader));
        assert!(!version_boot_loader.is_err());
        assert_eq!(version_boot_loader.unwrap(), b"getvar:version-bootloader".to_vec());

        let version_base_band =
            Vec::<u8>::try_from(Command::GetVar(ClientVariable::VersionBaseBand));
        assert!(!version_base_band.is_err());
        assert_eq!(version_base_band.unwrap(), b"getvar:version-baseband".to_vec());

        let product = Vec::<u8>::try_from(Command::GetVar(ClientVariable::Product));
        assert!(!product.is_err());
        assert_eq!(product.unwrap(), b"getvar:product".to_vec());

        let serial_number = Vec::<u8>::try_from(Command::GetVar(ClientVariable::SerialNumber));
        assert!(!serial_number.is_err());
        assert_eq!(serial_number.unwrap(), b"getvar:serialno".to_vec());

        let secure = Vec::<u8>::try_from(Command::GetVar(ClientVariable::Secure));
        assert!(!secure.is_err());
        assert_eq!(secure.unwrap(), b"getvar:secure".to_vec());

        let is_user_space = Vec::<u8>::try_from(Command::GetVar(ClientVariable::IsUserSpace));
        assert!(!is_user_space.is_err());
        assert_eq!(is_user_space.unwrap(), b"getvar:is-userspace".to_vec());
    }

    #[test]
    fn test_download() {
        let download = Vec::<u8>::try_from(Command::Download(u32::min_value()));
        assert!(!download.is_err());
        assert_eq!(download.unwrap(), b"download:00000000".to_vec());

        let download_max = Vec::<u8>::try_from(Command::Download(u32::max_value()));
        assert!(!download_max.is_err());
        assert_eq!(download_max.unwrap(), b"download:FFFFFFFF".to_vec());

        // Test something in between.
        let download_fifteen = Vec::<u8>::try_from(Command::Download(15));
        assert!(!download_fifteen.is_err());
        assert_eq!(download_fifteen.unwrap(), b"download:0000000F".to_vec());
    }

    #[test]
    fn test_upload() {
        let byte_vector = Vec::<u8>::try_from(Command::Upload);
        assert!(!byte_vector.is_err());
        assert_eq!(byte_vector.unwrap(), b"upload".to_vec());
    }

    #[test]
    fn test_flash() {
        let byte_vector = Vec::<u8>::try_from(Command::Flash("test".to_string()));
        assert!(!byte_vector.is_err());
        assert_eq!(byte_vector.unwrap(), b"flash:test".to_vec());

        let max_partition_name = vec![b'X'; MAX_COMMAND_LENGTH - b"flash:".len()];
        let max_vector =
            Vec::<u8>::try_from(Command::Flash(String::from_utf8(max_partition_name).unwrap()));
        assert!(!max_vector.is_err());

        let over_partition_name = vec![b'X'; MAX_COMMAND_LENGTH - b"flash:".len() + 1];
        let over_vector =
            Vec::<u8>::try_from(Command::Flash(String::from_utf8(over_partition_name).unwrap()));
        assert!(over_vector.is_err());
    }

    #[test]
    fn test_erase() {
        let byte_vector = Vec::<u8>::try_from(Command::Erase("test".to_string()));
        assert!(!byte_vector.is_err());
        assert_eq!(byte_vector.unwrap(), b"erase:test".to_vec());

        let max_partition_name = vec![b'X'; MAX_COMMAND_LENGTH - b"erase:".len()];
        let max_vector =
            Vec::<u8>::try_from(Command::Erase(String::from_utf8(max_partition_name).unwrap()));
        assert!(!max_vector.is_err());

        let over_partition_name = vec![b'X'; MAX_COMMAND_LENGTH - b"erase:".len() + 1];
        let over_vector =
            Vec::<u8>::try_from(Command::Erase(String::from_utf8(over_partition_name).unwrap()));
        assert!(over_vector.is_err());
    }

    #[test]
    fn test_boot() {
        let byte_vector = Vec::<u8>::try_from(Command::Boot);
        assert!(!byte_vector.is_err());
        assert_eq!(byte_vector.unwrap(), b"boot".to_vec());
    }

    #[test]
    fn test_continue() {
        let byte_vector = Vec::<u8>::try_from(Command::Continue);
        assert!(!byte_vector.is_err());
        assert_eq!(byte_vector.unwrap(), b"continue".to_vec());
    }

    #[test]
    fn test_reboot() {
        let byte_vector = Vec::<u8>::try_from(Command::Reboot);
        assert!(!byte_vector.is_err());
        assert_eq!(byte_vector.unwrap(), b"reboot".to_vec());
    }

    #[test]
    fn test_reboot_bootloader() {
        let byte_vector = Vec::<u8>::try_from(Command::RebootBootLoader);
        assert!(!byte_vector.is_err());
        assert_eq!(byte_vector.unwrap(), b"reboot-bootloader".to_vec());
    }

    #[test]
    fn test_update_super() {
        let byte_vector =
            Vec::<u8>::try_from(Command::UpdateSuper("test".to_string(), "test2".to_string()));
        assert!(!byte_vector.is_err());
        assert_eq!(byte_vector.unwrap(), b"update-super:test:test2".to_vec());

        let max_partition_name = vec![b'X'; MAX_COMMAND_LENGTH - b"update-super:".len() - 1];
        let max_vector = Vec::<u8>::try_from(Command::UpdateSuper(
            String::from_utf8(max_partition_name).unwrap(),
            "".to_string(),
        ));
        assert!(!max_vector.is_err());

        let over_partition_name = vec![b'X'; MAX_COMMAND_LENGTH - b"update-super:".len()];
        let over_vector = Vec::<u8>::try_from(Command::UpdateSuper(
            String::from_utf8(over_partition_name).unwrap(),
            "".to_string(),
        ));
        assert!(over_vector.is_err());
    }

    #[test]
    fn test_create_logical_partition() {
        let byte_vector =
            Vec::<u8>::try_from(Command::CreateLogicalPartition("test".to_string(), 5));
        assert!(!byte_vector.is_err());
        assert_eq!(byte_vector.unwrap(), b"create-logical-partition:test:5".to_vec());

        let max_partition_name =
            vec![b'X'; MAX_COMMAND_LENGTH - b"create-logical-partition:".len() - 2];
        let max_vector = Vec::<u8>::try_from(Command::CreateLogicalPartition(
            String::from_utf8(max_partition_name).unwrap(),
            0,
        ));
        assert!(!max_vector.is_err());

        let over_partition_name =
            vec![b'X'; MAX_COMMAND_LENGTH - b"create-logical-partition:".len()];
        let over_vector = Vec::<u8>::try_from(Command::CreateLogicalPartition(
            String::from_utf8(over_partition_name).unwrap(),
            0,
        ));
        assert!(over_vector.is_err());
    }

    #[test]
    fn test_delete_logical_partition() {
        let byte_vector = Vec::<u8>::try_from(Command::DeleteLogicalPartition("test".to_string()));
        assert!(!byte_vector.is_err());
        assert_eq!(byte_vector.unwrap(), b"delete-logical-partition:test".to_vec());

        let max_partition_name =
            vec![b'X'; MAX_COMMAND_LENGTH - b"delete-logical-partition:".len()];
        let max_vector = Vec::<u8>::try_from(Command::DeleteLogicalPartition(
            String::from_utf8(max_partition_name).unwrap(),
        ));
        assert!(!max_vector.is_err());

        let over_partition_name =
            vec![b'X'; MAX_COMMAND_LENGTH - b"delete-logical-partition:".len() + 1];
        let over_vector = Vec::<u8>::try_from(Command::DeleteLogicalPartition(
            String::from_utf8(over_partition_name).unwrap(),
        ));
        assert!(over_vector.is_err());
    }

    #[test]
    fn test_resize_logical_partition() {
        let byte_vector =
            Vec::<u8>::try_from(Command::ResizeLogicalPartition("test".to_string(), 5));
        assert!(!byte_vector.is_err());
        assert_eq!(byte_vector.unwrap(), b"resize-logical-partition:test:5".to_vec());

        let max_partition_name =
            vec![b'X'; MAX_COMMAND_LENGTH - b"resize-logical-partition:".len() - 2];
        let max_vector = Vec::<u8>::try_from(Command::ResizeLogicalPartition(
            String::from_utf8(max_partition_name).unwrap(),
            0,
        ));
        assert!(!max_vector.is_err());

        let over_partition_name =
            vec![b'X'; MAX_COMMAND_LENGTH - b"resize-logical-partition:".len()];
        let over_vector = Vec::<u8>::try_from(Command::ResizeLogicalPartition(
            String::from_utf8(over_partition_name).unwrap(),
            0,
        ));
        assert!(over_vector.is_err());
    }

    #[test]
    fn test_is_logical() {
        let byte_vector = Vec::<u8>::try_from(Command::IsLogical("test".to_string()));
        assert!(!byte_vector.is_err());
        assert_eq!(byte_vector.unwrap(), b"is-logical:test".to_vec());

        let max_partition_name = vec![b'X'; MAX_COMMAND_LENGTH - b"is-logical:".len()];
        let max_vector =
            Vec::<u8>::try_from(Command::IsLogical(String::from_utf8(max_partition_name).unwrap()));
        assert!(!max_vector.is_err());

        let over_partition_name = vec![b'X'; MAX_COMMAND_LENGTH - b"is-logical:".len() + 1];
        let over_vector = Vec::<u8>::try_from(Command::IsLogical(
            String::from_utf8(over_partition_name).unwrap(),
        ));
        assert!(over_vector.is_err());
    }
}
