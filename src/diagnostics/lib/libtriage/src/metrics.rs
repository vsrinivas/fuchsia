// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod fetch;

use {
    super::config::{self},
    fetch::{InspectFetcher, SelectorString, SelectorType},
    fuchsia_inspect_node_hierarchy::Property as DiagnosticProperty,
    serde::{Deserialize, Deserializer},
    serde_json::Value as JsonValue,
    std::{clone::Clone, collections::HashMap, convert::TryFrom},
};

/// The contents of a single Metric. Metrics produce a value for use in Actions or other Metrics.
#[derive(Clone, Debug)]
pub enum Metric {
    /// Selector tells where to find a value in the Inspect data.
    // Note: This can't be a fidl_fuchsia_diagnostics::Selector because it's not deserializable or
    // cloneable.
    Selector(SelectorString),
    /// Eval contains an arithmetic expression,
    // TODO(cphoenix): Parse and validate this at load-time.
    Eval(String),
}

impl<'de> Deserialize<'de> for Metric {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let value = String::deserialize(deserializer)?;
        if SelectorString::is_selector(&value) {
            Ok(Metric::Selector(SelectorString::try_from(value).map_err(serde::de::Error::custom)?))
        } else {
            Ok(Metric::Eval(value))
        }
    }
}

impl std::fmt::Display for Metric {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Metric::Selector(s) => write!(f, "{:?}", s),
            Metric::Eval(s) => write!(f, "{}", s),
        }
    }
}

/// [Metrics] are a map from namespaces to the named [Metric]s stored within that namespace.
pub type Metrics = HashMap<String, HashMap<String, Metric>>;

/// Contains all the information needed to look up and evaluate a Metric - other
/// [Metric]s that may be referred to, and Inspect data (entries for each
/// component) that can be accessed by Selector-type Metrics.
pub struct MetricState<'a> {
    pub metrics: &'a Metrics,
    pub inspect: &'a InspectFetcher,
}

/// The calculated or selected value of a Metric.
///
/// Missing means that the value could not be calculated; its String tells
/// the reason. Array and String are not used in v0.1 but will be useful later.
#[derive(Deserialize, Debug, Clone)]
pub enum MetricValue {
    // TODO(cphoenix): Support u64.
    Int(i64),
    Float(f64),
    String(String),
    Bool(bool),
    Array(Vec<MetricValue>),
    Bytes(Vec<u8>),
    Missing(String),
}

impl PartialEq for MetricValue {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (MetricValue::Int(l), MetricValue::Int(r)) => l == r,
            (MetricValue::Float(l), MetricValue::Float(r)) => l == r,
            (MetricValue::Int(l), MetricValue::Float(r)) => *l as f64 == *r,
            (MetricValue::Float(l), MetricValue::Int(r)) => *l == *r as f64,
            (MetricValue::String(l), MetricValue::String(r)) => l == r,
            (MetricValue::Bool(l), MetricValue::Bool(r)) => l == r,
            (MetricValue::Array(l), MetricValue::Array(r)) => l == r,
            (MetricValue::Missing(l), MetricValue::Missing(r)) => l == r,
            _ => false,
        }
    }
}

impl Eq for MetricValue {}

impl std::fmt::Display for MetricValue {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match &*self {
            MetricValue::Int(n) => write!(f, "Int({})", n),
            MetricValue::Float(n) => write!(f, "Float({})", n),
            MetricValue::Bool(n) => write!(f, "Bool({})", n),
            MetricValue::String(n) => write!(f, "String({})", n),
            MetricValue::Array(n) => write!(f, "Array({:?})", n),
            MetricValue::Bytes(n) => write!(f, "Bytes({:?})", n),
            MetricValue::Missing(n) => write!(f, "Missing({})", n),
        }
    }
}

impl Into<MetricValue> for f64 {
    fn into(self) -> MetricValue {
        MetricValue::Float(self)
    }
}

impl Into<MetricValue> for i64 {
    fn into(self) -> MetricValue {
        MetricValue::Int(self)
    }
}

