// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "argparse.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Convenience typedefs for the implementation.
typedef enum argparse_option_type     opt_type_t;
typedef struct argparse_option_layout opt_layout_t;
typedef enum argparse_status          status_t;

// Return true iff a given option |type| requires a parameter.
// Update this function when argparse_option_type changes!
static bool
opt_type_requires_parameter(opt_type_t const type)
{
  return (type == ARGPARSE_OPTION_TYPE_STRING) || (type == ARGPARSE_OPTION_TYPE_INT) ||
         (type == ARGPARSE_OPTION_TYPE_DOUBLE);
}

#ifndef NDEBUG
static bool
opt_layout_is_help_option(const opt_layout_t * const layout)
{
  return layout->opt_type == ARGPARSE_OPTION_TYPE_HELP;
}
#endif

// Print parsing error to stderr then return ARGPARSE_STATUS_ERROR
static bool
argparse_error(const char * fmt, ...)
{
  va_list args;
  fprintf(stderr, "ERROR: ");
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
  return false;
}

// Returns true if |arg| matches |layout| exactly (e.g. "--foo" or "-f").
static bool
opt_layout_matches(const opt_layout_t * layout, const char * arg)
{
  assert(arg[0] == '-');
  return (arg[1] == '-' && !strcmp(layout->opt_long, arg + 2)) ||
         (arg[1] == layout->opt_char && arg[2] == 0);
}

// Returns true if --help or -? appears on the command line, and is not
// a parameter of a previous option.
static bool
argparse_has_help_argument(int                  argc,
                           const char * const * argv,
                           const opt_layout_t * layouts,
                           const opt_layout_t * layouts_limit,
                           void * const * const val_ptrs)
{
  // Uses the fact that the help option is the sentinel at the end of the list.
  assert(opt_layout_is_help_option(&layouts_limit[-1]));
  const opt_layout_t * const help = layouts_limit - 1;
  for (int pos = 1; pos < argc; ++pos)
    {
      const char * arg = argv[pos];
      if (arg[0] != '-')
        continue;

      if (arg[1] == '-' && arg[2] == '\0')  // Treat -- as a parser stop.
        return false;

      if (!opt_layout_matches(help, arg))
        continue;

      // Found it, check previous argument now.
      bool is_help = true;
      if (pos > 1)
        {
          arg = argv[pos - 1];
          if (arg[0] == '-')
            {
              const opt_layout_t * layout;
              for (layout = layouts; layout < layouts_limit; ++layout)
                {
                  if (opt_layout_matches(layout, arg))
                    {
                      is_help = false;
                      break;
                    }
                }
            }
        }
      if (is_help)
        {
          *(bool *)val_ptrs[help - layouts] = true;  // set help_needed flag.
          return true;
        }
    }
  return false;
}

static bool
opt_layout_apply(const opt_layout_t * layout, void * const ptr, const char * parameter)
{
  switch (layout->opt_type)
    {
      case ARGPARSE_OPTION_TYPE_FLAG:
        *((bool *)ptr) = true;
        break;
      case ARGPARSE_OPTION_TYPE_STRING:
        *((const char **)ptr) = parameter;
        break;
      case ARGPARSE_OPTION_TYPE_COUNTER:
        *((int *)ptr) += 1;
        break;
        case ARGPARSE_OPTION_TYPE_INT: {
          assert(parameter);
          char * end      = NULL;
          long   val_long = strtol(parameter, &end, 0);
          if (val_long < INT_MIN || val_long > INT_MAX)
            return argparse_error("Integer value out of range: %s", parameter);
          if (!*parameter || *end != '\0')
            return argparse_error("Integer expected: %s", parameter);
          *((struct argparse_int *)ptr) =
            (struct argparse_int){ .used = 1, .value = (int)val_long };
          break;
        }
        case ARGPARSE_OPTION_TYPE_DOUBLE: {
          assert(parameter);
          char * end        = NULL;
          errno             = 0;
          double val_double = strtod(parameter, &end);
          if (errno == ERANGE)
            return argparse_error("Double value out of range: %s", parameter);
          if (!*parameter || *end != '\0')
            return argparse_error("Double expected: %s", parameter);
          *((struct argparse_double *)ptr) =
            (struct argparse_double){ .used = 1, .value = val_double };
          break;
        }
      case ARGPARSE_OPTION_TYPE_HELP:
        // This corresponds to -? and --help. Return an error, the client
        // should check options.help_needed after that,
        assert(opt_layout_is_help_option(layout));
        *((bool *)ptr) = true;
        return false;
    }
  return true;
}

