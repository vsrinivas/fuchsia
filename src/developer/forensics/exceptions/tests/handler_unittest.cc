// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/exception_handler/handler.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/sys/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>

#include <type_traits>

#include <garnet/public/lib/fostr/fidl/fuchsia/exception/formatting.h>
#include <gtest/gtest.h>
#include <third_party/crashpad/snapshot/minidump/process_snapshot_minidump.h>
#include <third_party/crashpad/util/file/string_file.h>

#include "src/developer/forensics/exceptions/tests/crasher_wrapper.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/test/test_settings.h"

namespace forensics {
namespace exceptions {

inline void ToString(const fuchsia::exception::ExceptionType& value, std::ostream* os) {
  *os << value;
}

namespace {

using fuchsia::exception::ExceptionInfo;
using fuchsia::exception::ExceptionType;
using fuchsia::exception::ProcessException;
using testing::UnorderedElementsAreArray;

// Handler unit test ------------------------------------------------------------------------------
//
// The main objective of this test is to verify that the connected crash reporter actually receive
// the exception.

class StubCrashReporter : public fuchsia::feedback::CrashReporter {
 public:
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) {
    reports_.push_back(std::move(report));

    fuchsia::feedback::CrashReporter_File_Result result;
    result.set_response({});
    callback(std::move(result));
  }

  fidl::InterfaceRequestHandler<fuchsia::feedback::CrashReporter> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::feedback::CrashReporter> request) {
      bindings_.AddBinding(this, std::move(request));
    };
  }

  const std::vector<fuchsia::feedback::CrashReport>& reports() const { return reports_; }

 private:
  std::vector<fuchsia::feedback::CrashReport> reports_;

  fidl::BindingSet<fuchsia::feedback::CrashReporter> bindings_;
};

class StubIntrospect : public fuchsia::sys::internal::Introspect {
 public:
  void FindComponentByProcessKoid(uint64_t process_koid,
                                  FindComponentByProcessKoidCallback callback) {
    using namespace fuchsia::sys::internal;
    if (pids_to_component_infos_.find(process_koid) == pids_to_component_infos_.end()) {
      callback(Introspect_FindComponentByProcessKoid_Result::WithErr(ZX_ERR_NOT_FOUND));
    } else {
      const auto& info = pids_to_component_infos_[process_koid];

      SourceIdentity source_identity;
      source_identity.set_component_url(info.component_url);

      if (info.realm_path.has_value()) {
        source_identity.set_realm_path(info.realm_path.value());
      }

      callback(Introspect_FindComponentByProcessKoid_Result::WithResponse(
          Introspect_FindComponentByProcessKoid_Response(std::move(source_identity))));
    }
  }

  fidl::InterfaceRequestHandler<fuchsia::sys::internal::Introspect> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::sys::internal::Introspect> request) {
      bindings_.AddBinding(this, std::move(request));
    };
  }

  struct ComponentInfo {
    std::string component_url;
    std::optional<std::vector<std::string>> realm_path;
  };

  void AddProcessKoidToComponentInfo(uint64_t process_koid, ComponentInfo component_info) {
    pids_to_component_infos_[process_koid] = component_info;
  }

 private:
  std::map<uint64_t, ComponentInfo> pids_to_component_infos_;

  fidl::BindingSet<fuchsia::sys::internal::Introspect> bindings_;
};

// Test Setup -------------------------------------------------------------------------------------
//
// Necessary elements for a fidl test to run. The ServiceDirectoryProvider is meant to mock the
// environment from which a process gets its services. This is the way we "inject" in our stub
// crash reporter instead of the real one.

struct TestContext {
  async::Loop loop;
  sys::testing::ServiceDirectoryProvider services;
  std::unique_ptr<StubCrashReporter> crash_reporter;
  std::unique_ptr<StubIntrospect> introspect;
};

std::unique_ptr<TestContext> CreateTestContext() {
  std::unique_ptr<TestContext> context(new TestContext{
      .loop = async::Loop(&kAsyncLoopConfigAttachToCurrentThread),
      .services = sys::testing::ServiceDirectoryProvider{},
      .crash_reporter = std::make_unique<StubCrashReporter>(),
      .introspect = std::make_unique<StubIntrospect>(),
  });

  return context;
}

// Runs a loop until |condition| is true. Does this by stopping every |step| to check the condition.
// If |condition| is never true, the thread will never leave this cycle. The test harness has to be
// able to handle this "hanging" case.
void RunUntil(TestContext* context, fit::function<bool()> condition,
              zx::duration step = zx::msec(10)) {
  while (!condition()) {
    context->loop.Run(zx::deadline_after(step));
  }
}

bool RetrieveExceptionContext(ExceptionContext* pe) {
  // Create a process that crashes and obtain the relevant handles and exception.
  // By the time |SpawnCrasher| has returned, the process has already thrown an exception.
  if (!SpawnCrasher(pe))
    return false;

  // We mark the exception to be handled. We need this because we pass on the exception to the
  // handler, which will resume it before we get the control back. If we don't mark it as handled,
  // the exception will bubble out of our environment.
  return MarkExceptionAsHandled(pe);
}

