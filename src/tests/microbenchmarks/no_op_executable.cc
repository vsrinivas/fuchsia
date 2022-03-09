// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a no-op test executable used by the process-spawn benchmarks
// (fdio_spawn.cc and process_spawn_posix.cc). It's important that it does no
// work and simply exits.
int main(int argc, char** argv) { return 0; }
