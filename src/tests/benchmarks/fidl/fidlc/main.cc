// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <perftest/perftest.h>

#include "src/tests/benchmarks/fidl/fidlc/benchmarks.h"
#include "tools/fidl/fidlc/include/fidl/json_generator.h"
#include "tools/fidl/fidlc/include/fidl/lexer.h"
#include "tools/fidl/fidlc/include/fidl/ordinals.h"
#include "tools/fidl/fidlc/include/fidl/parser.h"
#include "tools/fidl/fidlc/include/fidl/source_file.h"

// This measures the time to compile the given input fidl text and generate
// JSON IR output, which is discarded after it is produced in-memory.
//
// NOTE: This benchmark is run on fuchsia devices despite FIDL compilation
// typically taking place on host. This is intentional because we maintain
// systems that can take consistent measurements for fuchsia benchmarks but
// have no such systems currently for host. Performance characteristics may
// differ in unknown ways between host and fuchsia.
bool RunBenchmark(perftest::RepeatState* state, const char* fidl) {
  while (state->KeepRunning()) {
    fidl::SourceFile source_file("example.test.fidl", fidl);
    fidl::Reporter reporter;
    fidl::ExperimentalFlags experimental_flags;
    fidl::Lexer lexer(source_file, &reporter);
    fidl::Parser parser(&lexer, &reporter, experimental_flags);
    fidl::flat::Libraries all_libraries(&reporter);
    fidl::VersionSelection version_selection;
    fidl::flat::Compiler compiler(&all_libraries, &version_selection,
                                  fidl::ordinals::GetGeneratedOrdinal64, experimental_flags);
    auto ast = parser.Parse();
    bool enable_color = !std::getenv("NO_COLOR") && isatty(fileno(stderr));
    if (!parser.Success()) {
      reporter.PrintReports(enable_color);
      return false;
    }
    if (!compiler.ConsumeFile(std::move(ast))) {
      reporter.PrintReports(enable_color);
      return false;
    }
    if (!compiler.Compile()) {
      reporter.PrintReports(enable_color);
      return false;
    }
    auto compilation = all_libraries.Filter(&version_selection);
    fidl::JSONGenerator json_generator(compilation.get(), experimental_flags);
    json_generator.Produce();
  }
  return true;
}

void RegisterTests() {
  for (Benchmark b : benchmarks) {
    perftest::RegisterTest(b.name, RunBenchmark, b.fidl);
  }
}
PERFTEST_CTOR(RegisterTests)

int main(int argc, char** argv) {
  return perftest::PerfTestMain(argc, argv, "fuchsia.fidlc_microbenchmarks");
}
