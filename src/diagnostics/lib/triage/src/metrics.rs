// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub(crate) mod arithmetic;
pub(crate) mod context;
pub(crate) mod fetch;
pub(crate) mod metric_value;
pub(crate) mod parse;
pub(crate) mod variable;

use {
    fetch::{Fetcher, FileDataFetcher, SelectorString, TrialDataFetcher},
    metric_value::{MetricValue, Problem},
    regex::Regex,
    serde::{Deserialize, Deserializer, Serialize},
    std::{
        cell::RefCell,
        clone::Clone,
        cmp::min,
        collections::{HashMap, HashSet},
        convert::TryFrom,
    },
    variable::VariableName,
};

/// The contents of a single Metric. Metrics produce a value for use in Actions or other Metrics.
#[derive(Clone, Debug, PartialEq, Serialize)]
pub(crate) enum Metric {
    /// Selector tells where to find a value in the Inspect data. The
    /// first non-empty option is returned.
    // Note: This can't be a fidl_fuchsia_diagnostics::Selector because it's not deserializable or
    // cloneable.
    Selector(Vec<SelectorString>),
    /// Eval contains an arithmetic expression,
    Eval(ExpressionContext),
    // Directly specify a value that's hard to generate, for example Problem::UnhandledType.
    #[cfg(test)]
    Hardcoded(MetricValue),
}

impl std::fmt::Display for Metric {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Metric::Selector(s) => write!(f, "{:?}", s),
            Metric::Eval(s) => write!(f, "{}", s),
            #[cfg(test)]
            Metric::Hardcoded(value) => write!(f, "{:?}", value),
        }
    }
}

/// Contains a Metric and the resulting MetricValue if the Metric has been evaluated at least once.
/// If the Metric has not been evaluated at least once the metric_value contains None.
#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct ValueSource {
    pub(crate) metric: Metric,
    pub cached_value: RefCell<Option<MetricValue>>,
}

impl ValueSource {
    pub(crate) fn new(metric: Metric) -> Self {
        Self { metric, cached_value: RefCell::new(None) }
    }

    pub(crate) fn try_from_expression(expr: &str) -> Result<Self, anyhow::Error> {
        Ok(ValueSource::new(Metric::Eval(ExpressionContext::try_from(expr.to_string())?)))
    }
}

impl std::fmt::Display for ValueSource {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.metric)
    }
}

/// [Metrics] are a map from namespaces to the named [ValueSource]s stored within that namespace.
pub type Metrics = HashMap<String, HashMap<String, ValueSource>>;

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
    stack: RefCell<HashSet<String>>,
}

#[derive(Deserialize, Debug, Clone, PartialEq, Serialize)]
pub enum MathFunction {
    Add,
    Sub,
    Mul,
    FloatDiv,
    IntDiv,
    FloatDivChecked,
    IntDivChecked,
    Greater,
    Less,
    GreaterEq,
    LessEq,
    Max,
    Min,
    Abs,
}

#[derive(Deserialize, Debug, Clone, PartialEq, Serialize)]
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
    UnhandledType,
    Problem,
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
    StringMatches,
    True,
    False,
}

/// Lambda stores a function; its parameters and body are evaluated lazily.
/// Lambda's are created by evaluating the "Fn()" expression.
#[derive(Deserialize, Debug, Clone, Serialize)]
pub struct Lambda {
    parameters: Vec<String>,
    body: ExpressionTree,
}

impl Lambda {
    fn valid_parameters(parameters: &ExpressionTree) -> Result<Vec<String>, MetricValue> {
        match parameters {
            ExpressionTree::Vector(parameters) => parameters
                .iter()
                .map(|param| match param {
                    ExpressionTree::Variable(name) => {
                        if name.includes_namespace() {
                            Err(syntax_error("Namespaces not allowed in function params"))
                        } else {
                            Ok(name.original_name().to_string())
                        }
                    }
                    _ => Err(syntax_error("Function params must be valid identifier names")),
                })
                .collect::<Result<Vec<_>, _>>(),
            _ => return Err(syntax_error("Function params must be a vector of names")),
        }
    }