impl From<DiagnosticProperty> for MetricValue {
    fn from(property: DiagnosticProperty) -> Self {
        match property {
            DiagnosticProperty::String(_name, value) => Self::String(value),
            DiagnosticProperty::Bytes(_name, value) => Self::Bytes(value),
            DiagnosticProperty::Int(_name, value) => Self::Int(value),
            DiagnosticProperty::Uint(_name, value) => Self::Int(value as i64),
            DiagnosticProperty::Double(_name, value) => Self::Float(value),
            DiagnosticProperty::Bool(_name, value) => Self::Bool(value),
            // TODO(cphoenix): Support arrays - need to figure out what to do about histograms.
            DiagnosticProperty::DoubleArray(_name, _)
            | DiagnosticProperty::IntArray(_name, _)
            | DiagnosticProperty::UintArray(_name, _) => {
                Self::Missing("Arrays not supported yet".to_owned())
            }
        }
    }
}

impl From<JsonValue> for MetricValue {
    fn from(value: JsonValue) -> Self {
        match value {
            JsonValue::String(value) => Self::String(value),
            JsonValue::Bool(value) => Self::Bool(value),
            JsonValue::Number(value) => {
                if value.is_i64() {
                    Self::Int(value.as_i64().unwrap())
                } else if value.is_f64() {
                    Self::Float(value.as_f64().unwrap())
                } else {
                    Self::Missing("Unable to convert JSON number".to_owned())
                }
            }
            _ => Self::Missing("Unsupported JSON type".to_owned()),
        }
    }
}

#[derive(Deserialize, Debug, Clone, PartialEq)]
pub enum Function {
    Add,
    Sub,
    Mul,
    FloatDiv,
    IntDiv,
    Greater,
    Less,
    GreaterEq,
    LessEq,
    Equals,
    NotEq,
    Max,
    Min,
    And,
    Or,
    Not,
}

fn demand_numeric(value: &MetricValue) -> MetricValue {
    match value {
        MetricValue::Int(_) | MetricValue::Float(_) => {
            MetricValue::Missing("Internal bug - numeric passed to demand_numeric".to_string())
        }
        MetricValue::Missing(message) => MetricValue::Missing(message.clone()),
        other => MetricValue::Missing(format!("{} not numeric", other)),
    }
}

fn demand_both_numeric(value1: &MetricValue, value2: &MetricValue) -> MetricValue {
    match value1 {
        MetricValue::Float(_) | MetricValue::Int(_) => return demand_numeric(value2),
        _ => (),
    }
    match value2 {
        MetricValue::Float(_) | MetricValue::Int(_) => return demand_numeric(value1),
        _ => (),
    }
    let value1 = demand_numeric(value1);
    let value2 = demand_numeric(value2);
    MetricValue::Missing(format!("{} and {} not numeric", value1, value2))
}

/// Macro which handles applying a function to 2 operands and returns a
/// MetricValue.
///
/// The macro handles type promotion and promotion to the specified type.
macro_rules! apply_math_operands {
    ($left:expr, $right:expr, $function:expr, $ty:ty) => {
        match ($left, $right) {
            (MetricValue::Int(int1), MetricValue::Int(int2)) => {
                // TODO(cphoenix): Instead of converting to float, use int functions.
                ($function(int1 as f64, int2 as f64) as $ty).into()
            }
            (MetricValue::Int(int1), MetricValue::Float(float2)) => {
                $function(int1 as f64, float2).into()
            }
            (MetricValue::Float(float1), MetricValue::Int(int2)) => {
                $function(float1, int2 as f64).into()
            }
            (MetricValue::Float(float1), MetricValue::Float(float2)) => {
                $function(float1, float2).into()
            }
            (value1, value2) => demand_both_numeric(&value1, &value2),
        }
    };
}

