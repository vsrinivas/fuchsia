// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler/crash_reporter.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/sys/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>

#include <memory>
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
  void FindComponentByThreadKoid(uint64_t thread_koid, FindComponentByThreadKoidCallback callback) {
    using namespace fuchsia::sys::internal;
    if (tids_to_component_infos_.find(thread_koid) == tids_to_component_infos_.end()) {
      callback(CrashIntrospect_FindComponentByThreadKoid_Result::WithErr(ZX_ERR_NOT_FOUND));
    } else {
      const auto& info = tids_to_component_infos_[thread_koid];

      SourceIdentity source_identity;
      source_identity.set_component_url(info.component_url);

      if (info.realm_path.has_value()) {
        source_identity.set_realm_path(info.realm_path.value());
      }

      callback(CrashIntrospect_FindComponentByThreadKoid_Result::WithResponse(
          CrashIntrospect_FindComponentByThreadKoid_Response(std::move(source_identity))));
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

  void AddThreadKoidToComponentInfo(uint64_t thread_koid, ComponentInfo component_info) {
    tids_to_component_infos_[thread_koid] = component_info;
  }

 private:
  std::map<uint64_t, ComponentInfo> tids_to_component_infos_;

  fidl::BindingSet<fuchsia::sys::internal::CrashIntrospect> bindings_;
};

class HandlerTest : public UnitTestFixture {
 public:
  void HandleException(
      zx::exception exception, zx::duration component_lookup_timeout,
      ::fit::closure callback = [] {}) {
    handler_ = std::make_unique<CrashReporter>(dispatcher(), services(), component_lookup_timeout);

    zx::process process;
    exception.get_process(&process);

    zx::thread thread;
    exception.get_thread(&thread);

    handler_->Send(std::move(exception), std::move(process), std::move(thread),
                   std::move(callback));
    RunLoopUntilIdle();
  }

  void HandleException(
      zx::process process, zx::thread thread, zx::duration component_lookup_timeout,
      ::fit::closure callback = [] {}) {
    handler_ = std::make_unique<CrashReporter>(dispatcher(), services(), component_lookup_timeout);

    handler_->Send(zx::exception{}, std::move(process), std::move(thread), std::move(callback));
    RunLoopUntilIdle();
  }

  void SetUpCrashReporter() { InjectServiceProvider(&crash_reporter_); }
  void SetUpCrashIntrospect() { InjectServiceProvider(&introspect_); }

  const StubCrashReporter& crash_reporter() const { return crash_reporter_; }

  StubCrashIntrospect& introspect() { return introspect_; }
  const StubCrashIntrospect& introspect() const { return introspect_; }

 private:
  std::unique_ptr<CrashReporter> handler_{nullptr};

  StubCrashReporter crash_reporter_;
  StubCrashIntrospect introspect_;
};

bool RetrieveExceptionContext(ExceptionContext* pe) {
  // Create a process that crashes and obtain the relevant handles and exception.
  // By the time |SpawnCrasher| has returned, the thread has already thrown an exception.
  if (!SpawnCrasher(pe))
    return false;

  // We mark the exception to be handled. We need this because we pass on the exception to the
  // handler, which will resume it before we get the control back. If we don't mark it as handled,
  // the exception will bubble out of our environment.
  return MarkExceptionAsHandled(pe);
}

// Utilities ---------------------------------------------------------------------------------------

inline void ValidateGenericReport(const fuchsia::feedback::CrashReport& report,
                                  const std::string& crash_signature) {
  ASSERT_TRUE(report.has_specific_report());
  const fuchsia::feedback::SpecificCrashReport& specific_report = report.specific_report();

  ASSERT_TRUE(specific_report.is_generic());

  ASSERT_TRUE(specific_report.generic().has_crash_signature());
  EXPECT_EQ(specific_report.generic().crash_signature(), crash_signature);
}

inline void ValidateCrashReport(const fuchsia::feedback::CrashReport& report,
                                const std::string& program_name,
                                const std::map<std::string, std::string>& annotations) {
  ASSERT_TRUE(report.has_program_name());
  EXPECT_EQ(report.program_name(), program_name);

  if (!annotations.empty()) {
    ASSERT_TRUE(report.has_annotations());

    // Infer the type of |matchers|.
    auto matchers = std::vector({MatchesAnnotation("", "")});
    matchers.clear();

    for (const auto& [k, v] : annotations) {
      matchers.push_back(MatchesAnnotation(k.c_str(), v.c_str()));
    }

    EXPECT_THAT(report.annotations(), UnorderedElementsAreArray(matchers));
  }
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

  zx::thread thread;
  ASSERT_EQ(exception.exception.get_thread(&thread), ZX_OK);
  const std::string thread_name = fsl::GetObjectName(thread.get());
  const zx_koid_t thread_koid = fsl::GetKoid(thread.get());

  const std::string kComponentUrl = "component_url";
  introspect().AddThreadKoidToComponentInfo(thread_koid, StubCrashIntrospect::ComponentInfo{
                                                             .component_url = kComponentUrl,
                                                             .realm_path = std::nullopt,
                                                         });
  exception.exception.reset();

  bool called = false;
  HandleException(std::move(process), std::move(thread), zx::duration::infinite(),
                  [&called] { called = true; });

  ASSERT_TRUE(called);

  ASSERT_EQ(crash_reporter().reports().size(), 1u);
  auto& report = crash_reporter().reports().front();

  ValidateCrashReport(report, kComponentUrl,
                      {
                          {"crash.process.name", process_name},
                          {"crash.process.koid", std::to_string(process_koid)},
                          {"crash.thread.name", thread_name},
                          {"crash.thread.koid", std::to_string(thread_koid)},
                      });
  ValidateGenericReport(report, "fuchsia-no-minidump-exception-expired");

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exception.job.kill();
}

}  // namespace
}  // namespace handler
}  // namespace exceptions
}  // namespace forensics
