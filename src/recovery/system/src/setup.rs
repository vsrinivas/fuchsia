// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use futures::channel::mpsc;
use rouille::{self, router, Request, Response};
use std::thread;

const SERVER_IP: &str = "::";
const SERVER_PORT: &str = "8880";

pub enum SetupEvent {
    Root,
}

fn serve(request: &Request, rouille_sender: mpsc::UnboundedSender<SetupEvent>) -> Response {
    router!(request,
        (GET) (/) => {
            rouille_sender.unbounded_send(SetupEvent::Root).expect("Async thread closed the channel.");
            rouille::Response::text("Root document\n")
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
