// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod fetch;

use {
    super::config,
    serde_derive::Deserialize,
    serde_json as json,
    std::{clone::Clone, collections::HashMap},
};

/// [Metrics] are a map from namespaces to the named metrics stored within that namespace.
pub type Metrics = HashMap<String, MetricsSchema>;
pub type MetricsSchema = HashMap<String, Metric>;

/// Contains all the information needed to look up and evaluate a Metric - other
/// Metrics that may be referred to, and Inspect data (entries for each
/// component) that can be accessed by Selector-type Metrics.
pub struct MetricState<'a> {
    pub metrics: &'a Metrics,
    pub inspect_entries: &'a Vec<json::Value>,
}

/// The contents of a single Metric. Metrics produce a value for use in Actions or other Metrics.
#[derive(Deserialize, Clone, Debug)]
pub enum Metric {
    /// Selector tells where to find a value in the Inspect data.
    Selector(String),
    /// Eval contains an arithmetic expression,
    // TODO(cphoenix): Parse and validate this at load-time.
    Eval(String),
}

/// The calculated or selected value of a Metric.
///
/// Missing means that the value could not be calculated; its String tells
/// the reason. Array and String are not used in v0.1 but will be useful later.
#[derive(Deserialize, Debug, Clone, PartialEq)]
pub enum MetricValue {
    // TODO(cphoenix): Support u64.
    Int(i64),
    Float(f64),
    String(String),
    Bool(bool),
    Array(Vec<MetricValue>),
    Missing(String),
}

/// Expression represents the parsed body of an Eval Metric. It applies
/// one of several operators to sub-expressions, or stores the name of a
/// Metric or a basic Value.
#[derive(Deserialize, Debug, Clone, PartialEq)]
pub enum Expression {
    // Some operators have arity 1 or 2, some have arity N.
    // For symmetry/readability, I use the same operand-spec Vec<Expression> for all.
    // TODO(cphoenix): Check on load that all operators have a legal number of operands.
    Add(Vec<Expression>),
    Sub(Vec<Expression>),
    Mul(Vec<Expression>),
    Div(Vec<Expression>),
    Greater(Vec<Expression>),
    Less(Vec<Expression>),
    GreaterEq(Vec<Expression>),
    LessEq(Vec<Expression>),
    Equals(Vec<Expression>),
    NotEq(Vec<Expression>),
    Max(Vec<Expression>),
    Min(Vec<Expression>),
    And(Vec<Expression>),
    Or(Vec<Expression>),
    Not(Vec<Expression>),
    IsMissing(Vec<Expression>),
    Metric(String),
    Value(MetricValue),
}

