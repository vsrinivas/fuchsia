// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    fidl_fuchsia_virtualization::{
        ContainerStatus, LinuxGuestInfo, LinuxManagerEvent, LinuxManagerMarker,
    },
    fuchsia_async::{Duration, Interval},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
    futures::{future::ready, select, stream, StreamExt},
    std::io::Write,
    tracing,
};

// ANSI Escape sequences for terminal manipulation.
// see: https://en.wikipedia.org/wiki/ANSI_escape_code#CSI_(Control_Sequence_Introducer)_sequences
macro_rules! csi {
    ( $( $cmd:expr ),* ) => {
        concat!("\x1b[", $($cmd),*)
    }
}

const CURSOR_HIDE: &str = csi!("?", "25", "l");
const CURSOR_SHOW: &str = csi!("?", "25", "h");
const COLOUR0_NORMAL: &str = csi!("0", "m");
const COLOUR1_RED_BRIGHT: &str = csi!("1;31", "m");
const COLOUR2_GREEN_BRIGHT: &str = csi!("1;32", "m");
const COLOUR3_YELLOW: &str = csi!("33", "m");
const COLOUR5_MAGENTA: &str = csi!("35", "m");
const ERASE_REST_OF_LINE: &str = csi!("K");

fn move_forward(cells: usize) -> String {
    format!(csi!("{}", "C"), cells)
}

// This function maps ContainerStatus to arbitrary progress markers from 1 - 10
const fn get_container_status_progress(status: ContainerStatus) -> usize {
    match status {
        ContainerStatus::Transient | ContainerStatus::LaunchingGuest => 1,
        ContainerStatus::StartingVm => 2,
        ContainerStatus::Downloading => 4,
        ContainerStatus::Extracting => 6,
        ContainerStatus::Starting => 9,
        ContainerStatus::Failed | ContainerStatus::Ready => 10,
    }
}

const MAX_CONTAINER_STATUS_PROGRESS: usize = get_container_status_progress(ContainerStatus::Ready);

// This function defines the message to print for each stage of startup.
fn get_container_status_string(info: &LinuxGuestInfo) -> String {
    match info.container_status.expect("LinuxGuestInfo should contain a container_status") {
        ContainerStatus::Transient => String::new(),
        ContainerStatus::LaunchingGuest => "Initializing".to_string(),
        ContainerStatus::StartingVm => "Starting the virtual machine".to_string(),
        ContainerStatus::Downloading => {
            format!(
                "Downloading the Linux container image ({}%)",
                info.download_percent.expect("LinuxGuestInfo should contain a download_percent")
            )
        }
        ContainerStatus::Extracting => "Extracting the Linux container image".to_string(),
        ContainerStatus::Starting => "Starting the Linux container".to_string(),
        ContainerStatus::Ready => "Ready".to_string(),
        ContainerStatus::Failed => format!("Error starting guest: {:?}", info.failure_reason),
    }
}

// Print initial progress bar and hide the cursor.
fn print_progress_bar(w: &mut impl Write) -> Result<()> {
    let padding_width = MAX_CONTAINER_STATUS_PROGRESS;
    write!(w, "{CURSOR_HIDE}{COLOUR5_MAGENTA}[{:padding_width$}]", "")?;
    Ok(())
}

// Print the ContainerStatus string to the right of the rendered progress bar. The offset of the
// new end of line is returned so that sub-messages can be printed.
fn print_stage(
    w: &mut impl Write,
    colour: &str,
    status: ContainerStatus,
    output: &str,
) -> Result<usize> {
    let status_progress = get_container_status_progress(status);
    let progress_bar: String = "=".chars().cycle().take(status_progress).collect();
    let forward = move_forward(3 + (MAX_CONTAINER_STATUS_PROGRESS - status_progress));
    write!(w, "\r{COLOUR5_MAGENTA}[{progress_bar}{forward}{ERASE_REST_OF_LINE}{colour}{output}")?;

    // Return the offset of the end of line position
    Ok(4 + MAX_CONTAINER_STATUS_PROGRESS + output.len())
}

// Prints a message to the right of the progress bar and the last "stage" message printed.
fn print_after_stage(
    w: &mut impl Write,
    end_of_line: usize,
    colour: &str,
    output: &str,
) -> Result<()> {
    let forward = move_forward(end_of_line);
    write!(w, "\r{forward}{colour}: {output}")?;
    Ok(())
}

