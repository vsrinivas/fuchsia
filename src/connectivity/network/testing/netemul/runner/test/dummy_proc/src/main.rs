// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_netemul_environment::ManagedEnvironmentMarker,
    fidl_fuchsia_netemul_sync::{BusMarker, BusProxy, Event, SyncManagerMarker},
    fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon as zx,
    futures::TryStreamExt,
    structopt::StructOpt,
};

#[derive(StructOpt, Debug)]
struct Opt {
    #[structopt(short = "f")]
    fail: bool,
    #[structopt(short = "n", default_value = "root")]
    name: String,
    #[structopt(short = "w")]
    wait: Option<u64>,
    #[structopt(short = "p")]
    publish: Option<i32>,
    #[structopt(short = "e")]
    event: Option<i32>,
    #[structopt(short = "P")]
    check_path: Option<String>,
    #[structopt(short = "s")]
    service: Option<String>,
    #[structopt(short = "l")]
    log: Option<String>,
}

const BUS_NAME: &str = "test-bus";

pub struct BusConnection {
    bus: BusProxy,
}

impl BusConnection {
    pub fn new(client: &str) -> Result<BusConnection, Error> {
        let busm = client::connect_to_service::<SyncManagerMarker>()
            .context("SyncManager not available")?;
        let (bus, busch) = fidl::endpoints::create_proxy::<BusMarker>()?;
        busm.bus_subscribe(BUS_NAME, client, busch)?;
        Ok(BusConnection { bus })
    }

    pub async fn publish_code(&self, code: i32) -> Result<(), Error> {
        let () = self
            .bus
            .ensure_publish(Event { code: Some(code), message: None, arguments: None })
            .await?;
        Ok(())
    }

    pub async fn wait_for_event(&self, code: i32) -> Result<(), Error> {
        let mut stream = self.bus.take_event_stream().try_filter_map(|event| match event {
            fidl_fuchsia_netemul_sync::BusEvent::OnBusData { data } => match data.code {
                Some(rcv_code) => {
                    if rcv_code == code {
                        futures::future::ok(Some(()))
                    } else {
                        futures::future::ok(None)
                    }
                }
                None => futures::future::ok(None),
            },
            _ => futures::future::ok(None),
        });
        stream.try_next().await?;
        Ok(())
    }

    pub async fn perform_bus_ops(
        &self,
        publish: Option<i32>,
        wait: Option<i32>,
    ) -> Result<(), Error> {
        if let Some(code) = wait {
            let () = self.wait_for_event(code).await?;
        }
        if let Some(code) = publish {
            let () = self.publish_code(code).await?;
        }
        Ok(())
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opt = Opt::from_args();

    // Attempt to connect to the bus early on so that we do not miss any events.
    //
    // We won't actually unwrap `bus` until later in case this
    // test does not actually need the bus.
    let bus = BusConnection::new(&opt.name);

    if let Some(log) = opt.log {
        println!("Initing syslog...");
        let () = fuchsia_syslog::init().context("cannot init logger")?;

        println!("Logging to syslog: {}", log);
        log::info!("{}", log);
    }

    if let Some(svc) = opt.service {
        println!("Connecting to service [{}]...", svc);
        let env = client::connect_to_service::<ManagedEnvironmentMarker>()?;
        let (_dummy, server) = zx::Channel::create()?;
        env.connect_to_service(&svc, server)?;
    }

    if let Some(wait) = opt.wait {
        println!("Sleeping for {}...", wait);
        std::thread::sleep(std::time::Duration::from_millis(wait));
    }

    if let Some(path) = opt.check_path {
        println!("Checking path existence {}...", &path);
        if !std::path::Path::new(&path).exists() {
            return Err(format_err!("{} is not in namespace", &path));
        }
    }

    if opt.publish != None || opt.event != None {
        // Unwrap the `bus` which should be an error
        // if the test requires publish or event waiting operations.
        let bus = bus.context("Failed to connect to bus")?;

        println!("Publishing: {:?} | Waiting for event: {:?}", opt.publish, opt.event);
        let () = bus.perform_bus_ops(opt.publish, opt.event).await?;
    }

    if opt.fail {
        Err(format_err!("Failing because was asked to."))
    } else {
        println!("All done!");
        Ok(())
    }
}
