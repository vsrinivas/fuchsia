// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![allow(dead_code)]
use rand::Rng;

use anyhow::{format_err, Context as _};
use fidl::{self, endpoints::ServerEnd};
use fidl_fuchsia_kms::{
    AsymmetricKeyAlgorithm, AsymmetricPrivateKeyProxy, Error, KeyManagerMarker, KeyManagerProxy,
    KeyOrigin, KeyProvider,
};
use fidl_fuchsia_mem::Buffer;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_syslog as syslog;
use fuchsia_zircon as zx;
use log::info;
use serde_derive::{Deserialize, Serialize};
use serde_json;
use std::fs;

use mundane::hash::*;
use mundane::public::ec::ecdsa::EcdsaHash;
use mundane::public::ec::*;
use mundane::public::*;

static TEST_KEY_NAME: &str = "test-key";
static CONFIG_PATH: &str = "/config/data/crypto_provider_config.json";

fn map_err_to_string<T>(result: Result<Result<T, Error>, fidl::Error>) -> Result<T, String> {
    match result {
        Ok(Ok(a)) => Ok(a),
        Ok(Err(err)) => Err(format!("{:?}", err)),
        Err(err) => Err(format!("{:?}", err)),
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test() -> Result<(), anyhow::Error> {
    syslog::init_with_tags(&["kms_test_client"]).expect("syslog init should not fail");
    test_asymmetric_key(AsymmetricKeyAlgorithm::EcdsaSha256P256).await?;
    test_asymmetric_key(AsymmetricKeyAlgorithm::EcdsaSha512P384).await?;
    test_asymmetric_key(AsymmetricKeyAlgorithm::EcdsaSha512P521).await?;
    test_seal_unseal_data().await?;
    Ok(())
}

#[derive(Debug, Deserialize, PartialEq, Serialize)]
pub struct Config<'a> {
    pub crypto_provider: &'a str,
}

fn get_provider_from_config() -> Result<KeyProvider, anyhow::Error> {
    let json = fs::read_to_string(CONFIG_PATH)?;
    let config: Config<'_> = serde_json::from_str(&json)?;
    match config.crypto_provider {
        "OpteeProvider" => Ok(KeyProvider::OpteeProvider),
        "SoftwareProvider" => Ok(KeyProvider::SoftwareProvider),
        "SoftwareAsymmetricOnlyProvider" => Ok(KeyProvider::SoftwareAsymmetricOnlyProvider),
        _ => Err(format_err!("Unsupported provider {:?}", config.crypto_provider)),
    }
}

async fn test_asymmetric_key(algorithm: AsymmetricKeyAlgorithm) -> Result<(), anyhow::Error> {
    info!("Begin asymmetric key tests for algorithm: {:?}", algorithm);
    let key_manager = connect_to_service::<KeyManagerMarker>()
        .context("Failed to connect to key manager service")?;
    let public_key = {
        // Generate a new key.
        let asymmetric_key_proxy =
            generate_asymmetric_key(&key_manager, TEST_KEY_NAME, algorithm).await?;

        // Export public key.
        let public_key = test_export_public_key(&asymmetric_key_proxy).await?;

        // Get key property.
        let key_algorithm = get_asymmetric_key_algorithm(&asymmetric_key_proxy).await?;
        assert_eq!(key_algorithm, algorithm, "Key algorithm mismatch!");
        let key_origin = get_asymmetric_key_origin(&asymmetric_key_proxy).await?;
        assert_eq!(key_origin, KeyOrigin::Generated, "Key origin mismatch!");
        let provider = get_asymmetric_key_provider(&asymmetric_key_proxy).await?;
        assert_eq!(
            provider,
            get_provider_from_config().unwrap_or(KeyProvider::SoftwareProvider),
            "Using the wrong crypto provider."
        );

        public_key
    };

    // Compare the exported public key.
    let asymmetric_key_proxy = get_asymmetric_key(&key_manager, TEST_KEY_NAME).await?;
    let public_key_compare = test_export_public_key(&asymmetric_key_proxy).await?;
    assert_eq!(public_key, public_key_compare, "Public key for the same key mismatch.");

    // Test signing using the generated key.
    test_sign_verify_data(algorithm, &key_manager).await?;

    {
        // Test generating a key with the same name.
        test_generate_asymmetric_key_exists(&key_manager, TEST_KEY_NAME, algorithm).await?;
    }

    {
        // Read the generated key.
        let asymmetric_key_proxy = get_asymmetric_key(&key_manager, TEST_KEY_NAME).await?;
        // Test deleting the generated key.
        test_delete_key(&key_manager, TEST_KEY_NAME).await?;
        // Verify that the key is deleted.
        check_key_not_exist(&key_manager, TEST_KEY_NAME).await?;
        // Verify that user would get key_not_found if trying to use the deleted key.
        test_sign_key_not_exists(&asymmetric_key_proxy).await?;
        // Verify deleting a deleted key would fail.
        test_delete_key_not_exist(&key_manager, TEST_KEY_NAME).await?;
    }

    {
        // Generate a new key_manager to simulate another user.
        let key_manager_2 = connect_to_service::<KeyManagerMarker>()
            .context("Failed to connect to key manager service")?;
        // The first user create a new key.
        let asymmetric_key_proxy =
            generate_asymmetric_key(&key_manager, TEST_KEY_NAME, algorithm).await?;
        // The second user delete the same key.
        test_delete_key(&key_manager_2, TEST_KEY_NAME).await?;
        // The first user should get key_not_found if trying to get the deleted key.
        check_key_not_exist(&key_manager, TEST_KEY_NAME).await?;
        // The first user should get key_not_found if trying to use the deleted key.
        test_sign_key_not_exists(&asymmetric_key_proxy).await?;
    }

    {
        // Test importing private key.
        let (private_key_data, public_key_data) = match algorithm {
            AsymmetricKeyAlgorithm::EcdsaSha256P256 => {
                let test_key = EcPrivKey::<P256>::generate()?;
                (test_key.marshal_to_der(), test_key.public().marshal_to_der())
            }
            AsymmetricKeyAlgorithm::EcdsaSha512P384 => {
                let test_key = EcPrivKey::<P384>::generate()?;
                (test_key.marshal_to_der(), test_key.public().marshal_to_der())
            }
            AsymmetricKeyAlgorithm::EcdsaSha512P521 => {
                let test_key = EcPrivKey::<P521>::generate()?;
                (test_key.marshal_to_der(), test_key.public().marshal_to_der())
            }
            _ => panic!("not implemented!"),
        };

        let asymmetric_key_proxy =
            import_asymmetric_key(&key_manager, private_key_data, TEST_KEY_NAME, algorithm).await?;

        // Export public key.
        let public_key = test_export_public_key(&asymmetric_key_proxy).await?;
        assert_eq!(public_key, public_key_data, "Public key mismatch!");

        // Get key property.
        let key_algorithm = get_asymmetric_key_algorithm(&asymmetric_key_proxy).await?;
        assert_eq!(key_algorithm, algorithm, "Key algorithm in key property mismatch!");
        let key_origin = get_asymmetric_key_origin(&asymmetric_key_proxy).await?;
        assert_eq!(key_origin, KeyOrigin::Imported, "Key origin mismatch!");

        // Test signing using the imported key.
        test_sign_verify_data(algorithm, &key_manager).await?;

        // Test deleting the imported key.
        test_delete_key(&key_manager, TEST_KEY_NAME).await?;
    }

    info!("All asymmetric key tests for algorithm: {:?} passed!", algorithm);
    Ok(())
}

async fn test_sign_verify_data(
    algorithm: AsymmetricKeyAlgorithm,
    key_manager: &KeyManagerProxy,
) -> Result<(), anyhow::Error> {
    // Read the key.
    let asymmetric_key_proxy = get_asymmetric_key(&key_manager, TEST_KEY_NAME).await?;
    let public_key = test_export_public_key(&asymmetric_key_proxy).await?;

    // Sign test data with the key again.
    let test_data = generate_test_data();
    let signature = test_sign(&asymmetric_key_proxy, &test_data).await?;

    // Verify signature.
    verify_signature(algorithm, &signature, &public_key, &test_data)?;
    Ok(())
}

async fn test_seal_unseal_data() -> Result<(), anyhow::Error> {
    info!("Begin sealing and unsealing data test");
    let key_manager = connect_to_service::<KeyManagerMarker>()
        .context("Failed to connect to key manager service")?;
    let test_data = generate_test_data();
    let vmo = zx::Vmo::create(test_data.len() as u64)?;
    vmo.write(&test_data, 0)?;
    let mut buffer = Buffer { vmo, size: test_data.len() as u64 };
    let result = key_manager.seal_data(&mut buffer).await;
    let result_buffer =
        map_err_to_string(result).map_err(|err| format_err!("Error sealing data: {:?}", err))?;

    let mut result_data = vec![0; result_buffer.size as usize];
    result_buffer.vmo.read(&mut result_data, 0).unwrap();

    let vmo = zx::Vmo::create(result_data.len() as u64)?;
    vmo.write(&result_data, 0)?;
    let mut buffer = Buffer { vmo, size: result_data.len() as u64 };
    let result = key_manager.unseal_data(&mut buffer).await;
    let result_buffer =
        map_err_to_string(result).map_err(|err| format_err!("Error unsealing data: {:?}", err))?;
    let mut result_data = vec![0; result_buffer.size as usize];
    result_buffer.vmo.read(&mut result_data, 0)?;
    assert_eq!(&test_data, &result_data, "Unsealing after sealing gets different data!");
    info!("Sealing and unsealing data tests passed!");
    Ok(())
}

async fn generate_asymmetric_key<'a>(
    key_manager: &'a KeyManagerProxy,
    key_name: &'static str,
    key_algorithm: AsymmetricKeyAlgorithm,
) -> Result<AsymmetricPrivateKeyProxy, anyhow::Error> {
    let (server_chan, client_chan) = zx::Channel::create()?;
    let result = key_manager
        .generate_asymmetric_key_with_algorithm(
            key_name,
            key_algorithm,
            ServerEnd::new(server_chan),
        )
        .await;
    map_err_to_string(result)
        .map_err(|err| format_err!("Error generating asymmetric key: {:?}", err))?;
    info!("Asymmetric private key successfully generated.");
    let client_async = fasync::Channel::from_channel(client_chan).unwrap();
    let asymmetric_key_proxy = AsymmetricPrivateKeyProxy::new(client_async);
    Ok(asymmetric_key_proxy)
}

