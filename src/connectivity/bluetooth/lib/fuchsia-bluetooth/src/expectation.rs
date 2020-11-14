// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    pretty::{BoxAllocator, DocAllocator},
    std::{
        fmt::{self, Debug, Display, Formatter},
        sync::Arc,
    },
};

/// Asynchronous extensions to Expectation Predicates
pub mod asynchronous;
/// Expectations for the host driver
pub mod host_driver;
/// Expectations for remote peers
pub mod peer;
/// Useful convenience methods and macros for working with expectations
#[macro_use]
pub mod prelude;
/// Tests for the expectation module
#[cfg(test)]
pub mod test;

/// A String whose `Debug` implementation pretty-prints. Used when we need to return a string which
/// we know will be printed through it's 'Debug' implementation, but we want to ensure that it is
/// not further escaped (for example, because it already is escaped, or because it contains
/// characters that we do not want to be escaped).
///
/// Essentially, formatting a DebugString with '{:?}' formats as if it was '{}'
#[derive(Clone, PartialEq)]
pub struct DebugString(String);

// The default formatting width for pretty printing output
const FMT_CHAR_WIDTH: usize = 100;

impl fmt::Debug for DebugString {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

/// Simplified `DocBuilder` alias used in this file
type DocBuilder<'a> = pretty::DocBuilder<'a, BoxAllocator>;

fn parens<'a>(alloc: &'a BoxAllocator, doc: DocBuilder<'a>) -> DocBuilder<'a> {
    alloc.text("(").append(doc).append(alloc.text(")"))
}

/// Functionalized Standard Debug Formatting
fn show_debug<T: Debug>(t: &T) -> String {
    format!("{:?}", t)
}

/// Function to compare two `T`s
type Comparison<T> = Arc<dyn Fn(&T, &T) -> bool + Send + Sync + 'static>;
/// Function to show a representation of a given `T`
type Show<T> = Arc<dyn Fn(&T) -> String + Send + Sync + 'static>;

/// A Boolean predicate on type `T`. Predicate functions are a boolean algebra
/// just as raw boolean values are; they an be ANDed, ORed, NOTed. This allows
/// a clear and concise language for declaring test expectations.
pub enum Predicate<T> {
    Equal(Arc<T>, Comparison<T>, Show<T>),
    And(Box<Predicate<T>>, Box<Predicate<T>>),
    Or(Box<Predicate<T>>, Box<Predicate<T>>),
    Not(Box<Predicate<T>>),
    Predicate(Arc<dyn Fn(&T) -> bool + Send + Sync>, String),
    // Since we can't use an existential:
    //  for<U> Over(Predicate<U>, Fn(&T) -> &U)
    // we use the trait `IsOver` to hide the type `U` of the intermediary
    Over(Arc<dyn IsOver<T> + Send + Sync>),
    // Since we can't use an existential:
    //  for<I> Any(Fn(&T) -> I, Predicate<I::Elem>)
    //  where I::Elem: Debug
    // we use the trait `IsAny` to hide the type `I` of the iterator
    Any(Arc<dyn IsAny<T> + Send + Sync + 'static>),
    // Since we can't use an existential:
    //  for<I> All(Fn(&T) -> I, Predicate<I::Elem>)
    //  where I::Elem: Debug
    // we use the trait `IsAll` to hide the type `I` of the iterator
    All(Arc<dyn IsAll<T> + Send + Sync + 'static>),
}

// Bespoke clone implementation. This implementation is equivalent to the one that would be derived
// by #[derive(Clone)] _except_ for the trait bounds. The derived impl would automatically require
// `T: Clone`, when actually `Predicate<T>` is `Clone` for _all_ `T`, even those that are not
// `Clone`.
impl<T> Clone for Predicate<T> {
    fn clone(&self) -> Self {
        match self {
            Predicate::Equal(t, comp, repr) => {
                Predicate::Equal(t.clone(), comp.clone(), repr.clone())
            }
            Predicate::And(l, r) => Predicate::And(l.clone(), r.clone()),
            Predicate::Or(l, r) => Predicate::Or(l.clone(), r.clone()),
            Predicate::Not(x) => Predicate::Not(x.clone()),
            Predicate::Predicate(p, msg) => Predicate::Predicate(p.clone(), msg.clone()),
            Predicate::Over(x) => Predicate::Over(x.clone()),
            Predicate::Any(x) => Predicate::Any(x.clone()),
            Predicate::All(x) => Predicate::All(x.clone()),
        }
    }
}

impl<T> Debug for Predicate<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.describe(&BoxAllocator).1.pretty(FMT_CHAR_WIDTH).fmt(f)
    }
}

/// At most how many elements of an iterator to show in a falsification, when falsifying `any` or
/// `all`
const MAX_ITER_FALSIFICATIONS: usize = 5;

fn falsify_elem<'p, 't, 'd, T: Debug>(
    pred: &'p Predicate<T>,
    index: usize,
    el: &'t T,
    doc: &'d BoxAllocator,
) -> Option<DocBuilder<'d>> {
    pred.falsify(el, doc).map(move |falsification| {
        doc.text(format!("[{}]", index))
            .append(doc.space().append(doc.text(show_debug(el))).nest(2))
            .append(doc.space().append(doc.text("BECAUSE")))
            .append(doc.space().append(falsification.group()).nest(2))
            .append(doc.text(","))
            .group()
    })
}

