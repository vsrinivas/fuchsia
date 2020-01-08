// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/sys/cpp/component_context.h>

#include <string>

#include "src/lib/inspect_deprecated/deprecated/object_dir.h"
#include "src/lib/inspect_deprecated/inspect.h"

/* Inspection Example App
 *
 * This app demonstrates common features of the Inspect API.
 *
 * The specific application is an employee task manager. Each |Employee| has a
 * number of |Task|s assigned and may have a number of additional |Employee|s
 * reporting to them. The full tree of |Task| and |Employee| are exposed over
 * the Inspect API.
 *
 * We are concerned with obtaining each |Employee|'s individual performance and
 * the performance of their direct reports. In both cases, |EmployeePerformance|
 * is simply the average completion of assigned |Task|s, from 0.0 to 1.0.
 *
 */

namespace {

// Global metric counts.
// Global metrics should be raw pointers that are set back to nullptr when
// deleted. Additional concurrency control should be used in multithreaded
// settings.
inspect_deprecated::UIntMetric* number_of_employees = nullptr;
inspect_deprecated::UIntMetric* number_of_tasks = nullptr;

// Set the pointers to global values.
// Returns a deferred_action that automatically sets the global pointers to null
// when it goes out of scope.
fit::deferred_action<fit::closure> SetGlobals(inspect_deprecated::UIntMetric* employee_count,
                                              inspect_deprecated::UIntMetric* task_count) {
  number_of_employees = employee_count;
  number_of_tasks = task_count;
  return fit::defer(fit::closure([] {
    number_of_employees = nullptr;
    number_of_tasks = nullptr;
  }));
}

// Changes the global count of employees by the given amount.
void CountEmployees(int change) {
  if (number_of_employees) {
    number_of_employees->Add(change);
  }
}

// Changes the global count of tasks by the given amount.
void CountTasks(int change) {
  if (number_of_tasks) {
    number_of_tasks->Add(change);
  }
}

}  // namespace

// A |Task| represents something that needs to be done. It consists of a bug
// number for each tracking as well as a human-readable name.
// It also contains a completion ratio out of 1.0 that can be set dynamically.
class Task {
 public:
  // Construct a new |Task|.
  // Note that the constructor takes an |inspect_deprecated::Node|; this object is
  // provided by the parent to allow linking into the inspect hierarchy.
  Task(std::string bug_number, std::string name, inspect_deprecated::Node object)
      : bug_number_(std::move(bug_number)), name_(std::move(name)), object_(std::move(object)) {
    // Increment the global count metric.
    CountTasks(1);

    // Create two |StringProperty| members to hold the data for this |Task|.
    bug_number_property_ = object_.CreateStringProperty("bug", bug_number_);
    name_property_ = object_.CreateStringProperty("name", name_);

    // The completion of the task is an |inspect_deprecated::DoubleMetric|.
    completion_metric_ = object_.CreateDoubleMetric("completion", 0);
  }

  // Destroy the task by decrementing the global count metric.
  ~Task() { CountTasks(-1); }

  // Sets the completion of the |Task|.
  void SetCompletion(double completion) {
    completion_ = completion < 0 ? 0 : completion > 1 ? 1 : completion;
    completion_metric_.Set(completion_);
  }

  // Gets the completion of the |Task|.
  double GetCompletion() const { return completion_; }

 private:
  std::string bug_number_;
  std::string name_;
  double completion_ = 0;

  // Object for this |Task|. All exposed properties and metrics are rooted on
  // this Object.
  inspect_deprecated::Node object_;

  // |StringProperty| for bug number and name.
  inspect_deprecated::StringProperty bug_number_property_;
  inspect_deprecated::StringProperty name_property_;

  // Metric for this |Task|'s completion.
  inspect_deprecated::DoubleMetric completion_metric_;
};

// Structure representing an |Employee|'s performance.
// Simply holds the total tasks the |Employee| has and the sum of their
// completion ratios.
struct EmployeePerformance {
  uint64_t total_tasks;
  double total_completion;

  // Calculates average completion.
  double CalculateCompletion() { return total_tasks != 0 ? total_completion / total_tasks : 1; }

  // Adds another |EmployeePerformance| to this one, useful for getting the
  // average completion of a list of reports.
  EmployeePerformance& operator+=(const EmployeePerformance& other) {
    total_tasks += other.total_tasks;
    total_completion += other.total_completion;
    return *this;
  }
};

