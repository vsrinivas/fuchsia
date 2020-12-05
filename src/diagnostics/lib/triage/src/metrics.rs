// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub(crate) mod arithmetic;
pub(crate) mod fetch;
pub(crate) mod metric_value;
pub(crate) mod parse;
pub(crate) mod variable;

use {
    fetch::{Fetcher, FileDataFetcher, SelectorString, TrialDataFetcher},
    metric_value::MetricValue,
    serde::{Deserialize, Deserializer},
    std::{clone::Clone, cmp::min, collections::HashMap, convert::TryFrom},
    variable::VariableName,
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
/// [Metric]s that may be referred to, and a source of input values to calculate on.
///
/// Note: MetricState uses a single Now() value for all evaluations. If a MetricState is
/// retained and used for multiple evaluations at different times, provide a way to update
/// the `now` field.
pub struct MetricState<'a> {
    pub metrics: &'a Metrics,
    pub fetcher: Fetcher<'a>,
    now: Option<i64>,
}

#[derive(Deserialize, Debug, Clone, PartialEq)]
pub enum MathFunction {
    Add,
    Sub,
    Mul,
    FloatDiv,
    IntDiv,
    Greater,
    Less,
    GreaterEq,
    LessEq,
    Max,
    Min,
}

#[derive(Deserialize, Debug, Clone, PartialEq)]
pub enum Function {
    Math(MathFunction),
    // Equals and NotEq can apply to bools and strings, and handle int/float without needing
    // the mechanisms in mod arithmetic.
    Equals,
    NotEq,
    And,
    Or,
    Not,
    KlogHas,
    SyslogHas,
    BootlogHas,
    Missing,
    Annotation,
    Lambda,
    Apply,
    Map,
    Fold,
    Filter,
    Count,
    Nanos,
    Micros,
    Millis,
    Seconds,
    Minutes,
    Hours,
    Days,
    Now,
    OptionF,
}

/// Lambda stores a function; its parameters and body are evaluated lazily.
/// Lambda's are created by evaluating the "Fn()" expression.
#[derive(Deserialize, Debug, Clone)]
pub struct Lambda {
    parameters: Vec<String>,
    body: Expression,
}

impl Lambda {
    fn valid_parameters(parameters: &Expression) -> Result<Vec<String>, MetricValue> {
        match parameters {
            Expression::Vector(parameters) => parameters
                .iter()
                .map(|param| match param {
                    Expression::Variable(name) => {
                        if name.includes_namespace() {
                            Err(MetricValue::Missing(
                                "Namespaces not allowed in function params".to_string(),
                            ))
                        } else {
                            Ok(name.original_name().to_string())
                        }
                    }
                    _ => Err(MetricValue::Missing(
                        "Function params must be valid identifier names".to_string(),
                    )),
                })
                .collect::<Result<Vec<_>, _>>(),
            _ => {
                return Err(MetricValue::Missing(
                    "Function params must be a vector of names".to_string(),
                ))
            }
        }
    }

    fn as_metric_value(definition: &Vec<Expression>) -> MetricValue {
        if definition.len() != 2 {
            return MetricValue::Missing(
                "Function needs two parameters, list of params and expression".to_string(),
            );
        }
        let parameters = match Self::valid_parameters(&definition[0]) {
            Ok(names) => names,
            Err(problem) => return problem,
        };
        let body = definition[1].clone();
        MetricValue::Lambda(Box::new(Lambda { parameters, body }))
    }
}

// Behavior for short circuiting execution when applying operands.
#[derive(Copy, Clone, Debug)]
enum ShortCircuitBehavior {
    // Short circuit when the first true value is found.
    True,
    // Short circuit when the first false value is found.
    False,
}

/// Expression represents the parsed body of an Eval Metric. It applies
/// a function to sub-expressions, or stores a Missing error, the name of a
/// Metric, a vector of expressions, or a basic Value.
#[derive(Deserialize, Debug, Clone, PartialEq)]
pub enum Expression {
    // Some operators have arity 1 or 2, some have arity N.
    // For symmetry/readability, I use the same operand-spec Vec<Expression> for all.
    // TODO(cphoenix): Check on load that all operators have a legal number of operands.
    Function(Function, Vec<Expression>),
    Vector(Vec<Expression>),
    Variable(VariableName),
    Value(MetricValue),
}

// Selectors return a vec of values. Typically they will select a single
// value which we want to use for math without lots of boilerplate.
// So we "promote" a 1-entry vector into the value it contains.
// Other vectors will be passed through unchanged (and cause an error later).
fn unwrap_for_math<'b>(value: &'b MetricValue) -> &'b MetricValue {
    match value {
        MetricValue::Vector(v) if v.len() == 1 => &v[0],
        v => v,
    }
}