// Find the pointer past the last item in a |layouts| array.
static const opt_layout_t *
opt_layouts_get_limit(const opt_layout_t * layouts)
{
  while (layouts->opt_type != ARGPARSE_OPTION_TYPE_HELP)
    layouts++;
  assert(opt_layout_is_help_option(layouts));
  return layouts + 1;
}

void
argparse_print_help_internal(const char *               program_name,
                             const char *               program_description,
                             const opt_layout_t * const layouts,
                             argparse_print_func        print,
                             void *                     out)
{
  print(out, "Usage: %s [options] ...\n\n", program_name ? program_name : "<program>");

  if (program_description)
    print(out, "%s\n\n", program_description);

  const opt_layout_t * layouts_limit = opt_layouts_get_limit(layouts);
  const opt_layout_t * layout;

  // Liberally chosen esthetical constants.
  const size_t margin            = 2;
  const size_t max_column1_width = 16;
  const size_t max_line_width    = 64;

  // TECHNICAL NOTE: This uses the fact that printf("%*s", count, "") will
  // always output |count| spaces to the output. Used for margins and padding.

  // First pass is used to measure text widths, second pass is used to print.
  size_t column1_width = 0;
  size_t column2_pos   = 0;
  size_t column2_width = 0;

  for (int pass = 0; pass < 2; ++pass)
    {
      for (layout = layouts; layout < layouts_limit; ++layout)
        {
          // Print/measure the first column.
          bool do_print    = (pass == 1);
          bool add_newline = false;  // To add a newline after 'long' text.
          if (do_print)
            print(out, "%*s", margin, "");
          size_t pos = margin;
          if (layout->opt_char)
            {
              if (do_print)
                print(out, "-%c", layout->opt_char);
              pos += 2;
            }
          if (layout->opt_long)
            {
              if (layout->opt_char)
                {
                  if (do_print)
                    print(out, ", ");
                  pos += 2;
                }
              const char * option     = layout->opt_long;
              size_t       option_len = strlen(layout->opt_long);
              if (do_print)
                {
                  print(out, "--%s", option);
                }
              pos += 2 + option_len;
              if (opt_type_requires_parameter(layout->opt_type))
                {
                  if (do_print)
                    {
                      print(out, "=");
                      // print uppercase version of option
                      for (size_t n = 0; n < option_len; n++)
                        {
                          int ch = option[n];
                          if (ch >= 'a' && ch < 'z')  // avoid locale-dependent toupper().
                            ch ^= ('a' ^ 'A');
                          else if (ch == '-')
                            ch = '_';
                          print(out, "%c", ch);
                        }
                    }
                  pos += 1 + option_len;
                }
            }

          // Compute the width of the first column here.
          size_t width = pos - margin;
          if (width > max_column1_width)
            {
              width       = max_column1_width;
              add_newline = true;
            }
          if (width > column1_width)
            column1_width = width;

          // Print second column text (no measurement on pass 0).
          if (pass == 1)
            {
              if (!layout->opt_description)
                {
                  print(out, "\n");  // No description at all??
                }
              else
                {
                  if (pos > column2_pos)
                    {
                      print(out, "\n");
                      pos = 0;
                    }
                  if (pos < column2_pos)
                    {
                      print(out, "%*s", column2_pos - pos, "");
                      pos = column2_pos;
                    }
                  // Handle multi-line text now.
                  const char * text        = layout->opt_description;
                  const char * text_limit  = text + strlen(text);
                  size_t       line_margin = 0;
                  while (text < text_limit)
                    {
                      const char * line     = text;
                      size_t       line_len = (text_limit - text);
                      text                  = text_limit;
                      // Split lines longer than |column2_width| here.
                      if (line_len > column2_width)
                        {
                          const char * line_end = line + column2_width;
                          while (line_end > line && line_end[-1] != ' ')
                            line_end--;
                          if (line_end != line)
                            {
                              text = line_end;
                              while (line_end > line && line_end[-1] == ' ')
                                line_end--;
                              line_len = (line_end - line);
                            }
                          add_newline = true;
                        }
                      print(out, "%*s%.*s\n", line_margin, "", line_len, line);
                      line_margin = column2_pos;
                    }
                }
              if (add_newline)
                print(out, "\n");
            }
        }
      column2_pos   = margin + column1_width + 2;
      column2_width = max_line_width - column2_pos;
    }
}

