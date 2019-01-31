/* libunwind - a platform-independent unwind library

This file is part of libunwind.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  */

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

#include "libunwind_i.h"

HIDDEN int
unwi_print_error (const char *string)
{
  return write (2, string, strlen (string));
}

HIDDEN void
unwi_invalidate_edi (struct elf_dyn_info *edi)
{
  if (edi->ei.image)
    munmap (edi->ei.image, edi->ei.size);
  memset (edi, 0, sizeof (*edi));
  edi->di_cache.format = -1;
  edi->di_debug.format = -1;
#if UNW_TARGET_ARM
  edi->di_arm.format = -1;
#endif
}

HIDDEN void
unwi_invalidate_as_edi (struct as_elf_dyn_info *edi)
{
  free(edi->ehdr.data);
  free(edi->phdr.data);
  free(edi->eh.data);
  free(edi->dyn.data);

  memset (edi, 0, sizeof (*edi));
  edi->di_cache.format = -1;
  edi->di_debug.format = -1;
}

HIDDEN int
unwi_load_as_contents (unw_addr_space_t as, struct as_contents *contents,
                       unw_word_t offset, size_t size, void *arg)
{
  int ret;
  unw_accessors_t *a = unw_get_accessors (as);

  Debug(3, "(%p, %p, 0x%" PRIxPTR ", 0x%zx)\n", as, contents, offset, size);

  contents->data = malloc (size);
  if (contents->data == NULL)
    {
      Debug(3, "returning, OOM\n");
      return -UNW_ENOMEM;
    }
  ret = (*a->access_raw_mem) (as, offset, contents->data, size, 0, arg);
  if (ret < 0)
    {
        Debug(3, "returning, access_raw_mem failed: %d\n", ret);
      return -UNW_ENOINFO;
    }
  contents->size = size;
  Debug(3, "returning 0\n");
  return 0;
}
