// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![feature(asm)]

extern crate magenta;
extern crate magenta_sys;
extern crate mxruntime;
extern crate fidl;
extern crate application_services_service_provider;
extern crate application_services;
extern crate apps_ledger_services_public;
extern crate apps_ledger_services_internal;

mod fuchsia;
mod ledger;

use magenta::ClockId;
use fuchsia::{ApplicationContext, Launcher, install_panic_backtrace_hook};
use ledger::ledger_crash_callback;
use apps_ledger_services_public::*;

pub fn main() {
    println!("# installing panic hook");
    install_panic_backtrace_hook();

    println!("# getting app context");
    let mut app_context: ApplicationContext = ApplicationContext::new();
    println!("# getting launcher");
    let mut launcher = Launcher::new(&mut app_context);
    println!("# getting ledger");
    let ledger_id = "rust_ledger_example".to_owned().into_bytes();
    let mut ledger = ledger::Ledger::new(&mut launcher, "/data/test/rust_ledger_example".to_owned(), ledger_id);

    println!("# getting root page");
    let key = vec![0];
    let (mut page, page_request) = Page_new_pair();
    ledger.proxy.get_root_page(page_request).with(ledger_crash_callback);

    // get the current value
    println!("# getting page snapshot");
    let (mut snap, snap_request) = PageSnapshot_new_pair();
    page.get_snapshot(snap_request, Some(key.clone()), None).with(ledger_crash_callback);

    println!("# getting key value");
    let raw_res = snap.get(key.clone()).get().expect("fidl message for ledger key failed");
    let value_opt = ledger::value_result(raw_res).expect("failed to read value for key");
    println!("got value: {:?}", value_opt);
    let as_str = value_opt.and_then(|s| String::from_utf8(s).ok());
    println!("got value string: {:?}", as_str);

    // put a new value
    println!("# putting key value");
    let cur_time = magenta::time_get(ClockId::Monotonic);
    let put_status = page.put(key.clone(), cur_time.to_string().into_bytes()).get().unwrap();
    println!("put key with put_status: {:?}", put_status);

    // TODO: the following code causes a crash with 'unable to "resume" thread: -20 (ERR_BAD_STATE)'
    // it makes sense that the callback should never run if we don't wait, but it shouldn't cause a crash.

    // should still print old value, since we're using the same snapshot
    // println!("# getting key value with wait");
    // snap.get(key.clone()).with(|raw_res| {
    //     let raw_val = raw_res.expect("fidl message for ledger key failed");
    //     let value_opt = ledger::value_result(raw_val).expect("failed to read value for key");
    //     println!("got value after wait: {:?}", value_opt);
    //     let as_str = value_opt.and_then(|s| String::from_utf8(s).ok());
    //     println!("got value string after wait: {:?}", as_str);
    // });
}
