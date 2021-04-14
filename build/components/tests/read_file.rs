// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests {
    #[test]
    fn read_file() {
        let contents =
            std::fs::read_to_string("/pkg/data/hello_world.txt").expect("Failed to read file");
        assert_eq!(contents, "Hello world!\n");
    }
}
