use std::cmp;
use std::fmt;
use std::io;
use std::ops::Deref;
#[cfg(feature = "termcolor")]
use termcolor::{ColorSpec, WriteColor};

use Doc;

/// Trait representing the operations necessary to render a document
pub trait Render {
    type Error;

    fn write_str(&mut self, s: &str) -> Result<usize, Self::Error>;

    fn write_str_all(&mut self, mut s: &str) -> Result<(), Self::Error> {
        while !s.is_empty() {
            let count = self.write_str(s)?;
            s = &s[count..];
        }
        Ok(())
    }
}

/// Writes to something implementing `std::io::Write`
pub struct IoWrite<W> {
    upstream: W,
}

impl<W> IoWrite<W> {
    pub fn new(upstream: W) -> IoWrite<W> {
        IoWrite { upstream }
    }
}

impl<W> Render for IoWrite<W>
where
    W: io::Write,
{
    type Error = io::Error;

    fn write_str(&mut self, s: &str) -> io::Result<usize> {
        self.upstream.write(s.as_bytes())
    }

    fn write_str_all(&mut self, s: &str) -> io::Result<()> {
        self.upstream.write_all(s.as_bytes())
    }
}

/// Writes to something implementing `std::fmt::Write`
pub struct FmtWrite<W> {
    upstream: W,
}

impl<W> FmtWrite<W> {
    pub fn new(upstream: W) -> FmtWrite<W> {
        FmtWrite { upstream }
    }
}

impl<W> Render for FmtWrite<W>
where
    W: fmt::Write,
{
    type Error = fmt::Error;

    fn write_str(&mut self, s: &str) -> Result<usize, fmt::Error> {
        self.write_str_all(s).map(|_| s.len())
    }

    fn write_str_all(&mut self, s: &str) -> fmt::Result {
        self.upstream.write_str(s)
    }
}

/// Trait representing the operations necessary to write an annotated document.
pub trait RenderAnnotated<A>: Render {
    fn push_annotation(&mut self, annotation: &A) -> Result<(), Self::Error>;
    fn pop_annotation(&mut self) -> Result<(), Self::Error>;
}

impl<A, W> RenderAnnotated<A> for IoWrite<W>
where
    W: io::Write,
{
    fn push_annotation(&mut self, _: &A) -> Result<(), Self::Error> {
        Ok(())
    }

    fn pop_annotation(&mut self) -> Result<(), Self::Error> {
        Ok(())
    }
}

impl<A, W> RenderAnnotated<A> for FmtWrite<W>
where
    W: fmt::Write,
{
    fn push_annotation(&mut self, _: &A) -> Result<(), Self::Error> {
        Ok(())
    }

    fn pop_annotation(&mut self) -> Result<(), Self::Error> {
        Ok(())
    }
}

#[cfg(feature = "termcolor")]
pub struct TermColored<W> {
    color_stack: Vec<ColorSpec>,
    upstream: W,
}

#[cfg(feature = "termcolor")]
impl<W> TermColored<W> {
    pub fn new(upstream: W) -> TermColored<W> {
        TermColored {
            color_stack: Vec::new(),
            upstream,
        }
    }
}

#[cfg(feature = "termcolor")]
impl<W> Render for TermColored<W>
where
    W: io::Write,
{
    type Error = io::Error;

    fn write_str(&mut self, s: &str) -> io::Result<usize> {
        self.upstream.write(s.as_bytes())
    }

    fn write_str_all(&mut self, s: &str) -> io::Result<()> {
        self.upstream.write_all(s.as_bytes())
    }
}

#[cfg(feature = "termcolor")]
impl<W> RenderAnnotated<ColorSpec> for TermColored<W>
where
    W: WriteColor,
{
    fn push_annotation(&mut self, color: &ColorSpec) -> Result<(), Self::Error> {
        self.color_stack.push(color.clone());
        self.upstream.set_color(color)
    }

    fn pop_annotation(&mut self) -> Result<(), Self::Error> {
        self.color_stack.pop();
        match self.color_stack.last() {
            Some(previous) => self.upstream.set_color(previous),
            None => self.upstream.reset(),
        }
    }
}

