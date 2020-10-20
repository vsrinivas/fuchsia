// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/json_generator.h>
#include <fidl/lexer.h>
#include <fidl/ordinals.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <fidl/tables_generator.h>
#include <perftest/perftest.h>

struct Benchmark {
  const char* name;
  const char* fidl;
};

constexpr Benchmark benchmarks[] = {Benchmark{
    .name = "Struct/16",
    .fidl = R"FIDL(
library example;

struct TestStruct {
    int8 f0;
    int8 f1;
    int8 f2;
    int8 f3;
    int8 f4;
    int8 f5;
    int8 f6;
    int8 f7;
    int8 f8;
    int8 f9;
    int8 f10;
    int8 f11;
    int8 f12;
    int8 f13;
    int8 f14;
    int8 f15;
};
)FIDL",
}};

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
    fidl::flat::Typespace typespace(fidl::flat::Typespace::RootTypes(&reporter));
    fidl::flat::Libraries all_libraries;
    fidl::flat::Library library(&all_libraries, &reporter, &typespace,
                                fidl::ordinals::GetGeneratedOrdinal64, experimental_flags);
    auto ast = parser.Parse();
    if (!parser.Success())
      return false;
    if (!library.ConsumeFile(std::move(ast)))
      return false;
    if (!library.Compile())
      return false;
    fidl::JSONGenerator json_generator(&library);
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
