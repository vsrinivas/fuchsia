# Workflow: Questions and Answers

You are encouraged to add your own questions (and answers) here!

[TOC]

## Q: Is there a standard Git workflow for Fuchsia?

A: No. Instead, the Git tool offers infinite control and variety for defining
your own workflow. Carve out the workflow you need.

### Rebasing

Update all projects simultaneously, and rebase your work branch on `JIRI_HEAD`:

```shell
$ jiri update -gc -rebase-untracked
$ cd garnet  # go into a layer
$ git checkout <my_branch>
$ git rebase JIRI_HEAD
```

The `git rebase` to `JIRI_HEAD` should be done in *each* repo where you have
ongoing work. It's not needed for repos you haven't touched.

### Uploading a new patch set (snapshot) of a change

You'll need to *upload* a patch set to
[Gerrit](https://fuchsia-review.googlesource.com/) to have it reviewed by
others. We do this with `jiri upload`.

Gerrit uses an auto-generated metadata marker in the CL description to figure
out which Gerrit review thread to upload a patch to, such as: `Change-Id:
I681125d950205fa7654e4a8ac0b3fee7985f5a4f`

This is different from a git commit's SHA hash, and can be considered stable
during review, as you make edits to your changes and commits. Use the same
Change-Id for a given review (in case you are
[squashing](https://git-scm.com/book/en/v2/Git-Tools-Rewriting-History) multiple
commits).

If you've made changes and want to upload a new patch set, then (assuming that
this is the latest change in your branch; use `git log` to find out) you can do
something like:

```shell
$ git commit -a --amend
# -a for all uncommitted files, --amend to amend latest commit
$ jiri upload
```

### Resolving merge conflicts

Attempt a rebase:

```shell
$ git fetch origin && git rebase origin/master
# Resolve conflicts as needed...
$ jiri upload
```

But read below about how a `git rebase` can negatively interact with `jiri
update`.

### Stashing

You can save all uncommitted changes aside, and re-apply them at a later time.
This is often useful when you're starting out with Git.

```shell
$ git stash # uncommitted changes will go away
# do stuff
$ git stash pop # uncommitted changes will come back
```

## Q: I use **fx** and **jiri** a lot. How are they related?

A: [`jiri`](https://fuchsia.googlesource.com/jiri/+/master/) is source
management for multiple repositories.
[`fx`](https://fuchsia.googlesource.com/scripts/+/master) is a convenience
wrapper for configuring and running the build system (Make for Zircon,
[GN](https://fuchsia.git.corp.google.com/docs/+/HEAD/glossary.md#gn) and
[Ninja](https://fuchsia.git.corp.google.com/docs/+/HEAD/glossary.md#ninja) for
everything else), as well as facilities to help with day-to-day engineering (`fx
boot`, `fx log`, etc).

## Q: Will a git rebase to origin/master mess up my jiri-updated (i.e. synchronized) view of the repository?

A: No, if jiri is managing up to the *same layer* as your repository. Possibly
yes, if you git rebase a repository that is lower in the layer cake managed by
jiri.

When working at layer X (accomplished with `fx set-layer X`), `jiri update` will
rebase the local branches in repo X onto HEAD of origin/master. But lower
layers' repos will be synced to specific revisions that may be behind HEAD of
their origin/master.

Our continuous integration system (specifically rollers) makes a new revision of
a lower layer available to the higher layer only after testing that the new
revision doesn't break the higher layer. `jiri update` will always leave lower
layer repos synced to these successfully-tested revisions. But a git rebase to
origin/master for a lower layer may advance that repo beyond the tested
revision, which has the potential to introduce breaking changes. The result may
be that you can build up to a certain layer, but not past that layer (e.g.,
correctly build up to garnet, but not be able to build topaz).

If you have a particular commit that you want jiri to honor, download its
`jiri.update` file and feed it to `jiri update`.

## Q: What if I need an atomic commit across git repositories?

A: Can't, sorry. Try to arrange your CLs to not break each layer during a
transition (i.e., do a [soft
transition](multilayer_changes.md#soft-transitions-preferred)). But sometimes
you will necessarily break things; aim to minimize the duration of breakage
(i.e., a [hard transition](multilayer_changes.md#hard-transitions)).

Example scenario: I have an interface defined in a lower layer, and it is
implemented in an upper layer. If I change the interface, am I doomed to break
the upper layer?

Yes. But you can "babysit" the rollers so that the breakage range is minimized.
The gotcha with babysitting is that others may *also* be babysitting a breakage,
and you may end up babysitting for longer than you had intended.

Alternatively, you *could* do something as follows:

1.  Introduce a new interface in `lower` that is a copy of the original
    interface.
1.  Wait for `lower-roller` to roll into `upper`, or roll yourself by updating
    the file `upper/manifest`.
1.  Change `upper` to use the new clone interface that maintains the old
    contract.
1.  Change `lower` such that the original interface’s contract is modified to
    the new, desired form.
1.  Wait for `lower-roller`, or roll yourself.
1.  Change `upper` to use the original interface name, now with its new
    contract. Make any changes required.
1.  Delete the clone interface in `lower`.

## Q: How do I do parallel builds from a single set of sources?

A: Currently, this is not possible. The vanilla GN + Ninja workflow should allow
this, but `fx` maintains additional global state.

Another slight limitation is that GN files for Zircon are currently generated at
build-time, and running multiple parallel builds which both try to generate GN
files may confuse Ninja. It's unclear whether this is a real issue or not.

## Q: What if I want to build at a previous snapshot across the repos?

A: You'll need to `jiri update` against a *jiri snapshot file*, an XML file that
captures the state of each repo tracked by jiri.

## Q: I'm building on Mac, how to do I stop getting spammed with 'incoming network connection' notifications?

A: You'll want to run `fx setup-macos`, which registers all the relevant Fuchsia
tools with the MacOS Application Firewall.
