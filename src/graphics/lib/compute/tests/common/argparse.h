// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_ARGPARSE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_ARGPARSE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off

// An easy-to-use command-line parsing facility for C programs. Inspired by
// the Python argparse library:
//
//  - Simplify option list declarations and value collection.
//
//  - Automatically handle parsing errors (e.g. missing parameter, unknown
//    option, or integer overflow/underflow) for you.
//
//  - Automically handle printing a decent help message with --help or -?
//    is used on the command-line.
//
// Usage is the following:
//
// 1) Define a macro that must take a single |param| parameter, and which
//    consists in a series of ARGPARSE_OPTION_XXX() calls, where each call
//    corresponds to a command-line option, and should take 5 arguments
//    which are:
//
//        |param| is the single parameter of your custom list macro.
//        |name| is the option's result struct member name.
//        |char| is a single char for short-format options, or 0 if not needed.
//        |long| is a string for long-format options, or NULL if not needed.
//        |description| is a short description for the option.
//
//    For example:
//
//       #define MY_OPTIONS(param)                                                            \
//          ARGPARSE_OPTION_COUNTER(param, verbose, 'v', "verbose", "Increment verbosity.")   \
//          ARGPARSE_OPTION_COUNTER(param, quiet, 'q', "quiet", "Decrement verbosity.")       \
//          ARGPARSE_OPTION_FLAG(param, dry_run, 'n', "dry-run", "Dry-run.")                  \
//          ARGPARSE_OPTION_STRING(param, output, 'o', "output", "Output file path.")         \
//
//    There are several ARGPARSE_OPTION_XXX() macros defined below, use the one
//    corresponding to the option types you need.
//
// 2) Call ARGPARSE_DEFINE_OPTIONS_AND_PARSE_ARGS(my_options,MY_OPTIONS,...)
//    this will do all these things for you:
//
//     - Define a struct variable named |my_options|, whose members match the
//       names of your ARGPARSE_OPTION_XXX() calls.
//
//     - Pass |argc| and |argv| to the command-line parser, and removes all
//       processed options from them (so you can inspect additional arguments).
//
//     - In case of error, print an error message to stderr, then call
//       exit(EXIT_FAILURE).
//
//     - If needed, print the help message to stdout and exit(EXIT_SUCCESS).
//       This happens if --help or -? are used on the command-line.
//
//    After the call, the option values can be accessed directly through your
//    struct variable, e.g. |my_options.verbose|, |my_options.output|, etc.
//
//    The C type of these members corresponds their ARGPARSE_OPTION_XXX()
//    macro. E.g. it will be a bool for ARGPARSE_OPTION_FLAG(). See the
//    ARGPARSE_OPTION_XXX() documentation below for details.
//
//  Complete example:
//
//       #define MY_OPTIONS(param)                                                            \
//          ARGPARSE_OPTION_COUNTER(param, verbose, 'v', "verbose", "Increment verbosity.") \
//          ARGPARSE_OPTION_COUNTER(param, quiet, 'q', "quiet", "Decrement verbosity.")     \
//          ARGPARSE_OPTION_FLAG(param, dry_run, 'n', "dry-run", "Dry-run.")                \
//          ARGPARSE_OPTION_STRING(param, output, 'o', "output", "Output file path.")       \
//
//       int main(int argc, char** argv) {
//          ARGPARSE_DEFINE_OPTIONS_AND_PARSE_ARGS(
//              my_options,
//              MY_OPTIONS,
//              argc,
//              argv,
//              "myprogram",
//              "My program short description");
//
//          // Voila! Simply use members from |my_options| here.
//          ... use options.<name> variable now.
//       }
//
// Alternatively, you can perform these steps separately if you prefer by using
// the following calls:
//
// 1) Call ARGPARSE_DEFINE_OPTIONS_STRUCT(my_options, MY_OPTIONS) to define a
//    variable that is a struct holding your options values, e.g.:
//
//        ARGPARSE_DEFINE_OPTIONS_STRUCT(my_options, MY_OPTIONS);
//
//    It expands to a struct variable declaration like this one:
//
//        struct {
//          int verbose;
//          int quiet;
//          bool dry_run;
//          const char* output;
//          bool help_needed;    // automatically added by argparse!!
//        } my_options = {}
//
//    Note that |help_needed| is added automatically by the parser. One cannot
//    change its name, and it will be only true if the help message should be
//    printed (see below for exact conditions).
//
// 2) Call ARGPARSE_PARSE_ARGS(argc, argv, my_options, MY_OPTIONS) to parse
//    the command-line from |argc| and |argv| and fill |my_options| with the
//    appropriate values.
//
//    On success, this returns true and removes all processed options from
//    |argc| and |argv|. Note that '--' is always treated as a parser stop
//    marker, so anything that appears after it in the input is left as-is
//    in the output, e.g.
//
//         myprogram --verbose other_program -- --verbose argument
//            => myprogram other_program --verbose argument
//
//    If the --help or -? option appears on the command line, and is not a
//    parameter to a previous known option, and does not appear after --, then
//    the function returns false, but sets the 'help_needed' options member to
//    true. The caller should print the help message (e.g. using
//    ARGPARSE_PRINT_HELP()), then should exit.
//
//    If the parser detected an error, e.g. missing parameter or an unknown
//    option, if prints an error message to stderr, then returns false.
//
// 3) ARGPARSE_PRINT_HELP() can be used to print a help message to stdout.
//    The caller has the option of printing more text after the options
//    description text, but then should exit immediately, possibly with
//    exit(0);
//
//    ARGPARSE_PRINT_HELP("myprogram", "My program description", MY_OPTIONS)
//    will print something like:
//
//      Usage: myprogram [options] ...
//
//      My program description
//
//         -v, --verbose        Increment verbosity.
//         -q, --quiet          Decrement verbosity.
//         -n, --dry_run        Dry run, do not write output.
//         -o, --output OUTPUT  Output file path.
//
//  Complete example:
//
//       #define MY_OPTIONS(param)                                                            \
//          ARGPARSE_OPTION_COUNTER(param, verbose, 'v', "--verbose", "Increment verbosity.") \
//          ARGPARSE_OPTION_COUNTER(param, quiet, 'q', "--quiet", "Decrement verbosity.")     \
//          ARGPARSE_OPTION_FLAG(param, dry_run, 'n', "--dry-run", "Dry-run.")                \
//          ARGPARSE_OPTION_STRING(param, output, 'o', "--output", "Output file path.")       \
//
//       int main(int argc, char** argv) {
//          ARGPARSE_DEFINE_OPTIONS_STRUCT(options);
//          if (!ARGPARSE_PARSE_ARGS(options, MY_OPTIONS)) {
//             if (options.help) {
//               ARGPARSE_PRINT_HELP("myprogram", "my description", MY_OPTIONS);
//             }
//             exit(options.help ? EXIT_SUCCESS : EXIT_FAILURE);
//          }
//          // Process options and remaining parameters.
//       }
//

