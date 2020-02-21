# Triage codelab

Contributors: cphoenix@

This codelab explains the Triage utility.

* What it's for
* How to run it, including command line options
* How to add and test detection rules

The source files and examples to which this document refers are available at:

* [//src/diagnostics/examples/triage/inspect.json][triage-inspect-example].
* [//src/diagnostics/examples/triage/rules.triage][triage-rules-example].
* [//src/diagnostics/examples/triage/solution][triage-codelab-solution].

## What is Triage?

Triage allows you to scan bug dump files (bugreport.zip,
fuchsia_feedback_data.zip) for predefined conditions.

The Triage system makes it easy to configure new conditions, increasing the
usefulness of Triage for everyone.

The current version of Triage is a host-side command line tool, invoked with
`fx triage`.

## What you'll need

* Familiarity with `{"format": "JSON"}`
* Access to a Fuchsia source tree you can execute build commands in.
    * Do [Getting Started](/docs/getting_started.md)
    * Run `fx build`

## Find the "inspect.json" file

This codelab includes an `inspect.json` file with values to make the exercises
work predictably.

Note: `inspect.json` files are packaged in the `bugreport.zip` file produced by
`fx bugreport`. Use `unzip` to unpack this file.

Note: If you run `fx triage` without specifying a `--inspect` option, it
runs a fresh `fx bugreport` and analyzes its inspect.json file.

## Run triage

* To run Triage:

```shell
fx triage
```

This command downloads a fresh `bugreport.zip` file using the `fx bugreport`
command. This command runs the default rules from
`//src/diagnostics/config/triage/*.triage`.

* To analyze a specific `inspect.json` file:

```shell
$ fx triage --inspect my/foo/inspect.json
```

Note: You can only specify one `--inspect` argument.

* To use a specific configuration file or all `*.triage` files in the specific
directory:

```shell
fx triage --config my/directory --config my/file.triage
```

Note: You can use multiple `--config` arguments.

Note: If a `--config` argument is used, the default rules will not be
automatically loaded.

* This codelab uses this command:

```shell
fx triage --config . --inspect inspect.json
```

Running this command in the codelab directory with the unmodified codelab files
prints a line indicating that Triage is working properly:

```
Warning: 'always_triggered' in 'rules' detected 'Triage is running': 'always_true' was true
```

## Add selectors for the Inspect values

The inspect.json file in the codelab directory indicates a couple of problems
with the system. You're going to configure the triage system to detect those
problems.

This step configures Triage to extract values from the data in the
`inspect.json` file.

The `rules.triage` file contains a key-value section called "metrics".
The key name will be used in the body of other config entries. The key's value
is a selector structure.

Note: Names (of metrics, actions, tests, and basenames of config files) can be
any letter or underscore, followed by any number of letters, numbers, or
underscores.

The selector structure has the key `Selector`. Its value is a colon-separated
string that tells where in the Inspect data to find the number you need.

```json
"disk_used": {"Selector": "global_dat/storage:root.stats:used_bytes"}
```

Note: This line includes a typo which we'll fix later in the codelab.

Inspect data published by a component is organized as a tree of
nodes with values (properties) at the leaves. The inspect.json file is an
array of these trees, each with a `path` that identifies the source component.

The portion of the selector string before the first colon should match (be a
substring of) exactly one of the `path` strings in the inspect.json file.

The portion between the two colons is a `.`-separated list of node names.

The portion after the second colon is the property name.

The above selector string indicates a component whose path includes the string
`global_dat/storage`. It also indicates the `used_bytes` property from the
`stats` subnode of the `root` node of that component's Inspect Tree.

1. Copy the above "disk_used" selector metric, and add it to the "metrics"
section of the rules.triage file.

2. Write and add another selector named "disk_total" to select the "total_bytes"
property at the same node in the Inspect data.

Note: JSON is picky about commas. Make sure every selector except the last one
is followed by a comma.

## Add a computation

In addition to selecting values from the "inspect.json" file, you need to do
some logic, and probably some arithmetic, to see whether those values indicate
a condition worth flagging.

Copy and add the following metric to calculate how full the disk is:

```json
"disk_percentage": {"Eval": "disk_used / disk_total"}
```

* This is ordinary + - * / arithmetic, with ordinary order of operations.
* You can use parentheses.
* You can use the names of metrics as variables.

## Add a comparison

Copy and add the following metric to calculate whether the disk is 98% full.

```json
"disk98": {"Eval": "disk_percentage > 0.98"}
```

* This metric has a comparison, so its result type is Boolean. It will
be usable to trigger actions.
* Available comparisons are > >= < <= == !=
* You can combine computations and comparisons into a single rule (just one
comparison per rule, please).

## Add an action

In the "actions" part of the config file, add an action which prints a
warning when the disk is 98% full. Use the following line:

```json
"disk_full": {"trigger": "disk98", "print": "Disk is over 98% full"}
```

* The "trigger" is the name of a Boolean-type (comparison) metric.
* Currently, `print` is the only available action.

## Try it out

The following command will run Triage against the local config file.

```shell
fx triage --config . --inspect inspect.json
```

You will get several lines of error indications. What happened?

There was a typo in the selector rules. If you read past all the backslashes
(the next version of Triage will be friendlier), you'll see that
Triage could not find values needed to evaluate a rule. In fact, the correct
selector is "global_data" not "global_dat." Fix it in your selector rules
and try again.

```shell
fx triage --config . --inspect inspect.json
```

Now what happened? Nothing, right? So, how do you know whether there was no
problem in the inspect.json file, or a bug in your rule?

## Test your rule

You can (and should!) add tests for your actions. For each test, write a snippet
of inspect.json content and specify whether it should or should not trigger
your rule.

To test the rule you've added, add the following to the "tests" section of the
rules.triage file:

```json
"is_full": {"yes": ["disk_full"], "no": [],
    "inspect": [
        {"path": "global_data/storage",
        "contents": {"root": {"stats": {"total_bytes": 100, "used_bytes": 99}}}}
    ]
}
```

You can also test conditions in which actions should not trigger:

```json
"not_full": {"yes": [], "no": ["disk_full"],
    "inspect": [
        {"path": "global_data/storage",
        "contents": {"root": {"stats": {"total_bytes": 100, "used_bytes": 97}}}}
    ]
}
```

To run the test, just run Triage. It automatically self-tests each time it's
run.

```shell
fx triage --config . --inspect inspect.json
```

Whoops! That should signal an error:

`Test is_full failed: trigger disk98 of action disk_full returned Bool(false), expected true`

## Fix your rule

Triage's arithmetic engine preserves the type of the operands, so 99/100 is 0.
You can convert to floating point by adding 0.0. Modify your `disk_percentage`
rule:

```json
"disk_percentage": {"Eval": "(disk_used + 0.0) / disk_total"}
```

Run Triage again. The error should disappear, replaced by a warning that your
inspect.json file does in fact indicate a full disk.

`Warning: 'disk_full' in 'rules' detected 'Disk is 98% full': 'disk98' was true`

## Use multiple configuration files

You can add any number of Triage configuration files, and even use metrics
defined in one file in another file. This has lots of applications:

* One file for disk-related metrics, and another for network-related metrics
* A file for product-specific numbers
* Files for particular engineers or teams

Add a file "product.triage" containing the following:

```json
{
    "metrics": {
        "max_widgets": {"Eval": "4"}
    },
    "actions": {},
    "tests": {}
}
```

Add the following metrics to the rules.triage file:

```json
"actual_widgets": {"Selector": "widget_maker.cmx:root:total_widgets"}
```

That will extract how many widgets were active in the device.

```json
"too_many_widgets": {"Eval": "actual_widgets > product::max_widgets"}
```

That compares the actual widgets with the theoretical maximum for the
product.

Note: To use metrics from another file, combine the file name, two colons, and
the metric name.

Finally, add an action:

```json
"widget_overflow": {"trigger": "too_many_widgets", "print": "Too many widgets!"}
```

Unfortunately, this device tried to use 6 widgets, so this warning should
trigger when "fx triage" is run.

Note: The `trigger` of an action can also use `file::name` syntax to refer to a
metric in another file.

In a production environment, several "product.triage" files could be
maintained in different directories, and Triage could be directed to use any of
them with the "--config" command line argument. (Future versions of Triage
may be able to select the correct product file automatically.)

## Further Reading

See
[Triage (fx triage)](/src/diagnostics/triage/README.md)
for the latest features and options - Triage will keep improving!

[triage-inspect-example]: /src/diagnostics/examples/triage/inspect.json
[triage-rules-example]: /src/diagnostics/examples/triage/rules.triage
[triage-codelab-solution]: /src/diagnostics/examples/triage/solution

