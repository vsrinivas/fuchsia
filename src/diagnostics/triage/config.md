# Configuring `fx triage`

[Triage](README.md) analyzes bugreports according to config files.

## Overview

Triage allows anyone to easily add new ways to analyze `fx bugreport` data
for off-nominal conditions.

By default, the config files are read from
`//src/diagnostics/config/triage/*.triage`. Just add a new config file there.

Config file syntax is JSON.

Each config file specifies three kinds of configuration: Metrics, Actions, and
Tests.

* Metrics load and/or calculate values for use by Actions.
* Actions are triggered by boolean Metrics, and print warnings if triggered.
* Tests include sample data to verify that Actions trigger correctly.

Each Metric, Test, and Action has a name. Thus, the structure of a
config file is:

```JSON
{
    "metrics": {
        "metric1": { .... },
        "metric2": { .... }
    },
    "actions": {
        "action1": { .... },
        "action2": { .... }
    },
    "tests": {
        "test1": { .... },
        "test2": { .... }
    }
}
```

## Names and namespaces

Metric, Action, Test, and config file names consist of one
alphabetic-or-underscore character followed by
zero or more alphanumeric-or-underscore characters. Thus, "abc123" and
"_abc_123" are valid names, but "123abc" and "abc-123" are not.

Items (Metrics, Tests, and Actions) in one file can refer to items in another
file. The file basename is used as a namespace. `::` is used as the separator.
For example, if file `foo.triage` is loaded
and contains a Metric named `bar` then any config file may refer to `foo::bar`.

Names may be reused between Metrics, Tests, and Actions.

NOTE: The current version of the program is not guaranteed to enforce these
restrictions.

## Metrics

There are two kinds of Metrics: those that select data from the inspect.json
file, and those that calculate values based on other Metrics.

```JSON
    "metrics": {
        "metric1": {
            "Selector": "global_data:root.stats:total_bytes"
        },
        "metric2": {
            "Eval": "metric1 / 5.0 > 2"
        }
    },
```

### Selectors

Selectors use the Selector format. The text before the first `:` selects the
component name from the `inspect.json` file. The `.`-separated middle section
specifies Inspect Node names forming a path to the Property named after the
second `:`.

TODO(cphoenix) - Clarify this section once the correct selector-crate is
in place.

### Calculation

The string after `"Eval"` is an infix math expression with
normal operator precedence. () may be used.

Arithmetic operators are + - * /

Metric type follows the type read from the Inspect file. Currently, UInt
is converted to Int upon reading. Operating on an Int and Float promotes the
result to Float.

Boolean operations are > < >= <= == !=. The expression should have only 0 or 1
of them.

Functions of the form Func(param, param...) are not supported (yet).

Arrays / vectors are not supported (yet).

Whitespace is optional.

Metric names, including namespaced names, do not need to be specially delimited.

## Actions

Each Action specifies a trigger and a response. Currently, the only available
response is "print". Thus,

```JSON
    "actions": {
        "action1": {
            "trigger": "metric2", "print": "metric2 was true!"
        },
    }
```

`trigger` must specify the name of a Metric that supplies a Boolean value.

## Tests

Each Test specifies:

*   Sample data, keyed by `inspect`
*   A list of actions that should trigger given that data, keyed by `yes`
*   A list of actions that should not trigger given that data, keyed by `no`

The sample data is in the same format as an inspect.json file: an array
of maps where each map contains `path` and `contents` fields.

```JSON
    "tests": {
        "test1": {
            "yes": ["action1", "action2"],
            "no": ["action3"],
            "inspect": [
                {
                    "path": "global_data",
                    "contents": {"root": {"stats":
                        {"total_bytes": 10, "used_bytes": 9}}}
                }
            ]
        }
    }
```

