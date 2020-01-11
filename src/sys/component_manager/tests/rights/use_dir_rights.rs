// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_io as fio, fuchsia_async as fasync, std::io};

/// Contains all the information required to verify that a particular path contains some set of
/// rights.
struct RightsTestCase {
    path: &'static str,
    rights: u32,
}

impl RightsTestCase {
    /// Constructs a new rights test case.
    fn new(path: &'static str, rights: u32) -> RightsTestCase {
        RightsTestCase { path: path, rights: rights }
    }

    /// Returns every right not available for this path.
    fn unavailable_rights(&self) -> Vec<u32> {
        let all_rights = [
            fio::OPEN_RIGHT_READABLE,
            fio::OPEN_RIGHT_WRITABLE,
            fio::OPEN_RIGHT_EXECUTABLE,
            fio::OPEN_RIGHT_ADMIN,
        ];

        let mut unavailable_rights: Vec<u32> = vec![];
        for right_to_check in all_rights.iter() {
            if *right_to_check | self.rights != self.rights {
                unavailable_rights.push(*right_to_check);
            }
        }
        unavailable_rights
    }

    /// Verifies that the rights are correctly implemented for the type.
    fn verify(&self) -> io::Result<()> {
        fdio::open_fd(self.path, self.rights)?;
        for invalid_right in self.unavailable_rights() {
            if let Ok(_) = fdio::open_fd(self.path, invalid_right) {
                return Err(io::Error::new(
                    io::ErrorKind::Other,
                    format!(
                        "Path: {} with rights: {} was opened with invalid rights {}",
                        self.path, self.rights, invalid_right
                    ),
                ));
            }
        }
        return Ok(());
    }
}

#[fasync::run_singlethreaded]
async fn main() -> io::Result<()> {
    let tests = [
        RightsTestCase::new("/read_only", fio::OPEN_RIGHT_READABLE),
        RightsTestCase::new("/read_write", fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE),
        RightsTestCase::new("/read_write_dup", fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE),
        RightsTestCase::new("/read_exec", fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE),
        RightsTestCase::new("/read_admin", fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_ADMIN),
        RightsTestCase::new("/read_only_after_scoped", fio::OPEN_RIGHT_READABLE),
    ];
    for test_case in tests.iter() {
        if let Err(test_failure) = test_case.verify() {
            println!("Directory rights test failed: {} - {}", test_case.path, test_failure);
            return Ok(());
        }
    }
    println!("All tests passed");
    Ok(())
}
