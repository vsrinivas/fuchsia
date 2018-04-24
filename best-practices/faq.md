# Questions and Answers

You are encouraged to add your own questions (and answers) here!

[TOC]

## Workflow

### Q: Is there a standard Git workflow for Fuchsia?

A: No. Instead, the Git tool offers infinite control and variety for defining
your own workflow. Carve out the workflow you need.

#### Rebasing

Update all projects simultaneously, and rebase your work branch on `JIRI_HEAD`:

```shell
$ jiri update -gc -rebase-untracked
$ cd garnet  # go into a layer
$ git checkout <my_branch>
$ git rebase JIRI_HEAD
```

The `git rebase` to `JIRI_HEAD` should be done in *each* repo where you have
ongoing work. It's not needed for repos you haven't touched.

#### Uploading a new patch set (snapshot) of a change

You'll need to *upload* a patch set to [Gerrit](https://fuchsia-review.googlesource.com/) to have it reviewed by others. We do this with `jiri upload`.

Gerrit uses an auto-generated metadata marker in the CL description to figure out which Gerrit review thread to upload a patch to, such as:
```
Change-Id: I681125d950205fa7654e4a8ac0b3fee7985f5a4f
```

This is different from a git commit's SHA hash, and can be considered stable
during review, as you make edits to your changes and commits. Use the same
Change-Id for a given review (in case you are
[squashing](https://git-scm.com/book/en/v2/Git-Tools-Rewriting-History)
multiple commits).

If you've made changes and want to upload a new patch set, then (assuming that
this is the latest change in your branch; use `git log` to find out) you can
do something like:

```shell
$ git commit -a --amend
# -a for all uncommitted files, --amend to amend latest commit
$ jiri upload
```

#### Resolving merge conflicts

Attempt a rebase:

```shell
$ git fetch origin && git rebase origin/master
# Resolve conflicts as needed...
$ jiri upload
```

But read below about how a `git rebase` can negatively interact with `jiri update`.

#### Stashing

You can save all uncommitted changes aside, and re-apply them at a later time.
This is often useful when you're starting out with Git.

```shell
$ git stash # uncommitted changes will go away
# do stuff
$ git stash pop # uncommitted changes will come back
```

### Q: I use `fx` and `jiri` a lot. How are they related?