fn fmt_falsifications<'d>(
    i: impl Iterator<Item = DocBuilder<'d>>,
    doc: &'d BoxAllocator,
) -> Option<DocBuilder<'d>> {
    i.take(MAX_ITER_FALSIFICATIONS).fold(None, |acc: Option<DocBuilder<'d>>, falsification| {
        Some(acc.map_or(doc.nil(), |d| d.append(doc.space())).append(falsification))
    })
}

/// Trait representation of a Predicate on members of an existential iterator type
pub trait IsAny<T> {
    fn describe<'d>(&self, doc: &'d BoxAllocator) -> DocBuilder<'d>;
    fn falsify_any<'d>(&self, t: &T, doc: &'d BoxAllocator) -> Option<DocBuilder<'d>>;
}

impl<T: 'static, Elem: Debug + 'static> IsAny<T> for Predicate<Elem>
where
    for<'a> &'a T: IntoIterator<Item = &'a Elem>,
{
    fn describe<'d>(&self, doc: &'d BoxAllocator) -> DocBuilder<'d> {
        doc.text("ANY").append(doc.space().append(self.describe(doc)).nest(2)).group()
    }
    fn falsify_any<'d>(&self, t: &T, doc: &'d BoxAllocator) -> Option<DocBuilder<'d>> {
        if t.into_iter().any(|e| self.satisfied(e)) {
            None
        } else {
            // All are falsifications, so display all (up to MAX_ITER_FALSIFICATIONS)
            fmt_falsifications(
                t.into_iter().enumerate().filter_map(|(i, el)| falsify_elem(&self, i, &el, doc)),
                doc,
            )
            .or(Some(doc.text("<empty>")))
        }
    }
}

/// Trait representation of a Predicate on members of an existential iterator type
pub trait IsAll<T> {
    fn describe<'d>(&self, doc: &'d BoxAllocator) -> DocBuilder<'d>;
    fn falsify_all<'d>(&self, t: &T, doc: &'d BoxAllocator) -> Option<DocBuilder<'d>>;
}

