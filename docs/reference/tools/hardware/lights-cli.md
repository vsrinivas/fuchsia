<!--

// LINT.IfChange

-->

# lights-cli

Get information about lights and control their brightness.

## Usage {#usage}

```none
lights-cli print <id>
lights-cli set <id> <value>
lights-cli summary
```

## Commands {#commands}

### print {#print}

```none
lights-cli print <id>
```

View the brightness of a light. The reported brightness value is a floating
point number between `0.0` (completely off) and `1.0` (completely on).

### set {#set}

```none
lights-cli set <id> <brightness>
```

Set the brightness of a light. For lights that support pulse-width modulation
`<brightness>` can be any number between `0.0` (completely off) and `1.0`
(completely on). For lights that only support simple on and off states
`<brightness>` should only be `0.0` (off) or `1.0` (on).

### summary {#summary}

```none
lights-cli summary
```

View the total light count as well as the brightness and capabilities of each
light. Currently supported capabilities are `Brightness`, `Rgb`, and `Simple`.
`Brightness` is a value between `0.0` and `1.0` as explained in the `set`
command's description. `Rgb` is the RGB value of the light. `Simple` indicates
whether the light supports pulse-width modulation or only simple on and off
states.

## Examples

### View the brightness of a light

```none {:.devsite-disable-click-to-copy}
$ lights-cli print AMBER_LED
Value of AMBER_LED: 1.000000
```

### Set the brightness of a light

```none {:.devsite-disable-click-to-copy}
$ lights-cli set AMBER_LED 0.5
# This command exits silently.
```

### View the total light count and each light's brightness and capabilities

```none {:.devsite-disable-click-to-copy}
$ lights-cli summary
Total 1 lights
Value of AMBER_LED: 0.500000
    Capabilities: Brightness
```

## Notes

<<./_access.md>>

### Source code

Source code for `lights-cli`: [`//src/ui/light/bin/lights-cli/`][src]

[src]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/light/bin/lights-cli/

<!--

// LINT.ThenChange(//src/ui/light/bin/lights-cli/main.cc)

-->
