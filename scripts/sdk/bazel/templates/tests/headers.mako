<%include file="header_slash.mako" />

// This file verifies that all headers included in an SDK are valid.

% for header in sorted(data['headers']):
#include "${header}"
% endfor

int main(int argc, const char** argv) {}
