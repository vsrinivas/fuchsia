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

  1. Create a new directory under `third_party`, e.g. `third_party/fontname`.
  2. Place all font files in the new directory along with LICENSE and README files.
  3. Run `./upload.sh third_party/fontname` to package and upload the archive to
     Google Storage. The upload step will fail if you don't have upload
     permission for the `fuchsia-build` storage bucket, in which case you will
     need to file infra (INTK-) ticket to be added to the ACL.
  4. Run `git add third_party/fontname.stamp` to add the `.stamp` file created
     created by `upload.sh`.
  5. Add the new font files in `BUILD.gn`.
  6. Add the new fonts in `manifest.json`.