static void
argparse_fprintf(void * opaque, const char * fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vfprintf((FILE *)opaque, fmt, args);
  va_end(args);
}

void
argparse_print_help(const char *               program_name,
                    const char *               program_description,
                    const opt_layout_t * const layouts)
{
  argparse_print_help_internal(program_name,
                               program_description,
                               layouts,
                               argparse_fprintf,
                               stdout);
}

bool
argparse_parse_args(int * const                p_argc,
                    const char ** const        argv,
                    const opt_layout_t * const layouts,
                    void * const * const       val_ptrs)
{
  int                  argc          = *p_argc;
  const char **        argv_write    = argv + 1;
  const opt_layout_t * layouts_limit = opt_layouts_get_limit(layouts);

  int arg_pos = 1;  // Ignore first item, i.e program name.

  // A first pass to detect whether --help or -? appears on the command
  // line, and not a parameter of a previous option. In this case, return
  // immediately, without trying to parse the other options, which could
  // be totally random.
  if (argparse_has_help_argument(argc, argv, layouts, layouts_limit, val_ptrs))
    return false;

  for (arg_pos = 1; arg_pos < argc; arg_pos++)
    {
      const char * arg = argv[arg_pos];

      if (arg[0] != '-')
        {  // Not an option.
          *argv_write++ = arg;
          continue;
        }

      const opt_layout_t * layout;
      const char *         parameter = NULL;

      if (arg[1] != '-')
        {  // Short option
          arg += 1;
          do
            {  // Process all characters until one of them requires a parameter
              int ch = *arg++;
              // Find option in list
              for (layout = layouts; layout < layouts_limit; ++layout)
                {
                  if (layout->opt_char && layout->opt_char == ch)
                    break;
                }
              if (layout == layouts_limit)
                return argparse_error("Unknown option -%c, please see --help", ch);

              // Handle optional parameter
              if (opt_type_requires_parameter(layout->opt_type))
                {
                  if (arg[0])
                    {
                      // Parameter follows option char, e.g. -Iinclude
                      parameter = arg;
                      arg       = "";  // Stop iteration after option processing.
                    }
                  else
                    {
                      // Parameter must appear as the next command-line argument.
                      if (arg_pos + 1 >= argc)
                        {
                          return argparse_error("Missing parameter after -%c option!", ch);
                        }
                      arg_pos += 1;
                      parameter = argv[arg_pos];
                    }
                }
              if (!opt_layout_apply(layout, val_ptrs[layout - layouts], parameter))
                return false;
            }
          while (*arg);
          continue;
        }

      // Process long option
      arg += 2;

      if (*arg == '\0')
        {  // Treat '--' as a special case to stop processing.
          arg_pos += 1;
          break;
        }

      // Extract potential parameter and option name length
      const char * arg_end = strchr(arg, '=');
      size_t       arg_len;
      if (arg_end)
        {  // For --include=<dir>
          parameter = arg_end + 1;
          arg_len   = arg_end - arg;
        }
      else
        {
          arg_len = strlen(arg);
        }

      // Find in options list
      for (layout = layouts; layout < layouts_limit; layout++)
        {
          if (layout->opt_long)
            {
              size_t opt_len = strlen(layout->opt_long);
              if (opt_len == arg_len && !memcmp(layout->opt_long, arg, arg_len))
                break;
            }
        }
      if (layout == layouts_limit)
        return argparse_error("Unknown option --%s, please see --help", arg);

      // Extract parameter if needed and ensure there is no extra one!
      if (opt_type_requires_parameter(layout->opt_type))
        {
          if (!parameter)
            {
              if (arg_pos + 1 >= argc)
                return argparse_error("Missing parameter after --%s option!", arg);
              arg_pos += 1;
              parameter = argv[arg_pos];
            }
        }
      else if (parameter)
        {
          return argparse_error("Option --%s does not take a parameter!", arg);
        }
      if (!opt_layout_apply(layout, val_ptrs[layout - layouts], parameter))
        return false;
    }

  // Copy the rest of the arguments and adjust argc.
  while (arg_pos < argc)
    {
      *argv_write++ = argv[arg_pos++];
    }
  *p_argc = (argv_write - argv);

  // Done
  return true;
}
