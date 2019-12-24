// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of verifier thread.
//!
//! Generator makes verifiers job easy to check completion status. At the least
//! verifier checks if the IO commands returned successfully or not. In addition
//! to that it can perform IO content verification if requested/applicable.
//! IO packet has all the information whether
//! - we need verify a packet or not
//! - it needs extra IO
//! - how to verify

use {
    crate::generator::ActiveCommands,
    crate::io_packet::{InternalCommand, IoPacketType},
    crate::log::Stats,
    crate::operations::PipelineStages,
    anyhow::Error,
    log::{debug, error, warn},
    std::{
        collections::HashMap,
        process,
        sync::{
            mpsc::{Receiver, Sender},
            Arc, Mutex,
        },
    },
};

pub struct VerifierArgs {
    /// Human friendly name for this thread.
    name: String,

    /// Unique identifier for each verifier.
    verifier_unique_id: u64,

    // Verifier's stage in lifetime of an IO.
    stage: PipelineStages,

    // Channel used to receive commands from issuer
    from_issuer: Receiver<IoPacketType>,

    // Channel used to send verify commands to generator
    to_issuer: Sender<IoPacketType>,

    // True if verify should do anything more than checking completion status.
    verify: bool,

    // Hashmap shared between verifier and generator. We remove io packet entries
    // from the map once we are done with the packet.
    io_map: Arc<Mutex<HashMap<u64, IoPacketType>>>,

    // Stats to store data about time spent actively working on a packet.
    stats: Arc<Mutex<Stats>>,

    // Way to notify issuer that there is at least one verify packet on the other
    // end of to_issuer channel.
    active_commands: ActiveCommands,
}

// Current implementation of verifier checks only IO command return value.
impl VerifierArgs {
    pub fn new(
        base_name: String,
        verifier_unique_id: u64,
        from_issuer: Receiver<IoPacketType>,
        to_issuer: Sender<IoPacketType>,
        verify: bool,
        io_map: Arc<Mutex<HashMap<u64, IoPacketType>>>,
        stats: Arc<Mutex<Stats>>,
        active_commands: ActiveCommands,
    ) -> VerifierArgs {
        VerifierArgs {
            name: format!("{}-{}", base_name, verifier_unique_id),
            verifier_unique_id,
            from_issuer,
            to_issuer,
            stage: PipelineStages::Verify,
            verify,
            io_map,
            stats,
            active_commands,
        }
    }
}

pub fn run_verifier(mut args: VerifierArgs) -> Result<(), Error> {
    for mut cmd in args.from_issuer {
        debug!(
            "from verifier: {} id: {} io_seq: {} op: {:?}",
            args.name,
            args.verifier_unique_id,
            cmd.sequence_number(),
            cmd.operation_type(),
        );
        for stage in PipelineStages::iterator() {
            let r = cmd.io_offset_range();
            let (start, end) = cmd.interval_to_u64(*stage);
            debug!(
                "  completed cmd {:?}->({}, {}) {} {}..{}",
                stage,
                start,
                end,
                cmd.stage_timestamps()[stage.stage_number()].duration(),
                r.start,
                r.end
            );
        }

        cmd.timestamp_stage_start(args.stage);
        let _x = cmd.get_error()?;

        if cmd.verify_needs_io() {
            error!("VerifyIo not implemented");
            process::abort();
        }

        // This is not implemented but it shows how ActiveCommands will be used
        // when implemented.
        if args.verify {
            if args.to_issuer.send(cmd).is_err() {
                error!("error sending command from issuer");
            }
            args.active_commands.increment();
            error!("VerifyIo not implemented");
            process::abort();
        }
        cmd.timestamp_stage_end(args.stage);

        // Remove cmd from the hash map since we are done with verification
        {
            let mut map = args.io_map.lock().unwrap();
            map.remove(&cmd.sequence_number());
            debug!("removed cmd {}", cmd.sequence_number());
        }

        // Update stats
        {
            let mut stats = args.stats.lock().unwrap();
            stats.log(cmd.operation_type(), cmd.io_size(), cmd.stage_timestamps());
        }

        // For graceful exit or abort
        match cmd.abort_or_exit() {
            InternalCommand::Exit => {
                args.to_issuer.send(cmd).unwrap();
                args.active_commands.increment();
                debug!("{} - clean exit", args.name);
                break;
            }
            InternalCommand::Abort => {
                warn!("{} - aborted", args.name);
                break;
            }
            InternalCommand::None => {}
        }
    }

    Ok(())
}
