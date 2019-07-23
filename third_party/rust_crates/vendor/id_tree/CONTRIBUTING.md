# Contributing

First of all, thanks for your interest in this project!  I'm very interested in feedback,
suggestions, bug reports, and feature requests, so if you've got something to say please feel
free to open up an issue.

When submitting an issue (or a PR), keep in mind that tree structures can get quite confusing when
we're discussing them without pictures.  I highly encourage the use of images and/or
ascii-art to clarify your meaning.  It can really help when things start to get confusing.

It doesn't have to be anything fancy, just give an example, like this:
```
    A
   / \
  B   C
```
When you do so, it can also help to reference the nodes that you're talking about like `A`
or `B` with `` ` ``s.  Again, this isn't required, but it can definitely help clarify things.

Pull Requests are welcome, but please submit an issue with your proposed changes before
submitting one.

If you do submit a Pull Request please do the following:
- Include documentation for your changes where appropriate (public methods must be documented,
while documentation is appreciated but not required for private methods)
- Include tests for your changes wherever appropriate.  In particular, try to "prove" (if
possible) that your changes do what they are supposed to.  Also, show that the proper error
messages are being returned when necessary.
- Make sure you run `cargo fmt` (found [here](https://github.com/rust-lang-nursery/rustfmt))
on your changes or the travis build will likely fail.
- It is not required to run `cargo clippy` (found [here](https://github.com/Manishearth/rust-clippy/))
on your changes, but I would greatly appreciate it. Please don't worry about making Clippy
100% happy though, especially if you think a particular suggestion makes your code harder to
read for some reason.
- Don't forget to add yourself to the to the list of Contributors in the Readme! This is
(of course) not required, but it is very much encouraged!

If you're having trouble completing any of the above for a PR, please feel free to ask for
help!  I will be happy to do so if I can!
