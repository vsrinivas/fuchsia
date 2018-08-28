iquery(3)
=====

# NAME

`iquery` - the Fuchsia Inspect API Query Toolkit

# SYNOPSIS

`iquery [MODE] [<options>] PATH [...PATH]`

# DESCRIPTION

`iquery` is a utility program for inspecting component objects exposed over the [Inspect API](inspect.md). It accepts a list of paths to process, and how they are processed depends on the `MODE` setting and options.

# MODES

`MODE=(--cat|--ls|--find)`

## `--cat`
> DEFAULT. Treat each `PATH` as an object directory to open, and print
> the properties and metrics for those objects.

## `--ls`
> Treat each `PATH` as an object directory, and list the children for those objects.

## `--find`
> Recursively find all object directories under the filesystem paths
> passed in, and output the relative path one per line.

# OPTIONS

## `--full_paths`
> Rename each object to have its own relative path.
```
$ iquery a a/b
a:
b:
$ iquery --full_paths a a/b
a:
a/b:
```

## `--absolute_paths`
> Rename each object to have its own absolute path from the root.
```
$ cd /hub/c/
$ iquery a a/b
a:
b:
$ iquery --absolute_paths a a/b
/hub/c/a:
/hub/c/a/b:
```
