iquery(3)
=====

# NAME

`iquery` - the Fuchsia Inspect API Query Toolkit

# SYNOPSIS

```
iquery [MODE] [--recursive] [--format=<FORMAT>]
       [--sort]
       [(--full_paths|--absolute_paths|--display_paths)]
       PATH [...PATH]
```

# DESCRIPTION

`iquery` is a utility program for inspecting component nodes exposed over the
[Inspect API](gsw-inspect.md).
It accepts a list of paths to process, and how
they are processed depends on the `MODE` setting and options.

# MODES

`MODE=(--cat|--ls|--find|--report)`

## `--cat`

DEFAULT. Treat each `PATH` as a node directory to open, and print
the properties and metrics for those nodes.

## `--ls`

Treat each `PATH` as a node directory, and list the children for those nodes.

## `--find`

Recursively find all node directories under the filesystem paths
passed in, and output the relative path one per line.

## `--report`

Outputs a default system-wide report. Ignores all options other than
`--format`.

# OPTIONS

## `--recursive`

Continue to step down the hierarchy of elements. Mode dependent.


### For `--cat`

If false, will print the top level node only. True will output the complete node hierarchy.

Example:

```
$ find .
a/
a/fuchsia.inspect.Inspect
b/c

$ iquery --ls a
a_key
a_key2

$ iquery --cat a_key
a_key:
  a_value

$ iquery --cat --recursive a
a:
  a_key = a_value
  a_key2 = 3.4
```

### For `--find`

If false, it will descend into each branch until it finds a valid node.
True will output the complete tree, including nested nodes.

Example:

```
$ find .
a/
a/fuchsia.inspect.Inspect
b/c
b/c/fuchsia.inspect.Inspect

$ iquery --find .
a/
b/c

$ iquery --find --recursive .
a
a#a_key
a#a_key2
b/c
b/c#c_val
b/c#c_val2

```

## `--format=<FORMAT>`

What format the output should be in.

```
Current supported formatters:
- text: Meant for human inspection. This is the default option.
- json: Meant for machine consumption.
```

## `--sort`

When specified, sort the values for each Node before printing.

```
$ iquery root.inspect
root:
  c:
  a:
  b:

$ iquery --sort root.inspect
root:
  a:
  b:
  c:

$ iquery numeric.inspect
root:
  11:
  2:
  1:

# When all children are numeric, iquery sorts numerically.
$ iquery --sort numeric.inspect
root:
  1:
  2:
  11:
```

## `--full_paths`

Rename each node to have its own relative path.

```
$ iquery a a/b
a:
b:
$ iquery --full_paths a a/b
a:
a/b:
```

## `--absolute_paths`

Rename each node to have its own absolute path from the root.

```
$ cd /hub/c/
$ iquery a a/b
a:
b:
$ iquery --absolute_paths a a/b
/hub/c/a:
/hub/c/a/b:
```

## `--display_paths`

Rename only the top-level node to have its own relative path.
This mode is suitable for interactive display.

```
$ iquery --recrusive --display_paths a/b
b:
  c:
    c_val
    c_val2
$ iquery --display_paths a/b
a/b:
  c:
    c_val
    c_val2
```
