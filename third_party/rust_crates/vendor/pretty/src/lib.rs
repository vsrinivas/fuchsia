//! This crate defines a
//! [Wadler-style](http://homepages.inf.ed.ac.uk/wadler/papers/prettier/prettier.pdf)
//! pretty-printing API.
//!
//! Start with with the static functions of [Doc](enum.Doc.html).
//!
//! ## Quick start
//!
//! Let's pretty-print simple sexps!  We want to pretty print sexps like
//!
//! ```lisp
//! (1 2 3)
//! ```
//! or, if the line would be too long, like
//!
//! ```lisp
//! ((1)
//!  (2 3)
//!  (4 5 6))
//! ```
//!
//! A _simple symbolic expression_ consists of a numeric _atom_ or a nested ordered _list_ of
//! symbolic expression children.
//!
//! ```rust
//! # extern crate pretty;
//! # use pretty::*;
//! enum SExp {
//!     Atom(u32),
//!     List(Vec<SExp>),
//! }
//! use SExp::*;
//! # fn main() { }
//! ```
//!
//! We define a simple conversion to a [Doc](enum.Doc.html).  Atoms are rendered as strings; lists
//! are recursively rendered, with spaces between children where appropriate.  Children are
//! [nested]() and [grouped](), allowing them to be laid out in a single line as appropriate.
//!
//! ```rust
//! # extern crate pretty;
//! # use pretty::*;
//! # enum SExp {
//! #     Atom(u32),
//! #     List(Vec<SExp>),
//! # }
//! # use SExp::*;
//! impl SExp {
//!     /// Return a pretty printed format of self.
//!     pub fn to_doc(&self) -> Doc<BoxDoc<()>> {
//!         match *self {
//!             Atom(ref x) => Doc::as_string(x),
//!             List(ref xs) =>
//!                 Doc::text("(")
//!                     .append(Doc::intersperse(xs.into_iter().map(|x| x.to_doc()), Doc::space()).nest(1).group())
//!                     .append(Doc::text(")"))
//!         }
//!     }
//! }
//! # fn main() { }
//! ```
//!
//! Next, we convert the [Doc](enum.Doc.html) to a plain old string.
//!
//! ```rust
//! # extern crate pretty;
//! # use pretty::*;
//! # enum SExp {
//! #     Atom(u32),
//! #     List(Vec<SExp>),
//! # }
//! # use SExp::*;
//! # impl SExp {
//! #     /// Return a pretty printed format of self.
//! #     pub fn to_doc(&self) -> Doc<BoxDoc<()>> {
//! #         match *self {
//! #             Atom(ref x) => Doc::as_string(x),
//! #             List(ref xs) =>
//! #                 Doc::text("(")
//! #                     .append(Doc::intersperse(xs.into_iter().map(|x| x.to_doc()), Doc::space()).nest(1).group())
//! #                     .append(Doc::text(")"))
//! #         }
//! #     }
//! # }
//! impl SExp {
//!     pub fn to_pretty(&self, width: usize) -> String {
//!         let mut w = Vec::new();
//!         self.to_doc().render(width, &mut w).unwrap();
//!         String::from_utf8(w).unwrap()
//!     }
//! }
//! # fn main() { }
//! ```
//!
//! And finally we can test that the nesting and grouping behaves as we expected.
//!
//! ```rust
//! # extern crate pretty;
//! # use pretty::*;
//! # enum SExp {
//! #     Atom(u32),
//! #     List(Vec<SExp>),
//! # }
//! # use SExp::*;
//! # impl SExp {
//! #     /// Return a pretty printed format of self.
//! #     pub fn to_doc(&self) -> Doc<BoxDoc<()>> {
//! #         match *self {
//! #             Atom(ref x) => Doc::as_string(x),
//! #             List(ref xs) =>
//! #                 Doc::text("(")
//! #                     .append(Doc::intersperse(xs.into_iter().map(|x| x.to_doc()), Doc::space()).nest(1).group())
//! #                     .append(Doc::text(")"))
//! #         }
//! #     }
//! # }
//! # impl SExp {
//! #     pub fn to_pretty(&self, width: usize) -> String {
//! #         let mut w = Vec::new();
//! #         self.to_doc().render(width, &mut w).unwrap();
//! #         String::from_utf8(w).unwrap()
//! #     }
//! # }
//! # fn main() {
//! let atom = SExp::Atom(5);
//! assert_eq!("5", atom.to_pretty(10));
//! let list = SExp::List(vec![SExp::Atom(1), SExp::Atom(2), SExp::Atom(3)]);
//! assert_eq!("(1 2 3)", list.to_pretty(10));
//! assert_eq!("\
//! (1
//!  2
//!  3)", list.to_pretty(5));
//! # }
//! ```
//!
//! ## Advanced usage
//!
//! There's a more efficient pattern that uses the [DocAllocator](trait.DocAllocator.html) trait, as
//! implemented by [BoxAllocator](struct.BoxAllocator.html), to allocate
//! [DocBuilder](struct.DocBuilder.html) instances.  See
//! [examples/trees.rs](https://github.com/freebroccolo/pretty.rs/blob/master/examples/trees.rs#L39)
//! for this approach.

