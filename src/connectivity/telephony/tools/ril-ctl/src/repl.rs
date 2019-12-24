// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::{CmdHelper, ReplControl},
        // TODO finish repling!
        PROMPT,
    },
    anyhow::{Context as _, Error},
    fidl_fuchsia_telephony_ril::RadioInterfaceLayerProxy,
    fuchsia_async as fasync,
    futures::{
        channel::mpsc::{channel, SendError},
        Sink, SinkExt, Stream, StreamExt,
    },
    parking_lot::Mutex,
    rustyline::{error::ReadlineError, CompletionType, Config, EditMode, Editor},
    std::{sync::Arc, thread},
};

pub async fn run<'a>(
    ril_modem: &'a RadioInterfaceLayerProxy,
    state: Arc<Mutex<crate::Connections>>,
) -> Result<(), Error> {
    // `cmd_stream` blocks on input in a separate thread and passes commands and acks back to
    // the main thread via async channels.
    let (mut commands, mut acks) = cmd_stream();
    loop {
        if let Some(cmd) = commands.next().await {
            match crate::handle_cmd(&ril_modem, cmd, state.clone()).await {
                Ok(ReplControl::Continue) => {}
                Ok(ReplControl::Break) => {
                    break;
                }
                Err(e) => {
                    println!("Error handling command: {}", e);
                }
            }
        } else {
            break;
        }
        acks.send(()).await?;
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
fn cmd_stream() -> (impl Stream<Item = String>, impl Sink<(), Error = SendError>) {
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
                ack_receiver.next().await;
            }
        };
        exec.run_singlethreaded(fut)
    });
    (cmd_receiver, ack_sender)
}
