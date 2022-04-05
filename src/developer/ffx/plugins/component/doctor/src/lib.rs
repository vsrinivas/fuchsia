// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ansi_term::Colour,
    anyhow::{Context, Error, Result},
    component_hub::doctor::DoctorComponent,
    component_hub::io::Directory,
    errors::ffx_bail,
    ffx_component_doctor_args::DoctorCommand,
    ffx_core::ffx_plugin,
    ffx_writer::Writer,
    fidl::Status,
    fidl_fuchsia_developer_remotecontrol as rc,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, MonikerError},
    std::io,
    std::io::Write,
    thiserror::Error,
};

#[derive(Error, Debug)]
enum DoctorError {
    #[error("got unexpected error while running diagnostics: {}", error)]
    Unexpected { error: Error },

    #[error("could not parse the moniker: {}", error)]
    InvalidMoniker { error: MonikerError },

    #[error("one or more diagnostic checks failed")]
    DiagnosticFailed,
}

impl DoctorError {
    fn from_io_error(error: std::io::Error) -> DoctorError {
        return DoctorError::Unexpected { error: Error::from(error) };
    }
    fn from_error(error: Error) -> DoctorError {
        return DoctorError::Unexpected { error };
    }

    fn from_moniker_error(error: MonikerError) -> DoctorError {
        return DoctorError::InvalidMoniker { error };
    }
}

/// Read the monikers from stdin
fn read_monikers() -> Result<Vec<String>> {
    let mut res = vec![];
    let stdin = io::stdin();

    loop {
        let mut input = String::new();
        stdin.read_line(&mut input)?;

        // remove the newline character
        input.pop();

        if input.is_empty() {
            break;
        }
        res.push(input);
    }

    Ok(res)
}

/// Perform a series of diagnostic checks on a component at runtime.
/// The checks include:
///     * Validation that the lists of `outgoing` and `exposed` capabilities match
#[ffx_plugin("component.experimental")]
pub async fn doctor(
    remote_proxy: rc::RemoteControlProxy,
    #[ffx(machine = Vec<Component>)] mut writer: Writer,
    cmd: DoctorCommand,
) -> Result<()> {
    let (root, dir_server) = fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>()
        .context("creating hub root proxy")?;
    remote_proxy
        .open_hub(dir_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening hub")?;
    let hub_dir = Directory::from_proxy(root);

    let monikers = if !cmd.moniker.is_empty() { cmd.moniker } else { read_monikers()? };

    let mut failed_monikers: Vec<String> = vec![];
    for moniker in monikers {
        match doctor_impl(&moniker, &hub_dir, &mut writer).await {
            Ok(()) => {
                writeln!(
                    writer,
                    "{} All checks passed for component {}",
                    Colour::Green.paint("Success!"),
                    moniker
                )?;
            }
            Err(e) => {
                let code = match e {
                    DoctorError::Unexpected { .. } => "Error",
                    DoctorError::InvalidMoniker { .. } => "Invalid Moniker",
                    DoctorError::DiagnosticFailed { .. } => "Failed",
                };
                writeln!(writer, "{}: {}", Colour::Red.paint(code), e)?;
                failed_monikers.push(moniker.clone());
            }
        };
    }

    if failed_monikers.is_empty() {
        writeln!(
            writer,
            "\n{} All matching components have passed the diagnostic checks.",
            Colour::Green.paint("Success!"),
        )?;
        Ok(())
    } else {
        ffx_bail!(
            "\nOne or more diagnostic checks failed for the following components: {}",
            failed_monikers.join(", ")
        );
    }
}

async fn doctor_impl(
    moniker: &String,
    hub_dir: &Directory,
    writer: &mut Writer,
) -> Result<(), DoctorError> {
    writeln!(writer, "\nRunning diagnostics for `{}`... ", moniker)
        .map_err(DoctorError::from_io_error)?;

    let moniker =
        AbsoluteMoniker::parse_str(moniker.as_str()).map_err(DoctorError::from_moniker_error)?;

    let mut doctor =
        DoctorComponent::from_moniker(moniker, hub_dir).await.map_err(DoctorError::from_error)?;

    writer.line(doctor.check_exposed_outgoing_capabilities()).map_err(DoctorError::from_error)?;

    if doctor.failed {
        Err(DoctorError::DiagnosticFailed)
    } else {
        Ok(())
    }
}