#[cfg(feature = "termcolor")]
pub extern crate termcolor;
extern crate typed_arena;

use std::borrow::Cow;
use std::fmt;
use std::io;
use std::ops::Deref;
#[cfg(feature = "termcolor")]
use termcolor::{ColorSpec, WriteColor};

mod render;

#[cfg(feature = "termcolor")]
pub use self::render::TermColored;
pub use self::render::{FmtWrite, IoWrite, Render, RenderAnnotated};

/// The concrete document type. This type is not meant to be used directly. Instead use the static
/// functions on `Doc` or the methods on an `DocAllocator`.
///
/// The `T` parameter is used to abstract over pointers to `Doc`. See `RefDoc` and `BoxDoc` for how
/// it is used
#[derive(Clone, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum Doc<'a, T, A = ()> {
    Nil,
    Append(T, T),
    Group(T),
    Nest(usize, T),
    Space,
    Newline,
    Text(Cow<'a, str>),
    Annotated(A, T),
}

impl<'a, T, A> Doc<'a, T, A> {
    /// An empty document.
    #[inline]
    pub fn nil() -> Doc<'a, T, A> {
        Doc::Nil
    }

    /// The text `t.to_string()`.
    ///
    /// The given text must not contain line breaks.
    #[inline]
    pub fn as_string<U: ToString>(data: U) -> Doc<'a, T, A> {
        Doc::text(data.to_string())
    }

    /// A single newline.
    #[inline]
    pub fn newline() -> Doc<'a, T, A> {
        Doc::Newline
    }

    /// The given text, which must not contain line breaks.
    #[inline]
    pub fn text<U: Into<Cow<'a, str>>>(data: U) -> Doc<'a, T, A> {
        Doc::Text(data.into())
    }

    /// A space.
    #[inline]
    pub fn space() -> Doc<'a, T, A> {
        Doc::Space
    }
}

