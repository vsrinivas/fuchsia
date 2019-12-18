// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {failure::Error, fidl_fuchsia_io as fio, fuchsia_async as fasync, fuchsia_zircon as zx};

const SUPPORTED_RIGHTS: u32 =
    fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE | fio::OPEN_RIGHT_EXECUTABLE;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let tests = [
        ("/read_only", fio::OPEN_RIGHT_READABLE),
        ("/read_write", fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE),
        ("/read_exec", fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE),
    ];

    let mut tests_passed = true;
    for test in tests.iter() {
        tests_passed &= check_path_supports_strict_rights(test.0, test.1).await;
    }
    if tests_passed {
        println!("All tests passed");
    } else {
        println!("Some tests failed");
    }
    Ok(())
}

fn rights_str(rights: u32) -> String {
    let mut result = String::with_capacity(3);
    if rights & fio::OPEN_RIGHT_READABLE != 0 {
        result.push('r');
    }
    if rights & fio::OPEN_RIGHT_WRITABLE != 0 {
        result.push('w');
    }
    if rights & fio::OPEN_RIGHT_EXECUTABLE != 0 {
        result.push('x');
    }
    result
}

// Note that this function prints failed checks to stdout, rather than returning them in the
// Result.
// TODO(fxb/37419): This is necessary only because fuchsia.io.Node does not support directly
// querying the connection flags. Update this when File.GetFlags moves to Node.GetFlags.
async fn check_path_supports_strict_rights(path: &str, rights: u32) -> bool {
    if rights & !SUPPORTED_RIGHTS != 0 {
        panic!("check_dir_rights test included unsupported rights in call to check_path_supports_strict_rights: 0x{:x}", rights);
    }

    let mut test_failed = false;
    macro_rules! failed_check {
        ( $($x:expr),* ) => {
            test_failed = true;
            println!($($x),*);
        }
    };

    // Check that the path can be opened with the specified rights.
    match fdio::open_fd(path, rights) {
        Ok(_) => (),
        Err(zx::Status::ACCESS_DENIED) => {
            failed_check!("Access denied opening '{}' with rights '{}'", path, rights_str(rights));
        }
        Err(err) => {
            failed_check!(
                "Unexpected error opening '{}' with rights '{}': {}",
                path,
                rights_str(rights),
                err
            );
        }
    };

    // Check that the path can't be opened with rights other than the ones passed in 'rights'.
    let mut r = 1;
    while r < SUPPORTED_RIGHTS {
        let rights_bit = SUPPORTED_RIGHTS & r;
        r = r << 1;

        if (rights_bit == 0) || (rights & rights_bit != 0) {
            continue;
        }

        match fdio::open_fd(path, rights_bit) {
            Ok(_) => {
                failed_check!(
                    "Opening '{}' with right '{}' unexpectedly succeeded",
                    path,
                    rights_str(rights_bit)
                );
            }
            Err(zx::Status::ACCESS_DENIED) => {}
            Err(err) => {
                failed_check!(
                    "Unexpected error opening '{}' with rights '{}': {}",
                    path,
                    rights_str(rights_bit),
                    err
                );
            }
        }
    }
    !test_failed
}