ExceptionInfo ExceptionContextToExceptionInfo(const ExceptionContext& pe) {
  // Translate the exception to the fidl format.
  ExceptionInfo exception_info;
  exception_info.process_koid = pe.exception_info.pid;
  exception_info.thread_koid = pe.exception_info.tid;
  exception_info.type = static_cast<ExceptionType>(pe.exception_info.type);

  return exception_info;
}

// Utilities ---------------------------------------------------------------------------------------

inline void ValidateReport(const fuchsia::feedback::CrashReport& report,
                           const std::string& program_name,
                           const std::optional<std::string>& realm_path, bool validate_minidump) {
  ASSERT_TRUE(report.has_program_name());

  ASSERT_TRUE(report.has_specific_report());
  const fuchsia::feedback::SpecificCrashReport& specific_report = report.specific_report();

  ASSERT_TRUE(specific_report.is_native());
  const fuchsia::feedback::NativeCrashReport& native_report = specific_report.native();

  if (validate_minidump) {
    ASSERT_TRUE(report.has_annotations());
    auto matchers = std::vector({MatchesAnnotation("crash.process.name", "crasher")});

    if (realm_path.has_value()) {
      matchers.push_back(MatchesAnnotation("crash.realm-path", realm_path.value().c_str()));
    }

    if (program_name == "crasher") {
      matchers.push_back(MatchesAnnotation("debug.crash.component.url.set", "false"));
    }

    EXPECT_THAT(report.annotations(), UnorderedElementsAreArray(matchers));
  }

  // If the crash reporter could not get a minidump, it will not send a mem buffer.
  if (!validate_minidump) {
    ASSERT_FALSE(native_report.has_minidump());
    return;
  }

  EXPECT_EQ(report.program_name(), program_name);

  ASSERT_TRUE(native_report.has_minidump());
  const zx::vmo& minidump_vmo = native_report.minidump().vmo;

  uint64_t vmo_size;
  ASSERT_EQ(minidump_vmo.get_size(&vmo_size), ZX_OK);

  auto buf = std::make_unique<uint8_t[]>(vmo_size);
  ASSERT_EQ(minidump_vmo.read(buf.get(), 0, vmo_size), ZX_OK);

  // Read the vmo back into a file writer/reader interface.
  crashpad::StringFile string_file;
  string_file.Write(buf.get(), vmo_size);

  // Move the cursor to the beggining of the file.
  ASSERT_EQ(string_file.Seek(0, SEEK_SET), 0);

  // We verify that the minidump snapshot can validly read the file.
  crashpad::ProcessSnapshotMinidump minidump_snapshot;
  ASSERT_TRUE(minidump_snapshot.Initialize(&string_file));
}

inline void ValidateReport(const fuchsia::feedback::CrashReport& report,
                           const std::string& program_name, bool validate_minidump) {
  ValidateReport(report, program_name, std::nullopt, validate_minidump);
}

// Tests -------------------------------------------------------------------------------------------

TEST(HandlerTest, NoIntrospectConnection) {
  auto test_context = CreateTestContext();

  // We add the services we're injecting.
  test_context->services.AddService(test_context->crash_reporter->GetHandler());

  Handler handler(test_context->services.service_directory());

  // Create the exception.
  ExceptionContext exception;
  ASSERT_TRUE(RetrieveExceptionContext(&exception));

  bool called = false;
  handler.Handle(std::move(exception.exception), [&called]() { called = true; });

  // We wait until the crash reporter has received all exceptions.
  RunUntil(test_context.get(),
           [&test_context]() { return test_context->crash_reporter->reports().size() == 1u; });
  ASSERT_TRUE(called);

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exception.job.kill();
}

TEST(HandlerTest, NoCrashReporterConnection) {
  // We don't inject a stub service. This will make connecting to the service fail.
  auto test_context = CreateTestContext();

  Handler handler(test_context->services.service_directory());

  // Create the exception.
  ExceptionContext exception;
  ASSERT_TRUE(RetrieveExceptionContext(&exception));

  bool called = false;
  handler.Handle(std::move(exception.exception), [&called]() { called = true; });

  RunUntil(test_context.get(), [&called]() { return called; });

  // The stub shouldn't be called.
  ASSERT_EQ(test_context->crash_reporter->reports().size(), 0u);

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exception.job.kill();
}

TEST(HandlerTest, GettingInvalidVMO) {
  auto test_context = CreateTestContext();
  test_context->services.AddService(test_context->crash_reporter->GetHandler());
  test_context->services.AddService(test_context->introspect->GetHandler());

  Handler handler(test_context->services.service_directory());

  // We create a bogus exception, which will fail to create a valid VMO.
  bool called = false;
  handler.Handle(zx::exception{}, [&called]() { called = true; });

  RunUntil(test_context.get(),
           [&test_context]() { return test_context->crash_reporter->reports().size() == 1u; });
  ASSERT_TRUE(called);

  auto& report = test_context->crash_reporter->reports().front();

  ValidateReport(report, "crasher", false);
}

}  // namespace
}  // namespace exceptions
}  // namespace forensics
