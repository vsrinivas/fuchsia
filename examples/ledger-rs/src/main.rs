// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![feature(asm)]

extern crate apps_ledger_services_public;
extern crate apps_ledger_services_internal;
extern crate fidl;
extern crate futures;
extern crate garnet_public_lib_app_fidl;
extern crate garnet_public_lib_app_fidl_service_provider;
extern crate fuchsia_zircon as zircon;
extern crate fuchsia_zircon_sys as zircon_sys;
extern crate mxruntime;
extern crate tokio_core;

mod fuchsia;
mod ledger;

use zircon::ClockId;
use tokio_core::reactor;
use fuchsia::{ApplicationContext, Launcher, install_panic_backtrace_hook};
use apps_ledger_services_public::*;
use futures::Future;

pub fn main() {
    println!("# installing panic hook");
    install_panic_backtrace_hook();

    let mut core = reactor::Core::new().unwrap();
    let handle = core.handle();

    println!("# getting app context");
    let mut app_context: ApplicationContext = ApplicationContext::new(&handle).unwrap();
    println!("# getting launcher");
    let mut launcher = Launcher::new(&mut app_context, &handle).unwrap();
    println!("# getting ledger");
    let ledger_id = "rust_ledger_example".to_owned().into_bytes();
    let repo_path = "/data/test/rust_ledger_example".to_owned();
    let key = vec![0];
    let future = ledger::LedgerInstance::new(&mut launcher, repo_path, ledger_id, &handle)
        .and_then(|instance| {
            println!("# getting root page");
            Page::new_pair(&handle)
                .map(|pair| (pair, instance))
                .map_err(ledger::LedgerError::FidlError)
        }).and_then(|((page, page_request), mut instance)|
            instance.proxy.get_root_page(page_request)
                .then(ledger::map_ledger_error)
                .map(|()| (instance, page))
        ).and_then(|(instance, page)| {
            // get the current value
            println!("# getting page snapshot");
            PageSnapshot::new_pair(&handle)
                .map_err(ledger::LedgerError::FidlError)
                .map(|pair| ((instance, page), pair))
        }).and_then(|((instance, mut page), (snap, snap_request))|
            page.get_snapshot(snap_request, Some(key.clone()), None)
                .then(ledger::map_ledger_error)
                .map(|()| (instance, page, snap))
        ).and_then(|(instance, page, mut snap)| {
            println!("# getting key value");
            snap.get(key.clone()).then(ledger::map_value_result)
                .map(|value_opt| (instance, page, value_opt))
        }).and_then(|(instance, mut page, value_opt)| {
            println!("got value: {:?}", value_opt);
            let as_str = value_opt.and_then(|s| String::from_utf8(s).ok());
            println!("got value string: {:?}", as_str);

            // put a new value
            println!("# putting key value");
            let cur_time = zircon::time_get(ClockId::Monotonic);
            page.put(key.clone(), cur_time.to_string().into_bytes())
                .then(ledger::map_ledger_error)
                .map(|put_status| (instance, put_status))
        }).map(|(_instance, put_status)| {
            println!("put key with put_status: {:?}", put_status);
        });

    core.run(future).unwrap();
}