    fn as_metric_value(definition: &Vec<ExpressionTree>) -> MetricValue {
        if definition.len() != 2 {
            return syntax_error("Function needs two parameters, list of params and expression");
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

/// ExpressionTree represents the parsed body of an Eval Metric. It applies
/// a function to sub-expressions, or holds a Problem, the name of a
/// Metric, a vector of expressions, or a basic Value.
#[derive(Deserialize, Debug, Clone, PartialEq, Serialize)]
pub(crate) enum ExpressionTree {
    // Some operators have arity 1 or 2, some have arity N.
    // For symmetry/readability, I use the same operand-spec Vec<Expression> for all.
    // TODO(cphoenix): Check on load that all operators have a legal number of operands.
    Function(Function, Vec<ExpressionTree>),
    Vector(Vec<ExpressionTree>),
    Variable(VariableName),
    Value(MetricValue),
}

/// ExpressionContext represents a wrapper class which contains a DSL string
/// representing an expression and the resulting parsed ExpressionTree
#[derive(Debug, Clone, PartialEq, Serialize)]
pub(crate) struct ExpressionContext {
    pub(crate) raw_expression: String,
    pub(crate) parsed_expression: ExpressionTree,
}

impl TryFrom<String> for ExpressionContext {
    type Error = anyhow::Error;

    fn try_from(raw_expression: String) -> Result<Self, Self::Error> {
        let parsed_expression = parse::parse_expression(&raw_expression)?;
        Ok(Self { raw_expression, parsed_expression })
    }
}

impl<'de> Deserialize<'de> for ExpressionContext {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let value = String::deserialize(deserializer)?;
        Ok(ExpressionContext::try_from(value).map_err(serde::de::Error::custom)?)
    }
}

impl std::fmt::Display for ExpressionContext {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.raw_expression)
    }
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

// Construct Problem() metric from a message
fn missing(message: impl AsRef<str>) -> MetricValue {
    MetricValue::Problem(Problem::Missing(message.as_ref().to_string()))
}

fn syntax_error(message: impl AsRef<str>) -> MetricValue {
    MetricValue::Problem(Problem::SyntaxError(message.as_ref().to_string()))
}

fn value_error(message: impl AsRef<str>) -> MetricValue {
    MetricValue::Problem(Problem::ValueError(message.as_ref().to_string()))
}

fn internal_bug(message: impl AsRef<str>) -> MetricValue {
    MetricValue::Problem(Problem::InternalBug(message.as_ref().to_string()))
}

fn unhandled_type(message: impl AsRef<str>) -> MetricValue {
    MetricValue::Problem(Problem::UnhandledType(message.as_ref().to_string()))
}

fn evaluation_error(message: impl AsRef<str>) -> MetricValue {
    MetricValue::Problem(Problem::EvaluationError(message.as_ref().to_string()))
}

/// If Problem is a Multiple and all its entries are Ignore, combines them. Otherwise just
/// returns the Problem.
fn bubble_up_ignore(problem: Problem) -> Problem {
    match problem {
        Problem::Multiple(problems) => {
            let contains_non_ignore = problems.iter().any(|p| !matches!(p, Problem::Ignore(_)));
            if contains_non_ignore {
                Problem::Multiple(problems)
            } else {
                Problem::Ignore(
                    problems
                        .into_iter()
                        .filter_map(|p| if let Problem::Ignore(v) = p { Some(v) } else { None })
                        .flatten()
                        .collect(),
                )
            }
        }
        problem => problem,
    }
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

fn first_usable_value(values: impl Iterator<Item = MetricValue>) -> MetricValue {
    let mut found_empty = false;
    for value in values {
        match value {
            MetricValue::Problem(Problem::Missing(_)) => {}
            MetricValue::Vector(ref v)
                if v.len() == 1 && matches!(v[0], MetricValue::Problem(Problem::Missing(_))) => {}
            MetricValue::Vector(v) if v.len() == 0 => found_empty = true,
            value => return value,
        }
    }
    if found_empty {
        return MetricValue::Vector(vec![]);
    }
    // TODO(fxbug.dev/58922): Improve and simplify the error semantics
    return missing("Every value was missing");
}

impl<'a> MetricState<'a> {
    /// Create an initialized MetricState.
    pub fn new(metrics: &'a Metrics, fetcher: Fetcher<'a>, now: Option<i64>) -> MetricState<'a> {
        MetricState { metrics, fetcher, now, stack: RefCell::new(HashSet::new()) }
    }

    /// Forcefully evaluate all [Metric]s to populate the cached values.
    pub fn evaluate_all_metrics(&self) {
        for (namespace, metrics) in self.metrics.iter() {
            for (name, _value_source) in metrics.iter() {
                self.evaluate_variable(&namespace, &VariableName::new(name.clone()));
            }
        }
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
            return syntax_error(format!(
                "Name {} not in test values and refers outside the file",
                name
            ));
        }
        match self.metrics.get(namespace) {
            None => return internal_bug(format!("BUG! Bad namespace '{}'", namespace)),
            Some(metric_map) => match metric_map.get(name) {
                None => {
                    return syntax_error(format!("Metric '{}' Not Found in '{}'", name, namespace))
                }
                Some(value_source) => {
                    let resolved_value: MetricValue;
                    {
                        let cached_value_cell = value_source.cached_value.borrow();
                        match &*cached_value_cell {
                            None => {
                                resolved_value = match &value_source.metric {
                                    Metric::Selector(_) => missing(format!(
                                        "Selector {} can't be used in tests; please supply a value",
                                        name
                                    )),
                                    Metric::Eval(expression) => {
                                        self.evaluate(namespace, &expression.parsed_expression)
                                    }
                                    #[cfg(test)]
                                    Metric::Hardcoded(value) => value.clone(),
                                };
                            }
                            Some(cached_value) => return cached_value.clone(),
                        }
                    }
                    let mut cached_value_cell = value_source.cached_value.borrow_mut();
                    *cached_value_cell = Some(resolved_value.clone());
                    return resolved_value.clone();
                }
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
                None => return syntax_error(format!("Bad namespace '{}'", real_namespace)),
                Some(metric_map) => match metric_map.get(real_name) {
                    None => {
                        return syntax_error(format!(
                            "Metric '{}' Not Found in '{}'",
                            real_name, real_namespace
                        ))
                    }
                    Some(value_source) => {
                        if let Some(cached_value) = value_source.cached_value.borrow().as_ref() {
                            return cached_value.clone();
                        }
                        let resolved_value = match &value_source.metric {
                            Metric::Selector(selectors) => first_usable_value(
                                selectors.iter().map(|selector| fetcher.fetch(selector)),
                            ),
                            Metric::Eval(expression) => {
                                self.evaluate(real_namespace, &expression.parsed_expression)
                            }
                            #[cfg(test)]
                            Metric::Hardcoded(value) => value.clone(),
                        };
                        let mut cached_value_cell = value_source.cached_value.borrow_mut();
                        *cached_value_cell = Some(resolved_value.clone());
                        resolved_value
                    }
                },
            }
        } else {
            return syntax_error(format!("Bad name '{}'", name.original_name()));
        }
    }

    /// Calculate the value of a Metric specified by name and namespace.
    fn evaluate_variable(&self, namespace: &str, name: &VariableName) -> MetricValue {
        // TODO(cphoenix): When historical metrics are added, change semantics to refresh()
        // TODO(cphoenix): Improve the data structure on Metric names. Probably fill in
        //  namespace during parse.
        if self.stack.borrow().contains(&name.full_name(namespace)) {
            // Clear the stack for future reuse
            let _ = self.stack.replace(HashSet::new());
            return evaluation_error(&format!(
                "Cycle encountered while evaluating variable {} in the expression",
                name.name
            ));
        }
        // Insert the full name including namespace.
        self.stack.borrow_mut().insert(name.full_name(namespace));

        let value = match &self.fetcher {
            Fetcher::FileData(fetcher) => self.metric_value_for_file(fetcher, namespace, name),
            Fetcher::TrialData(fetcher) => self.metric_value_for_trial(fetcher, namespace, name),
        };

        self.stack.borrow_mut().remove(&name.full_name(namespace));
        value
    }

    /// Fetch or compute the value of a Metric expression from an action.
    pub(crate) fn eval_action_metric(
        &self,
        namespace: &str,
        value_source: &ValueSource,
    ) -> MetricValue {
        if let Some(cached_value) = &*value_source.cached_value.borrow() {
            return cached_value.clone();
        }

        let resolved_value = match &value_source.metric {
            Metric::Selector(_) => {
                syntax_error("Selectors aren't allowed in action triggers".to_owned())
            }
            Metric::Eval(expression) => {
                unwrap_for_math(&self.evaluate(namespace, &expression.parsed_expression)).clone()
            }
            #[cfg(test)]
            Metric::Hardcoded(value) => value.clone(),
        };

        let mut cached_value_cell = value_source.cached_value.borrow_mut();
        *cached_value_cell = Some(resolved_value.clone());
        return resolved_value;
    }

    #[cfg(test)]
    fn evaluate_value(&self, namespace: &str, expression: &str) -> MetricValue {
        match parse::parse_expression(expression) {
            Ok(expr) => self.evaluate(namespace, &expr),
            Err(e) => syntax_error(format!("Expression parse error\n{}", e)),
        }
    }

    /// Evaluate an Expression which contains only base values, not referring to other Metrics.
    pub(crate) fn evaluate_math(expr: &str) -> MetricValue {
        let tree = match ExpressionContext::try_from(expr.to_string()) {
            Ok(expr_context) => expr_context.parsed_expression,
            Err(err) => return syntax_error(format!("Failed to parse '{}': {}", expr, err)),
        };
        Self::evaluate_const_expression(&tree)
    }

    pub(crate) fn evaluate_const_expression(tree: &ExpressionTree) -> MetricValue {
        let values = HashMap::new();
        let fetcher = Fetcher::TrialData(TrialDataFetcher::new(&values));
        let files = HashMap::new();
        let metric_state = MetricState::new(&files, fetcher, None);
        metric_state.evaluate(&"".to_string(), &tree)
    }