async fn test_generate_asymmetric_key_exists<'a>(
    key_manager: &'a KeyManagerProxy,
    key_name: &'static str,
    key_algorithm: AsymmetricKeyAlgorithm,
) -> Result<(), anyhow::Error> {
    let (server_chan, _) = zx::Channel::create()?;
    let result = key_manager
        .generate_asymmetric_key_with_algorithm(
            key_name,
            key_algorithm,
            ServerEnd::new(server_chan),
        )
        .await
        .map_err(|err| format_err!("Error generating asymmetric key: {:?}", err))?;
    match result {
        Err(Error::KeyAlreadyExists) => {
            info!("Verify that the key already exists.");
            Ok(())
        }
        Ok(_) => Err(format_err!("Generating a key with the same name must not be allowed!")),
        Err(err) => Err(format_err!("Error generating asymmetric key: {:?}", err)),
    }
}

async fn get_asymmetric_key<'a>(
    key_manager: &'a KeyManagerProxy,
    key_name: &'static str,
) -> Result<AsymmetricPrivateKeyProxy, anyhow::Error> {
    let (server_chan, client_chan) = zx::Channel::create()?;
    let result =
        key_manager.get_asymmetric_private_key(key_name, ServerEnd::new(server_chan)).await;
    map_err_to_string(result)
        .map_err(|err| format_err!("Error getting asymmetric key: {:?}", err))?;
    info!("Asymmetric private key successfully read.");
    let client_async = fasync::Channel::from_channel(client_chan).unwrap();
    let asymmetric_key_proxy = AsymmetricPrivateKeyProxy::new(client_async);
    Ok(asymmetric_key_proxy)
}

