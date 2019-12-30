// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fuchsia_async::{self as fasync, futures::StreamExt},
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{self as inspect, Property},
};

/// A task represents something that needs to be done. It consists of a bug
/// number for each tracking as well as a human-readable name.
/// It also contains a completion ratio of 1.0 that can be set dynamically.
#[derive(Debug)]
struct Task {
    /// The bug number of the task.
    _bug_number: String,

    /// The name of the task.
    _name: String,

    /// The completion ratio of the task (between 0 and 1).
    completion: f64,

    // Inspect values
    // They are set as the Task is created. Not really used in the program, but
    // recorded in Inspect. When the struct is dropped, the inspect values will
    // be cleared.

    // Node for this task
    _node: inspect::Node,

    // Inspect properties for the bug number and name.
    _bug_number_property: inspect::StringProperty,
    _name_property: inspect::StringProperty,

    // The completion of the task.
    completion_metric: inspect::DoubleProperty,
}

impl Task {
    /// Constructs a new |Task|.
    /// The given |Node| is provided by the parent to allow linking into the
    /// inspect hierarchy.
    fn new(bug_number: &str, name: &str, node: inspect::Node) -> Self {
        let _bug_number_property = node.create_string("bug", bug_number);
        let _name_property = node.create_string("name", name);
        let completion_metric = node.create_double("completion", 0.0);
        Task {
            _bug_number: bug_number.to_string(),
            completion: 0.0,
            _name: name.to_string(),
            _node: node,
            _bug_number_property,
            _name_property,
            completion_metric,
        }
    }

    /// Sets the completion ratio of the task.
    fn set_completion(&mut self, completion: f64) {
        self.completion = 0.0_f64.max(completion.min(1.0));
        self.completion_metric.set(self.completion);
    }
}

/// Represents an individual employee of the company.
struct Employee {
    _name: String,
    _email: String,

    // Tasks assigned to this |Employee|.
    tasks: Vec<Task>,

    // |Employee|s reporting to this |Employee|.
    reports: Vec<Employee>,

    // Inspect values
    // They are set as the Task is created. Not really used in the program, but
    // recorded in Inspect. When the struct is dropped, the inspect values will
    // be cleared.

    // Node under which this |Employee| can expose information.
    _node: inspect::Node,

    // Node under which this |Employee| nests |Task|s.
    task_node: inspect::Node,

    // Node under which this |Employee| nests reporting |Employee|s.
    report_node: inspect::Node,
}

impl Employee {
    pub fn new(name: &str, email: &str, node: inspect::Node) -> Self {
        let task_node = node.create_child("tasks");
        let report_node = node.create_child("reports");
        Employee {
            _name: name.to_string(),
            _email: email.to_string(),
            _node: node,
            tasks: vec![],
            reports: vec![],
            task_node,
            report_node,
        }
    }

    pub fn task_count(&self) -> usize {
        self.tasks.len()
    }

    /// Add a new task to this employee.
    pub fn add_task(&mut self, bug_number: &str, name: &str) -> &mut Task {
        let report_index =
            (0..self.reports.len()).fold(None, |min: Option<usize>, i: usize| match min {
                Some(j) => Some(if self.reports[j].task_count() < self.reports[i].task_count() {
                    j
                } else {
                    i
                }),
                None => Some(i),
            });
        match report_index {
            None => self.add_task_inner(bug_number, name),
            Some(report_index) => {
                if self.task_count() < self.reports[report_index].task_count() {
                    self.add_task_inner(bug_number, name)
                } else {
                    self.reports[report_index].add_task(bug_number, name)
                }
            }
        }
    }

    fn add_task_inner(&mut self, bug_number: &str, name: &str) -> &mut Task {
        let node_name = format!("task-{}", bug_number);
        let task = Task::new(bug_number, name, self.task_node.create_child(&node_name));
        self.tasks.push(task);
        self.tasks.last_mut().unwrap()
    }

    /// Add a new report to this employee.
    pub fn add_report(&mut self, name: &str, email: &str) -> usize {
        let employee = Employee::new(name, email, self.report_node.create_child(email));
        self.reports.push(employee);
        self.reports.len() - 1
    }

