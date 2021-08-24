// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::process::Command;

fn main() {
    let mut args = std::env::args().skip(1);
    assert_eq!(args.len(), 2, "Expected 2 arguments with the binaries to run and compare outputs");

    let cmd1 = args.next().unwrap();
    let output1 = Command::new(&cmd1).output().expect("Failed to run 1st command");
    assert_eq!(output1.status.code(), Some(0), "1st command exited with non-zero status");
    let output1 =
        String::from_utf8(output1.stdout).expect("1st command's output wasn't valid UTF-8");

    let cmd2 = args.next().unwrap();
    let output2 = Command::new(&cmd2).output().expect("Failed to run 2nd command");
    assert_eq!(output2.status.code(), Some(0), "2nd command exited with non-zero status");
    let output2 =
        String::from_utf8(output2.stdout).expect("1st command's output wasn't valid UTF-8");

    if output1 != output2 {
        panic!(
            "Output of '{}' does not match '{}\nCommand 1 output: \n{}\n\nCommand 2 output: \n{}\n",
            cmd1, cmd2, output1, output2
        );
    }
}