impl<'a, A> Doc<'a, BoxDoc<'a, A>, A> {
    /// Append the given document after this document.
    #[inline]
    pub fn append<D>(self, that: D) -> Doc<'a, BoxDoc<'a, A>, A>
    where
        D: Into<Doc<'a, BoxDoc<'a, A>, A>>,
    {
        DocBuilder(&BOX_ALLOCATOR, self).append(that).into()
    }

    /// A single document concatenating all the given documents.
    #[inline]
    pub fn concat<I>(docs: I) -> Doc<'a, BoxDoc<'a, A>, A>
    where
        I: IntoIterator,
        I::Item: Into<Doc<'a, BoxDoc<'a, A>, A>>,
    {
        docs.into_iter().fold(Doc::nil(), |a, b| a.append(b))
    }

    /// A single document interspersing the given separator `S` between the given documents.  For
    /// example, if the documents are `[A, B, C, ..., Z]`, this yields `[A, S, B, S, C, S, ..., S, Z]`.
    ///
    /// Compare [the `intersperse` method from the `itertools` crate](https://docs.rs/itertools/0.5.9/itertools/trait.Itertools.html#method.intersperse).
    #[inline]
    pub fn intersperse<I, S>(docs: I, separator: S) -> Doc<'a, BoxDoc<'a, A>, A>
    where
        I: IntoIterator,
        I::Item: Into<Doc<'a, BoxDoc<'a, A>, A>>,
        S: Into<Doc<'a, BoxDoc<'a, A>, A>> + Clone,
        A: Clone,
    {
        let mut result = Doc::nil();
        let mut iter = docs.into_iter();

        if let Some(first) = iter.next() {
            result = result.append(first);

            for doc in iter {
                result = result.append(separator.clone());
                result = result.append(doc);
            }
        }

        result
    }

    /// Mark this document as a group.
    ///
    /// Groups are layed out on a single line if possible.  Within a group, all basic documents with
    /// several possible layouts are assigned the same layout, that is, they are all layed out
    /// horizontally and combined into a one single line, or they are each layed out on their own
    /// line.
    #[inline]
    pub fn group(self) -> Doc<'a, BoxDoc<'a, A>, A> {
        DocBuilder(&BOX_ALLOCATOR, self).group().into()
    }

    /// Increase the indentation level of this document.
    #[inline]
    pub fn nest(self, offset: usize) -> Doc<'a, BoxDoc<'a, A>, A> {
        DocBuilder(&BOX_ALLOCATOR, self).nest(offset).into()
    }

    #[inline]
    pub fn annotate(self, ann: A) -> Doc<'a, BoxDoc<'a, A>, A> {
        DocBuilder(&BOX_ALLOCATOR, self).annotate(ann).into()
    }
}

impl<'a, T, A, S> From<S> for Doc<'a, T, A>
where
    S: Into<Cow<'a, str>>,
{
    fn from(s: S) -> Doc<'a, T, A> {
        Doc::Text(s.into())
    }
}

pub struct Pretty<'a, T, A>
where
    A: 'a,
    T: 'a,
{
    doc: &'a Doc<'a, T, A>,
    width: usize,
}

impl<'a, T, A> fmt::Display for Pretty<'a, T, A>
where
    T: Deref<Target = Doc<'a, T, A>>,
{
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.doc.render_fmt(self.width, f)
    }
}

impl<'a, T, A> Doc<'a, T, A> {
    /// Writes a rendered document to a `std::io::Write` object.
    #[inline]
    pub fn render<'b, W>(&'b self, width: usize, out: &mut W) -> io::Result<()>
    where
        T: Deref<Target = Doc<'b, T, A>>,
        W: ?Sized + io::Write,
    {
        self.render_raw(width, &mut IoWrite::new(out))
    }

    /// Writes a rendered document to a `std::fmt::Write` object.
    #[inline]
    pub fn render_fmt<'b, W>(&'b self, width: usize, out: &mut W) -> fmt::Result
    where
        T: Deref<Target = Doc<'b, T, A>>,
        W: ?Sized + fmt::Write,
    {
        self.render_raw(width, &mut FmtWrite::new(out))
    }

    /// Writes a rendered document to a `RenderAnnotated<A>` object.
    #[inline]
    pub fn render_raw<'b, W>(&'b self, width: usize, out: &mut W) -> Result<(), W::Error>
    where
        T: Deref<Target = Doc<'b, T, A>>,
        W: ?Sized + render::RenderAnnotated<A>,
    {
        render::best(self, width, out)
    }

    /// Returns a value which implements `std::fmt::Display`
    ///
    /// ```
    /// use pretty::Doc;
    /// let doc = Doc::<_>::group(
    ///     Doc::text("hello").append(Doc::space()).append(Doc::text("world"))
    /// );
    /// assert_eq!(format!("{}", doc.pretty(80)), "hello world");
    /// ```
    #[inline]
    pub fn pretty<'b>(&'b self, width: usize) -> Pretty<'b, T, A>
    where
        T: Deref<Target = Doc<'b, T, A>>,
    {
        Pretty { doc: self, width }
    }
}