// |Employee| represents an individual employee of our company.
// They consist of a name and email, a list of assigned |Task|s, and a list of
// |Employee|s that directly report to this |Employee|.
class Employee {
 public:
  // Create a new |Employee|.
  // Note that the constructor takes an |inspect_deprecated::Node| that we may use to
  // expose our own metrics, properties, and children Objects.
  Employee(std::string name, std::string email, inspect_deprecated::Node object)
      : name_(std::move(name)), email_(std::move(email)), object_(std::move(object)) {
    // Increment the global employee count.
    CountEmployees(1);

    // Create an |inspect_deprecated::StringProperty| for the name and email of this
    // employee.
    name_property_ = object_.CreateStringProperty("name", name_);
    email_property_ = object_.CreateStringProperty("email", email_);

    // |Task| objects are nested under another child object, called "tasks".
    task_object_ = object_.CreateChild("tasks");

    // Each |Employee| reporting to this |Employee|  are nested under another
    // child object, called "reports".
    report_object_ = object_.CreateChild("reports");

    // Create an |inspect_deprecated::LazyMetric| for this |Employee|'s personal
    // performance. The "personal_performance" of an |Employee| is the average
    // completion of their |Task|s.
    lazy_metrics_.emplace_back(
        object_.CreateLazyMetric("personal_performance", [this](component::Metric* out) {
          // Callbacks have an "out" parameter that is set to the desired value.
          // In this case, set it to the double value of our
          // |EmployeePerformance|.
          out->SetDouble(GetPerformance().CalculateCompletion());
        }));

    // Create an |inspect_deprecated::LazyMetric| for the performance of this
    // |Employee|'s reports. The "report" performance of an |Employee| is the
    // average completion of all |Task|s assigned to their direct reports.
    lazy_metrics_.emplace_back(
        object_.CreateLazyMetric("report_performance", [this](component::Metric* out) {
          // Add together the performance for each report, and set the result in
          // the out parameter.
          EmployeePerformance perf = {};
          for (const auto& report : reports_) {
            perf += report->GetPerformance();
          }
          out->SetDouble(perf.CalculateCompletion());
        }));
  }

  // Destroy an |Employee| by subtracting from the global employee count.
  ~Employee() { CountEmployees(-1); }

  // |Employee|s may be moved but not copied.
  // This is often necessary because inspect_deprecated::* types are not copyable.
  Employee(Employee&&) = default;
  Employee(const Employee&) = delete;
  Employee& operator=(Employee&&) = default;
  Employee& operator=(const Employee&) = delete;

  // Add a new |Task| to this |Employee|.
  // |Employee|s always try to assign |Task|s to the report with the least
  // number of existing |Task|s before taking a |Task| a task for themself.
  //
  // Returns a pointer to the |Task| for additional modification.
  Task* AddTask(std::string bug_number, std::string name) {
    size_t least_loaded_count = GetTaskCount();
    Employee* least_loaded_employee = this;

    // Iterate over reports to find the report with the least number of existing
    // tasks.
    for (auto& report : reports_) {
      if (report->GetTaskCount() <= least_loaded_count) {
        least_loaded_count = report->GetTaskCount();
        least_loaded_employee = report.get();
      }
    }

    if (least_loaded_employee == this) {
      // If this |Employee| is the least loaded, take the |Task|...
      return tasks_
          .emplace_back(std::make_unique<Task>(
              std::move(bug_number), std::move(name),
              // Note: We need to pass an Object linked under this Object
              // into the new child. We use |inspect_deprecated::UniqueName| to assign a
              // globally unique suffix to the child's name.
              task_object_.CreateChild(inspect_deprecated::UniqueName("task-"))))
          .get();
    } else {
      // ... otherwise, recursively add the |Task| to the least loaded report.
      return least_loaded_employee->AddTask(std::move(bug_number), std::move(name));
    }
  }

  // Gets the number of |Task|s this |Employee| has.
  size_t GetTaskCount() const { return tasks_.size(); }

  // Add a new |Employee| reporting to this |Employee|.
  Employee* AddReport(std::string name, std::string email) {
    return reports_
        .emplace_back(
            std::make_unique<Employee>(std::move(name), std::string(email),
                                       // Note: We need to pass an Object linked under this Object
                                       // into the new child. We use the |email| directly, since
                                       // elsewhere we guarantee everyone's emails are unique.
                                       report_object_.CreateChild(std::move(email))))
        .get();
  }

  // Gets the performance for this |Employee|.
  EmployeePerformance GetPerformance() const {
    EmployeePerformance ret = {.total_tasks = tasks_.size(), .total_completion = 0};
    for (const auto& task : tasks_) {
      ret.total_completion += task->GetCompletion();
    }

    return ret;
  }

 private:
  std::string name_;
  std::string email_;

  // Vector of |Task|s assigned to this |Employee|.
  std::vector<std::unique_ptr<Task>> tasks_;
  // Vector of |Employee|s reporting to this |Employee|.
  std::vector<std::unique_ptr<Employee>> reports_;