/// Launches a Termina VM. An ascii progress bar and the current status info are rendered to `w`
/// until the launch sequence terminates.
pub async fn launch(w: &mut impl Write) -> Result<()> {
    const TERMINA_ENVIRONMENT_NAME: &str = "termina";

    let linux_manager =
        connect_to_protocol::<LinuxManagerMarker>().context("Failed to connect to LinuxManager")?;

    let linux_guest_info = linux_manager
        .start_and_get_linux_guest_info(TERMINA_ENVIRONMENT_NAME)
        .await?
        .map_err(zx::Status::from_raw)?;
    let mut info = linux_guest_info.clone();

    // Add the first event to the same stream as the subsequent ones to simplify handling
    let mut events = stream::once(ready(Ok(LinuxManagerEvent::OnGuestInfoChanged {
        label: TERMINA_ENVIRONMENT_NAME.to_string(),
        info: linux_guest_info,
    })))
    .chain(linux_manager.take_event_stream());

    print_progress_bar(w)?;

    let mut spinner = "|/-\\".chars().cycle();
    let mut end_of_line = 0;
    let mut interval = Interval::new(Duration::from_millis(100));
    let final_info = loop {
        info = select! {
            () = interval.select_next_some() => {
                let progress = get_container_status_progress(
                    info.container_status
                        .expect("LinuxGuestInfo should contain a container_status"),
                );
                write!(w,
                    "\r{}{}{}",
                    move_forward(progress),
                    COLOUR5_MAGENTA,
                    spinner.next().expect("Infinite iterator should not terminate")
                )?;
                info
            }
            maybe_event = events.next() => {
                let event = maybe_event
                    .ok_or(anyhow!("LinuxManagerEvent stream unexpectedly terminated"))?
                    .context("LinuxManagerEvent stream encountered a fidl error")?;
                let LinuxManagerEvent::OnGuestInfoChanged { label, info } = event;
                if &label != TERMINA_ENVIRONMENT_NAME {
                    continue;
                }

                tracing::debug!("LinuxManagerEvent: {:?}", info);

                let container_status = if let Some(status) = info.container_status {
                    status
                } else {
                    break info;
                };

                let stage_text = get_container_status_string(&info);
                end_of_line = match container_status {
                    ContainerStatus::Failed => {
                        print_after_stage(w, end_of_line, COLOUR1_RED_BRIGHT, &stage_text)?;
                        write!(w, "\r\n{ERASE_REST_OF_LINE}{COLOUR0_NORMAL}{CURSOR_SHOW}")?;
                        break info;
                    }
                    ContainerStatus::Ready => {
                        print_stage(w, COLOUR2_GREEN_BRIGHT, container_status, &stage_text)?;
                        write!(w, "\r\n{ERASE_REST_OF_LINE}{COLOUR0_NORMAL}{CURSOR_SHOW}")?;
                        break info;
                    }
                    _ => print_stage(w, COLOUR3_YELLOW, container_status, &stage_text)?,
                };

                info
            }
        };

        // Don't necessarily care too much about being unable to flush.
        w.flush().ok();
    };

    w.flush().ok();

    match final_info.container_status {
        Some(ContainerStatus::Ready) => {}
        Some(ContainerStatus::Failed) => anyhow::bail!("Container failed to start"),
        None => anyhow::bail!("container_status unexpectedly missing!"),
        _ => unreachable!(),
    };

    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use std::default::Default;
    use std::iter::repeat;
    use term_model::{
        ansi::{Color, NamedColor, Processor, TermInfo},
        clipboard::Clipboard,
        config::Config,
        event::{Event, EventListener},
        index::{Column, Line, Point},
        term::{
            cell::{Cell, Flags},
            mode::TermMode,
            SizeInfo,
        },
        Term,
    };

    struct TestListener;
    impl EventListener for TestListener {
        fn send_event(&self, _: Event) {
            // Don't yet care about events.
        }
    }

    fn make_term(lines: usize, cols: usize) -> Term<TestListener> {
        let cell_width = 8.0;
        let cell_height = 16.0;
        let config = Config::<()>::default();
        let size_info = SizeInfo {
            width: cell_width * cols as f32,
            height: cell_height * lines as f32,
            cell_width,
            cell_height,
            padding_x: 0.0,
            padding_y: 0.0,
            dpr: 1.0,
        };
        Term::new(&config, &size_info, Clipboard::new(), TestListener {})
    }

    // Get the terminal's currently displayed cells as a Vec<Vec<Cell>> with dimensions matching
    // that of the terminal.
    fn get_cells(term: &Term<TestListener>) -> Vec<Vec<Cell>> {
        let mut current_line = Line(0);
        let mut cells = vec![vec![]];
        for c in term.grid().display_iter() {
            if c.line != current_line {
                current_line = c.line;
                cells.push(vec![]);
            }
            cells.last_mut().unwrap().push(c.inner);
        }
        cells
    }

    // Get the text content of the terminal as a Vec of `num_lines` Strings of length `num_cols`.
    // In other words this is just `get_cells` with each line of cells collected into a string of
    // their characters.
    fn get_text(term: &Term<TestListener>) -> Vec<String> {
        get_cells(term).iter().map(|line| line.into_iter().map(|cell| cell.c).collect()).collect()
    }

    #[test]
    fn test_move_forward() {
        let mut term = make_term(24, 80);
        let mut processor = Processor::new();
        let mut writer: Vec<u8> = vec![];

        for c in move_forward(40).bytes() {
            processor.advance(&mut term, c, &mut writer);
        }
        assert_eq!(term.cursor().point, Point::new(Line(0), Column(40)));
        assert_eq!(
            get_cells(&term).concat(),
            repeat(Cell::default()).take(term.lines().0 * term.cols().0).collect::<Vec<Cell>>()
        );
        assert_eq!(writer, Vec::<u8>::new());
    }

    #[test]
    fn test_erase_in_line() {
        let mut term = make_term(1, 20);
        let mut processor = Processor::new();
        let mut writer: Vec<u8> = vec![];

        for c in "0123456789".bytes() {
            processor.advance(&mut term, c, &mut writer);
        }
        assert_eq!(get_text(&term), vec!["0123456789          "]);
        assert_eq!(writer, Vec::<u8>::new());

        for c in format!("\r{}{}", move_forward(5), ERASE_REST_OF_LINE).bytes() {
            processor.advance(&mut term, c, &mut writer);
        }
        assert_eq!(get_text(&term), vec!["01234               "]);
        assert_eq!(writer, Vec::<u8>::new());
    }

    #[test]
    fn test_cursor_hide_and_show() {
        let mut term = make_term(1, 20);
        let mut processor = Processor::new();
        let mut writer: Vec<u8> = vec![];

        let original_mode = *term.mode();
        assert!(original_mode.contains(TermMode::SHOW_CURSOR));

        for c in CURSOR_HIDE.bytes() {
            processor.advance(&mut term, c, &mut writer);
        }
        assert_eq!(*term.mode(), original_mode - TermMode::SHOW_CURSOR);

        for c in CURSOR_SHOW.bytes() {
            processor.advance(&mut term, c, &mut writer);
        }
        assert_eq!(*term.mode(), original_mode | TermMode::SHOW_CURSOR);

        assert_eq!(
            get_cells(&term).concat(),
            repeat(Cell::default()).take(term.lines().0 * term.cols().0).collect::<Vec<Cell>>()
        );
        assert_eq!(writer, Vec::<u8>::new());
    }

    #[test]
    fn test_colours() {
        let mut term = make_term(1, 12);
        let mut processor = Processor::new();
        let mut writer: Vec<u8> = vec![];

        for c in format!(
            "0{}12{}345{}6{}7{}8{}9 ",
            COLOUR3_YELLOW,
            COLOUR1_RED_BRIGHT,
            COLOUR0_NORMAL,
            COLOUR5_MAGENTA,
            COLOUR2_GREEN_BRIGHT,
            COLOUR5_MAGENTA,
        )
        .bytes()
        {
            processor.advance(&mut term, c, &mut writer);
        }

        assert_eq!(
            get_cells(&term),
            vec![vec![
                Cell { c: '0', ..Cell::default() },
                Cell { c: '1', fg: Color::Named(NamedColor::Yellow), ..Cell::default() },
                Cell { c: '2', fg: Color::Named(NamedColor::Yellow), ..Cell::default() },
                // One might notice that despite being named `*_BRIGHT`, the observed effect is the
                // specified colour with the BOLD display attribute set instead of the
                // NamedColor::Bright* variant. In most terminals bold was implemented as a
                // modifier on colour intensity rather than what the name suggests, and also the
                // Bright* series of colours were a later non-standard addition.
                Cell {
                    c: '3',
                    fg: Color::Named(NamedColor::Red),
                    flags: Flags::BOLD,
                    ..Cell::default()
                },
                Cell {
                    c: '4',
                    fg: Color::Named(NamedColor::Red),
                    flags: Flags::BOLD,
                    ..Cell::default()
                },
                Cell {
                    c: '5',
                    fg: Color::Named(NamedColor::Red),
                    flags: Flags::BOLD,
                    ..Cell::default()
                },
                Cell { c: '6', ..Cell::default() },
                Cell { c: '7', fg: Color::Named(NamedColor::Magenta), ..Cell::default() },
                Cell {
                    c: '8',
                    fg: Color::Named(NamedColor::Green),
                    flags: Flags::BOLD,
                    ..Cell::default()
                },
                // Notice that the previous bold state leaks through when there isn't a reset, even
                // though we haven't specified a "bright" magenta with the input control sequence.
                Cell {
                    c: '9',
                    fg: Color::Named(NamedColor::Magenta),
                    flags: Flags::BOLD,
                    ..Cell::default()
                },
                Cell {
                    c: ' ',
                    fg: Color::Named(NamedColor::Magenta),
                    flags: Flags::BOLD,
                    ..Cell::default()
                },
                Cell { c: ' ', ..Cell::default() },
            ]]
        );
        assert_eq!(writer, Vec::<u8>::new());
    }

    #[test]
    fn test_progress_bar() {
        let mut term = make_term(5, 20);
        let mut processor = Processor::new();
        let mut writer: Vec<u8> = vec![];
        let mut input = vec![];
        print_progress_bar(&mut input).unwrap();
        for c in input {
            processor.advance(&mut term, c, &mut writer);
        }

        assert!(!term.mode().contains(TermMode::SHOW_CURSOR));
        assert_eq!(
            get_text(&term),
            vec![
                "[          ]        ",
                "                    ",
                "                    ",
                "                    ",
                "                    ",
            ]
        );

        let mut progress_part = get_cells(&term).concat();
        let unformatted = progress_part.split_off(12);

        // We previously checked the cell contents, so clear the chars to check only formatting.
        for cell in progress_part.iter_mut() {
            cell.c = ' ';
        }
        assert_eq!(
            progress_part,
            repeat(Cell { fg: Color::Named(NamedColor::Magenta), ..Cell::default() })
                .take(12)
                .collect::<Vec<Cell>>()
        );
        assert_eq!(unformatted, repeat(Cell::default()).take(88).collect::<Vec<Cell>>());
        assert_eq!(writer, Vec::<u8>::new());
    }

    #[test]
    fn test_print_stage() {
        let mut term = make_term(2, 50);
        let mut processor = Processor::new();
        let mut writer: Vec<u8> = vec![];

        let mut input = vec![];
        print_progress_bar(&mut input).unwrap();
        for c in input {
            processor.advance(&mut term, c, &mut writer);
        }

        let mut input = vec![];
        let download_msg = "Some download message";
        let line_end =
            print_stage(&mut input, COLOUR3_YELLOW, ContainerStatus::Downloading, download_msg)
                .unwrap();
        for c in input {
            processor.advance(&mut term, c, &mut writer);
        }

        let mut input = vec![];
        let after_msg = "Err details";
        print_after_stage(&mut input, line_end, COLOUR5_MAGENTA, after_msg).unwrap();
        for c in input {
            processor.advance(&mut term, c, &mut writer);
        }

        assert_eq!(
            get_text(&term),
            vec![
                "[====      ]  Some download message: Err details  ",
                "                                                  ",
            ]
        );

        let mut linear_grid = get_cells(&term).concat();
        // We previously checked the cell contents, so clear the chars to check only formatting.
        for cell in linear_grid.iter_mut() {
            cell.c = ' ';
        }

        let rest = linear_grid.as_slice();
        let (progress_part, rest) = rest.split_at(12);
        let (unformatted1, rest) = rest.split_at(2);
        let (stage_part, rest) = rest.split_at(download_msg.len());
        let (after_stage_part, rest) = rest.split_at(after_msg.len() + 2);
        let unformatted2 = rest;

        assert_eq!(
            progress_part,
            repeat(Cell { fg: Color::Named(NamedColor::Magenta), ..Cell::default() })
                .take(12)
                .collect::<Vec<Cell>>()
        );
        assert_eq!(unformatted1, repeat(Cell::default()).take(2).collect::<Vec<Cell>>());
        assert_eq!(
            stage_part,
            repeat(Cell { fg: Color::Named(NamedColor::Yellow), ..Cell::default() })
                .take(download_msg.len())
                .collect::<Vec<Cell>>()
        );
        assert_eq!(
            after_stage_part,
            repeat(Cell { fg: Color::Named(NamedColor::Magenta), ..Cell::default() })
                .take(after_msg.len() + 2)
                .collect::<Vec<Cell>>()
        );
        assert_eq!(unformatted2, repeat(Cell::default()).take(52).collect::<Vec<Cell>>());

        assert_eq!(writer, Vec::<u8>::new());
    }
}
