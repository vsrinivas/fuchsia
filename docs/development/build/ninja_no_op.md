# Ninja no-op check

Fuchsia's Commit Queue (CQ) verifies that changes not only build successfully,
but also keep the build system in a state that it converges to no-op.

Continue reading this guide if you ran into the following error:

```
fuchsia confirm no-op
ninja build does not converge to a no-op
```

## Build graph convergence

Fuchsia uses the Ninja build system.
Ninja expresses the build as a graph of input/output files and actions that take
inputs and produce outputs.

When you run a build, e.g. with `fx build`, Ninja will traverse the build graph
and perform any actions whose outputs are not present or whose inputs have
changed since they last run, all in topological order (dependencies before
dependents).

The same build is run in CQ before changes can be merged into the source tree,
to ensure that changes don't break the build. After completing a build
successfully, CQ will invoke Ninja again and expect Ninja to report
`"no work to do"`. This serves as a soundness check, since a correct build graph
is expected to "converge" to no-op.

If this soundness check fails then CQ will report a failure on a step named
`fuchsia confirm no-op`.

## Reproducing Ninja convergence issues

With a source tree synced to your change, simply try the following:

```posix-terminal
fx build
```

This command should print:

```
ninja: no work to do.
```

If this is not the case, and actual build actions are being performed, run the
same command again. If the second invocation still didn't produce "no work",
then you've reproduced the issue. If you've arrived at "no work" still, try the
following:

```posix-terminal
# Clean your build cache
rm -rf out
# Set up the build specification again
fx set ...
# Build
fx build
# Build again, expecting no-op
fx build
```

## Troubleshooting Ninja convergence issues

In the CQ results page, under the failed step `confirm no-op`, you will see
several links:

* execution details
* ninja -d explain -n -v
* dirty paths

The link to `ninja -d explain -n -v` shows information that you should be able
to reproduce locally with the following command:

```posix-terminal
fx ninja -C $(fx get-build-dir) -d explain -n -v
```

This link to "dirty paths" shows the most relevant subset of the same
information. You will see a text file that will most likely begin as follows:

```
ninja explain: output <...> doesn't exist
...
```

Every line in this file is like a domino brick. You should begin troubleshooting
the problem by looking at the first domino brick that started the chain reaction
of extra work being done. For instance in the example above a particular output
file doesn't exist, which causes Ninja to re-run the build action that's
supposed to produce this output, and then subsequently rerun dependent actions.

Some common root causes for convergence issues include:

### An output isn't generated

If a build action is declared to produce an output but doesn't actually produce
that output (in some circumstances, or ever) then this will cause convergence
issues.

For instance, an action might declare that it generates a stamp file on success
but fail to generate this stamp file, or save it to the wrong location.

### An output is stale (not newer than all inputs)

Ninja knows that an output is fresh if it's newer than all inputs. If one or
more inputs have changed since the output was saved, then Ninja will repeat the
step(s) necessary to generate the output.

However if the action that generates the output doesn't update the output when
inputs have changed, this creates the appearance of a perpetual state of
staleness.

* A common mistake that causes this is when actions review their inputs, decide
  they have nothing to do/change with the contents of their outputs, but fail to
  update the modification timestamp on their outputs (i.e. "touch" or "stamp"
  their outputs).

* Another common mistake is when actions modify their inputs in the process of
  producing their outputs, and do so after producing their outputs. Actions are
  allowed to modify their inputs (though this is bad practice), but they must not
  leave a last modified timestamp on any of their inputs that is newer than any of
  their outputs.

## Common root causes

There are infinitely many ways to create Ninja convergence issues. That said,
prior experience taught us that there are common root causes for these problems.

### Timestamp granularity

Modern filesystems store timestamps on files (such as the time of last
modification) in nanosecond resolution. Some older runtimes, such as Python 2.7,
persist file timestamps in lower resolution, for instance milliseconds. It is
therefore possible for an action to read an input and write an output with a
timestamp that it considers to be "now" but is actually older than the timestamp
of the input, if for instance the input and output were both written at the same
millisecond and the output's timestamp is truncated after the millisecond
digits.

At the time of this writing we have mechanisms in place to ensure that all
Python actions in the build run with Python 3.x, in part to avoid this problem.

### Modifying inputs

It is possible for an action to modify its inputs. Typically inputs to an action
should be opened with read access only, however it's not out of the question to
write to them. That said, if your action needs to modify an input, it should do
so before writing any outputs. Or if you must modify inputs after writing
outputs, be sure to update the timestamp on your outputs before exiting the
action. Otherwise you will have updated one or more of your inputs to be newer
than one or more of your outputs, and thus confused Ninja into thinking that
your outputs are stale.