// clang-format on

// ARGPARSE_OPTION_XXX() macros, each one corresponds to a single option type
// supported by the library, and take the same parameter list, which is:
// |param| is the generic parameter taken by your cursomt options list macro.
// |name| is the option's name, used as a member name in your options struct.
// |chr| is the flag's single character for the command line, or 0 if not used.
// |string| is the flag's long name for the command line, or NULL if not used.
// |description| is a small string to describe the option.

// Used to declare an optional boolean flag. The corresponding struct
// member is a bool, initialized to false, and set to true if the flag
// is parsed any number of times.
#define ARGPARSE_OPTION_FLAG(macro, name, chr, string, description)                                \
  macro(ARGPARSE_OPTION_TYPE_FLAG, name, chr, string, description)

// Used to declare an optional string option. The corresponding struct
// member is a const char*, initialized to NULL, and set to the option's
// parameter if used. If parsed multiple times, this will always store the
// last parameter.
#define ARGPARSE_OPTION_STRING(macro, name, chr, string, description)                              \
  macro(ARGPARSE_OPTION_TYPE_STRING, name, chr, string, description)

// Used to declare an optional integer option. The corresponding options
// member is an argparse_int value, which contains a |used| boolean flag,
// and a |value| int32_t value that is only valid if |used == true|.
#define ARGPARSE_OPTION_INT(macro, name, chr, string, description)                                 \
  macro(ARGPARSE_OPTION_TYPE_INT, name, chr, string, description)

// Used to declare an optional counting flag. The corresponding options member
// is an int, initialized to 0, and incremented each time the flag is used.
#define ARGPARSE_OPTION_COUNTER(macro, name, chr, string, description)                             \
  macro(ARGPARSE_OPTION_TYPE_COUNTER, name, chr, string, description)

// Used to declare an optional double option. The corresponding options
// member is an argparse_double value, which contains a |used| boolean flag,
// and a |value| double value that is only valid if |used == true|.
#define ARGPARSE_OPTION_DOUBLE(macro, name, chr, string, description)                              \
  macro(ARGPARSE_OPTION_TYPE_DOUBLE, name, chr, string, description)

