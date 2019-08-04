#include <libgen.h>
#include <string.h>

char* dirname(char* s) {
  size_t i;
  if (!s || !*s)
    return (char*)".";
  i = strlen(s) - 1;
  for (; s[i] == '/'; i--)
    if (!i)
      return (char*)"/";
  for (; s[i] != '/'; i--)
    if (!i)
      return (char*)".";
  for (; s[i] == '/'; i--)
    if (!i)
      return (char*)"/";
  s[i + 1] = 0;
  return s;
}
