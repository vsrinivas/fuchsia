<%include file="header_slash.mako" />

// This file verifies that all headers included in an SDK are valid.

% for dep, headers in sorted(data['headers'].iteritems()):
  % if headers:
// ${dep}
    % for header in headers:
#include "${header}"
    % endfor
  % endif
% endfor

int main(int argc, const char** argv) {}
