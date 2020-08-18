// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler/handler.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/sys/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>

#include <type_traits>

#include <gtest/gtest.h>

#include "garnet/public/lib/fostr/fidl/fuchsia/exception/formatting.h"
#include "src/developer/forensics/exceptions/tests/crasher_wrapper.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/test/test_settings.h"
#include "third_party/crashpad/snapshot/minidump/process_snapshot_minidump.h"
#include "third_party/crashpad/util/file/string_file.h"

namespace forensics {
namespace exceptions {
namespace handler {

inline void ToString(const fuchsia::exception::ExceptionType& value, std::ostream* os) {
  *os << value;
}

namespace {

using fuchsia::exception::ExceptionInfo;
using fuchsia::exception::ExceptionType;
using fuchsia::exception::ProcessException;
using testing::UnorderedElementsAreArray;

constexpr zx::duration kDefaultTimeout{zx::duration::infinite()};

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

class StubCrashIntrospect : public fuchsia::sys::internal::CrashIntrospect {
 public:
  void FindComponentByProcessKoid(uint64_t process_koid,
                                  FindComponentByProcessKoidCallback callback) {
    using namespace fuchsia::sys::internal;
    if (pids_to_component_infos_.find(process_koid) == pids_to_component_infos_.end()) {
      callback(CrashIntrospect_FindComponentByProcessKoid_Result::WithErr(ZX_ERR_NOT_FOUND));
    } else {
      const auto& info = pids_to_component_infos_[process_koid];

      SourceIdentity source_identity;
      source_identity.set_component_url(info.component_url);

      if (info.realm_path.has_value()) {
        source_identity.set_realm_path(info.realm_path.value());
      }

      callback(CrashIntrospect_FindComponentByProcessKoid_Result::WithResponse(
          CrashIntrospect_FindComponentByProcessKoid_Response(std::move(source_identity))));
    }
  }

  fidl::InterfaceRequestHandler<fuchsia::sys::internal::CrashIntrospect> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::sys::internal::CrashIntrospect> request) {
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

  fidl::BindingSet<fuchsia::sys::internal::CrashIntrospect> bindings_;
};

class HandlerTest : public UnitTestFixture {
 public:
  HandlerTest() : executor_(dispatcher()) {}

  void HandleException(
      zx::exception exception, zx::duration component_lookup_timeout,
      ::fit::closure callback = [] {}) {
    executor_.schedule_task(
        Handle(std::move(exception), dispatcher(), services(), component_lookup_timeout)
            .then([callback = std::move(callback)](const ::fit::result<>& result) { callback(); }));
    RunLoopUntilIdle();
  }

  void HandleException(
      const std::string& process_name, const zx_koid_t process_koid,
      zx::duration component_lookup_timeout, ::fit::closure callback = [] {}) {
    executor_.schedule_task(
        Handle(process_name, process_koid, dispatcher(), services(), component_lookup_timeout)
            .then([callback = std::move(callback)](const ::fit::result<>& result) { callback(); }));
    RunLoopUntilIdle();
  }

  void SetUpCrashReporter() { InjectServiceProvider(&crash_reporter_); }
  void SetUpCrashIntrospect() { InjectServiceProvider(&introspect_); }

  const StubCrashReporter& crash_reporter() const { return crash_reporter_; }

  StubCrashIntrospect& introspect() { return introspect_; }
  const StubCrashIntrospect& introspect() const { return introspect_; }

 private:
  async::Executor executor_;

  StubCrashReporter crash_reporter_;
  StubCrashIntrospect introspect_;
};

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

TEST_F(HandlerTest, NoIntrospectConnection) {
  SetUpCrashReporter();

  // Create the exception.
  ExceptionContext exception;
  ASSERT_TRUE(RetrieveExceptionContext(&exception));

  bool called = false;
  HandleException(std::move(exception.exception), kDefaultTimeout, [&called] { called = true; });

  ASSERT_TRUE(called);
  EXPECT_EQ(crash_reporter().reports().size(), 1u);

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exception.job.kill();
}

TEST_F(HandlerTest, NoCrashReporterConnection) {
  SetUpCrashIntrospect();

  // Create the exception.
  ExceptionContext exception;
  ASSERT_TRUE(RetrieveExceptionContext(&exception));

  bool called = false;
  HandleException(std::move(exception.exception), kDefaultTimeout, [&called] { called = true; });

  ASSERT_TRUE(called);

  // The stub shouldn't be called.
  EXPECT_EQ(crash_reporter().reports().size(), 0u);

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exception.job.kill();
}

TEST_F(HandlerTest, GettingInvalidVMO) {
  SetUpCrashReporter();
  SetUpCrashIntrospect();

  bool called = false;
  HandleException(zx::exception{}, zx::duration::infinite(), [&called] { called = true; });

  ASSERT_TRUE(called);

  ASSERT_EQ(crash_reporter().reports().size(), 1u);
  auto& report = crash_reporter().reports().front();

  ValidateReport(report, "crasher", false);
}

TEST_F(HandlerTest, NoException) {
  SetUpCrashReporter();
  SetUpCrashIntrospect();

  // Create the exception.
  ExceptionContext exception;
  ASSERT_TRUE(RetrieveExceptionContext(&exception));

  zx::process process;
  ASSERT_EQ(exception.exception.get_process(&process), ZX_OK);

  const std::string process_name = fsl::GetObjectName(process.get());
  const zx_koid_t process_koid = fsl::GetKoid(process.get());

  const std::string kComponentUrl = "component_url";
  introspect().AddProcessKoidToComponentInfo(process_koid, StubCrashIntrospect::ComponentInfo{
                                                               .component_url = kComponentUrl,
                                                               .realm_path = std::nullopt,
                                                           });
  exception.exception.reset();

  bool called = false;
  HandleException(process_name, process_koid, zx::duration::infinite(),
                  [&called] { called = true; });

  ASSERT_TRUE(called);

  ASSERT_EQ(crash_reporter().reports().size(), 1u);
  auto& report = crash_reporter().reports().front();

  ValidateReport(report, kComponentUrl, false);

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exception.job.kill();
}

}  // namespace
}  // namespace handler
}  // namespace exceptions
}  // namespace forensics
