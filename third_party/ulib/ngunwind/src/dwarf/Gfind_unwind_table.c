/* libunwind - a platform-independent unwind library
   Copyright (C) 2003-2004 Hewlett-Packard Co
        Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

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

#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>

#include "libunwind_i.h"
#include "dwarf-eh.h"
#include "dwarf_i.h"

HIDDEN int
dwarf_find_unwind_table (struct elf_dyn_info *edi, unw_addr_space_t as,
                         char *path, unw_word_t segbase, unw_word_t mapoff,
                         unw_word_t ip)
{
  Elf_W(Phdr) *phdr, *ptxt = NULL, *peh_hdr = NULL, *pdyn = NULL;
  unw_word_t addr, eh_frame_start, fde_count, load_base;
  unw_word_t max_load_addr = 0;
  unw_word_t start_ip = (unw_word_t) -1;
  unw_word_t end_ip = 0;
  struct dwarf_eh_frame_hdr *hdr;
  unw_proc_info_t pi;
  unw_accessors_t *a;
  Elf_W(Ehdr) *ehdr;
#if UNW_TARGET_ARM
  const Elf_W(Phdr) *parm_exidx = NULL;
#endif
  int i, ret, found = 0;

  Debug (3, "(edi %p, %p, \"%s\", 0x%lx, 0x%lx, 0x%lx)\n", edi, as, path, segbase, mapoff, ip);

  /* XXX: Much of this code is Linux/LSB-specific.  */

  if (!elf_w(valid_object) (&edi->ei))
    {
      Debug(3, "returning, invalid elf object\n");
      return -UNW_ENOINFO;
    }

  ehdr = edi->ei.image;
  phdr = (Elf_W(Phdr) *) ((char *) edi->ei.image + ehdr->e_phoff);

  for (i = 0; i < ehdr->e_phnum; ++i)
    {
      switch (phdr[i].p_type)
        {
        case PT_LOAD:
          if (phdr[i].p_vaddr < start_ip)
            start_ip = phdr[i].p_vaddr;

          if (phdr[i].p_vaddr + phdr[i].p_memsz > end_ip)
            end_ip = phdr[i].p_vaddr + phdr[i].p_memsz;

          if (phdr[i].p_offset == mapoff)
            ptxt = phdr + i;
          if ((uintptr_t) edi->ei.image + phdr->p_filesz > max_load_addr)
            max_load_addr = (uintptr_t) edi->ei.image + phdr->p_filesz;
          break;

        case PT_GNU_EH_FRAME:
          peh_hdr = phdr + i;
          break;

        case PT_DYNAMIC:
          pdyn = phdr + i;
          break;

#if UNW_TARGET_ARM
        case PT_ARM_EXIDX:
          parm_exidx = phdr + i;
          break;
#endif

        default:
          break;
        }
    }

  if (!ptxt)
    {
      Debug(3, "returning 0, no text\n");
      return 0;
    }

  load_base = segbase - ptxt->p_vaddr;
  start_ip += load_base;
  end_ip += load_base;

  if (peh_hdr)
    {
      if (pdyn)
        {
          /* For dynamicly linked executables and shared libraries,
             DT_PLTGOT is the value that data-relative addresses are
             relative to for that object.  We call this the "gp".  */
                Elf_W(Dyn) *dyn = (Elf_W(Dyn) *)(pdyn->p_offset
                                                 + (char *) edi->ei.image);
          for (; dyn->d_tag != DT_NULL; ++dyn)
            if (dyn->d_tag == DT_PLTGOT)
              {
                /* Assume that _DYNAMIC is writable and GLIBC has
                   relocated it (true for x86 at least).  */
                edi->di_cache.gp = dyn->d_un.d_ptr;
                break;
              }
        }
      else
        /* Otherwise this is a static executable with no _DYNAMIC.  Assume
           that data-relative addresses are relative to 0, i.e.,
           absolute.  */
        edi->di_cache.gp = 0;

      hdr = (struct dwarf_eh_frame_hdr *) (peh_hdr->p_offset
                                           + (char *) edi->ei.image);
      if (hdr->version != DW_EH_VERSION)
        {
          Debug (1, "returning, table `%s' has unexpected version %d\n",
                 path, hdr->version);
          return -UNW_ENOINFO;
        }

      a = unw_get_accessors (unw_local_addr_space);
      addr = (unw_word_t) (hdr + 1);

      /* Fill in a dummy proc_info structure.  We just need to fill in
         enough to ensure that dwarf_read_encoded_pointer() can do it's
         job.  Since we don't have a procedure-context at this point, all
         we have to do is fill in the global-pointer.  */
      memset (&pi, 0, sizeof (pi));
      pi.gp = edi->di_cache.gp;

      /* (Optionally) read eh_frame_ptr: */
      if ((ret = dwarf_read_encoded_pointer (unw_local_addr_space, a,
                                             &addr, hdr->eh_frame_ptr_enc, &pi,
                                             &eh_frame_start, NULL)) < 0)
        {
          Debug(3, "returning, dwarf_read_encoded_pointer failed: %d\n", ret);
          return -UNW_ENOINFO;
        }

      /* (Optionally) read fde_count: */
      if ((ret = dwarf_read_encoded_pointer (unw_local_addr_space, a,
                                             &addr, hdr->fde_count_enc, &pi,
                                             &fde_count, NULL)) < 0)
        {
          Debug(3, "returning, dwarf_read_encoded_pointer failed: %d\n", ret);
          return -UNW_ENOINFO;
        }

      if (hdr->table_enc != (DW_EH_PE_datarel | DW_EH_PE_sdata4))
        {
    #if 1
          assert (0);
    #else
          unw_word_t eh_frame_end;

          /* If there is no search table or it has an unsupported
             encoding, fall back on linear search.  */
          if (hdr->table_enc == DW_EH_PE_omit)
            Debug (4, "EH lacks search table; doing linear search\n");
          else
            Debug (4, "EH table has encoding 0x%x; doing linear search\n",
                   hdr->table_enc);

          eh_frame_end = max_load_addr; /* XXX can we do better? */

          if (hdr->fde_count_enc == DW_EH_PE_omit)
            fde_count = ~0UL;
          if (hdr->eh_frame_ptr_enc == DW_EH_PE_omit)
            assert (0);

          return linear_search (unw_local_addr_space, ip,
                                eh_frame_start, eh_frame_end, fde_count,
                                pi, need_unwind_info, NULL);
    #endif
        }

      edi->di_cache.start_ip = start_ip;
      edi->di_cache.end_ip = end_ip;
      edi->di_cache.format = UNW_INFO_FORMAT_REMOTE_TABLE;
      edi->di_cache.u.rti.name_ptr = 0;
      /* two 32-bit values (ip_offset/fde_offset) per table-entry: */
      edi->di_cache.u.rti.table_len = (fde_count * 8) / sizeof (unw_word_t);
      edi->di_cache.u.rti.table_data = ((load_base + peh_hdr->p_vaddr)
                                        + sizeof(*hdr));

      /* For the binary-search table in the eh_frame_hdr, data-relative
         means relative to the start of that section... */
      edi->di_cache.u.rti.segbase = (load_base + peh_hdr->p_vaddr);
      found = 1;
    }

