// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error};
use futures::channel::mpsc;
use rouille::{self, router, Request, Response};
use serde::{Deserialize, Serialize};
use std::io::Read;
use std::thread;

const SERVER_IP: &str = "::";
const SERVER_PORT: &str = "8880";

pub enum SetupEvent {
    Root,
    DevhostOta { cfg: DevhostConfig },
}

/// Devhost configuration, passed to the actual OTA process.
pub struct DevhostConfig {
    pub url: String,
    pub authorized_keys: String,
}

#[derive(Deserialize, Serialize)]
/// Configuration provided by the host for the devhost OTA. Only used for de/serialization.
struct DevhostRequestInfo {
    /// We assume that the OTA server is running on the requester's address
    /// at the given port.
    pub port: u16,
    /// Contents of SSH authorized keys file to install to minfs.
    pub authorized_keys: String,
}

fn parse_ota_json(request: &Request) -> Result<DevhostConfig, Error> {
    let mut host_addr = request.remote_addr().ip().to_string();
    if request.remote_addr().is_ipv6() {
        host_addr = format!("[{}]", host_addr);
    }
    let mut body = request.data().ok_or(anyhow!("Post body already read?"))?;

    let mut json_str = String::new();
    body.read_to_string(&mut json_str).context("Failed to read request body")?;

    let cfg: DevhostRequestInfo =
        serde_json::from_str(&json_str).context("Failed to parse JSON")?;

    let result = DevhostConfig {
        url: format!("http://{}:{}/config.json", host_addr, cfg.port),
        authorized_keys: cfg.authorized_keys.clone(),
    };
    Ok(result)
}

fn serve(request: &Request, rouille_sender: mpsc::UnboundedSender<SetupEvent>) -> Response {
    router!(request,
        (GET) (/) => {
            rouille_sender.unbounded_send(SetupEvent::Root).expect("Async thread closed the channel.");
            rouille::Response::text("Root document\n")
        },
        (POST) (/ota/devhost) => {
            // get devhost info out of POST request.
            let result = parse_ota_json(request);
            match result {
                Err(e) => rouille::Response::text(format!("Bad request: {:?}", e)).with_status_code(400),
                Ok(cfg) => {
                    rouille_sender.unbounded_send(SetupEvent::DevhostOta { cfg }).expect("Async thread closed the channel.");
                    rouille::Response::text("Started OTA\n")
                },
            }
        },
        _ => {
            rouille::Response::text("Unknown command\n").with_status_code(404)
        }
    )
}

pub fn start_server() -> Result<mpsc::UnboundedReceiver<SetupEvent>, Error> {
    println!("recovery: start_server");

    let address = format!("{}:{}", SERVER_IP, SERVER_PORT);
    let (rouille_sender, async_receiver) = mpsc::unbounded();
    thread::Builder::new().name("setup-server".into()).spawn(move || {
        rouille::start_server(address, move |request| serve(&request, rouille_sender.clone()));
    })?;

    Ok(async_receiver)
}