// A small type used to store the value of an optional integer.
// If the option appears, |used| will be true, and |value| will be the
// value that appears on the command-line. Otherwise |used| is false and
// |value| will be default-initialized to 0.
struct argparse_int
{
  bool    used;
  int32_t value;
};

// Same as argparse_int, but for a double value.
struct argparse_double
{
  bool   used;
  double value;
};

// Call this macro to perform everything for you at the start of your main():
//
// 1) It will define an struct variable named |options| containing the values
//    of all your options.
//
// 2) It will parse |argc| and |argv| to process the options in MY_OPTIONS
//    and will also remove them, and their parameters, from the array.
//
// 3) If needed, print the help message to stdout and call exit(EXIT_SUCCESS).
//
// 4) On error, print an error message to stderr and call exit(EXIT_FAILURE).
//
// |options| is the name of your struct variable holding option values.
// |options_list| is a custom macro list of ARGPARSE_OPTION_XXX() calls, one per option.
// |argc| and |argv| are you main() function's command-line.
// |progname| is a short program name for the help message.
// |progdesc| is a short program description for the help message too.
//
#define ARGPARSE_DEFINE_OPTIONS_AND_PARSE_ARGS(options,                                            \
                                               options_list,                                       \
                                               argc,                                               \
                                               argv,                                               \
                                               progname,                                           \
                                               progdesc)                                           \
  ARGPARSE_DEFINE_OPTIONS_STRUCT(options, options_list);                                           \
  if (!ARGPARSE_PARSE_ARGS(argc, argv, options, options_list))                                     \
    {                                                                                              \
      if (options.help_needed)                                                                     \
        {                                                                                          \
          ARGPARSE_PRINT_HELP(progname, progdesc, options_list);                                   \
        }                                                                                          \
      exit(options.help_needed ? EXIT_SUCCESS : EXIT_FAILURE);                                     \
    }

// Call this macro to define a struct variable named |options| containing
// the option values defined through your custom |options_list| macro.
// All values in |options| will be designated-initialized.
#define ARGPARSE_DEFINE_OPTIONS_STRUCT(options, options_list)                                      \
  struct                                                                                           \
  {                                                                                                \
    ARGPARSE_LIST_APPLY(options_list, ARGPARSE_OPTION_TO_STRUCT_MEMBER_DEFINITION)                 \
  } options = {}

// Parse the command-line identified by |argc| and |argv| according to your
// custom |options_list| macro, placing the options value into |options|,
// which must have been declared previously with ARGPARSE_DEFINE_OPTIONS_STRUCT
// and the same |options| and |options_list| arguments.
//
// Return an argparse_status value.If not ARGPARSE_STATUS_OK (0), the caller
// should either print the help with ARGPARSE_PRINT_HELP(), or immediately
// exit.
#define ARGPARSE_PARSE_ARGS(argc, argv, options, options_list)                                     \
  argparse_parse_args(&(argc),                                                                     \
                      (argv),                                                                      \
                      ARGPARSE_LIST_TO_LAYOUT_ARRAY_LITERAL(options_list),                         \
                      ARGPARSE_LIST_TO_POINTER_ARRAY(options_list, options))

// Print the help description for |program_name|, where |program_description|
// is NULL or a string describing the program's purpose. |options_list| is your
// custom options macro list. Everything is printed to stdout.
#define ARGPARSE_PRINT_HELP(program_name, program_description, options_list)                       \
  argparse_print_help((program_name),                                                              \
                      (program_description),                                                       \
                      ARGPARSE_LIST_TO_LAYOUT_ARRAY_LITERAL(options_list))

// Internal structure used to describe the layout/properties of a given option.
struct argparse_option_layout;

// NOTE: Do not call this directly, use ARGPARSE_PARSE_ARGS() instead.
//
// Parse the command-line identified by |*p_argc| and |*p_argv|.
// On success, remove the options from |*p_argc| and |*p_argv|.
// |option_layout| and |option_ptrs| are two parallel arrays of the same size,
// each item corresponding to an option description and the pointer to its
// option struct member, respectively. The last item in |option_layout|
// should have the special value ARGPARSE_OPTION_LAYOUT_END, and the one
// in |option_ptrs| should be NULL.
//
// Return true on success, false on error/help needed.
bool
argparse_parse_args(int * const                                 p_argc,
                    const char ** const                         argv,
                    const struct argparse_option_layout * const option_layout,
                    void * const * const                        option_ptrs);