#[inline]
pub fn best<'a, W, T, A>(doc: &'a Doc<'a, T, A>, width: usize, out: &mut W) -> Result<(), W::Error>
where
    T: Deref<Target = Doc<'a, T, A>>,
    W: ?Sized + RenderAnnotated<A>,
{
    #[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
    enum Mode {
        Break,
        Flat,
    }

    type Cmd<'a, T, A> = (usize, Mode, &'a Doc<'a, T, A>);

    fn write_newline<W>(ind: usize, out: &mut W) -> Result<(), W::Error>
    where
        W: ?Sized + Render,
    {
        out.write_str_all("\n")?;
        write_spaces(ind, out)
    }

    fn write_spaces<W>(spaces: usize, out: &mut W) -> Result<(), W::Error>
    where
        W: ?Sized + Render,
    {
        macro_rules! make_spaces {
            () => { "" };
            ($s: tt $($t: tt)*) => { concat!("          ", make_spaces!($($t)*)) };
        }

        const SPACES: &str = make_spaces!(,,,,,,,,,,);
        let mut inserted = 0;
        while inserted < spaces {
            let insert = cmp::min(SPACES.len(), spaces - inserted);
            inserted += out.write_str(&SPACES[..insert])?;
        }

        Ok(())
    }

    #[inline]
    fn fitting<'a, T, A>(
        next: Cmd<'a, T, A>,
        bcmds: &[Cmd<'a, T, A>],
        fcmds: &mut Vec<Cmd<'a, T, A>>,
        mut rem: isize,
    ) -> bool
    where
        T: Deref<Target = Doc<'a, T, A>>,
    {
        let mut bidx = bcmds.len();
        fcmds.clear(); // clear from previous calls from best
        fcmds.push(next);

        while rem >= 0 {
            match fcmds.pop() {
                None => {
                    if bidx == 0 {
                        // All commands have been processed
                        return true;
                    } else {
                        fcmds.push(bcmds[bidx - 1]);
                        bidx -= 1;
                    }
                }
                Some((ind, mode, doc)) => {
                    match *doc {
                        Doc::Nil => {}
                        Doc::Append(ref ldoc, ref rdoc) => {
                            fcmds.push((ind, mode, rdoc));
                            // Since appended documents often appear in sequence on the left side we
                            // gain a slight performance increase by batching these pushes (avoiding
                            // to push and directly pop `Append` documents)
                            let mut doc = ldoc;
                            while let Doc::Append(ref l, ref r) = **doc {
                                fcmds.push((ind, mode, r));
                                doc = l;
                            }
                            fcmds.push((ind, mode, doc));
                        }
                        Doc::Group(ref doc) => {
                            fcmds.push((ind, mode, doc));
                        }
                        Doc::Nest(off, ref doc) => {
                            fcmds.push((ind + off, mode, doc));
                        }
                        Doc::Space => match mode {
                            Mode::Flat => {
                                rem -= 1;
                            }
                            Mode::Break => {
                                return true;
                            }
                        },
                        Doc::Newline => return true,
                        Doc::Text(ref str) => {
                            rem -= str.len() as isize;
                        }
                        Doc::Annotated(_, ref doc) => fcmds.push((ind, mode, doc)),
                    }
                }
            }
        }

        false
    }

    let mut pos = 0;
    let mut bcmds = vec![(0, Mode::Break, doc)];
    let mut fcmds = vec![];
    let mut annotation_levels = vec![];

    while let Some((ind, mode, doc)) = bcmds.pop() {
        match *doc {
            Doc::Nil => {}
            Doc::Append(ref ldoc, ref rdoc) => {
                bcmds.push((ind, mode, rdoc));
                let mut doc = ldoc;
                while let Doc::Append(ref l, ref r) = **doc {
                    bcmds.push((ind, mode, r));
                    doc = l;
                }
                bcmds.push((ind, mode, doc));
            }
            Doc::Group(ref doc) => match mode {
                Mode::Flat => {
                    bcmds.push((ind, Mode::Flat, doc));
                }
                Mode::Break => {
                    let next = (ind, Mode::Flat, &**doc);
                    let rem = width as isize - pos as isize;
                    if fitting(next, &bcmds, &mut fcmds, rem) {
                        bcmds.push(next);
                    } else {
                        bcmds.push((ind, Mode::Break, doc));
                    }
                }
            },
            Doc::Nest(off, ref doc) => {
                bcmds.push((ind + off, mode, doc));
            }
            Doc::Space => match mode {
                Mode::Flat => {
                    write_spaces(1, out)?;
                }
                Mode::Break => {
                    write_newline(ind, out)?;
                    pos = ind;
                }
            },
            Doc::Newline => {
                write_newline(ind, out)?;
                pos = ind;

                // Since this newline caused an early break we don't know if the remaining
                // documents fit the next line so recalculate if they fit
                fcmds.clear();
                let docs = bcmds.len()
                    - bcmds
                        .iter()
                        .rev()
                        .position(|t| t.1 == Mode::Break)
                        .unwrap_or_else(|| bcmds.len());
                fcmds.extend_from_slice(&bcmds[docs..]);
                if let Some(next) = fcmds.pop() {
                    let rem = width as isize - pos as isize;
                    if !fitting(next, &bcmds, &mut fcmds, rem) {
                        for &mut (_, ref mut mode, _) in &mut bcmds[docs..] {
                            *mode = Mode::Break;
                        }
                    }
                }
            }
            Doc::Text(ref s) => {
                out.write_str_all(s)?;
                pos += s.len();
            }
            Doc::Annotated(ref ann, ref doc) => {
                out.push_annotation(ann)?;
                annotation_levels.push(bcmds.len());
                bcmds.push((ind, mode, doc));
            }
        }

        if annotation_levels.last() == Some(&bcmds.len()) {
            annotation_levels.pop();
            out.pop_annotation()?;
        }
    }

    Ok(())
}
