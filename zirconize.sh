#!/bin/sh -e

git ls-tree -r HEAD | awk -v sedscript="$(dirname $0)/zirconize.sed" '
{
  $1 = $2 = $3 = ""
  file = gensub(/^ */, "", 1, $0)
  file = gensub(/ *$/, "", 1, file)
  if (file ~ /zirconize/) next
  if (file ~ /magenta/ || file ~ /[^a-z0-9]mx[^a-z0-9]/ || file ~ /mxio|mxtl|mxcpp/) {
    new = gensub(/magenta/, "zircon", "g", file)
    new = gensub(/([^a-z0-9])mx([^a-z0-9])/, "\\1zx\\2", "g", new)
    new = gensub(/mxio/, "fdio", "g", new)
    new = gensub(/mxtl/, "fbl", "g", new)
    new = gensub(/mxcpp/, "zxcpp", "g", new)
    files[file] = new
  } else {
    files[file] = file
  }
}
END {
  for (file in files) {
    new = files[file]
    if (new != file) {
      newdir = new
      gsub(/\/[^/]+$/, "", newdir)
      if (newdir != new) print "mkdir -p", "'\''"newdir "'\''"
      print "git mv", "'\''" file "'\''", "'\''" new "'\''"
    }
    print "sed -i -f", sedscript, "'\''" new "'\''"
  }
}' | sh -xe

# This URL hasn't changed yet.
test ! -f scripts/download-toolchain ||
sed -i -e '/ZIRCON_GS_BUCKET/s/zircon/magenta/' scripts/download-toolchain