A: [`jiri`](https://fuchsia.googlesource.com/jiri/+/master/) is source
management for multiple repositories.
[`fx`](https://fuchsia.googlesource.com/scripts/+/master) is a convenience
wrapper for configuring and running the build system (Make for Zircon,
[GN](https://fuchsia.git.corp.google.com/docs/+/HEAD/glossary.md#gn) and
[Ninja](https://fuchsia.git.corp.google.com/docs/+/HEAD/glossary.md#ninja) for
everything else), as well as facilities to help with day-to-day engineering (`fx
boot`, `fx log`, etc).

### Q: Will a git rebase to origin/master mess up my jiri-updated (ie synchronized) view of the repository?

A: No, if jiri is managing up to the *same layer* as your repository. Possibly
yes, if you git rebase a repository that is lower in the layer cake managed by
jiri.

Rollers are used to keep dependencies (lower layers) green. A lower layer may
only be compatible with an upper layer up to a certain CL. So a git rebase to
origin/master for a lower layer has the potential to introduce breaking changes
that have not yet made it past the roller going to the upper layer. The result
may be that you can build up to a certain layer, but not past that layer (e.g.,
correctly build up to garnet, but not be able to build topaz components).

`jiri update` interacts with the layered nature of our repositories. By having
jiri manage up to a particular layer, a git rebase of that layer itself is
relatively free from such dependency effects. You can adjust which layer is
managed by jiri with `fx set-layer`.

If you have a particular commit that you want jiri to honor, download its
`jiri.update` file and feed it to `jiri update`.

### Q: What if I need an atomic commit across git repositories?

A: Can't, sorry. Try to arrange your CLs to not break each layer during a
transition (i.e., do a [soft
transition](https://fuchsia.googlesource.com/docs/+/master/development/workflows/multilayer_changes.md#soft-transitions-preferred)).
But sometimes you will necessarily break things; aim to minimize the duration of
breakage (i.e., a [hard
transition](https://fuchsia.googlesource.com/docs/+/master/development/workflows/multilayer_changes.md#hard-transitions)).

Example scenario: I have an interface defined in a lower layer, and it is
implemented in an upper layer. If I change the interface, am I doomed to break
the upper layer?

Yes. But you can "babysit" the rollers so that the breakage range is minimized.
The gotcha with babysitting is that others may *also* be babysitting a breakage,
and you may end up babysitting for longer than you had intended.

Alternatively, you *could* do something as follows:
1. Introduce a new interface in `lower` that is a copy of the original interface.
1. Wait for `lower-roller` to roll into `upper`, or roll yourself by updating the file `upper/manifest`.
1. Change `upper` to use the new clone interface that maintains the old contract.
1. Change `lower` such that the original interface’s contract is modified to the new, desired form.
1. Wait for `lower-roller`, or roll yourself.
1. Change `upper` to use the original interface name, now with its new contract. Make any changes required.
1. Delete the clone interface in `lower`.

### Q: How do I do parallel builds from a single set of sources?

A: Currently, this is not possible. The vanilla GN + Ninja workflow should allow
this, but `fx` maintains additional global state.

Another slight limitation is that GN files to Zircon are currently being
generated and running multiple parallel builds which both try to generate GN
files may confuse Ninja. It's unclear whether this is a real issue or not.

### Q: What if I want to build at a previous snapshot across the repos?

A: You'll need to `jiri update` against a *jiri snapshot file*, an XML file that
captures the state of each repo tracked by jiri.

## Testing

### Q: How do I define a new unit test?

A: Use GTest constructs. You can define a new file if need be, such as:

(in a BUILD.gn file)
```code
executable("unittests") {
  output_name = "scenic_unittests"
  testonly = true
  sources = ["some_test.cc"],
  deps = [":some_dep"],
}
```

### Q: What ensures it is run?

A: An unbroken chain of dependencies that roll up to a config file under
`//<layer>/packages/tests/`, such as
[`//garnet/packages/tests/`](https://fuchsia.googlesource.com/garnet/+/master/packages/tests/).

For example:

`//garnet/lib/ui/scenic/tests:unittests`

is an executable, listed under the "tests" stanza of

`//garnet/bin/ui:scenic_tests`

which is a package, which is itself listed in the "packages" stanza of

`//garnet/packages/tests/scenic`

a file that defines what test binaries go into a system image.

Think of it as a blueprint file: a (transitive) manifest that details which
tests to try build and run.

Typically, one just adds a new test to an existing binary, or a new test binary to an existing package.

### Q: How do I run this unit test on a QEMU instance?

A: Start a QEMU instance on your workstation, and then *manually* invoke the unit test binary.

First, start QEMU with `fx run`.

In the QEMU shell, run `/system/test/scenic_unittests`. The filename is taken
from the value of "output_name" from the executable's build rule. All test
binaries live in the `/system/test` directory.

Note Well! The files are loaded into the QEMU instance at startup. So after
rebuilding a test, you'll need to shutdown and re-start the QEMU instance to see
the rebuilt test. To exit QEMU, `dm shutdown`.

### Q: How do I run this unit test on my development device?

A: Either manual invocation, like in QEMU, **or** `fx run-test` to a running device.

Note that the booted device may not contain your binary at startup, but `fx
run-test` will build the test binary, ship it over to the device, and run it,
while piping the output back to your workstation terminal. Slick!

Make sure your device is running (hit Ctrl-D to boot an existing image) and
connected to your workstation.

From your workstation, `fx run-test scenic_unittests`. The argument to
`run-test` is the name of the binary in `/system/test`.

### Q: Where are the test results captured?

A: The output is directed to your terminal.

There does exist a way to write test output into files (including a summary JSON
file), which is how CQ bots collect the test output for automated runs.

### Q: How do I run a bunch of tests automatically? How do I ensure all dependencies are tested?

A: Upload your patch to Gerrit and do a CQ dry run.

### Q: How do I run this unit test in a CQ dry run?

A: Clicking on CQ dry run (aka +1) will take a properly defined unit test and
run it on multiple bots, one for each build target (*x86-64* versus *arm64*, *release*
versus *debug*). Each job will have an output page showing all the tests that
ran.

### Q: Do we have Sanitizer support?

A: This is work in progress (SEC-27). ASAN is the closest to release (just
requires symbolization, TC-21).

### Q: How do I run with ASAN?

A: TBD

### Q: Do we have Fuzzers enabled?

A: No, sanitizer work takes precedence. Automated fuzz testing is SEC-44.