#[cfg(feature = "termcolor")]
impl<'a, T> Doc<'a, T, ColorSpec> {
    #[inline]
    pub fn render_colored<'b, W>(&'b self, width: usize, out: W) -> io::Result<()>
    where
        T: Deref<Target = Doc<'b, T, ColorSpec>>,
        W: WriteColor,
    {
        render::best(self, width, &mut TermColored::new(out))
    }
}

#[derive(Clone, Eq, Ord, PartialEq, PartialOrd)]
pub struct BoxDoc<'a, A>(Box<Doc<'a, BoxDoc<'a, A>, A>>);

impl<'a, A> fmt::Debug for BoxDoc<'a, A>
where
    A: fmt::Debug,
{
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.0.fmt(f)
    }
}

impl<'a, A> BoxDoc<'a, A> {
    fn new(doc: Doc<'a, BoxDoc<'a, A>, A>) -> BoxDoc<'a, A> {
        BoxDoc(Box::new(doc))
    }
}

impl<'a, A> Deref for BoxDoc<'a, A> {
    type Target = Doc<'a, BoxDoc<'a, A>, A>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

/// The `DocBuilder` type allows for convenient appending of documents even for arena allocated
/// documents by storing the arena inline.
#[derive(Eq, Ord, PartialEq, PartialOrd)]
pub struct DocBuilder<'a, D, A = ()>(pub &'a D, pub Doc<'a, D::Doc, A>)
where
    D: ?Sized + DocAllocator<'a, A> + 'a;

impl<'a, A, D> Clone for DocBuilder<'a, D, A>
where
    A: Clone,
    D: DocAllocator<'a, A> + 'a,
    D::Doc: Clone,
{
    fn clone(&self) -> Self {
        DocBuilder(self.0, self.1.clone())
    }
}

impl<'a, D, A> Into<Doc<'a, D::Doc, A>> for DocBuilder<'a, D, A>
where
    D: ?Sized + DocAllocator<'a, A>,
{
    fn into(self) -> Doc<'a, D::Doc, A> {
        self.1
    }
}