async fn test_sign_key_not_exists(key: &AsymmetricPrivateKeyProxy) -> Result<(), anyhow::Error> {
    let test_data = generate_test_data();
    let vmo = zx::Vmo::create(test_data.len() as u64)?;
    vmo.write(&test_data, 0)?;
    let result = key
        .sign(&mut Buffer { vmo, size: test_data.len() as u64 })
        .await
        .map_err(|err| format_err!("Error generating signature: {:?}", err))?;
    match result {
        Err(Error::KeyNotFound) => {
            info!("Verify that the key does not exist.");
            Ok(())
        }
        Ok(_) => Err(format_err!("Key not deleted.")),
        Err(err) => Err(format_err!("Error generating signature: {:?}", err)),
    }
}

async fn check_key_not_exist<'a>(
    key_manager: &'a KeyManagerProxy,
    key_name: &'static str,
) -> Result<(), anyhow::Error> {
    let (server_chan, _client_chan) = zx::Channel::create()?;
    let result = key_manager
        .get_asymmetric_private_key(key_name, ServerEnd::new(server_chan))
        .await
        .map_err(|err| format_err!("Error getting asymmetric key: {:?}", err))?;
    match result {
        Err(Error::KeyNotFound) => {
            info!("Verify that the key does not exist.");
            Ok(())
        }
        Ok(()) => Err(format_err!("Key not deleted.")),
        Err(err) => Err(format_err!("Error getting asymmetric key: {:?}", err)),
    }
}