    pub fn get_report(&mut self, index: usize) -> Option<&mut Employee> {
        self.reports.get_mut(index)
    }
}

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("error creating executor")?;
    let mut fs = ServiceFs::new();

    // Creates a new inspector object. This will create the "root" node in the
    // inspect tree to which further children objects can be added.
    let inspector = inspect::Inspector::new();

    // Serves the Inspect Tree at the standard location "/diagnostics/fuchsia.inspect.Tree"
    inspector.serve(&mut fs)?;

    // Create a CEO |Employee| nested underneath the |root_object|.
    // The name "reporting_tree" will appear as a child of the root object.
    let mut ceo =
        Employee::new("CEO", "ceo@example.com", inspector.root().create_child("reporting_tree"));

    // Create some reports for the CEO, named Bob, Prakash, and Svetlana.
    let bob_index = ceo.add_report("Bob", "bob@example.com");
    let prakash_index = ceo.add_report("Prakash", "prakash@example.com");
    let svetlana_index = ceo.add_report("Svetlana", "svetlana@example.com");

    // Bob has 3 reports: Julie, James, and Jun.
    ceo.get_report(bob_index).unwrap().add_report("Julie", "julie@example.com");
    ceo.get_report(bob_index).unwrap().add_report("James", "james@example.com");
    ceo.get_report(bob_index).unwrap().add_report("Jun", "jun@example.com");

    // Prakash has two reports: Gerald and Nathan.
    ceo.get_report(prakash_index).unwrap().add_report("Gerald", "gerald@example.com");
    // Nathan is an intern, so assign him a task to complete his training.
    let report_index =
        ceo.get_report(prakash_index).unwrap().add_report("Nathan", "nathan@example.com");
    ceo.get_report(report_index)
        .unwrap()
        .add_task("ABC-12", "Complete intern code training")
        .set_completion(1.0);

    // Bob has a lot of corporate tasks to complete, he will give these to people
    // in his reporting tree.
    let bob = ceo.get_report(bob_index).unwrap();
    bob.add_task("CORP-100", "Promote extra synergy").set_completion(0.5);
    bob.add_task("CORP-101", "Circle back and re-sync").set_completion(0.75);
    bob.add_task("CORP-102", "Look into issue with facilities").set_completion(0.8);
    bob.add_task("CORP-103", "Issue new badges").set_completion(0.2);

    // Prakash has a lot of engineering tasks to complete, he will give these to
    // people in his reporting tree.
    let prakash = ceo.get_report(prakash_index).unwrap();
    prakash.add_task("ENG-10", "Document key structures").set_completion(1.0);
    prakash.add_task("ENG-11", "Write login page").set_completion(0.1);
    prakash.add_task("ENG-12", "Create design for v2").set_completion(0.33);

    // Svetlana has a lot of infrastructure tasks to complete, she doesn't
    // currently have reports so she takes these herself.
    let svetlana = ceo.get_report(svetlana_index).unwrap();
    svetlana.add_task("INFRA-100", "Implement new infrastructure").set_completion(1.0);
    svetlana.add_task("INFRA-101", "Onboard new users").set_completion(0.8);

    // Svetlana hired new people to help with infrastructure.
    svetlana.add_report("Hector", "hector@example.com");
    svetlana.add_report("Dianne", "dianne@example.com");
    svetlana.add_report("Andre", "andre@example.com");

    // Good thing Svetlana just hired, here are a bunch of tasks.
    svetlana.add_task("INFRA-102", "Bring up new datacenter").set_completion(0.75);
    svetlana.add_task("INFRA-103", "Cleanup old file structure").set_completion(0.25);
    svetlana.add_task("INFRA-104", "Rewire the datacenter again").set_completion(0.9);
    svetlana.add_task("INFRA-105", "Upgrade the cooling system").set_completion(0.8);
    svetlana.add_task("INFRA-106", "Investigate opening a datacenter on Mars").set_completion(1.0);
    svetlana.add_task("INFRA-107", "Interface with the cloud").set_completion(0.05);

    fs.take_and_serve_directory_handle()?;

    let () = executor.run_singlethreaded(fs.collect());
    Ok(())
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_inspect::assert_inspect_tree};

    #[test]
    fn test_tree() {
        let inspector = inspect::Inspector::new();
        let mut ceo = Employee::new(
            "CEO",
            "ceo@example.com",
            inspector.root().create_child("reporting_tree"),
        );
        let bob_index = ceo.add_report("Bob", "bob@example.com");
        let bob = ceo.get_report(bob_index).unwrap();
        bob.add_report("Julie", "julie@example.com");
        bob.add_report("James", "james@example.com");
        bob.add_task("CORP-100", "Promote extra synergy").set_completion(0.5);
        bob.add_task("CORP-101", "Circle back and re-sync").set_completion(0.75);
        bob.add_task("CORP-102", "Look into issue with facilities").set_completion(0.8);

        assert_inspect_tree!(inspector, root: {
            reporting_tree: {
                tasks: {},
                reports: {
                    "bob@example.com": {
                        tasks: {
                            "task-CORP-102": {
                                bug: "CORP-102",
                                name: "Look into issue with facilities",
                                completion: 0.8
                            }
                        },
                        reports: {
                            "julie@example.com": {
                                tasks: {
                                    "task-CORP-101": {
                                        bug: "CORP-101",
                                        name: "Circle back and re-sync",
                                        completion: 0.75
                                    }
                                },
                                reports: {}
                            },
                            "james@example.com": {
                                tasks: {
                                    "task-CORP-100": {
                                        bug: "CORP-100",
                                        name: "Promote extra synergy",
                                        completion: 0.5
                                    }
                                },
                                reports: {}
                            }
                        }
                    }
                }
            }
        });
    }
}
