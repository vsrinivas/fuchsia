// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
extern crate rouille;
//extern crate serde_json;

use rouille::Request;
use rouille::Response;

//use std::collections::HashMap;
//use std::io;

// Template for storing information about each client that has connected
// To the simple server
struct ClientData {
    id: String,
}

// Skeleton of HTTP server using rouille
// TODO: Add JSON parsing
fn main() {
    //Config stuff, flexible for any ip/port combination
    const SERVER_IP: &str = "127.0.0.1";
    const SERVER_PORT: &str = "7979";
    let address = format!("{}:{}", SERVER_IP, SERVER_PORT);

    println!("Now listening on: {}", address);

    // Start listening on address
    rouille::start_server(address, move |request| {
        println!("The request is: {:?}", request);
        handle_traffic(&request)
    });
}

fn handle_traffic(request: &Request) -> Response {
    router!(request,
            (GET) (/) => {
                println!("Empty / GET.");
                rouille::Response::redirect_302("/hello/world")
            },
            (GET) (/hello/world) => {
                println!("Hello word GET");
                let hello = "hello world";
                rouille::Response::json(&hello)
            },
            (GET) (/{id: String}) => {
                println!("String GET {:?}", id);

                let formatted = format!("hello, {}", id);
                rouille::Response::json(&formatted)
            },
            _ => {
                println!("Unknown request!");
                rouille::Response::empty_404()
            }
        )
}
