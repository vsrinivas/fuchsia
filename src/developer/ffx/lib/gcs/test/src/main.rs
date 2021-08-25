// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A simple interactive test runner for the GCS lib.

use {
    anyhow::Result,
    fuchsia_hyper::new_https_client,
    gcs::{
        client::ClientFactory,
        token_store::{auth_code_url, TokenStore},
    },
};

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<()> {
    println!(
        "\nThis is a test program for the GCS download lib.\
        \nRead the code in //src/developer/ffx/lib/gcs/test/src/main.rs \
        to see what it does.\n"
    );
    let auth_code = get_auth_code();
    let token_store =
        TokenStore::new_with_code(&new_https_client(), &auth_code).await.expect("token_store");

    let factory = ClientFactory::new(token_store);
    let client = factory.create_client();

    // Test download of an existing blob (the choice of blob is arbitrary, feel
    // free to change it).
    let bucket = "fuchsia-sdk";
    let object = "development/LATEST_LINUX";
    let res = client.download(bucket, object).await.expect("client download");
    assert_eq!(res.status(), 200, "res {:?}", res);

    println!("\nSuccess. Test complete.");
    Ok(())
}

fn get_auth_code() -> String {
    use std::io::{stdin, stdout, Write};
    print!(
        "Please visit this site and copy the authentication code:\
        \n\n{}\n\nPaste the auth_code (from website) here and press return: ",
        auth_code_url(),
    );
    stdout().flush().expect("stdout flush");
    let mut auth_code = String::new();
    stdin().read_line(&mut auth_code).expect("Need an auth_code.");
    auth_code
}