/// A macro which extracts two binary operands from a vec of operands and
/// applies the given function.
macro_rules! extract_and_apply_math_operands {
    ($self:ident, $namespace:expr, $function:expr, $operands:expr, $ty:ty) => {
        match MetricState::extract_binary_operands($self, $namespace, $operands) {
            Ok((left, right)) => apply_math_operands!(left, right, $function, $ty),
            Err(value) => value,
        }
    };
}

/// Expression represents the parsed body of an Eval Metric. It applies
/// a function to sub-expressions, or stores a Missing error, the name of a
/// Metric, or a basic Value.
#[derive(Deserialize, Debug, Clone, PartialEq)]
pub enum Expression {
    // Some operators have arity 1 or 2, some have arity N.
    // For symmetry/readability, I use the same operand-spec Vec<Expression> for all.
    // TODO(cphoenix): Check on load that all operators have a legal number of operands.
    Function(Function, Vec<Expression>),
    IsMissing(Vec<Expression>),
    Metric(String),
    Value(MetricValue),
}

impl<'a> MetricState<'a> {
    /// Create an initialized MetricState.
    pub fn new(metrics: &'a Metrics, inspect: &'a InspectFetcher) -> MetricState<'a> {
        MetricState { metrics, inspect }
    }

    /// Calculate the value of a Metric specified by name and namespace.
    ///
    /// If [name] is of the form "namespace::name" then [namespace] is ignored.
    /// If [name] is just "name" then [namespace] is used.
    pub fn metric_value_by_name(&self, namespace: &str, name: &String) -> MetricValue {
        // TODO(cphoenix): When historical metrics are added, change semantics to refresh()
        // TODO(cphoenix): cache values
        // TODO(cphoenix): Detect infinite cycles/depth.
        // TODO(cphoenix): Improve the data structure on Metric names. Probably fill in
        //  namespace during parse.
        let name_parts = name.split("::").collect::<Vec<_>>();
        let real_namespace: &str;
        let real_name: &str;
        match name_parts.len() {
            1 => {
                real_namespace = namespace;
                real_name = name;
            }
            2 => {
                real_namespace = name_parts[0];
                real_name = name_parts[1];
            }
            _ => {
                return MetricValue::Missing(format!("Bad name '{}': too many '::'", name));
            }
        }
        match self.metrics.get(real_namespace) {
            None => return MetricValue::Missing(format!("Bad namespace '{}'", real_namespace)),
            Some(metric_map) => match metric_map.get(real_name) {
                None => {
                    return MetricValue::Missing(format!(
                        "Metric '{}' Not Found in '{}'",
                        real_name, real_namespace
                    ))
                }
                Some(metric) => self.metric_value(real_namespace, &metric),
            },
        }
    }

    /// Fetches or computes the value of a Metric.
    pub fn metric_value(&self, namespace: &str, metric: &Metric) -> MetricValue {
        match metric {
            Metric::Selector(selector) => match selector.selector_type {
                SelectorType::Inspect => {
                    let values = self.inspect.fetch(&selector);
                    match values.len() {
                        0 => MetricValue::Missing(format!(
                            "{} not found in Inspect data",
                            selector.body()
                        )),
                        1 => values[0].clone(),
                        _ => MetricValue::Missing(format!(
                            "Multiple {} found in Inspect data",
                            selector.body()
                        )),
                    }
                }
            },
            Metric::Eval(expression) => match config::parse::parse_expression(expression) {
                Ok(expr) => self.evaluate(namespace, &expr),
                Err(e) => MetricValue::Missing(format!("Expression parse error\n{}", e)),
            },
        }
    }

    /// Evaluate an Expression which contains only base values, not referring to other Metrics.
    #[cfg(test)]
    pub fn evaluate_math(e: &Expression) -> MetricValue {
        MetricState::new(&HashMap::new(), &InspectFetcher::new_empty()).evaluate(&"".to_string(), e)
    }

    fn evaluate_function(
        &self,
        namespace: &str,
        function: &Function,
        operands: &Vec<Expression>,
    ) -> MetricValue {
        match function {
            Function::Add => self.fold_math(namespace, &|a, b| a + b, operands),
            Function::Sub => self.apply_math(namespace, &|a, b| a - b, operands),
            Function::Mul => self.fold_math(namespace, &|a, b| a * b, operands),
            Function::FloatDiv => self.apply_math_f(namespace, &|a, b| a / b, operands),
            Function::IntDiv => self.apply_math(namespace, &|a, b| f64::trunc(a / b), operands),
            Function::Greater => self.apply_cmp(namespace, &|a, b| a > b, operands),
            Function::Less => self.apply_cmp(namespace, &|a, b| a < b, operands),
            Function::GreaterEq => self.apply_cmp(namespace, &|a, b| a >= b, operands),
            Function::LessEq => self.apply_cmp(namespace, &|a, b| a <= b, operands),
            Function::Equals => self.apply_metric_cmp(namespace, &|a, b| a == b, operands),
            Function::NotEq => self.apply_metric_cmp(namespace, &|a, b| a != b, operands),
            Function::Max => self.fold_math(namespace, &|a, b| if a > b { a } else { b }, operands),
            Function::Min => self.fold_math(namespace, &|a, b| if a < b { a } else { b }, operands),
            Function::And => self.fold_bool(namespace, &|a, b| a && b, operands),
            Function::Or => self.fold_bool(namespace, &|a, b| a || b, operands),
            Function::Not => self.not_bool(namespace, operands),
        }
    }

    fn evaluate(&self, namespace: &str, e: &Expression) -> MetricValue {
        match e {
            Expression::Function(f, operands) => self.evaluate_function(namespace, f, operands),
            Expression::IsMissing(operands) => self.is_missing(namespace, operands),
            Expression::Metric(name) => self.metric_value_by_name(namespace, name),
            Expression::Value(value) => value.clone(),
        }
    }

    // Applies an operator (which should be associative and commutative) to a list of operands.
    fn fold_math(
        &self,
        namespace: &str,
        function: &dyn (Fn(f64, f64) -> f64),
        operands: &Vec<Expression>,
    ) -> MetricValue {
        if operands.len() == 0 {
            return MetricValue::Missing("No operands in math expression".into());
        }
        let mut result: MetricValue = self.evaluate(namespace, &operands[0]);
        for operand in operands[1..].iter() {
            result = self.apply_math(
                namespace,
                function,
                &vec![Expression::Value(result), operand.clone()],
            );
        }
        result
    }

    // Applies a given function to two values, handling type-promotion.
    // This function will return a MetricValue::Int if both values are ints
    // and a MetricValue::Float if not.
    fn apply_math(
        &self,
        namespace: &str,
        function: &dyn (Fn(f64, f64) -> f64),
        operands: &Vec<Expression>,
    ) -> MetricValue {
        extract_and_apply_math_operands!(self, namespace, function, operands, i64)
    }

    // Applies a given function to two values, handling type-promotion.
    // This function will always return a MetricValue::Float
    fn apply_math_f(
        &self,
        namespace: &str,
        function: &dyn (Fn(f64, f64) -> f64),
        operands: &Vec<Expression>,
    ) -> MetricValue {
        extract_and_apply_math_operands!(self, namespace, function, operands, f64)
    }

    fn extract_binary_operands(
        &self,
        namespace: &str,
        operands: &Vec<Expression>,
    ) -> Result<(MetricValue, MetricValue), MetricValue> {
        if operands.len() != 2 {
            return Err(MetricValue::Missing(format!(
                "Bad arg list {:?} for binary operator",
                operands
            )));
        }
        Ok((self.evaluate(namespace, &operands[0]), self.evaluate(namespace, &operands[1])))
    }

    // Applies an ord operator to two numbers. (>, >=, <, <=)
    fn apply_cmp(
        &self,
        namespace: &str,
        function: &dyn (Fn(f64, f64) -> bool),
        operands: &Vec<Expression>,
    ) -> MetricValue {
        if operands.len() != 2 {
            return MetricValue::Missing(format!(
                "Bad arg list {:?} for binary operator",
                operands
            ));
        }
        let result = match (
            self.evaluate(namespace, &operands[0]),
            self.evaluate(namespace, &operands[1]),
        ) {
            // TODO(cphoenix): Instead of converting two ints to float, use int functions.
            (MetricValue::Int(int1), MetricValue::Int(int2)) => function(int1 as f64, int2 as f64),
            (MetricValue::Int(int1), MetricValue::Float(float2)) => function(int1 as f64, float2),
            (MetricValue::Float(float1), MetricValue::Int(int2)) => function(float1, int2 as f64),
            (MetricValue::Float(float1), MetricValue::Float(float2)) => function(float1, float2),
            (value1, value2) => return demand_both_numeric(&value1, &value2),
        };
        MetricValue::Bool(result)
    }

    // Transitional Function to allow for string equality comparisons.
    // This function will eventually replace the apply_cmp function once MetricValue
    // implements the std::cmp::PartialOrd trait
    fn apply_metric_cmp(
        &self,
        namespace: &str,
        function: &dyn (Fn(&MetricValue, &MetricValue) -> bool),
        operands: &Vec<Expression>,
    ) -> MetricValue {
        if operands.len() != 2 {
            return MetricValue::Missing(format!(
                "Bad arg list {:?} for binary operator",
                operands
            ));
        }
        let left = self.evaluate(namespace, &operands[0]);
        let right = self.evaluate(namespace, &operands[1]);

        match (&left, &right) {
            // Check if either of the values is a `Missing` metric and pass
            // it along. This allows us to preserve error messaging.
            (MetricValue::Missing(_), _) | (_, MetricValue::Missing(_)) => {
                MetricValue::Missing(format!("{:?} or {:?} not comparable", &left, &right))
            }
            _ => MetricValue::Bool(function(&left, &right)),
        }
    }

    fn fold_bool(
        &self,
        namespace: &str,
        function: &dyn (Fn(bool, bool) -> bool),
        operands: &Vec<Expression>,
    ) -> MetricValue {
        if operands.len() == 0 {
            return MetricValue::Missing("No operands in boolean expression".into());
        }
        let mut result: bool = match self.evaluate(namespace, &operands[0]) {
            MetricValue::Bool(value) => value,
            bad => return MetricValue::Missing(format!("{:?} is not boolean", bad)),
        };
        for operand in operands[1..].iter() {
            result = match self.evaluate(namespace, operand) {
                MetricValue::Bool(value) => function(result, value),
                bad => return MetricValue::Missing(format!("{:?} is not boolean", bad)),
            }
        }
        MetricValue::Bool(result)
    }

    fn not_bool(&self, namespace: &str, operands: &Vec<Expression>) -> MetricValue {
        if operands.len() != 1 {
            return MetricValue::Missing(format!(
                "Wrong number of args ({}) for unary bool operator",
                operands.len()
            ));
        }
        match self.evaluate(namespace, &operands[0]) {
            MetricValue::Bool(true) => MetricValue::Bool(false),
            MetricValue::Bool(false) => MetricValue::Bool(true),
            bad => return MetricValue::Missing(format!("{:?} not boolean", bad)),
        }
    }

    // Returns Bool true if the given metric is Missing, false if the metric has a value.
    fn is_missing(&self, namespace: &str, operands: &Vec<Expression>) -> MetricValue {
        if operands.len() != 1 {
            return MetricValue::Missing(format!("Bad operand"));
        }
        match self.evaluate(namespace, &operands[0]) {
            MetricValue::Missing(_) => MetricValue::Bool(true),
            _ => MetricValue::Bool(false),
        }
    }
}