    #[cfg(test)]
    pub(crate) fn evaluate_expression(&self, e: &ExpressionTree) -> MetricValue {
        self.evaluate(&"".to_string(), e)
    }

    fn evaluate_function(
        &self,
        namespace: &str,
        function: &Function,
        operands: &Vec<ExpressionTree>,
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
            Function::UnhandledType => self.is_unhandled_type(namespace, operands),
            Function::Problem => self.is_problem(namespace, operands),
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
            Function::StringMatches => self.regex(namespace, operands),
            Function::True => self.boolean(operands, true),
            Function::False => self.boolean(operands, false),
        }
    }

    fn regex(&self, namespace: &str, operands: &Vec<ExpressionTree>) -> MetricValue {
        if operands.len() != 2 {
            return syntax_error(
                "StringMatches(metric, regex) needs one string metric and one string regex"
                    .to_string(),
            );
        }

        let (value, regex) = match (
            self.evaluate(namespace, &operands[0]),
            self.evaluate(namespace, &operands[1]),
        ) {
            (MetricValue::String(value), MetricValue::String(regex)) => (value, regex),
            _ => {
                return syntax_error("Arguments to StringMatches must be strings".to_string());
            }
        };

        let regex = match Regex::new(&regex) {
            Ok(v) => v,
            Err(_) => {
                return syntax_error(format!("Could not parse `{}` as regex", regex));
            }
        };

        MetricValue::Bool(regex.is_match(&value))
    }

    fn option(&self, namespace: &str, operands: &Vec<ExpressionTree>) -> MetricValue {
        first_usable_value(operands.iter().map(|expression| self.evaluate(namespace, expression)))
    }

    fn now(&self, operands: &'a [ExpressionTree]) -> MetricValue {
        if !operands.is_empty() {
            return syntax_error("Now() requires no operands.");
        }
        match self.now {
            Some(time) => MetricValue::Int(time),
            None => missing("No valid time available"),
        }
    }

    fn apply_lambda(&self, namespace: &str, lambda: &Lambda, args: &[&MetricValue]) -> MetricValue {
        fn substitute_all(
            expressions: &[ExpressionTree],
            bindings: &HashMap<&str, &MetricValue>,
        ) -> Vec<ExpressionTree> {
            expressions.iter().map(|e| substitute(e, bindings)).collect::<Vec<_>>()
        }

        fn substitute(
            expression: &ExpressionTree,
            bindings: &HashMap<&str, &MetricValue>,
        ) -> ExpressionTree {
            match expression {
                ExpressionTree::Function(function, expressions) => ExpressionTree::Function(
                    function.clone(),
                    substitute_all(expressions, bindings),
                ),
                ExpressionTree::Vector(expressions) => {
                    ExpressionTree::Vector(substitute_all(expressions, bindings))
                }
                ExpressionTree::Variable(name) => {
                    let foo: &str = name.original_name();
                    if let Some(value) = bindings.get(foo) {
                        ExpressionTree::Value((*value).clone())
                    } else {
                        ExpressionTree::Variable(name.clone())
                    }
                }
                ExpressionTree::Value(value) => ExpressionTree::Value(value.clone()),
            }
        }

        let parameters = &lambda.parameters;
        if parameters.len() != args.len() {
            return syntax_error(format!(
                "Function has {} parameters and needs {} arguments, but has {}.",
                parameters.len(),
                parameters.len(),
                args.len()
            ));
        }
        let mut bindings = HashMap::new();
        for (name, value) in parameters.iter().zip(args.iter()) {
            bindings.insert(name as &str, *value);
        }
        let expression = substitute(&lambda.body, &bindings);
        self.evaluate(namespace, &expression)
    }

    fn unpack_lambda(
        &self,
        namespace: &str,
        operands: &'a [ExpressionTree],
        function_name: &str,
    ) -> Result<(Box<Lambda>, Vec<MetricValue>), MetricValue> {
        if operands.len() == 0 {
            return Err(syntax_error(format!(
                "{} needs a function in its first argument",
                function_name
            )));
        }
        let lambda = match self.evaluate(namespace, &operands[0]) {
            MetricValue::Lambda(lambda) => lambda,
            _ => {
                return Err(syntax_error(format!(
                    "{} needs a function in its first argument",
                    function_name
                )))
            }
        };
        let arguments =
            operands[1..].iter().map(|expr| self.evaluate(namespace, expr)).collect::<Vec<_>>();
        Ok((lambda, arguments))
    }

    /// This implements the Apply() function.
    fn apply(&self, namespace: &str, operands: &[ExpressionTree]) -> MetricValue {
        let (lambda, arguments) = match self.unpack_lambda(namespace, operands, "Apply") {
            Ok((lambda, arguments)) => (lambda, arguments),
            Err(problem) => return problem,
        };
        if arguments.len() < 1 {
            return syntax_error("Apply needs a second argument (a vector).");
        }
        if arguments.len() > 1 {
            return syntax_error("Apply only accepts one vector argument.");
        }

        match &arguments[0] {
            MetricValue::Vector(apply_args) => {
                self.apply_lambda(namespace, &lambda, &apply_args.iter().collect::<Vec<_>>())
            }
            _ => return value_error("Apply only accepts a vector as an argument."),
        }
    }

    /// This implements the Map() function.
    fn map(&self, namespace: &str, operands: &[ExpressionTree]) -> MetricValue {
        let (lambda, arguments) = match self.unpack_lambda(namespace, operands, "Map") {
            Ok((lambda, arguments)) => (lambda, arguments),
            Err(problem) => return problem,
        };
        // TODO(cphoenix): Clean this up - replace the next 25 lines with a for loop
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
    fn fold(&self, namespace: &str, operands: &[ExpressionTree]) -> MetricValue {
        let (lambda, arguments) = match self.unpack_lambda(namespace, operands, "Fold") {
            Ok((lambda, arguments)) => (lambda, arguments),
            Err(problem) => return problem,
        };
        if arguments.is_empty() {
            return syntax_error("Fold needs a second argument, a vector");
        }
        let vector = match &arguments[0] {
            MetricValue::Vector(items) => items,
            _ => return value_error("Second argument of Fold must be a vector"),
        };
        let (first, rest) = match arguments.len() {
            1 => match vector.split_first() {
                Some(first_rest) => first_rest,
                None => return value_error("Fold needs at least one value"),
            },
            2 => (&arguments[1], &vector[..]),
            _ => return syntax_error("Fold needs (function, vec) or (function, vec, start)"),
        };
        let mut result = first.clone();
        for item in rest {
            result = self.apply_lambda(namespace, &lambda, &[&result, item]);
        }
        result
    }

    /// This implements the Filter() function.
    fn filter(&self, namespace: &str, operands: &[ExpressionTree]) -> MetricValue {
        let (lambda, arguments) = match self.unpack_lambda(namespace, operands, "Filter") {
            Ok((lambda, arguments)) => (lambda, arguments),
            Err(problem) => return problem,
        };
        if arguments.len() != 1 {
            return syntax_error("Filter needs (function, vector)");
        }
        let result = match &arguments[0] {
            MetricValue::Vector(items) => items
                .iter()
                .filter_map(|item| match self.apply_lambda(namespace, &lambda, &[&item]) {
                    MetricValue::Bool(true) => Some(item.clone()),
                    MetricValue::Bool(false) => None,
                    MetricValue::Problem(problem) => Some(MetricValue::Problem(problem.clone())),
                    bad_type => Some(value_error(format!(
                        "Bad value {:?} from filter function should be Boolean",
                        bad_type
                    ))),
                })
                .collect(),
            _ => return syntax_error("Filter second argument must be a vector"),
        };
        MetricValue::Vector(result)
    }

    /// This implements the Count() function.
    fn count(&self, namespace: &str, operands: &[ExpressionTree]) -> MetricValue {
        if operands.len() != 1 {
            return syntax_error("Count requires one argument, a vector");
        }
        // TODO(fxbug.dev/58922): Refactor all the arg-sanitizing boilerplate into one function
        match self.evaluate(namespace, &operands[0]) {
            MetricValue::Vector(items) => {
                let errors = items
                    .clone()
                    .into_iter()
                    .filter_map(|item| match item {
                        MetricValue::Problem(problem) => Some(problem),
                        _ => None,
                    })
                    .collect::<Vec<_>>();
                return match errors.len() {
                    0 => MetricValue::Int(items.len() as i64),
                    1 => MetricValue::Problem(errors[0].clone()),
                    _ => MetricValue::Problem(bubble_up_ignore(Problem::Multiple(
                        errors
                            .iter()
                            .map(|e| {
                                let p: Problem = e.clone(); // Without this, clone() gives another &Problem
                                p
                            })
                            .collect(),
                    ))),
                };
            }
            bad => value_error(format!("Count only works on vectors, not {}", bad)),
        }
    }

    fn boolean(&self, operands: &[ExpressionTree], value: bool) -> MetricValue {
        if operands.len() != 0 {
            return syntax_error("Boolean functions don't take any arguments");
        }
        return MetricValue::Bool(value);
    }

    /// This implements the time-conversion functions.
    fn time(&self, namespace: &str, operands: &[ExpressionTree], multiplier: i64) -> MetricValue {
        if operands.len() != 1 {
            return syntax_error("Time conversion needs 1 numeric argument");
        }
        match self.evaluate(namespace, &operands[0]) {
            MetricValue::Int(value) => MetricValue::Int(value * multiplier),
            MetricValue::Float(value) => match safe_float_to_int(value * (multiplier as f64)) {
                None => value_error(format!(
                    "Time conversion needs 1 numeric argument; couldn't convert {}",
                    value
                )),
                Some(value) => MetricValue::Int(value),
            },
            MetricValue::Problem(oops) => return MetricValue::Problem(oops),
            bad => value_error(format!("Time conversion needs 1 numeric argument, not {}", bad)),
        }
    }

    fn evaluate(&self, namespace: &str, e: &ExpressionTree) -> MetricValue {
        match e {
            ExpressionTree::Function(f, operands) => self.evaluate_function(namespace, f, operands),
            ExpressionTree::Variable(name) => self.evaluate_variable(namespace, name),
            ExpressionTree::Value(value) => value.clone(),
            ExpressionTree::Vector(values) => {
                MetricValue::Vector(map_vec(values, |value| self.evaluate(namespace, value)))
            }
        }
    }

    fn annotation(&self, namespace: &str, operands: &Vec<ExpressionTree>) -> MetricValue {
        if operands.len() != 1 {
            return syntax_error("Annotation() needs 1 string argument");
        }
        match self.evaluate(namespace, &operands[0]) {
            MetricValue::String(string) => match &self.fetcher {
                Fetcher::TrialData(fetcher) => fetcher.annotations,
                Fetcher::FileData(fetcher) => fetcher.annotations,
            }
            .fetch(&string),
            _ => value_error("Annotation() needs a string argument"),
        }
    }

    fn log_contains(
        &self,
        log_type: &Function,
        namespace: &str,
        operands: &Vec<ExpressionTree>,
    ) -> MetricValue {
        let log_data = match &self.fetcher {
            Fetcher::TrialData(fetcher) => match log_type {
                Function::KlogHas => fetcher.klog,
                Function::SyslogHas => fetcher.syslog,
                Function::BootlogHas => fetcher.bootlog,
                _ => return internal_bug("Internal error, log_contains with non-log function"),
            },
            Fetcher::FileData(fetcher) => match log_type {
                Function::KlogHas => fetcher.klog,
                Function::SyslogHas => fetcher.syslog,
                Function::BootlogHas => fetcher.bootlog,
                _ => return internal_bug("Internal error, log_contains with non-log function"),
            },
        };
        if operands.len() != 1 {
            return syntax_error("Log matcher must use exactly 1 argument, an RE string.");
        }
        match self.evaluate(namespace, &operands[0]) {
            MetricValue::String(re) => MetricValue::Bool(log_data.contains(&re)),
            _ => value_error("Log matcher needs a string (RE)."),
        }
    }

    fn apply_boolean_function(
        &self,
        namespace: &str,
        function: &dyn (Fn(&MetricValue, &MetricValue) -> bool),
        operands: &Vec<ExpressionTree>,
    ) -> MetricValue {
        if operands.len() != 2 {
            return syntax_error(format!("Bad arg list {:?} for binary operator", operands));
        }
        let operand_values = map_vec(&operands, |operand| self.evaluate(namespace, operand));
        let args = map_vec_r(&operand_values, |operand| unwrap_for_math(operand));
        match (args[0], args[1]) {
            // TODO(fxbug.dev/58922): Refactor all the arg-sanitizing boilerplate into one function
            (MetricValue::Problem(p1), MetricValue::Problem(p2)) => {
                MetricValue::Problem(bubble_up_ignore(Problem::Multiple(vec![
                    p1.clone(),
                    p2.clone(),
                ])))
            }
            (MetricValue::Problem(p), _) => MetricValue::Problem(p.clone()),
            (_, MetricValue::Problem(p)) => MetricValue::Problem(p.clone()),
            _ => MetricValue::Bool(function(args[0], args[1])),
        }
    }

    fn fold_bool(
        &self,
        namespace: &str,
        function: &dyn (Fn(bool, bool) -> bool),
        operands: &Vec<ExpressionTree>,
        short_circuit_behavior: ShortCircuitBehavior,
    ) -> MetricValue {
        if operands.len() == 0 {
            return syntax_error("No operands in boolean expression");
        }
        let first = self.evaluate(namespace, &operands[0]);
        let mut result: bool = match unwrap_for_math(&first) {
            MetricValue::Bool(value) => *value,
            MetricValue::Problem(p) => return MetricValue::Problem(p.clone()),
            bad => return value_error(format!("{:?} is not boolean", bad)),
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
                MetricValue::Problem(p) => return MetricValue::Problem(p.clone()),
                bad => return value_error(format!("{:?} is not boolean", bad)),
            }
        }
        MetricValue::Bool(result)
    }

    fn not_bool(&self, namespace: &str, operands: &Vec<ExpressionTree>) -> MetricValue {
        if operands.len() != 1 {
            return syntax_error(format!(
                "Wrong number of arguments ({}) for unary bool operator",
                operands.len()
            ));
        }
        match unwrap_for_math(&self.evaluate(namespace, &operands[0])) {
            MetricValue::Bool(true) => MetricValue::Bool(false),
            MetricValue::Bool(false) => MetricValue::Bool(true),
            MetricValue::Problem(p) => return MetricValue::Problem(p.clone()),
            bad => return value_error(format!("{:?} not boolean", bad)),
        }
    }

    // Returns Bool true if the given metric is Unhandled, or a vector with one Unhandled (which
    // may be returned by fetch()), or a Multiple containing only Unhandled.
    // Returns false if the metric is a non-Problem value.
    // Propagates non-Unhandled Problems.
    fn is_unhandled_type(&self, namespace: &str, operands: &Vec<ExpressionTree>) -> MetricValue {
        if operands.len() != 1 {
            return syntax_error(format!(
                "Wrong number of operands for UnhandledType(): {}",
                operands.len()
            ));
        }
        let value = self.evaluate(namespace, &operands[0]);
        MetricValue::Bool(match value {
            MetricValue::Problem(Problem::UnhandledType(_)) => true,
            MetricValue::Problem(Problem::Multiple(ref problems)) => {
                if problems.iter().filter(|p| matches!(p, Problem::UnhandledType(_))).count()
                    == problems.len()
                {
                    true
                } else {
                    return value.clone();
                }
            }

            // TODO(fxbug.dev/58922): Well-designed errors and special cases, not hacks
            // is_unhandled_type() returns false on an empty vector, while is_missing() returns
            // true as a special case. So these functions can't easily be refactored. When 58922 is
            // completed this logic will be a lot simpler and more consistent.
            MetricValue::Vector(contents) if contents.len() == 1 => match contents[0] {
                MetricValue::Problem(Problem::UnhandledType(_)) => true,
                MetricValue::Problem(ref problem) => return MetricValue::Problem(problem.clone()),
                _ => false,
            },
            MetricValue::Problem(problem) => return MetricValue::Problem(problem.clone()),
            _ => false,
        })
    }

    // Returns Bool true if the given metric is Missing, false if the metric has a value. Propagates
    // non-Missing errors. Special case: An empty vector, or a vector containing one Missing,
    // counts as Missing.
    fn is_missing(&self, namespace: &str, operands: &Vec<ExpressionTree>) -> MetricValue {
        if operands.len() != 1 {
            return syntax_error(format!(
                "Wrong number of operands for Missing(): {}",
                operands.len()
            ));
        }
        let value = self.evaluate(namespace, &operands[0]);
        MetricValue::Bool(match value {
            MetricValue::Problem(Problem::Missing(_)) => true,
            MetricValue::Problem(Problem::Multiple(ref problems)) => {
                if problems.iter().filter(|p| matches!(p, Problem::Missing(_))).count()
                    == problems.len()
                {
                    true
                } else {
                    return value.clone();
                }
            }

            // TODO(fxbug.dev/58922): Well-designed errors and special cases, not hacks
            MetricValue::Vector(contents) if contents.len() == 0 => true,
            MetricValue::Vector(contents) if contents.len() == 1 => match contents[0] {
                MetricValue::Problem(Problem::Missing(_)) => true,
                MetricValue::Problem(ref problem) => return MetricValue::Problem(problem.clone()),
                _ => false,
            },
            MetricValue::Problem(problem) => return MetricValue::Problem(problem.clone()),
            _ => false,
        })
    }

    // Returns Bool true if and only if the value for the given metric is any sort of Problem.
    fn is_problem(&self, namespace: &str, operands: &Vec<ExpressionTree>) -> MetricValue {
        if operands.len() != 1 {
            return syntax_error(format!(
                "Wrong number of operands for Problem(): {}",
                operands.len()
            ));
        }
        let value = self.evaluate(namespace, &operands[0]);
        MetricValue::Bool(match value {
            MetricValue::Problem(_) => true,
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

    /// Problem should never equal anything, even an identical Problem. Code (tests) can use
    /// assert_problem!(MetricValue::Problem(_), "foo") to test error messages.
    #[macro_export]
    macro_rules! assert_problem {
        ($missing:expr, $message:expr) => {
            match $missing {
                MetricValue::Problem(problem) => assert_eq!(format!("{:?}", problem), $message),
                oops => {
                    // TODO: Add $missing to println, it currently returns a generics error.
                    println!("Non problem type {:?}", oops);
                    assert!(false, "Non-Problem type");
                }
            }
        };
    }

    #[macro_export]
    macro_rules! assert_not_missing {
        ($not_missing:expr) => {
            match $not_missing {
                MetricValue::Problem(Problem::Missing(message)) => {
                    assert!(false, "Expected not missing, was: {}", &message)
                }
                _ => {}
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
        static ref BAD_PAYLOAD_F: Vec<DiagnosticData> = {
            let s = r#"[{"moniker": "abcd", "payload": ["a", "b"]}]"#;
            vec![DiagnosticData::new("i".to_string(), Source::Inspect, s.to_string()).unwrap()]
        };
        static ref EMPTY_FILE_FETCHER: FileDataFetcher<'static> = FileDataFetcher::new(&EMPTY_F);
        static ref EMPTY_TRIAL_FETCHER: TrialDataFetcher<'static> = TrialDataFetcher::new_empty();
        static ref NO_PAYLOAD_FETCHER: FileDataFetcher<'static> =
            FileDataFetcher::new(&NO_PAYLOAD_F);
        static ref BAD_PAYLOAD_FETCHER: FileDataFetcher<'static> =
            FileDataFetcher::new(&BAD_PAYLOAD_F);
    }

    #[fuchsia::test]
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

    #[fuchsia::test]
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
        assert_problem!(
            state.evaluate_value("", "Annotation('bogus')"),
            "Missing: Key 'bogus' not found in annotations"
        );
        assert_problem!(
            state.evaluate_value("", "Annotation('bogus', 'Double bogus')"),
            "SyntaxError: Annotation() needs 1 string argument"
        );
        assert_problem!(
            state.evaluate_value("", "Annotation(42)"),
            "ValueError: Annotation() needs a string argument"
        );
        Ok(())
    }

    #[fuchsia::test]
    fn test_fetch_errors() {
        // Do not show errors when there is simply no payload.
        assert_eq!(0, NO_PAYLOAD_FETCHER.errors().len());
        assert_eq!(1, BAD_PAYLOAD_FETCHER.errors().len());
    }

    // Correct operation of the klog, syslog, and bootlog fields of TrialDataFetcher are tested
    // in the integration test via log_tests.triage.

    // Test evaluation on static values.
    #[fuchsia::test]
    fn test_evaluation() {
        let metrics: Metrics = [(
            "root".to_string(),
            [
                ("is42".to_string(), ValueSource::try_from_expression("42").unwrap()),
                ("isOk".to_string(), ValueSource::try_from_expression("'OK'").unwrap()),
                (
                    "unhandled".to_string(),
                    ValueSource::new(Metric::Hardcoded(unhandled_type("Unhandled"))),
                ),
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
        assert_problem!(
            state.evaluate_value("root", "missing"),
            "SyntaxError: Metric 'missing' Not Found in 'root'"
        );

        // Booleans short circuit
        assert_problem!(
            state.evaluate_value("root", "Or(is42 != 42, missing)"),
            "SyntaxError: Metric 'missing' Not Found in 'root'"
        );
        assert_eq!(
            state.evaluate_value("root", "Or(is42 == 42, missing)"),
            MetricValue::Bool(true)
        );
        assert_problem!(
            state.evaluate_value("root", "And(is42 == 42, missing)"),
            "SyntaxError: Metric 'missing' Not Found in 'root'"
        );

        assert_eq!(
            state.evaluate_value("root", "And(is42 != 42, missing)"),
            MetricValue::Bool(false)
        );

        // Missing() checks
        assert_eq!(state.evaluate_value("root", "Missing(is42)"), MetricValue::Bool(false));
        // An unknown variable is a SyntaxError, not a Missing(), and Missing() won't catch it.
        assert_problem!(
            state.evaluate_value("root", "Missing(not_found)"),
            "SyntaxError: Metric 'not_found' Not Found in 'root'"
        );
        assert_eq!(
            state.evaluate_value("root", "And(Not(Missing(is42)), is42 == 42)"),
            MetricValue::Bool(true)
        );
        assert_problem!(
            state.evaluate_value("root", "And(Not(Missing(not_found)), not_found == 'Hello')"),
            "SyntaxError: Metric 'not_found' Not Found in 'root'"
        );
        assert_eq!(
            state.evaluate_value("root", "Or(Missing(is42), is42 < 42)"),
            MetricValue::Bool(false)
        );
        assert_problem!(
            state.evaluate_value("root", "Or(Missing(not_found), not_found == 'Hello')"),
            "SyntaxError: Metric 'not_found' Not Found in 'root'"
        );
        assert_eq!(state.evaluate_value("root", "Missing([])"), MetricValue::Bool(true));

        // UnhandledType() checks
        assert_eq!(state.evaluate_value("root", "UnhandledType(is42)"), MetricValue::Bool(false));
        // An unknown variable is a SyntaxError, not a Missing(), and Missing() won't catch it.
        assert_problem!(
            state.evaluate_value("root", "UnhandledType(not_found)"),
            "SyntaxError: Metric 'not_found' Not Found in 'root'"
        );
        assert_eq!(
            state.evaluate_value("root", "And(Not(UnhandledType(is42)), is42 == 42)"),
            MetricValue::Bool(true)
        );
        assert_eq!(
            state.evaluate_value("root", "UnhandledType(unhandled)"),
            MetricValue::Bool(true)
        );
        assert_eq!(state.evaluate_value("root", "UnhandledType([])"), MetricValue::Bool(false));

        // Problem() checks
        assert_eq!(state.evaluate_value("root", "Problem(is42)"), MetricValue::Bool(false));
        // An unknown variable is a SyntaxError, not a Missing()
        assert_eq!(state.evaluate_value("root", "Problem(not_found)"), MetricValue::Bool(true));
        assert_eq!(
            state.evaluate_value("root", "And(Not(Problem(is42)), is42 == 42)"),
            MetricValue::Bool(true)
        );
        assert_eq!(
            state.evaluate_value("root", "And(Not(Problem(not_found)), not_found == 'Hello')"),
            MetricValue::Bool(false)
        );
        assert_eq!(
            state.evaluate_value("root", "Or(Problem(is42), is42 < 42)"),
            MetricValue::Bool(false)
        );
        assert_eq!(
            state.evaluate_value("root", "Or(Problem(not_found), not_found == 'Hello')"),
            MetricValue::Bool(true)
        );
        assert_problem!(
            state.evaluate_value("root", "Or(not_found == 'Hello', Problem(not_found))"),
            "SyntaxError: Metric 'not_found' Not Found in 'root'"
        );

        // Ensure evaluation for action converts vector values.
        assert_eq!(
            state.evaluate_value("root", "[0==0]"),
            MetricValue::Vector(vec![MetricValue::Bool(true)])
        );
        assert_eq!(
            state.eval_action_metric("root", &ValueSource::try_from_expression("[0==0]").unwrap()),
            MetricValue::Bool(true)
        );

        assert_eq!(
            state.evaluate_value("root", "[0==0, 0==0]"),
            MetricValue::Vector(vec![MetricValue::Bool(true), MetricValue::Bool(true)])
        );
        assert_eq!(
            state.eval_action_metric(
                "root",
                &ValueSource::try_from_expression("[0==0, 0==0]").unwrap()
            ),
            MetricValue::Vector(vec![MetricValue::Bool(true), MetricValue::Bool(true)])
        );

        // Test regex operations
        assert_eq!(
            state.eval_action_metric(
                "root",
                &ValueSource::try_from_expression("StringMatches('abcd', '^a.c')").unwrap()
            ),
            MetricValue::Bool(true)
        );
        assert_eq!(
            state.eval_action_metric(
                "root",
                &ValueSource::try_from_expression("StringMatches('abcd', 'a.c$')").unwrap()
            ),
            MetricValue::Bool(false)
        );
        assert_problem!(
            state.eval_action_metric(
                "root",
                &ValueSource::try_from_expression("StringMatches('abcd', '[[')").unwrap()
            ),
            "SyntaxError: Could not parse `[[` as regex"
        );
    }

    // Test caching after evaluating static values
    #[fuchsia::test]
    fn test_caching_after_evaluation() {
        let metrics: Metrics = [(
            "root".to_string(),
            [
                ("is42".to_string(), ValueSource::try_from_expression("42").unwrap()),
                ("is43".to_string(), ValueSource::try_from_expression("is42 + 1").unwrap()),
                (
                    "unhandled".to_string(),
                    ValueSource::new(Metric::Hardcoded(unhandled_type("Unhandled"))),
                ),
            ]
            .iter()
            .cloned()
            .collect(),
        )]
        .iter()
        .cloned()
        .collect();

        let state = MetricState::new(&metrics, Fetcher::FileData(EMPTY_FILE_FETCHER.clone()), None);
        let trial_state =
            MetricState::new(&metrics, Fetcher::TrialData(EMPTY_TRIAL_FETCHER.clone()), None);

        // Test correct initialization of RefCells
        assert_eq!(
            *state.metrics.get("root").unwrap().get("is42").unwrap().cached_value.borrow(),
            None
        );
        assert_eq!(
            *state.metrics.get("root").unwrap().get("is43").unwrap().cached_value.borrow(),
            None
        );
        assert_eq!(
            *state.metrics.get("root").unwrap().get("unhandled").unwrap().cached_value.borrow(),
            None
        );
        assert_eq!(
            *trial_state.metrics.get("root").unwrap().get("is42").unwrap().cached_value.borrow(),
            None
        );
        assert_eq!(
            *trial_state.metrics.get("root").unwrap().get("is43").unwrap().cached_value.borrow(),
            None
        );
        assert_eq!(
            *trial_state
                .metrics
                .get("root")
                .unwrap()
                .get("unhandled")
                .unwrap()
                .cached_value
                .borrow(),
            None
        );

        // Test correct caching of values after evaluation
        // Evaluating single metric
        assert_eq!(state.evaluate_value("root", "is42"), MetricValue::Int(42));
        assert_eq!(
            *state.metrics.get("root").unwrap().get("is42").unwrap().cached_value.borrow(),
            Some(MetricValue::Int(42))
        );
        assert_eq!(trial_state.evaluate_value("root", "is42"), MetricValue::Int(42));
        assert_eq!(
            *trial_state.metrics.get("root").unwrap().get("is42").unwrap().cached_value.borrow(),
            Some(MetricValue::Int(42))
        );

        // Evaluating metric with a nested metric
        // Ensure the previous metric cached value is not modified and new metric is
        // correctly stored
        assert_eq!(state.evaluate_value("root", "is43"), MetricValue::Int(43));
        assert_eq!(
            *state.metrics.get("root").unwrap().get("is42").unwrap().cached_value.borrow(),
            Some(MetricValue::Int(42))
        );
        assert_eq!(
            *state.metrics.get("root").unwrap().get("is43").unwrap().cached_value.borrow(),
            Some(MetricValue::Int(43))
        );
        assert_eq!(trial_state.evaluate_value("root", "is43"), MetricValue::Int(43));
        assert_eq!(
            *trial_state.metrics.get("root").unwrap().get("is42").unwrap().cached_value.borrow(),
            Some(MetricValue::Int(42))
        );
        assert_eq!(
            *trial_state.metrics.get("root").unwrap().get("is43").unwrap().cached_value.borrow(),
            Some(MetricValue::Int(43))
        );

        // Evaluating Hardcoded Metric and ensuring correct caching behavior
        state.evaluate_value("root", "unhandled");
        assert_problem!(
            (*state.metrics.get("root").unwrap().get("unhandled").unwrap().cached_value.borrow())
                .as_ref()
                .unwrap(),
            "UnhandledType: Unhandled"
        );
        trial_state.evaluate_value("root", "unhandled");
        assert_problem!(
            (*trial_state
                .metrics
                .get("root")
                .unwrap()
                .get("unhandled")
                .unwrap()
                .cached_value
                .borrow())
            .as_ref()
            .unwrap(),
            "UnhandledType: Unhandled"
        );
    }

    macro_rules! eval {
        ($e:expr) => {
            MetricState::evaluate_math($e)
        };
    }

    // TODO(fxbug.dev/58922): Modify or probably delete this function after better error design.
    #[fuchsia::test]
    fn test_missing_hacks() -> Result<(), Error> {
        assert_eq!(eval!("Missing(2>'a')"), MetricValue::Bool(true));
        assert_eq!(eval!("Missing([])"), MetricValue::Bool(true));
        assert_eq!(eval!("Missing([2>'a'])"), MetricValue::Bool(true));
        assert_eq!(eval!("Missing([2>'a', 2>'a'])"), MetricValue::Bool(false));
        assert_eq!(eval!("Missing([2>1])"), MetricValue::Bool(false));
        assert_eq!(eval!("Or(Missing(2>'a'), 2>'a')"), MetricValue::Bool(true));
        Ok(())
    }

    #[fuchsia::test]
    fn test_bubble_up_ignore() {
        let simple_foo = Problem::ValueError("foo".to_string());
        let simple_bar = Problem::ValueError("bar".to_string());
        let ignore_foo = Problem::Ignore(vec![simple_foo.clone()]);
        let ignore_bar = Problem::Ignore(vec![simple_bar.clone()]);
        let dont_ignore_fb = Problem::Multiple(vec![simple_foo.clone(), simple_bar.clone()]);
        let dont_ignore_fi = Problem::Multiple(vec![simple_foo.clone(), ignore_bar.clone()]);
        let dont_ignore_ib = Problem::Multiple(vec![ignore_foo.clone(), simple_bar.clone()]);
        let ignore_fb = Problem::Multiple(vec![ignore_foo.clone(), ignore_bar.clone()]);

        assert_problem!(MetricValue::Problem(bubble_up_ignore(simple_foo)), "ValueError: foo");
        assert_problem!(MetricValue::Problem(bubble_up_ignore(simple_bar)), "ValueError: bar");
        assert_problem!(
            MetricValue::Problem(bubble_up_ignore(ignore_foo)),
            "Ignore: ValueError: foo"
        );
        assert_problem!(
            MetricValue::Problem(bubble_up_ignore(ignore_bar)),
            "Ignore: ValueError: bar"
        );
        assert_problem!(
            MetricValue::Problem(bubble_up_ignore(dont_ignore_fb)),
            "MultipleErrors: [ValueError: foo; ValueError: bar; ]"
        );
        assert_problem!(
            MetricValue::Problem(bubble_up_ignore(dont_ignore_fi)),
            "MultipleErrors: [ValueError: foo; Ignore: ValueError: bar; ]"
        );
        assert_problem!(
            MetricValue::Problem(bubble_up_ignore(dont_ignore_ib)),
            "MultipleErrors: [Ignore: ValueError: foo; ValueError: bar; ]"
        );
        assert_problem!(
            MetricValue::Problem(bubble_up_ignore(ignore_fb)),
            "Ignore: [ValueError: foo; ValueError: bar; ]"
        );
    }

    #[fuchsia::test]
    fn test_ignores_in_expressions() {
        let dbz = "ValueError: Division by zero";
        assert_problem!(eval!("Count([1, 2, 3/0])"), dbz);
        assert_problem!(
            eval!("Count([1/0, 2, 3/0])"),
            format!("MultipleErrors: [{}; {}; ]", dbz, dbz)
        );
        assert_problem!(eval!("Count([1/?0, 2, 3/?0])"), format!("Ignore: [{}; {}; ]", dbz, dbz));
        assert_problem!(
            eval!("Count([1/0, 2, 3/?0])"),
            format!("MultipleErrors: [{}; Ignore: {}; ]", dbz, dbz)
        );
        // And() short-circuits so it will only see the first error
        assert_problem!(eval!("And(1 > 0, 3/0 > 0)"), dbz);
        assert_problem!(eval!("And(1/0 > 0, 3/0 > 0)"), dbz);
        assert_problem!(eval!("And(1/?0 > 0, 3/?0 > 0)"), format!("Ignore: {}", dbz));
        assert_problem!(eval!("And(1/?0 > 0, 3/0 > 0)"), format!("Ignore: {}", dbz));
        assert_problem!(eval!("1 == 3/0"), dbz);
        assert_problem!(eval!("1/0 == 3/0"), format!("MultipleErrors: [{}; {}; ]", dbz, dbz));
        assert_problem!(eval!("1/?0 == 3/?0"), format!("Ignore: [{}; {}; ]", dbz, dbz));
        assert_problem!(
            eval!("1/?0 == 3/0"),
            format!("MultipleErrors: [Ignore: {}; {}; ]", dbz, dbz)
        );
        assert_problem!(eval!("1 + 3/0"), dbz);
        assert_problem!(eval!("1/0 + 3/0"), format!("MultipleErrors: [{}; {}; ]", dbz, dbz));
        assert_problem!(eval!("1/?0 + 3/?0"), format!("Ignore: [{}; {}; ]", dbz, dbz));
        assert_problem!(
            eval!("1/?0 + 3/0"),
            format!("MultipleErrors: [Ignore: {}; {}; ]", dbz, dbz)
        );
    }

    /// Make sure that checked divide doesn't hide worse errors in its arguments
    #[fuchsia::test]
    fn test_checked_divide_preserves_errors() {
        let san = "Missing: String(a) not numeric";
        assert_problem!(eval!("(1+'a')/1"), san);
        assert_problem!(eval!("(1+'a')/?1"), san);
        assert_problem!(eval!("1/?(1+'a')"), san);
        assert_problem!(eval!("(1+'a')/?(1+'a')"), format!("MultipleErrors: [{}; {}; ]", san, san));
        // If the numerator has a Problem and the denominator doesn't, it won't get to the divide
        // logic, so both / and /? won't observe the "division by zero"
        assert_problem!(eval!("(1+'a')/0"), san);
        assert_problem!(eval!("(1+'a')/?0"), san);
        assert_problem!(eval!("(1+'a')//1"), san);
        assert_problem!(eval!("(1+'a')//?1"), san);
        assert_problem!(eval!("1//?(1+'a')"), san);
        assert_problem!(
            eval!("(1+'a')//?(1+'a')"),
            format!("MultipleErrors: [{}; {}; ]", san, san)
        );
        assert_problem!(eval!("(1+'a')//0"), san);
        assert_problem!(eval!("(1+'a')//?0"), san);
    }

    #[fuchsia::test]
    fn test_time() -> Result<(), Error> {
        let metrics = Metrics::new();
        let files = vec![];
        let state_1234 =
            MetricState::new(&metrics, Fetcher::FileData(FileDataFetcher::new(&files)), Some(1234));
        let state_missing =
            MetricState::new(&metrics, Fetcher::FileData(FileDataFetcher::new(&files)), None);
        let now_expression = parse::parse_expression("Now()").unwrap();
        assert_problem!(MetricState::evaluate_math("Now()"), "Missing: No valid time available");
        assert_eq!(state_1234.evaluate_expression(&now_expression), MetricValue::Int(1234));
        assert_problem!(
            state_missing.evaluate_expression(&now_expression),
            "Missing: No valid time available"
        );
        Ok(())
    }

    #[fuchsia::test]
    fn test_expression_context() {
        // Check correct error behavior when building expression from selector string
        let selector_expr = "INSPECT:foo:bar:baz".to_string();
        assert_eq!(
            format!("{:?}", ExpressionContext::try_from(selector_expr).err().unwrap()),
            "Expression Error: \n0: at line 0, in Eof:\nINSPECT:foo:bar:baz\n       ^\n\n"
        );

        // Check correct error behavior when building expression from invalid expression string
        let invalid_expr = "1 *".to_string();
        assert_eq!(
            format!("{:?}", ExpressionContext::try_from(invalid_expr).err().unwrap()),
            concat!("Expression Error: \n0: at line 0, in Eof:\n1 *\n  ^\n\n")
        );

        // Check expression correctly built from valid expression
        let valid_expr = "42 + 1";
        let parsed_expression = parse::parse_expression(&valid_expr).unwrap();
        assert_eq!(
            ExpressionContext::try_from(valid_expr.to_string()).unwrap(),
            ExpressionContext { raw_expression: valid_expr.to_string(), parsed_expression }
        );
    }

    #[fuchsia::test]
    fn test_not_valid_cycle_same_variable() {
        let metrics: Metrics = [
            (
                "root".to_string(),
                [
                    ("is42".to_string(), ValueSource::try_from_expression("42").unwrap()),
                    (
                        "shouldBe42".to_string(),
                        ValueSource::try_from_expression("is42 + 0").unwrap(),
                    ),
                ]
                .iter()
                .cloned()
                .collect(),
            ),
            (
                "n2".to_string(),
                [(
                    "is42".to_string(),
                    ValueSource::try_from_expression("root::shouldBe42").unwrap(),
                )]
                .iter()
                .cloned()
                .collect(),
            ),
        ]
        .iter()
        .cloned()
        .collect();
        let state = MetricState::new(&metrics, Fetcher::FileData(EMPTY_FILE_FETCHER.clone()), None);

        // Evaluation with the same variable name but different namespace should be successful.
        assert_eq!(state.evaluate_value("n2", "is42"), MetricValue::Int(42));
    }

    #[fuchsia::test]
    fn test_cycle_detected_correctly() {
        // Cycle is between n2::a -> n2::c -> root::b -> n2::a
        let metrics: Metrics = [
            (
                "root".to_string(),
                [
                    ("is42".to_string(), ValueSource::try_from_expression("42").unwrap()),
                    (
                        "shouldBe62".to_string(),
                        ValueSource::try_from_expression("is42 + 1 + n2::is19").unwrap(),
                    ),
                    ("b".to_string(), ValueSource::try_from_expression("is42 + n2::a").unwrap()),
                ]
                .iter()
                .cloned()
                .collect(),
            ),
            (
                "n2".to_string(),
                [
                    ("is19".to_string(), ValueSource::try_from_expression("19").unwrap()),
                    (
                        "shouldBe44".to_string(),
                        ValueSource::try_from_expression("root::is42 + 2").unwrap(),
                    ),
                    ("a".to_string(), ValueSource::try_from_expression("is19 + c").unwrap()),
                    (
                        "c".to_string(),
                        ValueSource::try_from_expression("root::b + root::is42").unwrap(),
                    ),
                ]
                .iter()
                .cloned()
                .collect(),
            ),
        ]
        .iter()
        .cloned()
        .collect();
        let state = MetricState::new(&metrics, Fetcher::FileData(EMPTY_FILE_FETCHER.clone()), None);

        // Evaluation when a cycle is not encountered should be successful.
        assert_eq!(state.evaluate_value("root", "shouldBe62 + 1"), MetricValue::Int(63));

        // Check evaluation with a cycle leads to an 'EvaluationError'.
        assert_problem!(
            state.evaluate_value("root", "n2::a"),
            "EvaluationError: Cycle encountered while evaluating variable n2::a in the expression"
        );

        // Check if the stack was reset properly after problem
        assert_eq!(state.evaluate_value("root", "n2::shouldBe44"), MetricValue::Int(44));

        // Check evaluation with a cycle leads to an 'EvaluationError'.
        assert_problem!(
            state.evaluate_value("root", "b"),
            "EvaluationError: Cycle encountered while evaluating variable n2::a in the expression"
        );

        // Check evaluation with a cycle leads to an 'EvaluationError'.
        assert_problem!(
            state.evaluate_value("root", "n2::c"),
            "EvaluationError: Cycle encountered while evaluating variable n2::a in the expression"
        );

        // Check if the stack was reset properly after problem
        assert_eq!(state.evaluate_value("root", "shouldBe62"), MetricValue::Int(62));
    }
    // Correct operation of annotations is tested via annotation_tests.triage.
}
