// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(try_from,async_await,await_macro)]

use failure::{err_msg, Error, ResultExt};
use fidl::endpoints::create_proxy;
use fidl_fuchsia_mem;
use fuchsia_app::client::connect_to_service;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::FutureExt;
use std::convert::{TryFrom, TryInto};
use std::env;
use std::str;

use fidl_fuchsia_stash::{KeyValue, ListItem, StoreMarker, Value};

fn main() -> Result<(), Error> {
    let opts = env::args().try_into()?;

    let mut executor = fasync::Executor::new().context("Error creating executor")?;

    let stashserver = connect_to_service::<StoreMarker>()?;

    // Identify
    let fut = stashserver.identify("stash_ctl")?;

    // Create an accessor
    let (acc, serverEnd) = create_proxy()?;
    stashserver.create_accessor(false, serverEnd)?;

    // Perform the operation
    match opts {
        StashOperation::Get(k) => {
            let fut = acc.get_value(&k)
                .map(|res| match res {
                    Ok(Some(val)) => print_val(&val),
                    Ok(None) => println!("no such key: {}", k),
                    Err(e) => println!("fidl error encountered: {:?}", e),
                });
            executor.run_singlethreaded(fut);
        }
        StashOperation::Set(k, mut v) => {
            acc.set_value(&k, &mut v)?;
            acc.commit()?;
            println!("{} set successfully", k);
        }
        StashOperation::Delete(k) => {
            acc.delete_value(&k)?;
            acc.commit()?;
            println!("{} deleted successfully", k);
        }
        StashOperation::ListPrefix(k) => {
            let (list_iterator, server_end) = create_proxy()?;
            acc.list_prefix(&k, server_end)?;

            let resp: Result<(), Error> = executor.run_singlethreaded(async {
                loop {
                    let res = await!(list_iterator.get_next())?;
                    if res.len() == 0 {
                        return Ok(());
                    }
                    print_listitems(res);
                }
            });
            resp?;
        }
        StashOperation::GetPrefix(k) => {
            let (get_iterator, server_end) = create_proxy()?;
            acc.get_prefix(&k, server_end)?;

            let resp: Result<(), Error> = executor.run_singlethreaded(async {
                loop {
                    let res = await!(get_iterator.get_next())?;
                    if res.len() == 0 {
                        return Ok(());
                    }
                    print_keyvalues(res);
                }
            });
            resp?;
        }
        StashOperation::DeletePrefix(k) => {
            acc.delete_prefix(&k)
                .map(|_| println!("{} prefix deleted successfully", k))?;
            acc.commit()?;
        }
    };
    Ok(())
}

enum StashOperation {
    Get(String),
    Set(String, Value),
    Delete(String),
    ListPrefix(String),
    GetPrefix(String),
    DeletePrefix(String),
}

impl TryFrom<env::Args> for StashOperation {
    type Error = Error;
    fn try_from(mut args: env::Args) -> Result<StashOperation, Error> {
        // ignore arg[0]
        let _ = args.next();
        // take advantage of the fact that `next()` will keep returning `None`
        let op  = args.next();
        let key = args.next();
        let ty  = args.next();
        let val = args.next();

        match (
            op.as_ref().map(|s| s.as_str()),
            key,
            ty.as_ref().map(|s| s.as_str()),
            val.as_ref().map(|s| s.as_str()),
        ) {
            (Some("get"), Some(key), None, None) =>
                Ok(StashOperation::Get(key)),
            (Some("set"), Some(key), Some(type_), Some(val)) =>
                Ok(StashOperation::Set(key, to_val(type_, val)?)),
            (Some("delete"), Some(key), None, None) =>
                Ok(StashOperation::Delete(key)),
            (Some("list-prefix"), Some(key), None, None) =>
                Ok(StashOperation::ListPrefix(key)),
            (Some("get-prefix"), Some(key), None, None) =>
                Ok(StashOperation::GetPrefix(key)),
            (Some("delete-prefix"), Some(key), None, None) =>
                Ok(StashOperation::DeletePrefix(key)),

            _ => {
                help();
                Err(err_msg("unable to parse args"))
            }
        }
    }
}

fn to_val(typ: &str, input: &str) -> Result<Value, Error> {
    match typ {
        "int" => Ok(Value::Intval(input.parse()?)),
        "float" => Ok(Value::Floatval(input.parse()?)),
        "text" => Ok(Value::Stringval(input.to_string())),
        "bool" => Ok(Value::Boolval(input.parse()?)),
        "bytes" => {
            let bytes = input.as_bytes();
            let vmo = zx::Vmo::create(bytes.len() as u64)
                .map_err(|s| err_msg(format!("error creating bytes buffer, zx status: {}", s)))?;
            vmo.write(&bytes, 0)
                .map_err(|s| err_msg(format!("error writing bytes buffer, zx status: {}", s)))?;
            Ok(Value::Bytesval(fidl_fuchsia_mem::Buffer {
                vmo: vmo,
                size: bytes.len() as u64,
            }))
        }
        _ => Err(err_msg(format!("unknown type: {}", typ))),
    }
}

fn help() {
    println!(
        r"Usage: stash_ctl get NAME
       stash_ctl set NAME [int|float|text|bool|bytes] VALUE
       stash_ctl delete NAME
       stash_ctl list-prefix PREFIX
       stash_ctl get-prefix PREFIX
       stash_ctl delete-prefix PREFIX"
    )
}

fn print_val(val: &Value) {
    match val {
        Value::Intval(i) => println!("{}", i),
        Value::Floatval(f) => println!("{}", f),
        Value::Boolval(b) => println!("{}", b),
        Value::Stringval(s) => println!("{}", s),
        Value::Bytesval(b) => {
            let mut bytes_buffer = vec![0; b.size as usize]; // TODO: make sure b.size is reasonable
            match b.vmo.read(&mut bytes_buffer, 0) {
                Ok(_) => match str::from_utf8(&bytes_buffer) {
                    Ok(s) => println!("{}", s),
                    Err(e) => println!("error decoding response: {}", e),
                },
                Err(e) => println!("error reading json buffer, zx status: {}", e),
            }
        }
    }
}

fn print_listitems(list: Vec<ListItem>) {
    if list.is_empty() {
        println!("no keys found");
        return;
    }

    list.iter().for_each(|item| println!("{}", item.key));
}

fn print_keyvalues(list: Vec<KeyValue>) {
    if list.is_empty() {
        println!("no keys found");
        return;
    }

    list.iter().for_each(|item| {
        print!("{}: ", item.key);
        print_val(&item.val);
    });
}
