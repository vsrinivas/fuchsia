// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler/crash_reporter.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/sys/internal/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#include <memory>
#include <type_traits>

#include <gtest/gtest.h>

#include "garnet/public/lib/fostr/fidl/fuchsia/exception/formatting.h"
#include "src/developer/forensics/exceptions/handler/component_lookup.h"
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

class StubCrashIntrospectV1 : public fuchsia::sys::internal::CrashIntrospect {
 public:
  struct ComponentInfo {
    std::string url;
    std::vector<std::string> realm_path;
    std::string name;
  };

  void FindComponentByThreadKoid(uint64_t thread_koid, FindComponentByThreadKoidCallback callback) {
    using namespace fuchsia::sys::internal;
    if (tids_to_component_infos_.find(thread_koid) == tids_to_component_infos_.end()) {
      callback(CrashIntrospect_FindComponentByThreadKoid_Result::WithErr(ZX_ERR_NOT_FOUND));
    } else {
      const auto& info = tids_to_component_infos_[thread_koid];

      SourceIdentity source_identity;
      source_identity.set_component_url(info.url)
          .set_realm_path(info.realm_path)
          .set_component_name(info.name);

      callback(CrashIntrospect_FindComponentByThreadKoid_Result::WithResponse(
          CrashIntrospect_FindComponentByThreadKoid_Response(std::move(source_identity))));
    }
  }

  fidl::InterfaceRequestHandler<fuchsia::sys::internal::CrashIntrospect> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::sys::internal::CrashIntrospect> request) {
      bindings_.AddBinding(this, std::move(request));
    };
  }

  void AddThreadKoidToComponentInfo(uint64_t thread_koid, ComponentInfo component_info) {
    tids_to_component_infos_[thread_koid] = component_info;
  }

 private:
  std::map<uint64_t, ComponentInfo> tids_to_component_infos_;

  fidl::BindingSet<fuchsia::sys::internal::CrashIntrospect> bindings_;
};

class StubCrashIntrospectV2 : public fuchsia::sys2::CrashIntrospect {
 public:
  struct ComponentInfo {
    std::string url;
  };
  void FindComponentByThreadKoid(uint64_t thread_koid, FindComponentByThreadKoidCallback callback) {
    using namespace fuchsia::sys2;
    if (tids_to_component_infos_.find(thread_koid) == tids_to_component_infos_.end()) {
      callback(CrashIntrospect_FindComponentByThreadKoid_Result::WithErr(
          fuchsia::component::Error::RESOURCE_NOT_FOUND));
    } else {
      const auto& info = tids_to_component_infos_[thread_koid];

      ComponentCrashInfo crash_info;
      crash_info.set_url(info.url);

      callback(CrashIntrospect_FindComponentByThreadKoid_Result::WithResponse(
          CrashIntrospect_FindComponentByThreadKoid_Response(std::move(crash_info))));
    }
  }

  fidl::InterfaceRequestHandler<fuchsia::sys2::CrashIntrospect> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::sys2::CrashIntrospect> request) {
      bindings_.AddBinding(this, std::move(request));
    };
  }

  void AddThreadKoidToComponentInfo(uint64_t thread_koid, ComponentInfo component_info) {
    tids_to_component_infos_[thread_koid] = component_info;
  }

 private:
  std::map<uint64_t, ComponentInfo> tids_to_component_infos_;

  fidl::BindingSet<fuchsia::sys2::CrashIntrospect> bindings_;
};

class HandlerTest : public UnitTestFixture {
 public:
  void HandleException(
      zx::exception exception, zx::duration component_lookup_timeout,
      CrashReporter::SendCallback callback = [](::fidl::StringPtr moniker) {}) {
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
      CrashReporter::SendCallback callback = [](::fidl::StringPtr moniker) {}) {
    handler_ = std::make_unique<CrashReporter>(dispatcher(), services(), component_lookup_timeout);

    handler_->Send(zx::exception{}, std::move(process), std::move(thread), std::move(callback));
    RunLoopUntilIdle();
  }

