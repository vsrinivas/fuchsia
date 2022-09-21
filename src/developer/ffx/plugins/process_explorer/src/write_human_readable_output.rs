// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//!  Utilities that prints information in a human-readable format.

use {
    crate::processes_data, anyhow::Result, ffx_writer::Writer, fuchsia_zircon_types as zx_types,
    processes_data::processed, std::io::Write,
};

/// Print to 'w' a human-readable presentation of `processes_data`.
pub fn pretty_print_processes_data(
    mut w: Writer,
    processes_data: processed::ProcessesData,
) -> Result<()> {
    writeln!(w, "Total processes found:    {}", processes_data.processes_count)?;
    writeln!(w)?;
    for process in processes_data.processes {
        writeln!(w, "Process name:             {}", process.name)?;
        writeln!(w, "Process koid:             {}", process.koid)?;
        writeln!(w, "Total objects:            {}", process.objects_count)?;
        for objects_by_type in process.objects {
            writeln!(
                w,
                "   {}: {}",
                get_object_type_name(objects_by_type.objects_type),
                objects_by_type.objects_count
            )?;
            for object in objects_by_type.objects {
                writeln!(
                    w,
                    "         Koid: {:6}    Related Koid: {:6}    Peer Owner Koid: {:6}",
                    object.koid, object.related_koid, object.peer_owner_koid
                )?;
            }
        }
        writeln!(w, "===========================================================================")?;
    }
    writeln!(w)?;
    Ok(())
}

/// Convert objects type from zx_obj_type_t to a string
/// to make the information more readable.
fn get_object_type_name(object_type: zx_types::zx_obj_type_t) -> String {
    match object_type {
        zx_types::ZX_OBJ_TYPE_NONE => "None",
        zx_types::ZX_OBJ_TYPE_PROCESS => "Processes",
        zx_types::ZX_OBJ_TYPE_THREAD => "Threads",
        zx_types::ZX_OBJ_TYPE_VMO => "VMOs",
        zx_types::ZX_OBJ_TYPE_CHANNEL => "Channels",
        zx_types::ZX_OBJ_TYPE_EVENT => "Events",
        zx_types::ZX_OBJ_TYPE_PORT => "Ports",
        zx_types::ZX_OBJ_TYPE_INTERRUPT => "Interrupts",
        zx_types::ZX_OBJ_TYPE_PCI_DEVICE => "PCI Devices",
        zx_types::ZX_OBJ_TYPE_DEBUGLOG => "Debuglogs",
        zx_types::ZX_OBJ_TYPE_SOCKET => "Sockets",
        zx_types::ZX_OBJ_TYPE_RESOURCE => "Resources",
        zx_types::ZX_OBJ_TYPE_EVENTPAIR => "Event pairs",
        zx_types::ZX_OBJ_TYPE_JOB => "Jobs",
        zx_types::ZX_OBJ_TYPE_VMAR => "VMARs",
        zx_types::ZX_OBJ_TYPE_FIFO => "FIFOs",
        zx_types::ZX_OBJ_TYPE_GUEST => "Guests",
        zx_types::ZX_OBJ_TYPE_VCPU => "VCPUs",
        zx_types::ZX_OBJ_TYPE_TIMER => "Timers",
        zx_types::ZX_OBJ_TYPE_IOMMU => "IOMMUs",
        zx_types::ZX_OBJ_TYPE_BTI => "BTIs",
        zx_types::ZX_OBJ_TYPE_PROFILE => "Profiles",
        zx_types::ZX_OBJ_TYPE_PMT => "PMTs",
        zx_types::ZX_OBJ_TYPE_SUSPEND_TOKEN => "Suspend tokens",
        zx_types::ZX_OBJ_TYPE_PAGER => "Pagers",
        zx_types::ZX_OBJ_TYPE_EXCEPTION => "Exceptions",
        zx_types::ZX_OBJ_TYPE_CLOCK => "Clocks",
        zx_types::ZX_OBJ_TYPE_STREAM => "Streams",
        _ => "Error",
    }
    .to_string()
}

/// Print to 'w' the name and koid of all processes.
pub fn pretty_print_processes_name_and_koid(
    mut w: Writer,
    processes_data: processed::ProcessesData,
) -> Result<()> {
    writeln!(w, "Total processes found:    {}", processes_data.processes_count)?;
    writeln!(w, "Koid:    Name:")?;
    for process in processes_data.processes {
        writeln!(w, "{:6}   {}", process.koid, process.name)?;
    }
    Ok(())
}

/// Print to 'w' koids that do not correspond to any existing processes
pub fn pretty_print_invalid_koids(
    mut w: Writer,
    invalid_koids: Vec<zx_types::zx_koid_t>,
) -> Result<()> {
    writeln!(w, "The following koids are not valid:")?;
    writeln!(w)?;
    for koid in invalid_koids {
        writeln!(w, "    {}", koid)?;
    }
    writeln!(w)?;
    writeln!(w, "Please input koids that correspond to existing processes.")?;
    writeln!(w, "Use the 'list' subcommand to view a list of all processes and their koid.")?;
    Ok(())
}