// NOTE: Do not call this directly, use ARGPARSE_PRINT_HELP() instead.
//
// Print a help message for program |progname|, using a small optional
// |description|, and the corresponding |option_layout| array, whose
// last item should have the special value ARGPARSE_OPTION_LAYOUT_END.
void
argparse_print_help(const char *                                progname,
                    const char *                                description,
                    const struct argparse_option_layout * const option_layout);

//
// Internal data types definitions.
//
// WARNING: All that appears below is subject to change, so clients should
//          never rely on them in their code (except unit-tests which need
//          that to get a finer control of the parser).
//
typedef void (*argparse_print_func)(void *, const char * fmt, ...);

// Same as argparse_print_help(), but allows passing a custom output function.
// Only used by unit-tests.
void
argparse_print_help_internal(const char *                                progname,
                             const char *                                description,
                             const struct argparse_option_layout * const option_layout,
                             argparse_print_func                         print_func,
                             void *                                      print_param);

// Expand |x| through the pre-processor. This is useful because using # or ##
// in an expression disables expansion.
#define ARGPARSE_EVAL(x) ARGPARSE_EVAL_(x)
#define ARGPARSE_EVAL_(x) x

#define ARGPARSE_OPTION_HELP(macro)                                                                \
  macro(ARGPARSE_OPTION_TYPE_HELP, help_needed, '?', "help", "Print help")

// A enum listing the supported option types. Must match the types
// used in ARGPARSE_OPTION_XXX().
//
enum argparse_option_type
{
  ARGPARSE_OPTION_TYPE_FLAG = 0,
  ARGPARSE_OPTION_TYPE_INT,
  ARGPARSE_OPTION_TYPE_STRING,
  ARGPARSE_OPTION_TYPE_COUNTER,
  ARGPARSE_OPTION_TYPE_DOUBLE,

  ARGPARSE_OPTION_TYPE_HELP,  // Must always be last.
};

#define ARGPARSE_OPTION_TYPE_FLAG_CTYPE bool
#define ARGPARSE_OPTION_TYPE_INT_CTYPE struct argparse_int
#define ARGPARSE_OPTION_TYPE_STRING_CTYPE char *
#define ARGPARSE_OPTION_TYPE_COUNTER_CTYPE int
#define ARGPARSE_OPTION_TYPE_DOUBLE_CTYPE struct argparse_double
#define ARGPARSE_OPTION_TYPE_HELP_CTYPE bool

#define ARGPARSE_OPTION_TYPE_CTYPE(type) ARGPARSE_EVAL(type##_CTYPE)

// ARGPARSE_OPTION_TO_STRUCT_MEMBER_DEFINITION is used to expand an options list
// into a list of struct member declarations with default initializers.
#define ARGPARSE_OPTION_TO_STRUCT_MEMBER_DEFINITION(type, name, chr, string, description)          \
  ARGPARSE_OPTION_TYPE_CTYPE(type) name;

// A small struct used internally to describe a given option to the parser.
// |opt_ptr| is the address of the option's value in the final object.
struct argparse_option_layout
{
  enum argparse_option_type opt_type;
  char                      opt_char;
  const char *              opt_long;
  const char *              opt_description;
};

// ARGPARSE_OPTION_TO_LAYOUT_LITERAL is used to expand an options list into
// a literal array of struct argparse_option_layout literal values.

// NOTE: This assumes that |options| is already defined in the expanding context!!
#define ARGPARSE_OPTION_TO_LAYOUT_LITERAL(type, name, chr, string, description)                    \
  {                                                                                                \
    .opt_type        = type,                                                                       \
    .opt_char        = chr,                                                                        \
    .opt_long        = string,                                                                     \
    .opt_description = description,                                                                \
  },

// Add the sentinel help option layout at the hend of |options_list| then
// invoke the result with |param|.
#define ARGPARSE_LIST_APPLY(options_list, param) options_list(param) ARGPARSE_OPTION_HELP(param)

#define ARGPARSE_LIST_TO_LAYOUT_ARRAY_LITERAL(options_list)                                        \
  (const struct argparse_option_layout[])                                                          \
  {                                                                                                \
    ARGPARSE_LIST_APPLY(options_list, ARGPARSE_OPTION_TO_LAYOUT_LITERAL)                           \
  }

#define ARGPARSE_OPTION_TO_MEMBER_POINTER(type, name, chr, string, description) &(options).name,

#define ARGPARSE_LIST_TO_POINTER_ARRAY(options_list, options)                                      \
  (void * const[])                                                                                 \
  {                                                                                                \
    ARGPARSE_LIST_APPLY(options_list, ARGPARSE_OPTION_TO_MEMBER_POINTER)                           \
  }

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_ARGPARSE_H_
