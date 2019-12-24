// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::generator::ActiveCommands,
    crate::io_packet::{InternalCommand, IoPacketType},
    crate::operations::PipelineStages,
    anyhow::Error,
    log::{debug, error, warn},
    std::sync::mpsc::TryRecvError,
    std::{
        process,
        sync::mpsc::{Receiver, Sender},
    },
};

pub struct IssuerArgs {
    /// Human friendly name for this thread.
    name: String,

    /// Unique identifier for each generator.
    issuer_unique_id: u64,

    // Issuer's stage in lifetime of an IO.
    stage: PipelineStages,

    // Channel used to receive commands from generator
    from_generator: Receiver<IoPacketType>,

    // Channel used to send commands to verifier
    to_verifier: Sender<IoPacketType>,

    // Channel used to receive commands from verifier
    from_verifier: Receiver<IoPacketType>,

    // Way for generator and verifier to notify the issuer that there are one or
    // more commands in the queue.
    active_commands: ActiveCommands,
}

impl IssuerArgs {
    pub fn new(
        base_name: String,
        issuer_unique_id: u64,
        from_generator: Receiver<IoPacketType>,
        to_verifier: Sender<IoPacketType>,
        from_verifier: Receiver<IoPacketType>,
        active_commands: ActiveCommands,
    ) -> IssuerArgs {
        IssuerArgs {
            name: format!("{}-{}", base_name, issuer_unique_id),
            issuer_unique_id,
            from_generator,
            to_verifier,
            from_verifier,
            stage: PipelineStages::Issue,
            active_commands,
        }
    }
}

pub fn run_issuer(args: IssuerArgs) -> Result<(), Error> {
    let mut next_cmd = {
        let mut active_commands = args.active_commands;
        let from_verifier = args.from_verifier;
        let from_generator = args.from_generator;

        move || {
            // May block
            active_commands.decrement();

            // We are here because we decremented active_commands. This implies there is at least one
            // command in the queues. We don't know in which queue yet.
            // We give priority to io packets from verifier. We look for command on
            // generator channel only when we find verifier channel empty.
            match from_verifier.try_recv() {
                Ok(cmd) => (cmd, true),
                Err(TryRecvError::Empty) => match from_generator.try_recv() {
                    Ok(cmd) => (cmd, false),
                    Err(TryRecvError::Empty) => panic!(
                        "Both verifier and generator queues are empty, yet the active_commands \
                         count was not."
                    ),
                    Err(TryRecvError::Disconnected) => panic!("Generator has closed it's channel."),
                },
                Err(TryRecvError::Disconnected) => panic!("Verifier has closed it's channel."),
            }
        }
    };

    // Even on happy path, either generator or verifier can be the first to
    // close the channel. These two variables keep track of whether the channel
    // was closed or not.
    let mut scan_generator = true;
    let mut scan_verifier = true;

    // This thread/loop is not done till we hear explicitly from generator and
    // from verifier that they both are done. We keep track of who is done.
    while scan_generator || scan_verifier {
        let (mut cmd, verifying_cmd) = next_cmd();

        debug!(
            "from issuer: {} id: {} io_seq: {} op: {:?} verifying_cmd: {}",
            args.name,
            args.issuer_unique_id,
            cmd.sequence_number(),
            cmd.operation_type(),
            verifying_cmd,
        );

        cmd.timestamp_stage_start(args.stage);
        cmd.do_io();
        if !cmd.is_complete() {
            error!("Asynchronous commands not implemented yet.");
            process::abort();
        }

        // Mark done timestamps.
        cmd.timestamp_stage_end(args.stage);

        // Cloning the command
        let internal_command = cmd.abort_or_exit();

        // Check if this was an internal command and if so take appropriate
        // action.
        match internal_command {
            InternalCommand::Exit => {
                if verifying_cmd {
                    scan_verifier = false;
                    // if this internal command is coming from verifier,
                    // skip sending it to verifier.
                    continue;
                } else {
                    scan_generator = false;
                }
                debug!("{} - clean exit", args.name);
            }
            InternalCommand::Abort => {
                warn!("{} - aborted", args.name);
                break;
            }
            InternalCommand::None => {}
        }

        if args.to_verifier.send(cmd).is_err() {
            error!("error sending command from issuer");
            process::abort();
        }
    }

    Ok(())
}
