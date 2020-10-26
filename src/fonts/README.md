# Fonts

An app that provides font data to other apps.

## Provided fonts.
Currently, we provide the Roboto and Noto fonts.

We additionally provide the Material Design Fonts, which must be kept
in sync with Flutter's version of it. See
[here](https://github.com/flutter/flutter/wiki/Updating-Material-Design-Fonts)
for their update instructions.

## Adding new fonts.
To add new fonts:

  1. Place all font files in the new directory along with LICENSE and README files.
  2. Add the relevant repository and font files to the recipe [here](https://fuchsia.googlesource.com/infra/recipes/+/HEAD/recipes/fonts.py).
  3. Update the Fonts section of the CIPD ensure file [here](https://fuchsia.googlesource.com/fuchsia/+/HEAD/garnet/tools/cipd.ensure) with the new git_revisions (which can be retrieved from [here](https://chrome-infra-packages.appspot.com/p/fuchsia/third_party/fonts/+/)).
  4. Trigger the bot to run with the changes from the LUCI scheduler [here](https://luci-scheduler.appspot.com/jobs/fuchsia/fonts).
  5. Add the new font files in `BUILD.gn`.
  6. Add the new fonts in `manifest.json`.
