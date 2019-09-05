# factoryctl

Command line tool to list and read factory files from fuchsia.factory APIs and raw data from
fuschsia.boot.FactoryItems.

## Building
```
$ fx set <product>.<board> --with //src/factory/factoryctl
```
## Usage

```
# Built-in help
$ fx shell factoryctl -h

# Text payload
$ fx shell factoryctl misc dump props.txt
name=test_device
device_prop=some_prop
another_prop=another_value

# Binary payload
$ fx shell factoryctl factory-items dump 0
00000000        f9 3f 6d a5 b4 00 00 00 00 00 00 00 00 00 00 00         �?m��...........
00000010        07 00 00 00 8f 00 00 00 00 10 00 00 68 77 2e 74         ....�.......hw.t
00000020        78 74 00 00 10 00 00 00 06 00 00 00 00 20 00 00         xt........... ..
...
```
