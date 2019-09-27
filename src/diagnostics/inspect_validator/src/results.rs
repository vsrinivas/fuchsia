// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{metrics::Metrics, validate::*},
    serde_derive::Serialize,
    std::collections::HashSet,
};

#[derive(Serialize, Debug)]
pub struct Results {
    messages: Vec<String>,
    unimplemented: HashSet<String>,
    failed: bool,
    metrics: Vec<Metrics>,
}

trait Summary {
    fn summary(&self) -> String;
}

impl Summary for Number {
    fn summary(&self) -> String {
        match self {
            Number::IntT(_) => "Int",
            Number::UintT(_) => "Uint",
            Number::DoubleT(_) => "Double",
            _ => "Unknown",
        }
        .to_string()
    }
}

impl Summary for NumberType {
    fn summary(&self) -> String {
        match self {
            NumberType::Int => "Int",
            NumberType::Uint => "Uint",
            NumberType::Double => "Double",
        }
        .to_string()
    }
}

impl Summary for Action {
    fn summary(&self) -> String {
        match self {
            Action::CreateNode(_) => "CreateNode".to_string(),
            Action::DeleteNode(_) => "DeleteNode".to_string(),
            Action::CreateNumericProperty(CreateNumericProperty { value, .. }) => {
                format!("CreateProperty({})", value.summary())
            }
            Action::CreateBytesProperty(_) => "CreateProperty(Bytes)".to_string(),
            Action::CreateStringProperty(_) => "CreateProperty(String)".to_string(),
            Action::DeleteProperty(_) => "DeleteProperty".to_string(),
            Action::SetBytes(_) => "Set(Bytes)".to_string(),
            Action::SetString(_) => "Set(String)".to_string(),
            Action::AddNumber(AddNumber { value, .. }) => format!("Add({})", value.summary()),
            Action::SubtractNumber(SubtractNumber { value, .. }) => {
                format!("Subtract({})", value.summary())
            }
            Action::SetNumber(SetNumber { value, .. }) => format!("Set({})", value.summary()),
            Action::CreateArrayProperty(CreateArrayProperty { number_type, .. }) => {
                format!("CreateArrayProperty({})", number_type.summary())
            }
            Action::ArraySet(ArraySet { value, .. }) => format!("ArraySet({})", value.summary()),
            Action::ArrayAdd(ArrayAdd { value, .. }) => format!("ArrayAdd({})", value.summary()),
            Action::ArraySubtract(ArraySubtract { value, .. }) => {
                format!("ArraySubtract({})", value.summary())
            }
            Action::CreateLinearHistogram(CreateLinearHistogram { floor, .. }) => {
                format!("CreateLinearHistogram({})", floor.summary())
            }
            Action::CreateExponentialHistogram(CreateExponentialHistogram { floor, .. }) => {
                format!("CreateExponentialHistogram({})", floor.summary())
            }
            Action::Insert(Insert { value, .. }) => format!("Insert({})", value.summary()),
            Action::InsertMultiple(InsertMultiple { value, .. }) => {
                format!("InsertMultiple({})", value.summary())
            }
            _ => "Unknown".to_string(),
        }
    }
}

impl Results {
    pub fn new() -> Results {
        Results {
            messages: Vec::new(),
            metrics: Vec::new(),
            unimplemented: HashSet::new(),
            failed: false,
        }
    }

    pub fn error(&mut self, message: String) {
        self.messages.push(message);
        self.failed = true;
    }

    pub fn unimplemented(&mut self, puppet_name: &str, action: &Action) {
        self.unimplemented.insert(format!("{}: {}", puppet_name, action.summary()));
    }

    pub fn remember_metrics(&mut self, metrics: Metrics) {
        self.metrics.push(metrics);
    }

    pub fn to_json(&self) -> String {
        match serde_json::to_string(self) {
            Ok(string) => string,
            Err(e) => format!("{{error: \"Converting to json: {:?}\"}}", e),
        }
    }

    pub fn print_pretty_text(&self) {
        if self.failed {
            println!("FAILED, sorry about that.");
        } else {
            println!("SUCCESS on all tests!");
        }
        for message in self.messages.iter() {
            println!("{}", message);
        }
        if self.unimplemented.len() > 0 {
            println!("\nUnimplemented:");
            for info in self.unimplemented.iter() {
                println!("  {}", info);
            }
        }
        if self.metrics.len() > 0 {
            println!("\nMetrics:");
            for metric in self.metrics.iter() {
                println!("  {:?}", metric);
            }
        }
    }

    pub fn failed(&self) -> bool {
        self.failed
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::*};

    #[test]
    fn error_result_fails() {
        let mut results = Results::new();
        assert!(!results.failed());
        results.error("Oops!".to_string());
        assert!(results.failed());
    }

    #[test]
    fn unimplemented_does_not_fail() {
        let mut results = Results::new();
        results.unimplemented("foo", &delete_node!(id:17));
        assert!(!results.failed());
    }

    #[test]
    fn unimplemented_does_not_duplicate() {
        let mut results = Results::new();
        results.unimplemented("foo", &delete_node!(id:17));
        assert!(results.to_json().split("DeleteNode").collect::<Vec<_>>().len() == 2);
        // Adding a second instance of the same command and puppet name doesn't increase reports.
        results.unimplemented("foo", &delete_node!(id:123));
        assert!(results.to_json().split("DeleteNode").collect::<Vec<_>>().len() == 2);
        // But adding the same command for a different puppet does increase reports.
        results.unimplemented("bar", &delete_node!(id:123));
        assert!(results.to_json().split("DeleteNode").collect::<Vec<_>>().len() == 3);
    }

