// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl::endpoints::RequestStream as _,
    fidl_examples_canvas_baseline::{BoundingBox, InstanceRequest, InstanceRequestStream, Point},
    fuchsia_async::{Time, Timer},
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::{self as zx},
    futures::future::join,
    futures::prelude::*,
    std::sync::{Arc, Mutex},
};

// A struct that stores the two things we care about for this example: the bounding box the lines
// that have been added thus far, and bit to track whether or not there have been changes since the
// last `OnDrawn` event.
#[derive(Debug)]
struct CanvasState {
    // Tracks whether there has been a change since the last send, to prevent redundant updates.
    changed: bool,
    bounding_box: BoundingBox,
}

/// Handler for the `AddLine` method.
fn add_line(state: &mut CanvasState, line: [Point; 2]) {
    // Update the bounding box to account for the new lines we've just "added" to the canvas.
    let mut bounds = &mut state.bounding_box;
    for point in line {
        if point.x < bounds.top_left.x {
            bounds.top_left.x = point.x;
        }
        if point.y > bounds.top_left.y {
            bounds.top_left.y = point.y;
        }
        if point.x > bounds.bottom_right.x {
            bounds.bottom_right.x = point.x;
        }
        if point.y < bounds.bottom_right.y {
            bounds.bottom_right.y = point.y;
        }
    }

    // Mark the state as "dirty", so that an update is sent back to the client on the next tick.
    state.changed = true
}

/// Creates a new instance of the server, paired to a single client across a zircon channel.
async fn run_server(stream: InstanceRequestStream) -> Result<(), Error> {
    // Create a new in-memory state store for the state of the canvas. The store will live for the
    // lifetime of the connection between the server and this particular client.
    let state = Arc::new(Mutex::new(CanvasState {
        changed: true,
        bounding_box: BoundingBox {
            top_left: Point { x: 0, y: 0 },
            bottom_right: Point { x: 0, y: 0 },
        },
    }));

    // Take ownership of the control_handle from the stream, which will allow us to push events from
    // a different async task.
    let control_handle = stream.control_handle();

    // A separate watcher task periodically "draws" the canvas, and notifies the client of the new
    // state. We'll need a cloned reference to the canvas state to be accessible from the new
    // task.
    let state_ref = state.clone();
    let update_sender = || async move {
        loop {
            // Our server sends one update per second.
            Timer::new(Time::after(zx::Duration::from_seconds(1))).await;
            let mut state = state_ref.lock().unwrap();
            if !state.changed {
                continue;
            }

            // After acquiring the lock, this is where we would draw the actual lines. Since this is
            // just an example, we'll avoid doing the actual rendering, and simply send the bounding
            // box to the client instead.
            let mut bounds = state.bounding_box;
            match control_handle.send_on_drawn(&mut bounds.top_left, &mut bounds.bottom_right) {
                Ok(_) => println!(
                    "OnDrawn event sent: top_left: {:?}, bottom_right: {:?}",
                    bounds.top_left, bounds.bottom_right
                ),
                Err(_) => return,
            }

            // Reset the change tracker.
            state.changed = false
        }
    };

    // Handle requests on the protocol sequentially - a new request is not handled until its
    // predecessor has been processed.
    let state_ref = &state;
    let request_handler =
        stream.map(|result| result.context("failed request")).try_for_each(|request| async move {
            // Match based on the method being invoked.
            match request {
                InstanceRequest::AddLine { line, .. } => {
                    println!("AddLine request received: {:?}", line);
                    add_line(&mut state_ref.lock().unwrap(), line);
                }
            }
            Ok(())
        });

    // This line will only be reached if the server errors out. The stream will await indefinitely,
    // thereby creating a long-lived server. Here, we first wait for the updater task to realize the
    // connection has died, then bubble up the error.
    join(request_handler, update_sender()).await.0
}

// A helper enum that allows us to treat a `Instance` service instance as a value.
enum IncomingService {
    Instance(InstanceRequestStream),
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    println!("Started");

    // Add a discoverable instance of our `Instance` protocol - this will allow the client to see
    // the server and connect to it.
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::Instance);
    fs.take_and_serve_directory_handle()?;
    println!("Listening for incoming connections");

    // The maximum number of concurrent clients that may be served by this process.
    const MAX_CONCURRENT: usize = 10;

    // Serve each connection simultaneously, up to the `MAX_CONCURRENT` limit.
    fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::Instance(stream)| {
        run_server(stream).unwrap_or_else(|e| println!("{:?}", e))
    })
    .await;

    Ok(())
}
