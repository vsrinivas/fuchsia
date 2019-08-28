<!-- Copyright 2019 The Fuchsia Authors. All rights reserved.
Use of this source code is governed by a BSD-style license that can be
found in the LICENSE file. -->

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/en/1.0.0/).

## [0.2.0] - 2019-08-27

### Removed
- Removed all SIMD code as recent optimizations removed the need for SIMD optimizations.

### Changed
- `update` now takes the original checksum value as `[u8; 2]` instead `u16`.
- `Checksum::checksum` and `checksum` now return the checksum value as `[u8; 2]` instead of `u16`.
- Updated microbenchmarks to reflect the latest state of this crate.
