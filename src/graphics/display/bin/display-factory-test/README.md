# display_png

`display_png` is a [Carnelian](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/lib/ui/carnelian/)-based program used to test displays.

    Usage: display_png [--file <file>] [--gamma-file <gamma-file>] [--gamma-divisor <gamma-divisor>] [--background <background>] [--position <position>] [--timeout <timeout>]

    Display Png.

    Options:
      --file            PNG file to load
      --gamma-file      path to file containing gamma values. The file format is a
                        header line plus three lines of 256 three-digit hexadecimal
                        value groups.
      --gamma-divisor   integer value with which to divide the gamma values found in
                        gamma values file. Does nothing if no gamma file is
                        specified.
      --background      background color (default is white)
      --position        an optional x,y position for the image (default is center)
      --timeout         seconds of delay before application exits (default is 1
                        second)
      --help            display usage information

## About files

`display_png` reads files from the target device's file system.
You will need to copy a file to the target as follows:

    fx cp [host image path] "[target image path]"

Note that the quoting of the target path is needed for zsh but might not be needed for your shell.

If you are using an SDK, rather than building Fuchsia from source, check your SDK documentation for how to copy files to an attached device.

Once the file is copied, invoking `display_png` via `fx shell` should display a png:

```
$ fx shell display_png --file [file name]
```
