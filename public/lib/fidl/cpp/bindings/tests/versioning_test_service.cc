// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include <map>
#include <memory>

#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "lib/fidl/cpp/bindings/strong_binding.h"
#include "lib/fxl/macros.h"
#include "lib/fidl/compiler/interfaces/tests/versioning_test_service.fidl.h"

namespace fidl {
namespace test {
namespace versioning {

struct EmployeeInfo {
 public:
  EmployeeInfo() {}

  EmployeePtr employee;
  Array<uint8_t> finger_print;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(EmployeeInfo);
};

class HumanResourceDatabaseImpl : public HumanResourceDatabase {
 public:
  explicit HumanResourceDatabaseImpl(
      InterfaceRequest<HumanResourceDatabase> request)
      : strong_binding_(this, request.Pass()) {
    // Pretend that there is already some data in the system.
    EmployeeInfo* info = new EmployeeInfo();
    employees_[1] = info;
    info->employee = Employee::New();
    info->employee->employee_id = 1;
    info->employee->name = "Homer Simpson";
    info->employee->department = Department::DEV;
    info->employee->birthday = Date::New();
    info->employee->birthday->year = 1955;
    info->employee->birthday->month = 5;
    info->employee->birthday->day = 12;
    info->finger_print.resize(1024);
    for (uint32_t i = 0; i < 1024; ++i)
      info->finger_print[i] = i;
  }

  ~HumanResourceDatabaseImpl() override {
    for (auto iter = employees_.begin(); iter != employees_.end(); ++iter)
      delete iter->second;
  }

  void AddEmployee(EmployeePtr employee,
                   const AddEmployeeCallback& callback) override {
    uint64_t id = employee->employee_id;
    if (employees_.find(id) == employees_.end())
      employees_[id] = new EmployeeInfo();
    employees_[id]->employee = employee.Pass();
    callback.Run(true);
  }

  void QueryEmployee(uint64_t id,
                     bool retrieve_finger_print,
                     const QueryEmployeeCallback& callback) override {
    if (employees_.find(id) == employees_.end()) {
      callback.Run(nullptr, nullptr);
      return;
    }
    callback.Run(
        employees_[id]->employee.Clone(),
        retrieve_finger_print ? employees_[id]->finger_print.Clone() : nullptr);
  }

  void AttachFingerPrint(uint64_t id,
                         Array<uint8_t> finger_print,
                         const AttachFingerPrintCallback& callback) override {
    if (employees_.find(id) == employees_.end()) {
      callback.Run(false);
      return;
    }
    employees_[id]->finger_print = finger_print.Pass();
    callback.Run(true);
  }

 private:
  std::map<uint64_t, EmployeeInfo*> employees_;

  StrongBinding<HumanResourceDatabase> strong_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(HumanResourceDatabaseImpl);
};

class HumanResourceSystemServer : public ApplicationImplBase {
 public:
  HumanResourceSystemServer() {}
  ~HumanResourceSystemServer() override {}

  // |ApplicationImplBase| overrides:
  bool OnAcceptConnection(ServiceProviderImpl* service_provider_impl) override {
    service_provider_impl->AddService<HumanResourceDatabase>(
        [](const ConnectionContext& connection_context,
           InterfaceRequest<HumanResourceDatabase> hr_db_request) {
          // It will be deleted automatically when the underlying pipe
          // encounters a connection error.
          new HumanResourceDatabaseImpl(hr_db_request.Pass());
        });
    return true;
  }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(HumanResourceSystemServer);
};

}  // namespace versioning
}  // namespace test
}  // namespace fidl

mx_status_t MojoMain(MojoHandle application_request) {
  fidl::test::versioning::HumanResourceSystemServer hr_system_server;
  return fidl::RunApplication(application_request, &hr_system_server);
}
