// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_bluetooth_gatt::ClientProxy,
    fuchsia_async as fasync,
    fuchsia_bluetooth::error::Error as BTError,
    futures::{
        channel::mpsc::{channel, SendError},
        Sink, SinkExt, Stream, StreamExt,
    },
    rustyline::{error::ReadlineError, CompletionType, Config, EditMode, Editor},
    std::thread,
};

use super::{
    commands::{Cmd, CmdHelper},
    do_connect, do_disable_notify, do_enable_notify, do_list, do_read_chr, do_read_desc,
    do_read_long_chr, do_read_long_desc, do_write_chr, do_write_desc, GattClient, GattClientPtr,
};

const PROMPT: &str = "GATT> ";
// Escape codes:
/// Clear the pty line on which the cursor is located.
/// Used when evented output is intermingled with the REPL prompt.
pub static CLEAR_LINE: &str = "\x1b[2K";
/// Move cursor to column 1
pub static CHA: &str = "\x1b[1G";

// Starts the GATT REPL. This first requests a list of remote services and resolves the
// returned future with an error if no services are found.
pub async fn start_gatt_loop<'a>(proxy: ClientProxy) -> Result<(), Error> {
    let client = GattClient::new(proxy);

    println!("  discovering services...");
    let list_services = client.read().proxy.list_services(None);

    let (status, services) = await!(list_services).map_err(|e| {
        let err = BTError::new(&format!("failed to list services: {}", e));
        println!("{}", e);
        err
    })?;

    match status.error {
        None => client.write().set_services(services),
        Some(e) => {
            let err = BTError::from(*e).into();
            println!("failed to list services: {}", err);
            return Err(err);
        }
    }

    let (mut commands, mut acks) = cmd_stream();
    while let Some(cmd) = await!(commands.next()) {
        await!(handle_cmd(cmd, &client)).map_err(|e| {
            println!("Error: {}", e);
            e
        })?;
        await!(acks.send(()))?;
    }

    Ok(())
}

/// Generates a rustyline `Editor` in a separate thread to manage user input. This input is returned
/// as a `Stream` of lines entered by the user.
///
/// The thread exits and the `Stream` is exhausted when an error occurs on stdin or the user
/// sends a ctrl-c or ctrl-d sequence.
///
/// Because rustyline shares control over output to the screen with other parts of the system, a
/// `Sink` is passed to the caller to send acknowledgements that a command has been processed and
/// that rustyline should handle the next line of input.
fn cmd_stream() -> (impl Stream<Item = String>, impl Sink<(), SinkError = SendError>) {
    // Editor thread and command processing thread must be synchronized so that output
    // is printed in the correct order.
    let (mut cmd_sender, cmd_receiver) = channel(512);
    let (ack_sender, mut ack_receiver) = channel(512);

    thread::spawn(move || -> Result<(), Error> {
        let mut exec = fasync::Executor::new().context("error creating readline event loop")?;

        let fut = async {
            let config = Config::builder()
                .auto_add_history(true)
                .history_ignore_space(true)
                .completion_type(CompletionType::List)
                .edit_mode(EditMode::Emacs)
                .build();
            let c = CmdHelper::new();
            let mut rl: Editor<CmdHelper> = Editor::with_config(config);
            rl.set_helper(Some(c));
            loop {
                let readline = rl.readline(PROMPT);
                match readline {
                    Ok(line) => {
                        cmd_sender.try_send(line)?;
                    }
                    Err(ReadlineError::Eof) | Err(ReadlineError::Interrupted) => {
                        return Ok(());
                    }
                    Err(e) => {
                        println!("Error: {:?}", e);
                        return Err(e.into());
                    }
                }
                // wait until processing thread is finished evaluating the last command
                // before running the next loop in the repl
                await!(ack_receiver.next());
            }
        };
        exec.run_singlethreaded(fut)
    });
    (cmd_receiver, ack_sender)
}

// Processes `cmd` and returns its result.
async fn handle_cmd(line: String, client: &GattClientPtr) -> Result<(), Error> {
    let mut components = line.trim().split_whitespace();
    let cmd = components.next().map(|c| c.parse());
    let args: Vec<&str> = components.collect();

    match cmd {
        Some(Ok(Cmd::Exit)) | Some(Ok(Cmd::Quit)) => {
            return Err(BTError::new("exited").into());
        }
        Some(Ok(Cmd::Help)) => {
            print!("{}", Cmd::help_msg());
            Ok(())
        }
        Some(Ok(Cmd::List)) => {
            do_list(&args, client);
            Ok(())
        }
        Some(Ok(Cmd::Connect)) => await!(do_connect(&args, client)),
        Some(Ok(Cmd::ReadChr)) => await!(do_read_chr(&args, client)),
        Some(Ok(Cmd::ReadLongChr)) => await!(do_read_long_chr(&args, client)),
        Some(Ok(Cmd::WriteChr)) => await!(do_write_chr(args, client)),
        Some(Ok(Cmd::ReadDesc)) => await!(do_read_desc(&args, client)),
        Some(Ok(Cmd::ReadLongDesc)) => await!(do_read_long_desc(&args, client)),
        Some(Ok(Cmd::WriteDesc)) => await!(do_write_desc(args, client)),
        Some(Ok(Cmd::EnableNotify)) => await!(do_enable_notify(&args, client)),
        Some(Ok(Cmd::DisableNotify)) => await!(do_disable_notify(&args, client)),
        Some(Err(e)) => {
            eprintln!("Unknown command: {:?}", e);
            Ok(())
        }
        None => Ok(()),
    }
}