/// The `DocAllocator` trait abstracts over a type which can allocate (pointers to) `Doc`.
pub trait DocAllocator<'a, A = ()> {
    type Doc: Deref<Target = Doc<'a, Self::Doc, A>>;

    fn alloc(&'a self, Doc<'a, Self::Doc, A>) -> Self::Doc;

    /// Allocate an empty document.
    #[inline]
    fn nil(&'a self) -> DocBuilder<'a, Self, A> {
        DocBuilder(self, Doc::Nil)
    }

    /// Allocate a single newline.
    #[inline]
    fn newline(&'a self) -> DocBuilder<'a, Self, A> {
        DocBuilder(self, Doc::Newline)
    }

    /// Allocate a single space.
    #[inline]
    fn space(&'a self) -> DocBuilder<'a, Self, A> {
        DocBuilder(self, Doc::Space)
    }

    /// Allocate a document containing the text `t.to_string()`.
    ///
    /// The given text must not contain line breaks.
    #[inline]
    fn as_string<U: ToString>(&'a self, data: U) -> DocBuilder<'a, Self, A> {
        self.text(data.to_string())
    }

    /// Allocate a document containing the given text.
    ///
    /// The given text must not contain line breaks.
    #[inline]
    fn text<U: Into<Cow<'a, str>>>(&'a self, data: U) -> DocBuilder<'a, Self, A> {
        DocBuilder(self, Doc::Text(data.into()))
    }

    /// Allocate a document concatenating the given documents.
    #[inline]
    fn concat<I>(&'a self, docs: I) -> DocBuilder<'a, Self, A>
    where
        I: IntoIterator,
        I::Item: Into<Doc<'a, Self::Doc, A>>,
    {
        docs.into_iter().fold(self.nil(), |a, b| a.append(b))
    }

    /// Allocate a document that intersperses the given separator `S` between the given documents
    /// `[A, B, C, ..., Z]`, yielding `[A, S, B, S, C, S, ..., S, Z]`.
    ///
    /// Compare [the `intersperse` method from the `itertools` crate](https://docs.rs/itertools/0.5.9/itertools/trait.Itertools.html#method.intersperse).
    #[inline]
    fn intersperse<I, S>(&'a self, docs: I, separator: S) -> DocBuilder<'a, Self, A>
    where
        I: IntoIterator,
        I::Item: Into<Doc<'a, Self::Doc, A>>,
        S: Into<Doc<'a, Self::Doc, A>> + Clone,
    {
        let mut result = self.nil();
        let mut iter = docs.into_iter();

        if let Some(first) = iter.next() {
            result = result.append(first);

            for doc in iter {
                result = result.append(separator.clone());
                result = result.append(doc);
            }
        }

        result
    }
}

impl<'a, 's, D, A> DocBuilder<'a, D, A>
where
    D: ?Sized + DocAllocator<'a, A>,
{
    /// Append the given document after this document.
    #[inline]
    pub fn append<E>(self, that: E) -> DocBuilder<'a, D, A>
    where
        E: Into<Doc<'a, D::Doc, A>>,
    {
        let DocBuilder(allocator, this) = self;
        let that = that.into();
        let doc = match (this, that) {
            (Doc::Nil, that) => that,
            (this, Doc::Nil) => this,
            (this, that) => Doc::Append(allocator.alloc(this), allocator.alloc(that)),
        };
        DocBuilder(allocator, doc)
    }

    /// Mark this document as a group.
    ///
    /// Groups are layed out on a single line if possible.  Within a group, all basic documents with
    /// several possible layouts are assigned the same layout, that is, they are all layed out
    /// horizontally and combined into a one single line, or they are each layed out on their own
    /// line.
    #[inline]
    pub fn group(self) -> DocBuilder<'a, D, A> {
        let DocBuilder(allocator, this) = self;
        DocBuilder(allocator, Doc::Group(allocator.alloc(this)))
    }

    /// Increase the indentation level of this document.
    #[inline]
    pub fn nest(self, offset: usize) -> DocBuilder<'a, D, A> {
        if offset == 0 {
            return self;
        }
        let DocBuilder(allocator, this) = self;
        DocBuilder(allocator, Doc::Nest(offset, allocator.alloc(this)))
    }

    #[inline]
    pub fn annotate(self, ann: A) -> DocBuilder<'a, D, A> {
        let DocBuilder(allocator, this) = self;
        DocBuilder(allocator, Doc::Annotated(ann, allocator.alloc(this)))
    }
}

/// Newtype wrapper for `&Doc`
#[derive(Clone, Eq, Ord, PartialEq, PartialOrd)]
pub struct RefDoc<'a, A: 'a>(&'a Doc<'a, RefDoc<'a, A>, A>);

impl<'a, A> fmt::Debug for RefDoc<'a, A>
where
    A: fmt::Debug,
{
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.0.fmt(f)
    }
}