// Condense the visual clutter of iter() and collect::<Vec<_>>().
fn map_vec<T, U, F>(vec: &Vec<T>, f: F) -> Vec<U>
where
    F: FnMut(&T) -> U,
{
    vec.iter().map(f).collect::<Vec<U>>()
}

// Condense visual clutter of iterating over vec of references.
fn map_vec_r<'a, T, U, F>(vec: &'a [T], f: F) -> Vec<&'a U>
where
    F: FnMut(&'a T) -> &'a U,
{
    vec.iter().map(f).collect::<Vec<&'a U>>()
}

// Construct Missing() metric from a message
fn missing(message: &str) -> MetricValue {
    MetricValue::Missing(message.to_string())
}

pub fn safe_float_to_int(float: f64) -> Option<i64> {
    if !float.is_finite() {
        return None;
    }
    if float > i64::MAX as f64 {
        return Some(i64::MAX);
    }
    if float < i64::MIN as f64 {
        return Some(i64::MIN);
    }
    Some(float as i64)
}

impl<'a> MetricState<'a> {
    /// Create an initialized MetricState.
    pub fn new(metrics: &'a Metrics, fetcher: Fetcher<'a>, now: Option<i64>) -> MetricState<'a> {
        MetricState { metrics, fetcher, now }
    }

    /// Any [name] found in the trial's "values" uses the corresponding value, regardless of
    /// whether it is a Selector or Eval Metric, and regardless of whether it includes
    /// a namespace; the string match must be exact.
    /// If not found in "values" the name must be an Eval metric from the current file.
    fn metric_value_for_trial(
        &self,
        fetcher: &TrialDataFetcher<'_>,
        namespace: &str,
        variable: &VariableName,
    ) -> MetricValue {
        let name = variable.original_name();
        if fetcher.has_entry(name) {
            return fetcher.fetch(name);
        }
        if variable.includes_namespace() {
            return MetricValue::Missing(format!(
                "Name {} not in test values and refers outside the file",
                name
            ));
        }
        match self.metrics.get(namespace) {
            None => return MetricValue::Missing(format!("BUG! Bad namespace '{}'", namespace)),
            Some(metric_map) => match metric_map.get(name) {
                None => {
                    return MetricValue::Missing(format!(
                        "Metric '{}' Not Found in '{}'",
                        name, namespace
                    ))
                }
                Some(metric) => match metric {
                    Metric::Selector(_) => MetricValue::Missing(format!(
                        "Selector {} can't be used in tests; please supply a value",
                        name
                    )),
                    Metric::Eval(expression) => self.evaluate_value(namespace, &expression),
                },
            },
        }
    }

    /// If [name] is of the form "namespace::name" then [namespace] is ignored.
    /// If [name] is just "name" then [namespace] is used to look up the Metric.
    fn metric_value_for_file(
        &self,
        fetcher: &FileDataFetcher<'_>,
        namespace: &str,
        name: &VariableName,
    ) -> MetricValue {
        if let Some((real_namespace, real_name)) = name.name_parts(namespace) {
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
                        Metric::Selector(selector) => fetcher.fetch(selector),
                        Metric::Eval(expression) => {
                            self.evaluate_value(real_namespace, &expression)
                        }
                    },
                },
            }
        } else {
            return MetricValue::Missing(format!("Bad name '{}'", name.original_name()));
        }
    }

    /// Calculate the value of a Metric specified by name and namespace.
    fn evaluate_variable(&self, namespace: &str, name: &VariableName) -> MetricValue {
        // TODO(cphoenix): When historical metrics are added, change semantics to refresh()
        // TODO(cphoenix): cache values
        // TODO(cphoenix): Detect infinite cycles/depth.
        // TODO(cphoenix): Improve the data structure on Metric names. Probably fill in
        //  namespace during parse.
        match &self.fetcher {
            Fetcher::FileData(fetcher) => self.metric_value_for_file(fetcher, namespace, name),
            Fetcher::TrialData(fetcher) => self.metric_value_for_trial(fetcher, namespace, name),
        }
    }

    /// Fetch or compute the value of a Metric expression from an action.
    pub fn eval_action_metric(&self, namespace: &str, metric: &Metric) -> MetricValue {
        match metric {
            Metric::Selector(_) => {
                MetricValue::Missing("Selectors aren't allowed in action triggers".to_owned())
            }
            Metric::Eval(string) => {
                unwrap_for_math(&self.evaluate_value(namespace, string)).clone()
            }
        }
    }

    fn evaluate_value(&self, namespace: &str, expression: &str) -> MetricValue {
        match parse::parse_expression(expression) {
            Ok(expr) => self.evaluate(namespace, &expr),
            Err(e) => MetricValue::Missing(format!("Expression parse error\n{}", e)),
        }
    }

    /// Evaluate an Expression which contains only base values, not referring to other Metrics.
    pub fn evaluate_math(expr: &str) -> MetricValue {
        let parsed = match parse::parse_expression(expr) {
            Ok(p) => p,
            Err(err) => {
                return MetricValue::Missing(format!("Failed to parse '{}': {}", expr, err))
            }
        };
        let values = HashMap::new();
        let fetcher = Fetcher::TrialData(TrialDataFetcher::new(&values));
        let files = HashMap::new();
        let metric_state = MetricState::new(&files, fetcher, None);
        metric_state.evaluate(&"".to_string(), &parsed)
    }

    #[cfg(test)]
    pub fn evaluate_expression(&self, e: &Expression) -> MetricValue {
        self.evaluate(&"".to_string(), e)
    }

    fn evaluate_function(
        &self,
        namespace: &str,
        function: &Function,
        operands: &Vec<Expression>,
    ) -> MetricValue {
        match function {
            Function::Math(operation) => arithmetic::calculate(
                operation,
                &map_vec(operands, |o| self.evaluate(namespace, o)),
            ),
            Function::Equals => self.apply_boolean_function(namespace, &|a, b| a == b, operands),
            Function::NotEq => self.apply_boolean_function(namespace, &|a, b| a != b, operands),
            Function::And => {
                self.fold_bool(namespace, &|a, b| a && b, operands, ShortCircuitBehavior::False)
            }
            Function::Or => {
                self.fold_bool(namespace, &|a, b| a || b, operands, ShortCircuitBehavior::True)
            }
            Function::Not => self.not_bool(namespace, operands),
            Function::KlogHas | Function::SyslogHas | Function::BootlogHas => {
                self.log_contains(function, namespace, operands)
            }
            Function::Missing => self.is_missing(namespace, operands),
            Function::Annotation => self.annotation(namespace, operands),
            Function::Lambda => Lambda::as_metric_value(operands),
            Function::Apply => self.apply(namespace, operands),
            Function::Map => self.map(namespace, operands),
            Function::Fold => self.fold(namespace, operands),
            Function::Filter => self.filter(namespace, operands),
            Function::Count => self.count(namespace, operands),
            Function::Nanos => self.time(namespace, operands, 1),
            Function::Micros => self.time(namespace, operands, 1_000),
            Function::Millis => self.time(namespace, operands, 1_000_000),
            Function::Seconds => self.time(namespace, operands, 1_000_000_000),
            Function::Minutes => self.time(namespace, operands, 1_000_000_000 * 60),
            Function::Hours => self.time(namespace, operands, 1_000_000_000 * 60 * 60),
            Function::Days => self.time(namespace, operands, 1_000_000_000 * 60 * 60 * 24),
            Function::Now => self.now(operands),
            Function::OptionF => self.option(namespace, operands),
        }
    }

    fn option(&self, namespace: &str, operands: &Vec<Expression>) -> MetricValue {
        for op in operands.iter() {
            match self.evaluate(namespace, op) {
                MetricValue::Missing(_) => {}
                value => return value,
            }
        }
        // This will be improved when we get structured output and structured errors.
        return missing("Every value was missing");
    }

    fn now(&self, operands: &'a [Expression]) -> MetricValue {
        if !operands.is_empty() {
            return missing("Now() requires no operands.");
        }
        match self.now {
            Some(time) => MetricValue::Int(time),
            None => missing("No valid time available"),
        }
    }

    fn apply_lambda(&self, namespace: &str, lambda: &Lambda, args: &[&MetricValue]) -> MetricValue {
        fn substitute_all(
            expressions: &[Expression],
            bindings: &HashMap<String, &MetricValue>,
        ) -> Vec<Expression> {
            expressions.iter().map(|e| substitute(e, bindings)).collect::<Vec<_>>()
        }

        fn substitute(
            expression: &Expression,
            bindings: &HashMap<String, &MetricValue>,
        ) -> Expression {
            match expression {
                Expression::Function(function, expressions) => {
                    Expression::Function(function.clone(), substitute_all(expressions, bindings))
                }
                Expression::Vector(expressions) => {
                    Expression::Vector(substitute_all(expressions, bindings))
                }
                Expression::Variable(name) => {
                    if let Some(value) = bindings.get(name.original_name()) {
                        Expression::Value((*value).clone())
                    } else {
                        Expression::Variable(name.clone())
                    }
                }
                Expression::Value(value) => Expression::Value(value.clone()),
            }
        }

        let parameters = &lambda.parameters;
        if parameters.len() != args.len() {
            return MetricValue::Missing(format!(
                "Function has {} parameters and needs {} arguments, but has {}.",
                parameters.len(),
                parameters.len(),
                args.len()
            ));
        }
        let mut bindings = HashMap::new();
        for (name, value) in parameters.iter().zip(args.iter()) {
            bindings.insert(name.clone(), value.clone());
        }
        let expression = substitute(&lambda.body, &bindings);
        self.evaluate(namespace, &expression)
    }

    fn unpack_lambda(
        &self,
        namespace: &str,
        operands: &'a [Expression],
    ) -> Result<(Box<Lambda>, Vec<MetricValue>), ()> {
        if operands.len() == 0 {
            return Err(());
        }
        let lambda = match self.evaluate(namespace, &operands[0]) {
            MetricValue::Lambda(lambda) => lambda,
            _ => return Err(()),
        };
        let arguments =
            operands[1..].iter().map(|expr| self.evaluate(namespace, expr)).collect::<Vec<_>>();
        Ok((lambda, arguments))
    }

    /// This implements the Apply() function.
    fn apply(&self, namespace: &str, operands: &[Expression]) -> MetricValue {
        let (lambda, arguments) = match self.unpack_lambda(namespace, operands) {
            Ok((lambda, arguments)) => (lambda, arguments),
            Err(()) => return missing("Apply needs a function in its first argument."),
        };
        self.apply_lambda(namespace, &lambda, &arguments.iter().collect::<Vec<_>>())
    }

    /// This implements the Map() function.
    fn map(&self, namespace: &str, operands: &[Expression]) -> MetricValue {
        let (lambda, arguments) = match self.unpack_lambda(namespace, operands) {
            Ok((lambda, arguments)) => (lambda, arguments),
            Err(()) => return missing("Map needs a function in its first argument."),
        };
        let vector_args = arguments
            .iter()
            .filter(|item| match item {
                MetricValue::Vector(_) => true,
                _ => false,
            })
            .collect::<Vec<_>>();
        let result_length = match vector_args.len() {
            0 => 0,
            _ => {
                let start = match vector_args[0] {
                    MetricValue::Vector(vec) => vec.len(),
                    _ => unreachable!(),
                };
                vector_args.iter().fold(start, |accum, item| {
                    min(
                        accum,
                        match item {
                            MetricValue::Vector(items) => items.len(),
                            _ => 0,
                        },
                    )
                })
            }
        };
        let mut result = Vec::new();
        for index in 0..result_length {
            let call_args = arguments
                .iter()
                .map(|arg| match arg {
                    MetricValue::Vector(vec) => &vec[index],
                    other => other,
                })
                .collect::<Vec<_>>();
            result.push(self.apply_lambda(namespace, &lambda, &call_args));
        }
        MetricValue::Vector(result)
    }

    /// This implements the Fold() function.
    fn fold(&self, namespace: &str, operands: &[Expression]) -> MetricValue {
        let (lambda, arguments) = match self.unpack_lambda(namespace, operands) {
            Ok((lambda, arguments)) => (lambda, arguments),
            Err(()) => return missing("Fold needs a function as the first argument."),
        };
        if arguments.is_empty() {
            return missing("Fold needs a second argument, a vector");
        }
        let vector = match &arguments[0] {
            MetricValue::Vector(items) => items,
            _ => return missing("Second argument of Fold must be a vector"),
        };
        let (first, rest) = match arguments.len() {
            1 => match vector.split_first() {
                Some(first_rest) => first_rest,
                None => return missing("Fold needs at least one value"),
            },
            2 => (&arguments[1], &vector[..]),
            _ => return missing("Fold needs (function, vec) or (function, vec, start)"),
        };
        let mut result = first.clone();
        for item in rest {
            result = self.apply_lambda(namespace, &lambda, &[&result, item]);
        }
        result
    }

    /// This implements the Filter() function.
    fn filter(&self, namespace: &str, operands: &[Expression]) -> MetricValue {
        let (lambda, arguments) = match self.unpack_lambda(namespace, operands) {
            Ok((lambda, arguments)) => (lambda, arguments),
            Err(()) => return missing("Filter needs a function"),
        };
        if arguments.len() != 1 {
            return missing("Filter needs (function, vector)");
        }
        let result = match &arguments[0] {
            MetricValue::Vector(items) => items
                .iter()
                .filter_map(|item| match self.apply_lambda(namespace, &lambda, &[&item]) {
                    MetricValue::Bool(true) => Some(item.clone()),
                    MetricValue::Bool(false) => None,
                    MetricValue::Missing(message) => Some(MetricValue::Missing(message.clone())),
                    bad_type => Some(MetricValue::Missing(format!(
                        "Bad value {:?} from filter function should be true, false, or Missing",
                        bad_type
                    ))),
                })
                .collect(),
            _ => return missing("Filter second argument must be a vector"),
        };
        MetricValue::Vector(result)
    }

    /// This implements the Count() function.
    fn count(&self, namespace: &str, operands: &[Expression]) -> MetricValue {
        if operands.len() != 1 {
            return missing("Count requires one argument, a vector");
        }
        match self.evaluate(namespace, &operands[0]) {
            MetricValue::Vector(items) => MetricValue::Int(items.len() as i64),
            bad => MetricValue::Missing(format!("Count only works on vectors, not {}", bad)),
        }
    }

    /// This implements the time-conversion functions.
    fn time(&self, namespace: &str, operands: &[Expression], multiplier: i64) -> MetricValue {
        if operands.len() != 1 {
            return missing("Time conversion needs 1 numeric argument");
        }
        match self.evaluate(namespace, &operands[0]) {
            MetricValue::Int(value) => MetricValue::Int(value * multiplier),
            MetricValue::Float(value) => match safe_float_to_int(value * (multiplier as f64)) {
                None => missing("Time conversion needs 1 numeric argument"),
                Some(value) => MetricValue::Int(value),
            },
            _ => missing("Time conversion needs 1 numeric argument"),
        }
    }

    fn evaluate(&self, namespace: &str, e: &Expression) -> MetricValue {
        match e {
            Expression::Function(f, operands) => self.evaluate_function(namespace, f, operands),
            Expression::Variable(name) => self.evaluate_variable(namespace, name),
            Expression::Value(value) => value.clone(),
            Expression::Vector(values) => {
                MetricValue::Vector(map_vec(values, |value| self.evaluate(namespace, value)))
            }
        }
    }

    fn annotation(&self, namespace: &str, operands: &Vec<Expression>) -> MetricValue {
        if operands.len() != 1 {
            return MetricValue::Missing("Annotation() needs 1 string argument".to_string());
        }
        match self.evaluate(namespace, &operands[0]) {
            MetricValue::String(string) => match &self.fetcher {
                Fetcher::TrialData(fetcher) => fetcher.annotations,
                Fetcher::FileData(fetcher) => fetcher.annotations,
            }
            .fetch(&string),
            _ => MetricValue::Missing("Annotation() needs a string argument".to_string()),
        }
    }

    fn log_contains(
        &self,
        log_type: &Function,
        namespace: &str,
        operands: &Vec<Expression>,
    ) -> MetricValue {
        let log_data = match &self.fetcher {
            Fetcher::TrialData(fetcher) => match log_type {
                Function::KlogHas => fetcher.klog,
                Function::SyslogHas => fetcher.syslog,
                Function::BootlogHas => fetcher.bootlog,
                _ => {
                    return MetricValue::Missing(
                        "Internal error, log_contains with non-log function".to_string(),
                    )
                }
            },
            Fetcher::FileData(fetcher) => match log_type {
                Function::KlogHas => fetcher.klog,
                Function::SyslogHas => fetcher.syslog,
                Function::BootlogHas => fetcher.bootlog,
                _ => {
                    return MetricValue::Missing(
                        "Internal error, log_contains with non-log function".to_string(),
                    )
                }
            },
        };
        if operands.len() != 1 {
            return MetricValue::Missing(
                "Log matcher must use exactly 1 argument, an RE string.".into(),
            );
        }
        match self.evaluate(namespace, &operands[0]) {
            MetricValue::String(re) => MetricValue::Bool(log_data.contains(&re)),
            _ => MetricValue::Missing("Log matcher needs a string (RE).".into()),
        }
    }

    fn apply_boolean_function(
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
        let operand_values = map_vec(&operands, |operand| self.evaluate(namespace, operand));
        let args = map_vec_r(&operand_values, |operand| unwrap_for_math(operand));
        match (args[0], args[1]) {
            // We forward ::Missing for better error messaging.
            (MetricValue::Missing(reason), _) => MetricValue::Missing(reason.to_string()),
            (_, MetricValue::Missing(reason)) => MetricValue::Missing(reason.to_string()),
            _ => MetricValue::Bool(function(args[0], args[1])),
        }
    }

    fn fold_bool(
        &self,
        namespace: &str,
        function: &dyn (Fn(bool, bool) -> bool),
        operands: &Vec<Expression>,
        short_circuit_behavior: ShortCircuitBehavior,
    ) -> MetricValue {
        if operands.len() == 0 {
            return MetricValue::Missing("No operands in boolean expression".into());
        }
        let first = self.evaluate(namespace, &operands[0]);
        let mut result: bool = match unwrap_for_math(&first) {
            MetricValue::Bool(value) => *value,
            MetricValue::Missing(reason) => {
                return MetricValue::Missing(reason.to_string());
            }
            bad => return MetricValue::Missing(format!("{:?} is not boolean", bad)),
        };
        for operand in operands[1..].iter() {
            match (result, short_circuit_behavior) {
                (true, ShortCircuitBehavior::True) => {
                    break;
                }
                (false, ShortCircuitBehavior::False) => {
                    break;
                }
                _ => {}
            };
            let nth = self.evaluate(namespace, operand);
            result = match unwrap_for_math(&nth) {
                MetricValue::Bool(value) => function(result, *value),
                MetricValue::Missing(reason) => {
                    return MetricValue::Missing(reason.to_string());
                }
                bad => return MetricValue::Missing(format!("{:?} is not boolean", bad)),
            }
        }
        MetricValue::Bool(result)
    }

    fn not_bool(&self, namespace: &str, operands: &Vec<Expression>) -> MetricValue {
        if operands.len() != 1 {
            return MetricValue::Missing(format!(
                "Wrong number of arguments ({}) for unary bool operator",
                operands.len()
            ));
        }
        match unwrap_for_math(&self.evaluate(namespace, &operands[0])) {
            MetricValue::Bool(true) => MetricValue::Bool(false),
            MetricValue::Bool(false) => MetricValue::Bool(true),
            MetricValue::Missing(reason) => {
                return MetricValue::Missing(reason.to_string());
            }
            bad => return MetricValue::Missing(format!("{:?} not boolean", bad)),
        }
    }

    // Returns Bool true if the given metric is Missing, false if the metric has a value.
    fn is_missing(&self, namespace: &str, operands: &Vec<Expression>) -> MetricValue {
        if operands.len() != 1 {
            return MetricValue::Missing(format!("Bad operand"));
        }
        MetricValue::Bool(match self.evaluate(namespace, &operands[0]) {
            MetricValue::Missing(_) => true,
            // TODO(fxbug.dev/58922): Well-designed errors and special cases, not hacks
            MetricValue::Vector(contents) if contents.len() == 0 => true,
            MetricValue::Vector(contents) if contents.len() == 1 => match contents[0] {
                MetricValue::Missing(_) => true,
                _ => false,
            },
            _ => false,
        })
    }
}

