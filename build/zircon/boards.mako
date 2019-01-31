<%include file="header.mako" />

% for arch, boards in sorted(data.iteritems()):
${'else ' if not loop.first else ''}if (target_cpu == "${arch}") {
  zircon_boards = [
    % for board in boards:
    "${board}",
    % endfor
  ]
}
% endfor
else {
  assert(false, "Unsupported architecture: $target_cpu.")
}