async fn test_sign<'a>(
    key: &'a AsymmetricPrivateKeyProxy,
    test_data: &'a Vec<u8>,
) -> Result<Vec<u8>, anyhow::Error> {
    let vmo = zx::Vmo::create(test_data.len() as u64)?;
    vmo.write(test_data, 0)?;
    let result = key.sign(&mut Buffer { vmo, size: test_data.len() as u64 }).await;
    let signature = map_err_to_string(result)
        .map_err(|err| format_err!("Error generating signature: {:?}", err))?;
    let signature_data = signature.bytes;
    info!("Signature successfully generated with size: {}", signature_data.len());
    Ok(signature_data)
}

async fn test_export_public_key(key: &AsymmetricPrivateKeyProxy) -> Result<Vec<u8>, anyhow::Error> {
    let result = key.get_public_key().await;
    let public_key = map_err_to_string(result)
        .map_err(|err| format_err!("Error exporting public key: {:?}", err))?;
    let public_key_data = public_key.bytes;
    info!("Public key successfully exported with size: {}", public_key_data.len());
    Ok(public_key_data)
}

async fn test_delete_key<'a>(
    key_manager: &'a KeyManagerProxy,
    key_name: &'static str,
) -> Result<(), anyhow::Error> {
    let result = key_manager.delete_key(key_name).await;
    map_err_to_string(result).map_err(|err| format_err!("Error deleting key: {:?}", err))?;
    info!("Key successfully deleted.");
    Ok(())
}

async fn test_delete_key_not_exist<'a>(
    key_manager: &'a KeyManagerProxy,
    key_name: &'static str,
) -> Result<(), anyhow::Error> {
    let result = key_manager
        .delete_key(key_name)
        .await
        .map_err(|err| format_err!("Error deleting key: {:?}", err))?;
    match result {
        Err(Error::KeyNotFound) => {
            info!("Verify a key that does not exist could not be deleted.");
            Ok(())
        }
        Ok(()) => Err(format_err!(
            "KEY_NOT_FOUND must be returned if the key to be deleted is not found."
        )),
        Err(err) => Err(format_err!("Error deleting key: {:?}", err)),
    }
}

