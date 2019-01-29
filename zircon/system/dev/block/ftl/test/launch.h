// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Launches a new process to execute the desired command. Returns the exit code
// of the executed program, and -1 if a problem was found during launch.
int Execute(int argc, const char** argv);
