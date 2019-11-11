#!/bin/awk

BEGIN {
  OFS = " | "
  FS = " *= *"
}

{
  if (FILENAME != filename) {
    process()
    filename = FILENAME
    summary = ""
    category = ""
    deprecated = 0
    contrib = 0
  }
}

/^### / {
  if (summary == "") {
    summary = substr($0, 5)
    next
  }
}

/^#### +CATEGORY *=/ {
  if (category == "") {
    category = $2
    next
  }
}

/^#### +DEPRECATED/ {
  deprecated = 1
}

function process() {
  if (filename && (show_deprecated || !deprecated)) {
    toolname = filename
    sub(/.fx$/, "", toolname)
    sub(/^.*\//, "",  toolname)

    if (match(filename, "/contrib/")) {
      summary = "(contrib) " summary
      contrib = 1
    }

    if (!contrib || !hide_contrib) {
      if (match(filename, "^.*/vendor/")) {
        vendor_starts=RLENGTH+1
        if (match(filename, "[^/]+/scripts/devshell/")) {
          toolname = "vendor " substr(filename, vendor_starts, RLENGTH - length("/scripts/devshell/")) " " toolname
        }
      }

      if (!category) {
        category = "unknown category"
      }
      if (deprecated == 1) {
        summary = "(DEPRECATED) " summary
      }
      by_cat[category] = by_cat[category] "  " toolname " | " summary "\n"
    }
  }
}

function print_category(category) {
  if (by_cat[category]) {
    print category ":"
    print by_cat[category] | command
    close(command)
    print ""
    delete by_cat[category]
  }
}

END {
  process()
  command="sort | column -t -s '|' -c 2"

  # print known categories in a specific order
  print_category("Source tree")
  print_category("Documentation")
  print_category("Build")
  print_category("Device discovery")
  print_category("Device management")
  print_category("Software delivery")
  print_category("Run, inspect and debug")
  print_category("Other")

  # print the remaining categories in no defined order (no easy way to reorder
  # an array in posix awk)
  for (category in by_cat) {
    print_category(category)
  }
}

