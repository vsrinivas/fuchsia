// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        filter::filter_data_to_lines,
        screen::{Line, Screen},
        terminal::{Terminal, Termion},
    },
    anyhow::{Context, Result},
    diagnostics_data::{Inspect, InspectData},
    ffx_core::ffx_plugin,
    ffx_inspect_apply_selectors_args::ApplySelectorsCommand,
    ffx_inspect_common::DiagnosticsBridgeProvider,
    fidl_fuchsia_developer_remotecontrol::{RemoteControlProxy, RemoteDiagnosticsBridgeProxy},
    iquery::commands::DiagnosticsProvider,
    std::{
        fs::read_to_string,
        io::{stdin, stdout},
        path::Path,
    },
    termion::{
        event::{Event, Key},
        input::TermRead,
        raw::IntoRawMode,
    },
};

mod filter;
mod screen;
mod terminal;

#[cfg(test)]
mod test_utils;

#[ffx_plugin(
    RemoteDiagnosticsBridgeProxy = "core/remote-diagnostics-bridge:expose:fuchsia.developer.remotecontrol.RemoteDiagnosticsBridge"
)]
pub async fn apply_selectors(
    rcs_proxy: Result<RemoteControlProxy>,
    diagnostics_proxy: Result<RemoteDiagnosticsBridgeProxy>,
    cmd: ApplySelectorsCommand,
) -> Result<()> {
    // Get full inspect data
    // If a snapshot file (inspect.json) is provided we use it to get inspect data,
    // else we use DiagnosticsProvider to get snapshot data.
    let inspect_data = if let Some(snapshot_file) = &cmd.snapshot_file {
        serde_json::from_str(
            &read_to_string(snapshot_file)
                .context(format!("Unable to read {}.", snapshot_file.display()))?,
        )
        .context(format!("Unable to deserialize {}.", snapshot_file.display()))?
    } else {
        let provider = DiagnosticsBridgeProvider::new(diagnostics_proxy?, rcs_proxy?);
        provider.snapshot::<Inspect>(&cmd.accessor_path, &[]).await?
    };

    interactive_apply(&cmd.selector_file, &inspect_data, &cmd.moniker)?;

    Ok(())
}

fn interactive_apply(
    selector_file: &Path,
    data: &[InspectData],
    requested_moniker: &Option<String>,
) -> Result<()> {
    let stdin = stdin();
    let stdout = stdout().into_raw_mode().context("Unable to convert terminal to raw mode.")?;

    let mut screen = Screen::new(
        Termion::new(stdout),
        filter_data_to_lines(selector_file, data, requested_moniker)?,
    );

    screen.terminal.switch_interactive();
    screen.refresh_screen_and_flush();

    for c in stdin.events() {
        let evt = c.unwrap();
        let should_update = match evt {
            Event::Key(Key::Char('q') | Key::Ctrl('c') | Key::Ctrl('d')) => break,
            Event::Key(Key::Char('h')) => {
                screen.set_filter_removed(!screen.filter_removed);
                screen.clear_screen();
                true
            }
            Event::Key(Key::Char('r')) => {
                screen.set_lines(vec![Line::new("Refressing filtered hierarchiers...")]);
                screen.clear_screen();
                screen.refresh_screen_and_flush();
                screen.set_lines(filter_data_to_lines(selector_file, data, requested_moniker)?);
                true
            }
            Event::Key(Key::PageUp) => screen.scroll(-screen.max_lines(), 0),
            Event::Key(Key::PageDown) => screen.scroll(screen.max_lines(), 0),
            Event::Key(Key::Up) => screen.scroll(-1, 0),
            Event::Key(Key::Down) => screen.scroll(1, 0),
            Event::Key(Key::Left) => screen.scroll(0, -1),
            Event::Key(Key::Right) => screen.scroll(0, 1),
            _e => false,
        };
        if should_update {
            screen.refresh_screen_and_flush();
        }
    }

    screen.terminal.switch_normal();
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_utils::FakeTerminal;
    use ffx_inspect_test_utils::get_v1_json_dump;
    use std::io::Write;
    use tempfile::NamedTempFile;

    #[fuchsia::test]
    fn hide_filtered_selectors() {
        let selectors = "realm1/realm2/sessio5/account_manager.cmx:root/accounts:total
realm1/realm2/session5/account_manager.cmx:root/listeners:total_opened
realm1/realm2/session5/account_manager.cmx:root/listeners:active";

        let mut selector_file =
            NamedTempFile::new().expect("Unable to create tempfile for testing.");
        selector_file
            .write_all(selectors.as_bytes())
            .expect("Unable to write selectors to tempfile.");

        let data: Vec<InspectData> =
            serde_json::from_value(get_v1_json_dump()).expect("Unable to parse Inspect Data.");

        let fake_terminal = FakeTerminal::new(90, 30);
        let mut screen = Screen::new(
            fake_terminal.clone(),
            filter_data_to_lines(&selector_file.path(), &data, &None)
                .expect("Unable to filter hierarchy."),
        );

        screen.refresh_screen_and_flush();

        screen.set_filter_removed(true);
        screen.clear_screen();
        screen.refresh_screen_and_flush();

        // The output should not contain these selectors
        // realm1/realm2/session5/account_manager.cmx:root/accounts:active
        // realm1/realm2/session5/account_manager.cmx:root/auth_providers:types
        // realm1/realm2/session5/account_manager.cmx:root/listeners:events

        let screen_output = fake_terminal.screen_without_help_footer();

        assert_eq!(
            screen_output.contains("\"active\": 0"),
            false,
            "{} contains: '\"active\": 0' but it was expected to be filtered.",
            &screen_output
        );

        assert_eq!(
            screen_output.contains("auth_providers"),
            false,
            "{} contains: 'auth_providers' but it was expected to be filtered.",
            &screen_output
        );

        assert_eq!(
            screen_output.contains("\"events\": 0"),
            false,
            "{} contains: '\"events\": 0' but it was expected to be filtered.",
            &screen_output
        );
    }
}