// The evaluation of math expressions is tested pretty exhaustively in parse.rs unit tests.

// The use of metric names in expressions and actions, with and without namespaces, is tested in
// the integration test.
//   $ fx triage --test
// TODO(cphoenix): Test metric names in unit tests also, since integration tests aren't
// run automatically.

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_equality() {
        // Equal Value, Equal Type
        assert_eq!(MetricValue::Int(1), MetricValue::Int(1));
        assert_eq!(MetricValue::Float(1.0), MetricValue::Float(1.0));
        assert_eq!(MetricValue::String("A".to_string()), MetricValue::String("A".to_string()));
        assert_eq!(MetricValue::Bool(true), MetricValue::Bool(true));
        assert_eq!(MetricValue::Bool(false), MetricValue::Bool(false));
        assert_eq!(
            MetricValue::Array(vec![
                MetricValue::Int(1),
                MetricValue::Float(1.0),
                MetricValue::String("A".to_string()),
                MetricValue::Bool(true),
            ]),
            MetricValue::Array(vec![
                MetricValue::Int(1),
                MetricValue::Float(1.0),
                MetricValue::String("A".to_string()),
                MetricValue::Bool(true),
            ])
        );

        assert_eq!(MetricValue::Int(1), MetricValue::Float(1.0));

        // Nested array
        assert_eq!(
            MetricValue::Array(vec![
                MetricValue::Int(1),
                MetricValue::Float(1.0),
                MetricValue::String("A".to_string()),
                MetricValue::Bool(true),
            ]),
            MetricValue::Array(vec![
                MetricValue::Int(1),
                MetricValue::Float(1.0),
                MetricValue::String("A".to_string()),
                MetricValue::Bool(true),
            ])
        );

        // Missing should never be equal
        assert_eq!(
            MetricValue::Missing("err".to_string()),
            MetricValue::Missing("err".to_string())
        );
    }

    #[test]
    fn test_inequality() {
        // Different Value, Equal Type
        assert_ne!(MetricValue::Int(1), MetricValue::Int(2));
        assert_ne!(MetricValue::Float(1.0), MetricValue::Float(2.0));
        assert_ne!(MetricValue::String("A".to_string()), MetricValue::String("B".to_string()));
        assert_ne!(MetricValue::Bool(true), MetricValue::Bool(false));
        assert_ne!(
            MetricValue::Array(vec![
                MetricValue::Int(1),
                MetricValue::Float(1.0),
                MetricValue::String("A".to_string()),
                MetricValue::Bool(true),
            ]),
            MetricValue::Array(vec![
                MetricValue::Int(2),
                MetricValue::Float(2.0),
                MetricValue::String("B".to_string()),
                MetricValue::Bool(false),
            ])
        );

        // Different Type
        assert_ne!(MetricValue::Int(2), MetricValue::Float(1.0));
        assert_ne!(MetricValue::Int(1), MetricValue::String("A".to_string()));
        assert_ne!(MetricValue::Int(1), MetricValue::Bool(true));
        assert_ne!(MetricValue::Float(1.0), MetricValue::String("A".to_string()));
        assert_ne!(MetricValue::Float(1.0), MetricValue::Bool(true));
        assert_ne!(MetricValue::String("A".to_string()), MetricValue::Bool(true));
    }

    #[test]
    fn test_fmt() {
        assert_eq!(format!("{}", MetricValue::Int(3)), "Int(3)");
        assert_eq!(format!("{}", MetricValue::Float(3.5)), "Float(3.5)");
        assert_eq!(format!("{}", MetricValue::Bool(true)), "Bool(true)");
        assert_eq!(format!("{}", MetricValue::Bool(false)), "Bool(false)");
        assert_eq!(format!("{}", MetricValue::String("cat".to_string())), "String(cat)");
        assert_eq!(
            format!("{}", MetricValue::Array(vec![MetricValue::Int(1), MetricValue::Float(2.5)])),
            "Array([Int(1), Float(2.5)])"
        );
        assert_eq!(format!("{}", MetricValue::Bytes(vec![1u8, 2u8])), "Bytes([1, 2])");
        assert_eq!(
            format!("{}", MetricValue::Missing("Where is Waldo?".to_string())),
            "Missing(Where is Waldo?)"
        );
    }
}