#if UNW_TARGET_ARM
  if (parm_exidx)
    {
      edi->di_arm.format = UNW_INFO_FORMAT_ARM_EXIDX;
      edi->di_arm.start_ip = start_ip;
      edi->di_arm.end_ip = end_ip;
      edi->di_arm.u.rti.name_ptr = (unw_word_t) path;
      edi->di_arm.u.rti.table_data = load_base + parm_exidx->p_vaddr;
      edi->di_arm.u.rti.table_len = parm_exidx->p_memsz;
      found = 1;
    }
#endif

#ifdef CONFIG_DEBUG_FRAME
  /* Try .debug_frame. */
  found = dwarf_find_debug_frame (found, &edi->di_debug, ip, load_base, path,
                                  start_ip, end_ip);
#endif

  Debug(3, "returning, found %d\n", found);
  return found;
}

/* Exported version that uses the address space of the potentially
   remote process.
   We only need to read memory, but pass |as| to keep a consistent API.  */

HIDDEN int
dwarf_as_find_unwind_table (struct as_elf_dyn_info *edi, unw_addr_space_t as,
                            const char *path, unw_word_t segbase, unw_word_t mapoff,
                            unw_word_t ip)
{
  Elf_W(Phdr) *phdr, *ptxt = NULL, *peh_hdr = NULL, *pdyn = NULL;
  unw_word_t addr, eh_frame_start, fde_count, load_base;
  unw_word_t start_ip = (unw_word_t) -1;
  unw_word_t end_ip = 0;
  struct dwarf_eh_frame_hdr *hdr;
  unw_proc_info_t pi;
  Elf_W(Ehdr) *ehdr;
#if 0
#if UNW_TARGET_ARM
  const Elf_W(Phdr) *parm_exidx = NULL;
#endif
#endif
  int i, ret, found = 0;

  Debug (3, "(edi %p, %p, \"%s\", 0x%lx, 0x%lx, 0x%lx)\n", edi, as, path, segbase, mapoff, ip);

  // TODO(dje): byteswapping of ehdr values
  ret = unwi_load_as_contents(as, &edi->ehdr, segbase, sizeof(*ehdr), edi->arg);
  if (ret < 0)
    {
      Debug(3, "returning, unwi_load_as_contents failed: %d\n", ret);
      return ret;
    }
  ehdr = edi->ehdr.data;

  {
    /* Construct a fake elf_image sufficient for valid_object.  */
    struct elf_image ei;
    ei.image = ehdr;
    ei.size = sizeof (*ehdr);
    if (!elf_w(valid_object) (&ei))
      {
        Debug(3, "returning, invalid elf object\n");
        return -UNW_ENOINFO;
      }
  }

  // TODO(dje): byteswapping of ehdr values
  {
    size_t phdr_size = ehdr->e_phnum * ehdr->e_phentsize;
    ret = unwi_load_as_contents(as, &edi->phdr, segbase + ehdr->e_phoff, phdr_size, edi->arg);
    if (ret < 0)
      {
        Debug(3, "returning, unwi_load_as_contents failed: %d\n", ret);
        return ret;
      }
    phdr = edi->phdr.data;
  }

  Debug (3, "scanning phdrs\n");

  for (i = 0; i < ehdr->e_phnum; ++i)
    {
      Debug (5, "phdr[%d]: type 0x%x, vaddr 0x%lx, memsz 0x%lx\n",
             i, phdr[i].p_type, (long) phdr[i].p_vaddr, (long) phdr[i].p_memsz);

      switch (phdr[i].p_type)
        {
        case PT_LOAD:
          if (phdr[i].p_vaddr < start_ip)
            start_ip = phdr[i].p_vaddr;

          if (phdr[i].p_vaddr + phdr[i].p_memsz > end_ip)
            end_ip = phdr[i].p_vaddr + phdr[i].p_memsz;

          if (phdr[i].p_offset == mapoff)
            ptxt = phdr + i;

          break;

        case PT_GNU_EH_FRAME:
          peh_hdr = phdr + i;
          ret = unwi_load_as_contents(as, &edi->eh, segbase + peh_hdr->p_vaddr, peh_hdr->p_memsz, edi->arg);
          if (ret < 0)
            {
              Debug(3, "returning, unwi_load_as_contents failed: %d\n", ret);
              return ret;
            }
          break;

        case PT_DYNAMIC:
          pdyn = phdr + i;
          ret = unwi_load_as_contents(as, &edi->dyn, segbase + pdyn->p_vaddr, pdyn->p_memsz, edi->arg);
          if (ret < 0)
            {
              Debug(3, "returning, unwi_load_as_contents failed: %d\n", ret);
              return ret;
            }
          break;

#if 0
#if UNW_TARGET_ARM
        case PT_ARM_EXIDX:
          parm_exidx = phdr + i;
          break;
#endif
#endif

        default:
          break;
        }
    }

  Debug (3, "scanning phdrs, ptxt %p\n", ptxt);

  if (!ptxt)
    {
      Debug(3, "returning 0, no text\n");
      return 0;
    }

  load_base = segbase - ptxt->p_vaddr; // ???
  start_ip += load_base;
  end_ip += load_base;

  Debug (3, "load_base 0x%lx, start_ip 0x%lx, end_ip 0x%lx\n",
         (long) load_base, (long) start_ip, (long) end_ip);

  if (peh_hdr)
    {
      if (pdyn)
        {
          /* For dynamicly linked executables and shared libraries,
             DT_PLTGOT is the value that data-relative addresses are
             relative to for that object.  We call this the "gp".  */
          Elf_W(Dyn) *dyn = (Elf_W(Dyn) *)(edi->dyn.data);
          for (; dyn->d_tag != DT_NULL; ++dyn)
            if (dyn->d_tag == DT_PLTGOT)
              {
                /* Assume that _DYNAMIC is writable and GLIBC has
                   relocated it (true for x86 at least).  */
                edi->di_cache.gp = dyn->d_un.d_ptr;
                break;
              }
        }
      else
        /* Otherwise this is a static executable with no _DYNAMIC.  Assume
           that data-relative addresses are relative to 0, i.e.,
           absolute.  */
        edi->di_cache.gp = 0;

      hdr = (struct dwarf_eh_frame_hdr *) (edi->eh.data);
      if (hdr->version != DW_EH_VERSION)
        {
          Debug (1, "returning, table `%s' has unexpected version %d\n",
                 path, hdr->version);
          return -UNW_ENOINFO;
        }

      addr = (unw_word_t) (hdr + 1);

      /* Fill in a dummy proc_info structure.  We just need to fill in
         enough to ensure that dwarf_read_encoded_pointer() can do it's
         job.  Since we don't have a procedure-context at this point, all
         we have to do is fill in the global-pointer.  */
      memset (&pi, 0, sizeof (pi));
      pi.gp = edi->di_cache.gp;

      // We're reading from the .eh_frame we just read in, so it's our local addr space.
      unw_accessors_t *la = unw_get_accessors (unw_local_addr_space);

      Debug(5, "unw_local_addr_space %p, la %p\n", unw_local_addr_space, la);

      /* (Optionally) read eh_frame_ptr: */
      if ((ret = dwarf_read_encoded_pointer (unw_local_addr_space, la,
                                             &addr, hdr->eh_frame_ptr_enc, &pi,
                                             &eh_frame_start, NULL)) < 0)
        {
          Debug(3, "returning, dwarf_read_encoded_pointer failed: %d\n", ret);
          return -UNW_ENOINFO;
        }

      /* (Optionally) read fde_count: */
      if ((ret = dwarf_read_encoded_pointer (unw_local_addr_space, la,
                                             &addr, hdr->fde_count_enc, &pi,
                                             &fde_count, NULL)) < 0)
        {
          Debug(3, "returning, dwarf_read_encoded_pointer failed: %d\n", ret);
          return -UNW_ENOINFO;
        }

      if (hdr->table_enc != (DW_EH_PE_datarel | DW_EH_PE_sdata4))
        {
          /* If there is no search table or it has an unsupported
             encoding, fail.  For now.  */
          Debug(3, "returning, weird table encoding\n");
          return -UNW_ENOINFO;
        }

      //Debug(3, "fde_count %lu \n", (long) fde_count);

      edi->di_cache.start_ip = start_ip;
      edi->di_cache.end_ip = end_ip;
      edi->di_cache.format = UNW_INFO_FORMAT_REMOTE_TABLE;
      edi->di_cache.u.rti.name_ptr = 0;
      /* two 32-bit values (ip_offset/fde_offset) per table-entry: */
      edi->di_cache.u.rti.table_len = (fde_count * 8) / sizeof (unw_word_t);
      edi->di_cache.u.rti.table_data = ((load_base + peh_hdr->p_vaddr)
                                        + sizeof(*hdr));

      /* For the binary-search table in the eh_frame_hdr, data-relative
         means relative to the start of that section... */
      edi->di_cache.u.rti.segbase = (load_base + peh_hdr->p_vaddr);
      found = 1;
    }

#if 0
#if UNW_TARGET_ARM
  if (parm_exidx)
    {
      edi->di_arm.format = UNW_INFO_FORMAT_ARM_EXIDX;
      edi->di_arm.start_ip = start_ip;
      edi->di_arm.end_ip = end_ip;
      edi->di_arm.u.rti.name_ptr = (unw_word_t) path;
      edi->di_arm.u.rti.table_data = load_base + parm_exidx->p_vaddr;
      edi->di_arm.u.rti.table_len = parm_exidx->p_memsz;
      found = 1;
    }
#endif
#endif

#ifdef CONFIG_DEBUG_FRAME
  /* Try .debug_frame. */
  found = dwarf_find_debug_frame (found, &edi->di_debug, ip, load_base, path,
                                  start_ip, end_ip);
#endif

  Debug(3, "returning, found %d\n", found);
  return found;
}
