This is a copy of [musl](http://www.musl-libc.org/) at commit
49631b7b6c8b8c055aae5b6948930c219b95fdef which has been ported to work
on the Fuchsia operating system.

In order to diff what is here vs what is in musl upstream, you can use
the following commands:

```
git remote add musl-upstream git://git.musl-libc.org/musl && git fetch musl-upstream
git diff musl-upstream/master:src/some_file.c -- zircon/third_party/ulib/musl/src/some_file.c
```
