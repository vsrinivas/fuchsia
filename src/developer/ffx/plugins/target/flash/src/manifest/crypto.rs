// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Result},
    async_fs::OpenOptions,
    byteorder::{ByteOrder, LittleEndian},
    errors::ffx_error,
    fidl_fuchsia_developer_bridge::FastbootProxy,
    futures::prelude::*,
    std::fs::File,
    std::io::copy,
    std::path::{Path, PathBuf},
    tempfile::tempdir,
    zip::read::ZipArchive,
};

const UNLOCK_CHALLENGE: &str = "vx-get-unlock-challenge";

const CHALLENGE_STRUCT_SIZE: u64 = 52;
const CHALLENGE_DATA_SIZE: usize = 16;
const PRODUCT_ID_HASH_SIZE: usize = 32;

#[derive(Debug)]
pub(crate) struct UnlockChallenge {
    #[cfg_attr(not(test), allow(unused))]
    pub(crate) version: u32,
    pub(crate) product_id_hash: [u8; PRODUCT_ID_HASH_SIZE],
    pub(crate) challenge: [u8; CHALLENGE_DATA_SIZE],
}

impl UnlockChallenge {
    fn new(buffer: &Vec<u8>) -> Self {
        let mut result = Self {
            version: LittleEndian::read_u32(&buffer[..4]),
            product_id_hash: [0; PRODUCT_ID_HASH_SIZE],
            challenge: [0; CHALLENGE_DATA_SIZE],
        };
        result.product_id_hash.clone_from_slice(&buffer[4..PRODUCT_ID_HASH_SIZE + 4]);
        result.challenge.clone_from_slice(&buffer[PRODUCT_ID_HASH_SIZE + 4..]);
        result
    }
}

pub(crate) async fn get_unlock_challenge(
    fastboot_proxy: &FastbootProxy,
) -> Result<UnlockChallenge> {
    let dir = tempdir()?;
    let path = dir.path().join("challenge");
    let filepath = path.to_str().ok_or(anyhow!("error getting tempfile path"))?;
    fastboot_proxy
        .oem(UNLOCK_CHALLENGE)
        .await?
        .map_err(|_| anyhow!("There was an error sending oem command \"{}\"", UNLOCK_CHALLENGE))?;
    fastboot_proxy
        .get_staged(filepath)
        .await?
        .map_err(|_| anyhow!("There was an error sending upload command"))?;
    let mut file = OpenOptions::new().read(true).open(path.clone()).await?;
    let size = file.metadata().await?.len();
    if size != CHALLENGE_STRUCT_SIZE {
        bail!("Device returned a file with invalid unlock challenge length")
    }
    let mut buffer = Vec::new();
    file.read_to_end(&mut buffer).await?;
    Ok(UnlockChallenge::new(&buffer))
}

// The certificates are AvbAtxCertificate structs as defined in
// libavb_atx, not an X.509 certificate. Do a basic length check
// when reading them.
const EXPECTED_CERTIFICATE_SIZE: usize = 1620;
const PIK_CERT: &str = "pik_certificate.bin";
const PUK_CERT: &str = "puk_certificate.bin";
const _PUK: &str = "puk.pem";

const CERT_SUBJECT_OFFSET: usize = 4 + 1032;
const CERT_SUBJECT_LENGTH: usize = 32;

pub(crate) struct UnlockCredentials {
    pub(crate) intermediate_cert: [u8; EXPECTED_CERTIFICATE_SIZE],
    pub(crate) unlock_cert: [u8; EXPECTED_CERTIFICATE_SIZE],
    //TODO: figure out how to import the rsa key puk.pem
    //pub(crate) unlock_key: RsaKey?
}

impl UnlockCredentials {
    pub(crate) async fn new<T: AsRef<Path>>(path: T) -> Result<Self> {
        let temp_dir = tempdir()?;
        let file =
            File::open(path).map_err(|e| ffx_error!("Could not open archive file: {}", e))?;
        let mut archive =
            ZipArchive::new(file).map_err(|e| ffx_error!("Could not read archive: {}", e))?;

        for i in 0..archive.len() {
            let mut archive_file = archive.by_index(i)?;
            let outpath = archive_file.sanitized_name();

            let mut dest = PathBuf::new();
            dest.push(temp_dir.path());
            dest.push(outpath);
            let mut outfile = File::create(&dest)?;
            copy(&mut archive_file, &mut outfile)?;
        }

        let mut result = Self {
            intermediate_cert: [0; EXPECTED_CERTIFICATE_SIZE],
            unlock_cert: [0; EXPECTED_CERTIFICATE_SIZE],
        };

        let pik_cert_file = temp_dir.path().join(PIK_CERT);
        let mut pik_file = OpenOptions::new().read(true).open(pik_cert_file).await?;
        let pik_size = pik_file.metadata().await?.len();
        if pik_size as usize != EXPECTED_CERTIFICATE_SIZE {
            bail!("Invalid intermediate key certificate length")
        }
        let mut pik_buffer = Vec::new();
        pik_file.read_to_end(&mut pik_buffer).await?;
        result.intermediate_cert.clone_from_slice(&pik_buffer[..]);

        let puk_cert_file = temp_dir.path().join(PUK_CERT);
        let mut puk_file = OpenOptions::new().read(true).open(puk_cert_file).await?;
        let puk_size = puk_file.metadata().await?.len();
        if puk_size as usize != EXPECTED_CERTIFICATE_SIZE {
            bail!("Invalid product unlock key certificate length")
        }
        let mut puk_buffer = Vec::new();
        puk_file.read_to_end(&mut puk_buffer).await?;
        result.unlock_cert.clone_from_slice(&puk_buffer[..]);

        //TODO: how to import the RSA key from puk.pem
        Ok(result)
    }

    pub(crate) fn get_atx_certificate_subject(&self) -> &[u8] {
        &self.unlock_cert[CERT_SUBJECT_OFFSET..CERT_SUBJECT_OFFSET + CERT_SUBJECT_LENGTH]
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_unlock_challenge() -> Result<()> {
        let mut buffer = Vec::with_capacity(52);
        let mut i = 0;
        while i < 4 {
            buffer.push(i);
            i += 1;
        }
        LittleEndian::write_u32(&mut buffer[..4], 1);
        let product_id_hash: [u8; PRODUCT_ID_HASH_SIZE] = [1; PRODUCT_ID_HASH_SIZE];
        let challenge: [u8; CHALLENGE_DATA_SIZE] = [0; CHALLENGE_DATA_SIZE];
        product_id_hash.iter().for_each(|b| buffer.push(*b));
        challenge.iter().for_each(|b| buffer.push(*b));

        let unlock_challenge = UnlockChallenge::new(&buffer.to_vec());
        assert_eq!(unlock_challenge.version, 1);
        assert_eq!(unlock_challenge.product_id_hash, product_id_hash);
        assert_eq!(unlock_challenge.challenge, challenge);
        Ok(())
    }
}