async fn get_asymmetric_key_algorithm(
    key: &AsymmetricPrivateKeyProxy,
) -> Result<AsymmetricKeyAlgorithm, anyhow::Error> {
    let result = key.get_key_algorithm().await;
    map_err_to_string(result).map_err(|err| format_err!("Error exporting public key: {:?}", err))
}

async fn get_asymmetric_key_origin(
    key: &AsymmetricPrivateKeyProxy,
) -> Result<KeyOrigin, anyhow::Error> {
    let result = key.get_key_origin().await;
    map_err_to_string(result).map_err(|err| format_err!("Error getting key origin: {:?}", err))
}

async fn get_asymmetric_key_provider(
    key: &AsymmetricPrivateKeyProxy,
) -> Result<KeyProvider, anyhow::Error> {
    let result = key.get_key_provider().await;
    map_err_to_string(result)
        .map_err(|err| format_err!("Error getting crypto provider name: {:?}", err))
}

async fn import_asymmetric_key<'a>(
    key_manager: &'a KeyManagerProxy,
    private_key_data: Vec<u8>,
    key_name: &'static str,
    key_algorithm: AsymmetricKeyAlgorithm,
) -> Result<AsymmetricPrivateKeyProxy, anyhow::Error> {
    let (server_chan, client_chan) = zx::Channel::create()?;
    let result = key_manager
        .import_asymmetric_private_key(
            &mut private_key_data.into_iter(),
            key_name,
            key_algorithm,
            ServerEnd::new(server_chan),
        )
        .await;
    map_err_to_string(result)
        .map_err(|err| format_err!("Error importing asymmetric key: {:?}", err))?;
    info!("Asymmetric private key successfully imported.");
    let client_async = fasync::Channel::from_channel(client_chan).unwrap();
    let asymmetric_key_proxy = AsymmetricPrivateKeyProxy::new(client_async);
    Ok(asymmetric_key_proxy)
}

fn generate_test_data() -> Vec<u8> {
    let mut test_data: Vec<u8> = vec![0; 512];
    let mut rng = rand::thread_rng();
    for i in 0..test_data.len() {
        test_data[i] = rng.gen();
    }
    test_data
}

fn verify_signature<'a>(
    algorithm: AsymmetricKeyAlgorithm,
    signature: &'a Vec<u8>,
    public_key: &'a Vec<u8>,
    test_data: &'a Vec<u8>,
) -> Result<(), anyhow::Error> {
    match algorithm {
        AsymmetricKeyAlgorithm::EcdsaSha256P256 => {
            verify_sig::<P256, Sha256>(&signature, &public_key, &test_data)?;
        }
        AsymmetricKeyAlgorithm::EcdsaSha512P384 => {
            verify_sig::<P384, Sha512>(&signature, &public_key, &test_data)?;
        }
        AsymmetricKeyAlgorithm::EcdsaSha512P521 => {
            verify_sig::<P521, Sha512>(&signature, &public_key, &test_data)?;
        }
        _ => (),
    }
    Ok(())
}

fn verify_sig<'a, C: PCurve, H: Hasher + EcdsaHash<C>>(
    signature: &'a Vec<u8>,
    public_key: &'a Vec<u8>,
    test_data: &'a Vec<u8>,
) -> Result<(), anyhow::Error> {
    let ec_key = EcPubKey::<C>::parse_from_der(public_key)
        .map_err(|_| format_err!("Failed to parse public key!"))?;
    let result = ecdsa::EcdsaSignature::<C, H>::from_bytes(signature).is_valid(&ec_key, test_data);
    if !result {
        Err(format_err!("Failed to verify signature."))
    } else {
        info!("Signature verified successfully with the exported public key.");
        Ok(())
    }
}