// The evaluation of math expressions is tested pretty exhaustively in parse.rs unit tests.

// The use of metric names in expressions and actions, with and without namespaces, is tested in
// the integration test.
//   $ fx test triage_lib_test

#[cfg(test)]
pub(crate) mod test {
    use super::*;
    use {
        crate::config::{DiagnosticData, Source},
        anyhow::Error,
        lazy_static::lazy_static,
    };

    /// Missing should never equal anything, even an identical Missing. Code (tests) can use
    /// assert_missing!(MetricValue::Missing("foo".to_string()), "foo") to test error messages.
    #[macro_export]
    macro_rules! assert_missing {
        ($missing:expr, $message:expr) => {
            match $missing {
                MetricValue::Missing(actual_message) => assert_eq!(&actual_message, $message),
                _ => assert!(false, "Non-Missing type"),
            }
        };
    }

    lazy_static! {
        static ref EMPTY_F: Vec<DiagnosticData> = {
            let s = r#"[]"#;
            vec![DiagnosticData::new("i".to_string(), Source::Inspect, s.to_string()).unwrap()]
        };
        static ref NO_PAYLOAD_F: Vec<DiagnosticData> = {
            let s = r#"[{"moniker": "abcd", "payload": null}]"#;
            vec![DiagnosticData::new("i".to_string(), Source::Inspect, s.to_string()).unwrap()]
        };
        static ref EMPTY_FILE_FETCHER: FileDataFetcher<'static> = FileDataFetcher::new(&EMPTY_F);
        static ref NO_PAYLOAD_FETCHER: FileDataFetcher<'static> =
            FileDataFetcher::new(&NO_PAYLOAD_F);
    }

