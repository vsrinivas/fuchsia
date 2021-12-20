// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::common::{
        done_time, file::FileResolver, handle_upload_progress_for_staging, is_locked,
        map_fidl_error, UNLOCK_ERR,
    },
    anyhow::{anyhow, bail, Result},
    async_fs::OpenOptions,
    byteorder::{ByteOrder, LittleEndian},
    chrono::Utc,
    errors::{ffx_bail, ffx_error},
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_developer_bridge::{FastbootProxy, UploadProgressListenerMarker},
    futures::{prelude::*, try_join},
    ring::{
        rand,
        signature::{RsaKeyPair, RSA_PKCS1_SHA512},
    },
    std::fs::File,
    std::io::copy,
    std::io::Write,
    std::path::{Path, PathBuf},
    tempfile::tempdir,
    zip::read::ZipArchive,
};

const UNLOCK_CHALLENGE: &str = "vx-get-unlock-challenge";

const CHALLENGE_STRUCT_SIZE: u64 = 52;
const CHALLENGE_DATA_SIZE: usize = 16;
const PRODUCT_ID_HASH_SIZE: usize = 32;

#[derive(Debug)]
struct UnlockChallenge {
    #[cfg_attr(not(test), allow(unused))]
    version: u32,
    product_id_hash: [u8; PRODUCT_ID_HASH_SIZE],
    challenge_data: [u8; CHALLENGE_DATA_SIZE],
}

impl UnlockChallenge {
    fn new(buffer: &Vec<u8>) -> Self {
        let mut result = Self {
            version: LittleEndian::read_u32(&buffer[..4]),
            product_id_hash: [0; PRODUCT_ID_HASH_SIZE],
            challenge_data: [0; CHALLENGE_DATA_SIZE],
        };
        result.product_id_hash.clone_from_slice(&buffer[4..PRODUCT_ID_HASH_SIZE + 4]);
        result.challenge_data.clone_from_slice(&buffer[PRODUCT_ID_HASH_SIZE + 4..]);
        result
    }
}

async fn get_unlock_challenge(fastboot_proxy: &FastbootProxy) -> Result<UnlockChallenge> {
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
const PUK: &str = "puk.pem";

const CERT_SUBJECT_OFFSET: usize = 4 + 1032;
const CERT_SUBJECT_LENGTH: usize = 32;

const PRIVATE_KEY_BEGIN: &str = "-----BEGIN PRIVATE KEY-----";
const PRIVATE_KEY_END: &str = "-----END PRIVATE KEY-----";

struct UnlockCredentials {
    intermediate_cert: [u8; EXPECTED_CERTIFICATE_SIZE],
    unlock_cert: [u8; EXPECTED_CERTIFICATE_SIZE],
    unlock_key: RsaKeyPair,
}

impl UnlockCredentials {
    pub async fn new<T: AsRef<Path>>(path: T) -> Result<Self> {
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

        // Decrypt the base64 key from the pem file.
        let puk_file = temp_dir.path().join(PUK);
        let contents = async_fs::read_to_string(puk_file).await?;

        let private_key_pem = contents
            .replace(PRIVATE_KEY_BEGIN, "")
            .replace("\r\n", "")
            .replace("\n", "")
            .replace(PRIVATE_KEY_END, "");

        let private_key_pem_bytes = base64::decode(&private_key_pem)?;

        let mut result = Self {
            intermediate_cert: [0; EXPECTED_CERTIFICATE_SIZE],
            unlock_cert: [0; EXPECTED_CERTIFICATE_SIZE],
            unlock_key: RsaKeyPair::from_pkcs8(&private_key_pem_bytes[..])
                .map_err(|e| ffx_error!("Could not decode RSA private key: {}", e))?,
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

        Ok(result)
    }

    fn get_atx_certificate_subject(&self) -> &[u8] {
        &self.unlock_cert[CERT_SUBJECT_OFFSET..CERT_SUBJECT_OFFSET + CERT_SUBJECT_LENGTH]
    }
}

pub async fn unlock_device<W: Write, F: FileResolver + Sync>(
    writer: &mut W,
    file_resolver: &mut F,
    creds: &Vec<String>,
    fastboot_proxy: &FastbootProxy,
) -> Result<()> {
    let search = Utc::now();
    write!(writer, "Looking for unlock credentials...")?;
    writer.flush()?;
    let challenge = get_unlock_challenge(&fastboot_proxy).await?;
    for cred in creds {
        let cred_file = file_resolver.get_file(writer, cred).await?;
        let unlock_creds = UnlockCredentials::new(&cred_file).await?;
        if challenge.product_id_hash[..] == *unlock_creds.get_atx_certificate_subject() {
            let d = Utc::now().signed_duration_since(search);
            done_time(writer, d)?;
            return unlock_device_with_creds(writer, unlock_creds, challenge, fastboot_proxy).await;
        }
    }
    ffx_bail!("{}", UNLOCK_ERR);
}

async fn unlock_device_with_creds<W: Write>(
    writer: &mut W,
    unlock_creds: UnlockCredentials,
    challenge: UnlockChallenge,
    fastboot_proxy: &FastbootProxy,
) -> Result<()> {
    let gen = Utc::now();
    write!(writer, "Generating unlock token...")?;
    writer.flush()?;

    let rng = rand::SystemRandom::new();
    let mut signature = vec![0; unlock_creds.unlock_key.public_modulus_len()];
    unlock_creds
        .unlock_key
        .sign(&RSA_PKCS1_SHA512, &rng, &challenge.challenge_data, &mut signature)
        .map_err(|_| ffx_error!("Could not sign unlocking keys"))?;

    let dir = tempdir()?;
    let path = dir.path().join("token");

    let mut file = async_fs::OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(path.clone())
        .await?;

    let mut buf = [0; 4];
    LittleEndian::write_u32(&mut buf, 1);
    file.write_all(&buf).await?;
    file.write_all(&unlock_creds.intermediate_cert).await?;
    file.write_all(&unlock_creds.unlock_cert).await?;
    file.write_all(&signature).await?;
    file.flush().await?;

    let d_gen = Utc::now().signed_duration_since(gen);
    done_time(writer, d_gen)?;

    writeln!(writer, "Preparing to upload unlock token")?;

    let file_path = path.to_str().ok_or(anyhow!("Could not get path for temporary token file"))?;
    let (prog_client, prog_server) = create_endpoints::<UploadProgressListenerMarker>()?;
    try_join!(
        fastboot_proxy.stage(&file_path.to_string(), prog_client).map_err(map_fidl_error),
        handle_upload_progress_for_staging(writer, prog_server),
    )
    .and_then(|(stage, _)| {
        stage.map_err(|e| anyhow!("There was an error staging {}: {:?}", file_path, e))
    })?;

    fastboot_proxy
        .oem("vx-unlock")
        .await?
        .map_err(|_| anyhow!("There was an error sending vx-unlock command"))?;

    match is_locked(fastboot_proxy).await {
        Ok(true) => bail!("Could not unlock device."),
        Ok(false) => Ok(()),
        Err(e) => bail!("Could not verify unlocking worked: {}", e),
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
        assert_eq!(unlock_challenge.challenge_data, challenge);
        Ok(())
    }
}