impl<T: 'static, Elem: Debug + 'static> IsAll<T> for Predicate<Elem>
where
    for<'a> &'a T: IntoIterator<Item = &'a Elem>,
{
    fn describe<'d>(&self, doc: &'d BoxAllocator) -> DocBuilder<'d> {
        doc.text("ALL").append(doc.space().append(self.describe(doc)).nest(2)).group()
    }

    fn falsify_all<'d>(&self, t: &T, doc: &'d BoxAllocator) -> Option<DocBuilder<'d>> {
        fmt_falsifications(
            t.into_iter().enumerate().filter_map(|(i, el)| falsify_elem(&self, i, &el, doc)),
            doc,
        )
    }
}

/// A wrapping newtype to differentiate those types which are iterators themselves, from those that
/// implement `IntoIterator`.
struct OverIterator<Elem>(Predicate<Elem>);

impl<'e, Iter, Elem> IsAll<Iter> for OverIterator<Elem>
where
    Elem: Debug + 'static,
    Iter: Iterator<Item = &'e Elem> + Clone,
{
    fn describe<'d>(&self, doc: &'d BoxAllocator) -> DocBuilder<'d> {
        doc.text("ALL").append(doc.space().append(self.0.describe(doc)).nest(2)).group()
    }

    fn falsify_all<'d>(&self, iter: &Iter, doc: &'d BoxAllocator) -> Option<DocBuilder<'d>> {
        fmt_falsifications(
            iter.clone().enumerate().filter_map(|(i, el)| falsify_elem(&self.0, i, &el, doc)),
            doc,
        )
    }
}

impl<'e, Iter, Elem> IsAny<Iter> for OverIterator<Elem>
where
    Elem: Debug + 'static,
    Iter: Iterator<Item = &'e Elem> + Clone,
{
    fn describe<'d>(&self, doc: &'d BoxAllocator) -> DocBuilder<'d> {
        doc.text("ANY").append(doc.space().append(self.0.describe(doc)).nest(2)).group()
    }

    fn falsify_any<'d>(&self, iter: &Iter, doc: &'d BoxAllocator) -> Option<DocBuilder<'d>> {
        if iter.clone().any(|e| self.0.satisfied(e)) {
            None
        } else {
            // All are falsifications, so display all (up to MAX_ITER_FALSIFICATIONS)
            fmt_falsifications(
                iter.clone().enumerate().filter_map(|(i, el)| falsify_elem(&self.0, i, &el, doc)),
                doc,
            )
            .or(Some(doc.text("<empty>")))
        }
    }
}

/// A Predicate lifted over a mapping function from `T` to `U`. Such mappings allow converting a
/// predicate from one type - say, a struct field - to an upper type - such as a whole struct -
/// whilst maintaining information about the relationship.
enum OverPred<T, U> {
    ByRef(Predicate<U>, Arc<dyn Fn(&T) -> &U + Send + Sync + 'static>, String),
    ByValue(Predicate<U>, Arc<dyn Fn(&T) -> U + Send + Sync + 'static>, String),
}

/// Trait representation of OverPred where `U` is existential
pub trait IsOver<T> {
    fn describe<'d>(&self, doc: &'d BoxAllocator) -> DocBuilder<'d>;
    fn falsify_over<'d>(&self, t: &T, doc: &'d BoxAllocator) -> Option<DocBuilder<'d>>;
}

impl<T, U> IsOver<T> for OverPred<T, U> {
    fn describe<'a>(&self, doc: &'a BoxAllocator) -> DocBuilder<'a> {
        let (pred, path) = match self {
            OverPred::ByRef(pred, _, path) => (pred, path),
            OverPred::ByValue(pred, _, path) => (pred, path),
        };
        doc.as_string(path).append(doc.space()).append(pred.describe(doc)).group()
    }
    fn falsify_over<'d>(&self, t: &T, doc: &'d BoxAllocator) -> Option<DocBuilder<'d>> {
        let (d, path) = match self {
            OverPred::ByRef(pred, project, path) => (pred.falsify((project)(t), doc), path),
            OverPred::ByValue(pred, project, path) => (pred.falsify(&(project)(t), doc), path),
        };
        d.map(|falsification| {
            doc.as_string(path).append(doc.space().append(falsification).nest(2)).group()
        })
    }
}

