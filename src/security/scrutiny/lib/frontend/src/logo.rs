// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use termion::color;

/// Prints the ASCII logo for scrutiny.
pub fn print_logo() {
    println!(
        "{}
╔═══════════════════════════════════════════╗
║  _____                 _   _              ║
║ /  ___|               | | (_)             ║
║ \\ `--.  ___ _ __ _   _| |_ _ _ __  _   _  ║
║  `--. \\/ __| '__| | | | __| | '_ \\| | | | ║
║ /\\__/ / (__| |  | |_| | |_| | | | | |_| | ║
║ \\____/ \\___|_|   \\__,_|\\__|_|_| |_|\\__, | ║
║                                     __/ | ║
║                                    |___/  ║
╚═══════════════════════════════════════════╝{}",
        color::Fg(color::Yellow),
        color::Fg(color::Reset)
    )
}
