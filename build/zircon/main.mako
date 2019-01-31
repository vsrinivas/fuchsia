<%include file="header.mako" />

action("boards") {
  script = "//build/zircon/list_boards.py"

  board_file = "$root_out_dir/zircon-boards.list"

  outputs = [
    board_file,
  ]

  args = [
    "--out",
    rebase_path(board_file),
    "--boards",
  ]
  % for arch, boards in sorted(data.iteritems()):
  ${'else ' if not loop.first else ''}if (target_cpu == "${arch}") {
    args += [
      % for board in boards:
      "${board}",
      % endfor
    ]
  }
  % endfor
  else {
    assert(false, "Unsupported architecture: $target_cpu.")
  }
}