  // Object under which this |Employee| can expose inspect information.
  inspect_deprecated::Node object_;

  // Properties for name and email.
  inspect_deprecated::StringProperty name_property_;
  inspect_deprecated::StringProperty email_property_;

  // Object under which this |Employee| nests |Task|s.
  inspect_deprecated::Node task_object_;
  // Object under which this |Employee| nests reporting |Employee|s.
  inspect_deprecated::Node report_object_;

  // Container for various computed "Lazy" metrics we wish to expose.
  std::vector<inspect_deprecated::LazyMetric> lazy_metrics_;
};

int main(int argc, const char** argv) {
  // Standard component setup, create an event loop and obtain the
  // |StartupContext|.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  // Create a root object and bind it to out/
  auto root_object_dir = component::ObjectDir::Make("root");
  inspect_deprecated::Node root_object(root_object_dir);
  fidl::BindingSet<fuchsia::inspect::deprecated::Inspect> inspect_bindings_;
  context->outgoing()
      ->GetOrCreateDirectory("diagnostics")
      ->AddEntry(fuchsia::inspect::deprecated::Inspect::Name_,
                 std::make_unique<vfs::Service>(
                     inspect_bindings_.GetHandler(root_object_dir.object().get())));

  // Create global metrics and globally publish pointers to them.
  auto employee_count = root_object.CreateUIntMetric("employee_count", 0);
  auto task_count = root_object.CreateUIntMetric("task_count", 0);
  auto cleanup = SetGlobals(&employee_count, &task_count);

  // Create a CEO |Employee| nested underneath the |root_object|.
  // The name "reporting_tree" will appear as a child of the root object.
  Employee ceo("CEO", "ceo@example.com", root_object.CreateChild("reporting_tree"));

  // Create some reports for the CEO, named Bob, Prakash, and Svetlana.
  auto* bob = ceo.AddReport("Bob", "bob@example.com");
  auto* prakash = ceo.AddReport("Prakash", "prakash@example.com");
  auto* svetlana = ceo.AddReport("Svetlana", "svetlana@example.com");

  // Bob has 3 reports: Julie, James, and Jun.
  bob->AddReport("Julie", "julie@example.com");
  bob->AddReport("James", "james@example.com");
  bob->AddReport("Jun", "jun@example.com");

  // Prakash has two reports: Gerald and Nathan.
  prakash->AddReport("Gerald", "gerald@example.com");
  // Nathan is an intern, so assign him a task to complete his training.
  prakash->AddReport("Nathan", "nathan@example.com")
      ->AddTask("ABC-12", "Complete intern code training")
      ->SetCompletion(1);

  // Bob has a lot of corporate tasks to complete, he will give these to people
  // in his reporting tree.
  bob->AddTask("CORP-100", "Promote extra synergy")->SetCompletion(.5);
  bob->AddTask("CORP-101", "Circle back and re-sync")->SetCompletion(.75);
  bob->AddTask("CORP-102", "Look into issue with facilities")->SetCompletion(.8);
  bob->AddTask("CORP-103", "Issue new badges")->SetCompletion(.2);

  // Prakash has a lot of engineering tasks to complete, he will give these to
  // people in his reporting tree.
  prakash->AddTask("ENG-10", "Document key structures")->SetCompletion(1);
  prakash->AddTask("ENG-11", "Write login page")->SetCompletion(.1);
  prakash->AddTask("ENG-12", "Create design for v2")->SetCompletion(.33);

  // Svetlana has a lot of infrastructure tasks to complete, she doesn't
  // currently have reports so she takes these herself.
  svetlana->AddTask("INFRA-100", "Implement new infrastructure")->SetCompletion(1);
  svetlana->AddTask("INFRA-101", "Onboard new users")->SetCompletion(.8);

  // Svetlana hired new people to help with infrastructure.
  svetlana->AddReport("Hector", "hector@example.com");
  svetlana->AddReport("Dianne", "dianne@example.com");
  svetlana->AddReport("Andre", "andre@example.com");

  // Good thing Svetlana just hired, here are a bunch of tasks.
  svetlana->AddTask("INFRA-102", "Bring up new datacenter")->SetCompletion(.75);
  svetlana->AddTask("INFRA-103", "Cleanup old file structure")->SetCompletion(.25);
  svetlana->AddTask("INFRA-104", "Rewire the datacenter again")->SetCompletion(.9);
  svetlana->AddTask("INFRA-105", "Upgrade the cooling system")->SetCompletion(.8);
  svetlana->AddTask("INFRA-106", "Investigate opening a datacenter on Mars")->SetCompletion(1);
  svetlana->AddTask("INFRA-107", "Interface with the cloud")->SetCompletion(.05);

  loop.Run();
  return 0;
}