impl<T> Predicate<T> {
    fn is_equal(&self) -> bool {
        if let Predicate::Equal(_, _, _) = self {
            true
        } else {
            false
        }
    }
    pub fn describe<'a>(&self, doc: &'a BoxAllocator) -> DocBuilder<'a> {
        match self {
            Predicate::Equal(expected, _, repr) => {
                doc.text("==").append(doc.space()).append(doc.text(repr(expected))).group()
            }
            Predicate::And(left, right) => parens(doc, left.describe(doc))
                .append(doc.space())
                .append(doc.text("AND"))
                .append(doc.space())
                .append(parens(doc, right.describe(doc)))
                .group(),
            Predicate::Or(left, right) => parens(doc, left.describe(doc))
                .nest(2)
                .append(doc.space())
                .append(doc.text("OR"))
                .append(doc.space())
                .append(parens(doc, right.describe(doc)).nest(2))
                .group(),
            // Succinct override for NOT-Equal cases
            Predicate::Not(inner) if inner.is_equal() => {
                if let Predicate::Equal(expected, _, repr) = &**inner {
                    doc.text("!=").append(doc.space()).append(doc.text(repr(&*expected))).group()
                } else {
                    // This branch is guarded by inner.is_equal()
                    unreachable!()
                }
            }
            Predicate::Not(inner) => {
                doc.text("NOT").append(doc.space().append(inner.describe(doc)).nest(2)).group()
            }
            Predicate::Predicate(_, desc) => doc.as_string(desc).group(),
            Predicate::Over(over) => over.describe(doc),
            Predicate::Any(any) => any.describe(doc),
            Predicate::All(all) => all.describe(doc),
        }
    }
    /// Provide a minimized falsification of the predicate, if possible
    pub fn falsify<'d>(&self, t: &T, doc: &'d BoxAllocator) -> Option<DocBuilder<'d>> {
        match self {
            Predicate::Equal(expected, are_eq, repr) => {
                if are_eq(expected, t) {
                    None
                } else {
                    Some(
                        doc.text(repr(t))
                            .append(doc.space())
                            .append(doc.text("!="))
                            .append(doc.space())
                            .append(doc.text(repr(expected)))
                            .group(),
                    )
                }
            }
            Predicate::And(left, right) => match (left.falsify(t, doc), right.falsify(t, doc)) {
                (Some(l), Some(r)) => Some(
                    parens(doc, l)
                        .append(doc.space())
                        .append(doc.text("AND"))
                        .append(doc.space())
                        .append(parens(doc, r))
                        .group(),
                ),
                (Some(l), None) => Some(l),
                (None, Some(r)) => Some(r),
                (None, None) => None,
            },
            Predicate::Or(left, right) => left
                .falsify(t, doc)
                .and_then(|l| right.falsify(t, doc).map(|r| parens(doc, l).append(parens(doc, r)))),
            Predicate::Not(inner) => match inner.falsify(t, doc) {
                Some(_) => None,
                None => match &**inner {
                    Predicate::Equal(expected, _, repr) => Some(
                        doc.text(repr(t))
                            .append(doc.space())
                            .append(doc.text("=="))
                            .append(doc.space())
                            .append(doc.text(repr(&expected))),
                    ),
                    _ => Some(
                        doc.text("NOT")
                            .append(doc.space().append(inner.describe(doc)).nest(2))
                            .group(),
                    ),
                },
            },
            Predicate::Predicate(pred, desc) => {
                if pred(t) {
                    None
                } else {
                    Some(doc.text("NOT").append(doc.space().append(doc.as_string(desc)).nest(2)))
                }
            }
            Predicate::Over(over) => over.falsify_over(t, doc),
            Predicate::Any(any) => any.falsify_any(t, doc),
            Predicate::All(all) => all.falsify_all(t, doc),
        }
    }
    pub fn satisfied<'t>(&self, t: &'t T) -> bool {
        match self {
            Predicate::Equal(expected, are_eq, _) => are_eq(t, expected),
            Predicate::And(left, right) => left.satisfied(t) && right.satisfied(t),
            Predicate::Or(left, right) => left.satisfied(t) || right.satisfied(t),
            Predicate::Not(inner) => !inner.satisfied(t),
            Predicate::Predicate(pred, _) => pred(t),
            Predicate::Over(over) => over.falsify_over(t, &BoxAllocator).is_none(),
            Predicate::Any(any) => any.falsify_any(t, &BoxAllocator).is_none(),
            Predicate::All(all) => all.falsify_all(t, &BoxAllocator).is_none(),
        }
    }
    pub fn assert_satisfied(&self, t: &T) -> Result<(), DebugString> {
        let doc = BoxAllocator;
        match self.falsify(t, &doc) {
            Some(falsification) => {
                let d = doc
                    .text("FAILED EXPECTATION")
                    .append(doc.newline().append(self.describe(&doc)).nest(2))
                    .append(doc.newline().append(doc.text("BECAUSE")))
                    .append(doc.newline().append(falsification).nest(2));
                Err(DebugString(d.1.pretty(FMT_CHAR_WIDTH).to_string()))
            }
            None => Ok(()),
        }
    }
    pub fn and(self, rhs: Predicate<T>) -> Predicate<T> {
        Predicate::And(Box::new(self), Box::new(rhs))
    }
    pub fn or(self, rhs: Predicate<T>) -> Predicate<T> {
        Predicate::Or(Box::new(self), Box::new(rhs))
    }
    pub fn not(self) -> Predicate<T> {
        Predicate::Not(Box::new(self))
    }
    /// Construct a simple predicate function
    pub fn predicate<F, S>(f: F, label: S) -> Predicate<T>
    where
        F: for<'t> Fn(&'t T) -> bool + Send + Sync + 'static,
        S: Into<String>,
    {
        Predicate::Predicate(Arc::new(f), label.into())
    }
}