    #[test]
    fn unimplemented_renders_everything() {
        let mut results = Results::new();
        results.unimplemented("foo", &create_node!(parent: 42, id:42, name: "bar"));
        assert!(results.to_json().contains("foo: CreateNode"));
        results.unimplemented("foo", &delete_node!(id:42));
        assert!(results.to_json().contains("foo: DeleteNode"));
        results.unimplemented(
            "foo",
            &create_numeric_property!(parent:42, id:42, name: "bar", value: Number::IntT(42)),
        );
        assert!(results.to_json().contains("foo: CreateProperty(Int)"));
        results.unimplemented(
            "foo",
            &create_numeric_property!(parent:42, id:42, name: "bar", value: Number::UintT(42)),
        );
        assert!(results.to_json().contains("foo: CreateProperty(Uint)"));
        results.unimplemented(
            "foo",
            &create_numeric_property!(parent:42, id:42, name: "bar", value: Number::DoubleT(42.0)),
        );
        assert!(results.to_json().contains("foo: CreateProperty(Double)"));
        results.unimplemented(
            "foo",
            &create_bytes_property!(parent:42, id:42, name: "bar", value: vec![42]),
        );
        assert!(results.to_json().contains("foo: CreateProperty(Bytes)"));
        results.unimplemented(
            "foo",
            &create_string_property!(parent:42, id:42, name: "bar", value: "bar"),
        );
        assert!(results.to_json().contains("foo: CreateProperty(String)"));
        results.unimplemented("foo", &set_string!(id:42, value: "bar"));
        assert!(results.to_json().contains("foo: Set(String)"));
        results.unimplemented("foo", &set_bytes!(id:42, value: vec![42]));
        assert!(results.to_json().contains("foo: Set(Bytes)"));
        results.unimplemented("foo", &set_number!(id:42, value: Number::IntT(42)));
        assert!(results.to_json().contains("foo: Set(Int)"));
        results.unimplemented("foo", &set_number!(id:42, value: Number::UintT(42)));
        assert!(results.to_json().contains("foo: Set(Uint)"));
        results.unimplemented("foo", &set_number!(id:42, value: Number::DoubleT(42.0)));
        assert!(results.to_json().contains("foo: Set(Double)"));
        results.unimplemented("foo", &add_number!(id:42, value: Number::IntT(42)));
        assert!(results.to_json().contains("foo: Add(Int)"));
        results.unimplemented("foo", &add_number!(id:42, value: Number::UintT(42)));
        assert!(results.to_json().contains("foo: Add(Uint)"));
        results.unimplemented("foo", &add_number!(id:42, value: Number::DoubleT(42.0)));
        assert!(results.to_json().contains("foo: Add(Double)"));
        results.unimplemented("foo", &subtract_number!(id:42, value: Number::IntT(42)));
        assert!(results.to_json().contains("foo: Subtract(Int)"));
        results.unimplemented("foo", &subtract_number!(id:42, value: Number::UintT(42)));
        assert!(results.to_json().contains("foo: Subtract(Uint)"));
        results.unimplemented("foo", &subtract_number!(id:42, value: Number::DoubleT(42.0)));
        assert!(results.to_json().contains("foo: Subtract(Double)"));
        results.unimplemented("foo", &delete_property!(id:42));
        assert!(results.to_json().contains("foo: DeleteProperty"));

        results.unimplemented("foo", &create_array_property!(parent: 42, id:42, name: "foo", slots: 42, type: NumberType::Uint));
        assert!(results.to_json().contains("foo: CreateArrayProperty(Uint)"));
        results.unimplemented("foo", &array_set!(id:42, index: 42, value: Number::UintT(42)));
        assert!(results.to_json().contains("foo: ArraySet(Uint)"));
        results.unimplemented("foo", &array_add!(id:42, index: 42, value: Number::UintT(42)));
        assert!(results.to_json().contains("foo: ArrayAdd(Uint)"));
        results.unimplemented("foo", &array_subtract!(id:42, index:42, value:Number::UintT(42)));
        assert!(results.to_json().contains("foo: ArraySubtract(Uint)"));

        results.unimplemented(
            "foo",
            &create_linear_histogram!(parent: 42, id:42, name: "foo", floor: 42, step_size: 42,
                                buckets: 42, type: IntT),
        );
        assert!(results.to_json().contains("foo: CreateLinearHistogram(Int)"));
        results.unimplemented("foo", &create_exponential_histogram!(parent: 42, id:42, name: "foo", floor: 42, initial_step: 42,
                                step_multiplier: 42, buckets: 42, type: UintT));
        assert!(results.to_json().contains("foo: CreateExponentialHistogram(Uint)"));
        results.unimplemented("foo", &insert!(id:42, value:Number::UintT(42)));
        assert!(results.to_json().contains("foo: Insert(Uint)"));
        results.unimplemented("foo", &insert_multiple!(id:42, value:Number::UintT(42), count: 42));
        assert!(results.to_json().contains("foo: InsertMultiple(Uint)"));

        assert!(!results.to_json().contains("42"));
        assert!(!results.to_json().contains("bar"));
        assert!(!results.to_json().contains("Unknown"));
    }
}
