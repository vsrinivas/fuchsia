// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file has been copied from src/devices/block/drivers/ftl/test/launch.h.

// Launches a new process to execute the desired command. Returns the exit code
// of the executed program, and -1 if a problem was found during launch.
// Note: the given argument list must be terminated by a null pointer.
int Execute(const char** argv);