/// Convenient implementations that rely on `Debug` to provide output on falsification, and
/// `PartialEq` to provide equality. If you wish to create predicates for `?Debug` types, use the
/// `Predicate` or `Equal` constructors directly.
impl<T: PartialEq + Debug + 'static> Predicate<T> {
    /// Construct a Predicate that expects two `T`s to be equal
    pub fn equal(t: T) -> Predicate<T> {
        Predicate::Equal(Arc::new(t), Arc::new(T::eq), Arc::new(show_debug))
    }
    /// Construct a Predicate that expects two `T`s to be non-equal
    pub fn not_equal(t: T) -> Predicate<T> {
        Predicate::Equal(Arc::new(t), Arc::new(T::eq), Arc::new(show_debug)).not()
    }
}

/// Predicates on iterable types, those convertible into iterators via IntoIterator (e.g. Vec<_>)
impl<Elem, T> Predicate<T>
where
    T: Send + Sync + 'static,
    for<'a> &'a T: IntoIterator<Item = &'a Elem>,
    Elem: Debug + Send + Sync + 'static,
{
    /// Construct a predicate that all elements of an iterable type match a given predicate
    /// If the iterable is empty, the predicate will succeed
    pub fn all(pred: Predicate<Elem>) -> Predicate<T> {
        Predicate::All(Arc::new(pred))
    }

    /// Construct a predicate that at least one element of an iterable type match a given predicate
    /// If the iterable is empty, the predicate will fail
    pub fn any(pred: Predicate<Elem>) -> Predicate<T> {
        Predicate::Any(Arc::new(pred))
    }
}

