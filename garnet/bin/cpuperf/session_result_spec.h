// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_SESSION_RESULT_SPEC_H_
#define GARNET_BIN_CPUPERF_SESSION_RESULT_SPEC_H_

#include <cstddef>
#include <string>

namespace cpuperf {

struct SessionResultSpec {
  SessionResultSpec(const std::string& config_name, const std::string& model_name,
                    size_t num_iterations, size_t num_traces,
                    const std::string& output_path_prefix);
  SessionResultSpec() = default;

  // Return true if results are to be saved.
  bool save_results() const { return output_path_prefix != ""; }

  // Given an iteration number and trace number, return the output file.
  std::string GetTraceFilePath(size_t iter_num, size_t trace_num) const;

  std::string config_name;
  std::string model_name;
  size_t num_iterations = 0;
  size_t num_traces = 0;
  std::string output_path_prefix;
};

bool DecodeSessionResultSpec(const std::string& json, SessionResultSpec* out_spec);

bool WriteSessionResultSpec(const std::string& output_file_path, const SessionResultSpec& spec);

}  // namespace cpuperf

#endif  // GARNET_BIN_CPUPERF_SESSION_RESULT_SPEC_H_
