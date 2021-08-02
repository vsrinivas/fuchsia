// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    byteorder::{ByteOrder, LittleEndian},
    fidl_fuchsia_fxfs::{CryptRequest, CryptRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{
        stream::{StreamExt, TryStreamExt},
        TryFutureExt,
    },
    rand::RngCore,
};

enum Services {
    Crypt(CryptRequestStream),
}

const WRAP_XOR: u64 = 0x012345678abcdef;

async fn handle_request(stream: Services) -> Result<(), Error> {
    match stream {
        Services::Crypt(mut stream) => {
            while let Some(request) = stream.try_next().await.context("Reading request")? {
                match request {
                    CryptRequest::CreateKey { wrapping_key_id, owner, responder } => {
                        assert_eq!(wrapping_key_id, 0);
                        let mut rng = rand::thread_rng();
                        let mut key = [0; 32];
                        rng.fill_bytes(&mut key);
                        let mut wrapped = [0; 32];
                        for (i, chunk) in key.chunks_exact(8).enumerate() {
                            LittleEndian::write_u64(
                                &mut wrapped[i * 8..i * 8 + 8],
                                LittleEndian::read_u64(chunk) ^ WRAP_XOR ^ owner,
                            );
                        }
                        responder.send(&mut Ok((wrapped.into(), key.into()))).unwrap_or_else(|e| {
                            log::error!("Failed to send CreateKey response: {:?}", e)
                        });
                    }
                    CryptRequest::UnwrapKeys { wrapping_key_id, owner, keys, responder } => {
                        assert_eq!(wrapping_key_id, 0);
                        let mut unwrapped_keys = Vec::new();
                        for key in keys {
                            let mut unwrapped = [0; 32];
                            for (chunk, mut unwrapped) in
                                key.chunks_exact(8).zip(unwrapped.chunks_exact_mut(8))
                            {
                                LittleEndian::write_u64(
                                    &mut unwrapped,
                                    LittleEndian::read_u64(chunk) ^ WRAP_XOR ^ owner,
                                );
                            }
                            unwrapped_keys.push(unwrapped.into());
                        }
                        responder.send(&mut Ok(unwrapped_keys)).unwrap_or_else(|e| {
                            log::error!("Failed to send UnwrapKeys response: {:?}", e)
                        });
                    }
                }
            }
        }
    }
    Ok(())
}

#[fasync::run(10)]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init().unwrap();

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(Services::Crypt);
    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 10_000;
    fs.for_each_concurrent(MAX_CONCURRENT, |request| {
        handle_request(request).unwrap_or_else(|e| log::error!("{}", e))
    })
    .await;

    Ok(())
}