/// Predicates on types which are an `Iterator`
impl<'e, Elem, Iter> Predicate<Iter>
where
    Iter: Iterator<Item = &'e Elem> + Clone,
    Elem: Debug + Send + Sync + 'static,
{
    /// Construct a predicate that all elements of an Iterator match a given predicate
    /// If the Iterator is empty, the predicate will succeed
    pub fn iter_all(pred: Predicate<Elem>) -> Predicate<Iter> {
        Predicate::All(Arc::new(OverIterator(pred)))
    }

    /// Construct a predicate that at least one element of an Iterator match a given predicate
    /// If the iterator is empty, the predicate will fail
    pub fn iter_any(pred: Predicate<Elem>) -> Predicate<Iter> {
        Predicate::Any(Arc::new(OverIterator(pred)))
    }
}

impl<U: Send + Sync + 'static> Predicate<U> {
    /// Lift a predicate over a projection from `T -> &U`. This constructs a Predicate<T> from a
    /// predicate on some field (or arbitrary projection) of type `U`.
    ///
    /// This allows:
    ///   * Creating a Predicate on a struct from a predicate on a field (or field of a field) of
    ///     that struct.
    ///   * Creating a Predicate on a type from a predicate on some value computable from that type
    ///
    /// Compared to writing an arbitrary function using the `predicate()` method, an
    /// `over` projection allows for more minimal falsification and better error reporting in
    /// failure cases.
    ///
    /// Use `over` when your projection function returns a reference. If you need to return a value
    /// (for example, a temporary whose reference lifetime would not escape the function), use
    /// `over_value`.
    pub fn over<F, T, P>(self, project: F, path: P) -> Predicate<T>
    where
        F: Fn(&T) -> &U + Send + Sync + 'static,
        P: Into<String>,
        T: 'static,
    {
        Predicate::Over(Arc::new(OverPred::ByRef(self, Arc::new(project), path.into())))
    }

    /// Lift a predicate over a projection from `T -> U`. This constructs a Predicate<T> from a
    /// predicate on some field (or arbitrary projection) of type `U`.
    ///
    /// This allows:
    ///   * Creating a Predicate on a struct from a predicate on a field (or field of a field) of
    ///     that struct.
    ///   * Creating a Predicate on a type from a predicate on some value computable from that type
    ///
    /// Compared to writing an arbitrary function using the `predicate()` method, an
    /// `over` projection allows for more minimal falsification and better error reporting in
    /// failure cases.
    ///
    /// Use `over_value` when your projection function needs to return a value. If you can return a
    /// reference, use `over`.
    pub fn over_value<F, T, P>(self, project: F, path: P) -> Predicate<T>
    where
        F: Fn(&T) -> U + Send + Sync + 'static,
        P: Into<String>,
        T: 'static,
    {
        Predicate::Over(Arc::new(OverPred::ByValue(self, Arc::new(project), path.into())))
    }
}

/// A macro for allowing more ergonomic projection of predicates 'over' some projection function
/// that focuses on a subset of a data type.
///
/// Specify the name of the parent type, the field to focus on, and then pass in the predicate over
/// that field
///
/// usage: over!(Type:field, predicate)
///
/// e.g.
///
/// ```
/// let predicate = over!(RemoteDevice:name, P::equal(Some("INCORRECT_NAME".to_string())));
/// ```
#[macro_export]
macro_rules! over {
    ($type:ty : $selector:tt, $pred:expr) => {
        $pred.over(|var: &$type| &var.$selector, format!(".{}", stringify!($selector)))
    };
}

/// A simple macro to allow idiomatic assertion of predicates, producing well formatted
/// falsifications if the predicate fails.
///
/// usage:
///
/// ```
/// let predicate = correct_name().or(correct_address());
/// assert_satisfies!(&test_peer(), predicate);
/// ```
#[macro_export]
macro_rules! assert_satisfies {
    ($subject:expr, $pred:expr) => {
        if let Err(msg) = $pred.assert_satisfied($subject) {
            panic!(msg.0)
        }
    };
}