    #[test]
    fn logs_work() -> Result<(), Error> {
        let syslog_text = "line 1\nline 2\nsyslog".to_string();
        let klog_text = "first line\nsecond line\nklog\n".to_string();
        let bootlog_text = "Yes there's a bootlog with one long line".to_string();
        let syslog = DiagnosticData::new("sys".to_string(), Source::Syslog, syslog_text)?;
        let klog = DiagnosticData::new("k".to_string(), Source::Klog, klog_text)?;
        let bootlog = DiagnosticData::new("boot".to_string(), Source::Bootlog, bootlog_text)?;
        let metrics = HashMap::new();
        let mut data = vec![klog, syslog, bootlog];
        let fetcher = FileDataFetcher::new(&data);
        let state = MetricState::new(&metrics, Fetcher::FileData(fetcher), None);
        assert_eq!(state.evaluate_value("", r#"KlogHas("lin")"#), MetricValue::Bool(true));
        assert_eq!(state.evaluate_value("", r#"KlogHas("l.ne")"#), MetricValue::Bool(true));
        assert_eq!(state.evaluate_value("", r#"KlogHas("fi.*ne")"#), MetricValue::Bool(true));
        assert_eq!(state.evaluate_value("", r#"KlogHas("fi.*sec")"#), MetricValue::Bool(false));
        assert_eq!(state.evaluate_value("", r#"KlogHas("first line")"#), MetricValue::Bool(true));
        // Full regex; even capture groups are allowed but the values can't be extracted.
        assert_eq!(
            state.evaluate_value("", r#"KlogHas("f(.)rst \bline")"#),
            MetricValue::Bool(true)
        );
        // Backreferences don't work; this is regex, not fancy_regex.
        assert_eq!(
            state.evaluate_value("", r#"KlogHas("f(.)rst \bl\1ne")"#),
            MetricValue::Bool(false)
        );
        assert_eq!(state.evaluate_value("", r#"KlogHas("second line")"#), MetricValue::Bool(true));
        assert_eq!(
            state.evaluate_value("", "KlogHas(\"second line\n\")"),
            MetricValue::Bool(false)
        );
        assert_eq!(state.evaluate_value("", r#"KlogHas("klog")"#), MetricValue::Bool(true));
        assert_eq!(state.evaluate_value("", r#"KlogHas("line 2")"#), MetricValue::Bool(false));
        assert_eq!(state.evaluate_value("", r#"SyslogHas("line 2")"#), MetricValue::Bool(true));
        assert_eq!(state.evaluate_value("", r#"SyslogHas("syslog")"#), MetricValue::Bool(true));
        assert_eq!(state.evaluate_value("", r#"BootlogHas("bootlog")"#), MetricValue::Bool(true));
        assert_eq!(state.evaluate_value("", r#"BootlogHas("syslog")"#), MetricValue::Bool(false));
        data.pop();
        let fetcher = FileDataFetcher::new(&data);
        let state = MetricState::new(&metrics, Fetcher::FileData(fetcher), None);
        assert_eq!(state.evaluate_value("", r#"SyslogHas("syslog")"#), MetricValue::Bool(true));
        assert_eq!(state.evaluate_value("", r#"BootlogHas("bootlog")"#), MetricValue::Bool(false));
        assert_eq!(state.evaluate_value("", r#"BootlogHas("syslog")"#), MetricValue::Bool(false));
        Ok(())
    }

    #[test]
    fn annotations_work() -> Result<(), Error> {
        let annotation_text = r#"{ "build.board": "chromebook-x64", "answer": 42 }"#.to_string();
        let annotations =
            DiagnosticData::new("a".to_string(), Source::Annotations, annotation_text)?;
        let metrics = HashMap::new();
        let data = vec![annotations];
        let fetcher = FileDataFetcher::new(&data);
        let state = MetricState::new(&metrics, Fetcher::FileData(fetcher), None);
        assert_eq!(
            state.evaluate_value("", "Annotation('build.board')"),
            MetricValue::String("chromebook-x64".to_string())
        );
        assert_eq!(state.evaluate_value("", "Annotation('answer')"), MetricValue::Int(42));
        assert_missing!(
            state.evaluate_value("", "Annotation('bogus')"),
            "Key 'bogus' not found in annotations"
        );
        assert_missing!(
            state.evaluate_value("", "Annotation('bogus', 'Double bogus')"),
            "Annotation() needs 1 string argument"
        );
        assert_missing!(
            state.evaluate_value("", "Annotation(42)"),
            "Annotation() needs a string argument"
        );
        Ok(())
    }

    #[test]
    fn test_fetch_errors() {
        assert_eq!(1, NO_PAYLOAD_FETCHER.errors().len());
    }

    // Correct operation of the klog, syslog, and bootlog fields of TrialDataFetcher are tested
    // in the integration test via log_tests.triage.

    // Test evaluation on static values.
    #[test]
    fn test_evaluation() {
        let metrics: Metrics = [(
            "root".to_string(),
            [
                ("is42".to_string(), Metric::Eval("42".to_string())),
                ("isOk".to_string(), Metric::Eval("'OK'".to_string())),
            ]
            .iter()
            .cloned()
            .collect(),
        )]
        .iter()
        .cloned()
        .collect();
        let state = MetricState::new(&metrics, Fetcher::FileData(EMPTY_FILE_FETCHER.clone()), None);

        // Can read a value.
        assert_eq!(state.evaluate_value("root", "is42"), MetricValue::Int(42));

        // Basic arithmetic
        assert_eq!(state.evaluate_value("root", "is42 + 1"), MetricValue::Int(43));
        assert_eq!(state.evaluate_value("root", "is42 - 1"), MetricValue::Int(41));
        assert_eq!(state.evaluate_value("root", "is42 * 2"), MetricValue::Int(84));
        // Automatic float conversion and truncating divide.
        assert_eq!(state.evaluate_value("root", "is42 / 4"), MetricValue::Float(10.5));
        assert_eq!(state.evaluate_value("root", "is42 // 4"), MetricValue::Int(10));

        // Order of operations
        assert_eq!(
            state.evaluate_value("root", "is42 + 10 / 2 * 10 - 2 "),
            MetricValue::Float(90.0)
        );
        assert_eq!(state.evaluate_value("root", "is42 + 10 // 2 * 10 - 2 "), MetricValue::Int(90));

        // Boolean
        assert_eq!(
            state.evaluate_value("root", "And(is42 == 42, is42 < 100)"),
            MetricValue::Bool(true)
        );
        assert_eq!(
            state.evaluate_value("root", "And(is42 == 42, is42 > 100)"),
            MetricValue::Bool(false)
        );
        assert_eq!(
            state.evaluate_value("root", "Or(is42 == 42, is42 > 100)"),
            MetricValue::Bool(true)
        );
        assert_eq!(
            state.evaluate_value("root", "Or(is42 != 42, is42 < 100)"),
            MetricValue::Bool(true)
        );
        assert_eq!(
            state.evaluate_value("root", "Or(is42 != 42, is42 > 100)"),
            MetricValue::Bool(false)
        );
        assert_eq!(state.evaluate_value("root", "Not(is42 == 42)"), MetricValue::Bool(false));

        // Read strings
        assert_eq!(state.evaluate_value("root", "isOk"), MetricValue::String("OK".to_string()));

        // Missing value
        assert_missing!(
            state.evaluate_value("root", "missing"),
            "Metric 'missing' Not Found in 'root'"
        );

        // Booleans short circuit
        assert_missing!(
            state.evaluate_value("root", "Or(is42 != 42, missing)"),
            "Metric 'missing' Not Found in 'root'"
        );
        assert_eq!(
            state.evaluate_value("root", "Or(is42 == 42, missing)"),
            MetricValue::Bool(true)
        );
        assert_missing!(
            state.evaluate_value("root", "And(is42 == 42, missing)"),
            "Metric 'missing' Not Found in 'root'"
        );

        assert_eq!(
            state.evaluate_value("root", "And(is42 != 42, missing)"),
            MetricValue::Bool(false)
        );

        // Missing checks
        assert_eq!(state.evaluate_value("root", "Missing(is42)"), MetricValue::Bool(false));
        assert_eq!(state.evaluate_value("root", "Missing(missing)"), MetricValue::Bool(true));
        assert_eq!(
            state.evaluate_value("root", "And(Not(Missing(is42)), is42 == 42)"),
            MetricValue::Bool(true)
        );
        assert_eq!(
            state.evaluate_value("root", "And(Not(Missing(missing)), missing == 'Hello')"),
            MetricValue::Bool(false)
        );
        assert_eq!(
            state.evaluate_value("root", "Or(Missing(is42), is42 < 42)"),
            MetricValue::Bool(false)
        );
        assert_eq!(
            state.evaluate_value("root", "Or(Missing(missing), missing == 'Hello')"),
            MetricValue::Bool(true)
        );

        // Ensure evaluation for action converts vector values.
        assert_eq!(
            state.evaluate_value("root", "[0==0]"),
            MetricValue::Vector(vec![MetricValue::Bool(true)])
        );
        assert_eq!(
            state.eval_action_metric("root", &Metric::Eval("[0==0]".to_string())),
            MetricValue::Bool(true)
        );

        assert_eq!(
            state.evaluate_value("root", "[0==0, 0==0]"),
            MetricValue::Vector(vec![MetricValue::Bool(true), MetricValue::Bool(true)])
        );
        assert_eq!(
            state.eval_action_metric("root", &Metric::Eval("[0==0, 0==0]".to_string())),
            MetricValue::Vector(vec![MetricValue::Bool(true), MetricValue::Bool(true)])
        );
    }

    // TODO(fxbug.dev/58922): Modify or probably delete this function after better error design.
    #[test]
    fn test_missing_hacks() -> Result<(), Error> {
        macro_rules! eval {
            ($e:expr) => {
                MetricState::evaluate_math($e)
            };
        }
        assert_eq!(eval!("Missing(2>'a')"), MetricValue::Bool(true));
        assert_eq!(eval!("Missing([])"), MetricValue::Bool(true));
        assert_eq!(eval!("Missing([2>'a'])"), MetricValue::Bool(true));
        assert_eq!(eval!("Missing([2>'a', 2>'a'])"), MetricValue::Bool(false));
        assert_eq!(eval!("Missing([2>1])"), MetricValue::Bool(false));
        assert_eq!(eval!("Or(Missing(2>'a'), 2>'a')"), MetricValue::Bool(true));
        Ok(())
    }

    #[test]
    fn test_time() -> Result<(), Error> {
        let metrics = Metrics::new();
        let files = vec![];
        let state_1234 =
            MetricState::new(&metrics, Fetcher::FileData(FileDataFetcher::new(&files)), Some(1234));
        let state_missing =
            MetricState::new(&metrics, Fetcher::FileData(FileDataFetcher::new(&files)), None);
        let now_expression = parse::parse_expression("Now()").unwrap();
        assert_missing!(MetricState::evaluate_math("Now()"), "No valid time available");
        assert_eq!(state_1234.evaluate_expression(&now_expression), MetricValue::Int(1234));
        assert_missing!(
            state_missing.evaluate_expression(&now_expression),
            "No valid time available"
        );
        Ok(())
    }

    // Correct operation of annotations is tested via annotation_tests.triage.
}