impl<'a> MetricState<'a> {
    /// Create an initialized MetricState.
    pub fn new(metrics: &'a Metrics, inspect_entries: &'a Vec<json::Value>) -> MetricState<'a> {
        MetricState { metrics, inspect_entries }
    }

    /// Calculate the value of a Metric specified by name and namespace.
    ///
    /// If [name] is of the form "namespace::name" then [namespace] is ignored.
    /// If [name] is just "name" then [namespace] is used.
    pub fn metric_value(&self, namespace: &String, name: &String) -> MetricValue {
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
                Some(metric) => match metric {
                    Metric::Selector(selector) => fetch::fetch(&self.inspect_entries, &selector),
                    Metric::Eval(expression) => match config::parse::parse_expression(expression) {
                        Ok(expr) => self.evaluate(namespace, &expr),
                        Err(e) => MetricValue::Missing(format!("Expression parse error\n{}", e)),
                    },
                },
            },
        }
    }

    /// Evaluate an Expression which contains only base values, not referring to other Metrics.
    #[cfg(test)]
    pub fn evaluate_math(e: &Expression) -> MetricValue {
        MetricState::new(&HashMap::new(), &vec![]).evaluate(&"".to_string(), e)
    }

    fn evaluate(&self, namespace: &String, e: &Expression) -> MetricValue {
        match e {
            Expression::Add(operands) => self.fold_math(namespace, &|a, b| a + b, operands),
            Expression::Sub(operands) => self.apply_math(namespace, &|a, b| a - b, operands),
            Expression::Mul(operands) => self.fold_math(namespace, &|a, b| a * b, operands),
            Expression::Div(operands) => self.apply_math(namespace, &|a, b| a / b, operands),
            Expression::Greater(operands) => self.apply_cmp(namespace, &|a, b| a > b, operands),
            Expression::Less(operands) => self.apply_cmp(namespace, &|a, b| a < b, operands),
            Expression::GreaterEq(operands) => self.apply_cmp(namespace, &|a, b| a >= b, operands),
            Expression::LessEq(operands) => self.apply_cmp(namespace, &|a, b| a <= b, operands),
            Expression::Equals(operands) => self.apply_cmp(namespace, &|a, b| a == b, operands),
            Expression::NotEq(operands) => self.apply_cmp(namespace, &|a, b| a != b, operands),
            Expression::Max(operands) => {
                self.fold_math(namespace, &|a, b| if a > b { a } else { b }, operands)
            }
            Expression::Min(operands) => {
                self.fold_math(namespace, &|a, b| if a < b { a } else { b }, operands)
            }
            Expression::And(operands) => self.fold_bool(namespace, &|a, b| a && b, operands),
            Expression::Or(operands) => self.fold_bool(namespace, &|a, b| a || b, operands),
            Expression::Not(operands) => self.not_bool(namespace, operands),
            Expression::IsMissing(operands) => self.is_missing(namespace, operands),
            Expression::Metric(name) => self.metric_value(namespace, name),
            Expression::Value(value) => value.clone(),
        }
    }

    // Applies an operator (which should be associative and commutative) to a list of operands.
    fn fold_math(
        &self,
        namespace: &String,
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
    fn apply_math(
        &self,
        namespace: &String,
        function: &dyn (Fn(f64, f64) -> f64),
        operands: &Vec<Expression>,
    ) -> MetricValue {
        if operands.len() != 2 {
            return MetricValue::Missing(format!(
                "Bad arg list {:?} for binary operator",
                operands
            ));
        }
        match (self.evaluate(namespace, &operands[0]), self.evaluate(namespace, &operands[1])) {
            (MetricValue::Int(int1), MetricValue::Int(int2)) => {
                // TODO(cphoenix): Instead of converting to float, use int functions.
                MetricValue::Int(function(int1 as f64, int2 as f64) as i64)
            }
            (MetricValue::Int(int1), MetricValue::Float(float2)) => {
                MetricValue::Float(function(int1 as f64, float2))
            }
            (MetricValue::Float(float1), MetricValue::Int(int2)) => {
                MetricValue::Float(function(float1, int2 as f64))
            }
            (MetricValue::Float(float1), MetricValue::Float(float2)) => {
                MetricValue::Float(function(float1, float2))
            }
            (bad1, bad2) => MetricValue::Missing(format!("{:?} or {:?} not numeric", bad1, bad2)),
        }
    }

    // Applies a comparison operator to two numbers.
    fn apply_cmp(
        &self,
        namespace: &String,
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
            (bad1, bad2) => {
                return MetricValue::Missing(format!("{:?} or {:?} not numeric", bad1, bad2))
            }
        };
        MetricValue::Bool(result)
    }

    fn fold_bool(
        &self,
        namespace: &String,
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

    fn not_bool(&self, namespace: &String, operands: &Vec<Expression>) -> MetricValue {
        if operands.len() != 1 {
            return MetricValue::Missing(format!(
                "Bad arg list {:?} for binary bool operator",
                operands
            ));
        }
        match self.evaluate(namespace, &operands[0]) {
            MetricValue::Bool(true) => MetricValue::Bool(false),
            MetricValue::Bool(false) => MetricValue::Bool(true),
            bad => return MetricValue::Missing(format!("{:?} not boolean", bad)),
        }
    }

    // Returns Bool true if the given metric is Missing, false if the metric has a value.
    fn is_missing(&self, namespace: &String, operands: &Vec<Expression>) -> MetricValue {
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