impl<'a, A> Deref for RefDoc<'a, A> {
    type Target = Doc<'a, RefDoc<'a, A>, A>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

/// An arena which can be used to allocate `Doc` values.
pub type Arena<'a, A = ()> = typed_arena::Arena<Doc<'a, RefDoc<'a, A>, A>>;

impl<'a, D, A> DocAllocator<'a, A> for &'a D
where
    D: ?Sized + DocAllocator<'a, A>,
{
    type Doc = D::Doc;

    #[inline]
    fn alloc(&'a self, doc: Doc<'a, Self::Doc, A>) -> Self::Doc {
        (**self).alloc(doc)
    }
}

impl<'a, A> DocAllocator<'a, A> for Arena<'a, A> {
    type Doc = RefDoc<'a, A>;

    #[inline]
    fn alloc(&'a self, doc: Doc<'a, Self::Doc, A>) -> Self::Doc {
        RefDoc(match doc {
            // Return 'static references for unit variants to save a small
            // amount of space in the arena
            Doc::Nil => &Doc::Nil,
            Doc::Space => &Doc::Space,
            Doc::Newline => &Doc::Newline,
            _ => Arena::alloc(self, doc),
        })
    }
}

pub struct BoxAllocator;

static BOX_ALLOCATOR: BoxAllocator = BoxAllocator;

impl<'a, A> DocAllocator<'a, A> for BoxAllocator {
    type Doc = BoxDoc<'a, A>;

    #[inline]
    fn alloc(&'a self, doc: Doc<'a, Self::Doc, A>) -> Self::Doc {
        BoxDoc::new(doc)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    macro_rules! test {
        ($size:expr, $actual:expr, $expected:expr) => {
            let mut s = String::new();
            $actual.render_fmt($size, &mut s).unwrap();
            assert_eq!(s, $expected);
        };
        ($actual:expr, $expected:expr) => {
            test!(70, $actual, $expected)
        };
    }

    #[test]
    fn box_doc_inference() {
        let doc = Doc::<_>::group(
            Doc::text("test")
                .append(Doc::space())
                .append(Doc::text("test")),
        );

        test!(doc, "test test");
    }

    #[test]
    fn newline_in_text() {
        let doc = Doc::<_>::group(
            Doc::text("test").append(
                Doc::space()
                    .append(Doc::text("\"test\n     test\""))
                    .nest(4),
            ),
        );

        test!(5, doc, "test\n    \"test\n     test\"");
    }

    #[test]
    fn forced_newline() {
        let doc = Doc::<_>::group(
            Doc::text("test")
                .append(Doc::newline())
                .append(Doc::text("test")),
        );

        test!(doc, "test\ntest");
    }

    #[test]
    fn space_do_not_reset_pos() {
        let doc = Doc::<_>::group(Doc::text("test").append(Doc::space()))
            .append(Doc::text("test"))
            .append(Doc::group(Doc::space()).append(Doc::text("test")));

        test!(9, doc, "test test\ntest");
    }

    // Tests that the `Doc::newline()` does not cause the rest of document to think that it fits on
    // a single line but instead breaks on the `Doc::space()` to fit with 6 columns
    #[test]
    fn newline_does_not_cause_next_line_to_be_to_long() {
        let doc = Doc::<_>::group(
            Doc::text("test").append(Doc::newline()).append(
                Doc::text("test")
                    .append(Doc::space())
                    .append(Doc::text("test")),
            ),
        );

        test!(6, doc, "test\ntest\ntest");
    }

    #[test]
    fn block() {
        let doc = Doc::<_>::group(
            Doc::text("{")
                .append(
                    Doc::space()
                        .append(Doc::text("test"))
                        .append(Doc::space())
                        .append(Doc::text("test"))
                        .nest(2),
                )
                .append(Doc::space())
                .append(Doc::text("}")),
        );

        test!(5, doc, "{\n  test\n  test\n}");
    }

    #[test]
    fn annotation_no_panic() {
        let doc = Doc::group(
            Doc::text("test")
                .annotate(())
                .append(Doc::newline())
                .annotate(())
                .append(Doc::text("test")),
        );

        test!(doc, "test\ntest");
    }
}
