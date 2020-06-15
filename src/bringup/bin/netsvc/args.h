// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_ARGS_H_
#define SRC_BRINGUP_BIN_NETSVC_ARGS_H_

// Parse the command line arguments in |argv|, returning the presence of boolean flags
//
// --netboot
// --nodename
// --advertise
// --all-features
//
// and the value of the char* flag
//
// --interface
//
// in the corresponding out parameters.
//
// If parse_netsvc_args returns < 0, an error string will be returned in |error|.
int parse_netsvc_args(int argc, char** argv, const char** error, bool* netboot, bool* nodename,
                      bool* advertise, bool* all_features, const char** interface);

// Parse the command line arguments in |argv|, returning the value of char* flags
//
// --interface
// --nodename
//
// in the corresponding out parameters.
//
// If parse_device_name_provider_args returns < 0, an error string will be returned in |error|.
int parse_device_name_provider_args(int argc, char** argv, const char** error,
                                    const char** interface, const char** nodename,
                                    const char** ethdir);

#endif  // SRC_BRINGUP_BIN_NETSVC_ARGS_H_