  void SetUpCrashReporter() { InjectServiceProvider(&crash_reporter_); }
  void SetUpCrashIntrospect() {
    InjectServiceProvider(&introspect_v1_);
    InjectServiceProvider(&introspect_v2_);
  }

  const StubCrashReporter& crash_reporter() const { return crash_reporter_; }

  StubCrashIntrospectV1& introspect_v1() { return introspect_v1_; }
  const StubCrashIntrospectV1& introspect_v1() const { return introspect_v1_; }

  StubCrashIntrospectV2& introspect_v2() { return introspect_v2_; }
  const StubCrashIntrospectV2& introspect_v2() const { return introspect_v2_; }

 private:
  std::unique_ptr<CrashReporter> handler_{nullptr};

  StubCrashReporter crash_reporter_;
  StubCrashIntrospectV1 introspect_v1_;
  StubCrashIntrospectV2 introspect_v2_;
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

inline void ValidateCrashSignature(const fuchsia::feedback::CrashReport& report,
                                   const std::string& crash_signature) {
  ASSERT_TRUE(report.has_crash_signature());
  EXPECT_EQ(report.crash_signature(), crash_signature);
}

inline void ValidateCrashReport(const fuchsia::feedback::CrashReport& report,
                                const std::string& expected_program_name,
                                const std::string& expected_process_name,
                                const zx_koid_t expected_process_koid,
                                const std::string& expected_thread_name,
                                const zx_koid_t expected_thread_koid,
                                const std::map<std::string, std::string>& expected_annotations) {
  ASSERT_TRUE(report.has_program_name());
  EXPECT_EQ(report.program_name(), expected_program_name);

  ASSERT_TRUE(report.has_specific_report());
  ASSERT_TRUE(report.specific_report().is_native());
  EXPECT_EQ(report.specific_report().native().process_name(), expected_process_name);
  EXPECT_EQ(report.specific_report().native().process_koid(), expected_process_koid);
  EXPECT_EQ(report.specific_report().native().thread_name(), expected_thread_name);
  EXPECT_EQ(report.specific_report().native().thread_koid(), expected_thread_koid);

  if (!expected_annotations.empty()) {
    ASSERT_TRUE(report.has_annotations());

    // Infer the type of |matchers|.
    auto matchers = std::vector({MatchesAnnotation("", "")});
    matchers.clear();

    for (const auto& [k, v] : expected_annotations) {
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
  std::optional<std::string> out_moniker{std::nullopt};
  HandleException(std::move(exception.exception), kDefaultTimeout,
                  [&called, &out_moniker](const ::fidl::StringPtr moniker) {
                    called = true;
                    if (moniker.has_value()) {
                      out_moniker = moniker.value();
                    }
                  });

  ASSERT_TRUE(called);
  ASSERT_FALSE(out_moniker.has_value());
  EXPECT_EQ(crash_reporter().reports().size(), 1u);

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exception.job.kill();
}

TEST_F(HandlerTest, NoCrashReporterConnectionV1) {
  SetUpCrashIntrospect();

  // Create the exception.
  ExceptionContext exception;
  ASSERT_TRUE(RetrieveExceptionContext(&exception));

  zx::thread thread;
  ASSERT_EQ(exception.exception.get_thread(&thread), ZX_OK);
  const zx_koid_t thread_koid = fsl::GetKoid(thread.get());

  const std::string kComponentUrl = "component_url";
  const std::vector<std::string> kRealmPath = {"realm", "path"};
  const std::string kComponentName = "component_name";
  introspect_v1().AddThreadKoidToComponentInfo(thread_koid, StubCrashIntrospectV1::ComponentInfo{
                                                                .url = kComponentUrl,
                                                                .realm_path = kRealmPath,
                                                                .name = kComponentName,
                                                            });

  bool called = false;
  std::optional<std::string> out_moniker{std::nullopt};
  HandleException(std::move(exception.exception), kDefaultTimeout,
                  [&called, &out_moniker](const ::fidl::StringPtr moniker) {
                    called = true;
                    if (moniker.has_value()) {
                      out_moniker = moniker.value();
                    }
                  });

  ASSERT_TRUE(called);
  ASSERT_TRUE(out_moniker.has_value());
  EXPECT_EQ(out_moniker.value(), "realm/path/component_name");

  // The stub shouldn't be called.
  EXPECT_EQ(crash_reporter().reports().size(), 0u);

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exception.job.kill();
}

TEST_F(HandlerTest, NoCrashReporterConnectionV2) {
  SetUpCrashIntrospect();

  // Create the exception.
  ExceptionContext exception;
  ASSERT_TRUE(RetrieveExceptionContext(&exception));

  zx::thread thread;
  ASSERT_EQ(exception.exception.get_thread(&thread), ZX_OK);
  const zx_koid_t thread_koid = fsl::GetKoid(thread.get());

  const std::string kComponentUrl = "component_url";
  introspect_v2().AddThreadKoidToComponentInfo(thread_koid, StubCrashIntrospectV2::ComponentInfo{
                                                                .url = kComponentUrl,
                                                            });

  bool called = false;
  std::optional<std::string> out_moniker{std::nullopt};
  HandleException(std::move(exception.exception), kDefaultTimeout,
                  [&called, &out_moniker](const ::fidl::StringPtr moniker) {
                    called = true;
                    if (moniker.has_value()) {
                      out_moniker = moniker.value();
                    }
                  });

  ASSERT_TRUE(called);
  ASSERT_FALSE(out_moniker.has_value());

  // The stub shouldn't be called.
  EXPECT_EQ(crash_reporter().reports().size(), 0u);

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exception.job.kill();
}

TEST_F(HandlerTest, NoExceptionV1) {
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
  const std::vector<std::string> kRealmPath = {"realm", "path"};
  const std::string kComponentName = "component_name";
  introspect_v1().AddThreadKoidToComponentInfo(thread_koid, StubCrashIntrospectV1::ComponentInfo{
                                                                .url = kComponentUrl,
                                                                .realm_path = kRealmPath,
                                                                .name = kComponentName,
                                                            });
  exception.exception.reset();

  bool called = false;
  std::optional<std::string> out_moniker{std::nullopt};
  HandleException(std::move(process), std::move(thread), zx::duration::infinite(),
                  [&called, &out_moniker](const ::fidl::StringPtr moniker) {
                    called = true;
                    if (moniker.has_value()) {
                      out_moniker = moniker.value();
                    }
                  });

  ASSERT_TRUE(called);
  ASSERT_TRUE(out_moniker.has_value());
  EXPECT_EQ(out_moniker.value(), "realm/path/component_name");

  ASSERT_EQ(crash_reporter().reports().size(), 1u);
  auto& report = crash_reporter().reports().front();

  ValidateCrashReport(report, kComponentUrl, process_name, process_koid, thread_name, thread_koid,
                      {
                          {"crash.realm-path", "/realm/path"},
                      });
  ValidateCrashSignature(report, "fuchsia-no-minidump-exception-expired");

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exception.job.kill();
}

TEST_F(HandlerTest, NoExceptionV2) {
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
  introspect_v2().AddThreadKoidToComponentInfo(thread_koid, StubCrashIntrospectV2::ComponentInfo{
                                                                .url = kComponentUrl,
                                                            });
  exception.exception.reset();

  bool called = false;
  std::optional<std::string> out_moniker{std::nullopt};
  HandleException(std::move(process), std::move(thread), zx::duration::infinite(),
                  [&called, &out_moniker](const ::fidl::StringPtr moniker) {
                    called = true;
                    if (moniker.has_value()) {
                      out_moniker = moniker.value();
                    }
                  });

  ASSERT_TRUE(called);
  ASSERT_FALSE(out_moniker.has_value());

  ASSERT_EQ(crash_reporter().reports().size(), 1u);
  auto& report = crash_reporter().reports().front();

  ValidateCrashReport(report, kComponentUrl, process_name, process_koid, thread_name, thread_koid,
                      {});
  ValidateCrashSignature(report, "fuchsia-no-minidump-exception-expired");

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exception.job.kill();
}

}  // namespace
}  // namespace handler
}  // namespace exceptions
}  // namespace forensics
