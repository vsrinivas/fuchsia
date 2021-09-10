# Only generate completions if $FUCHSIA_DIR is set
if test -z "$FUCHSIA_DIR"
    exit
end

complete -c fx --no-files --require-parameter

complete -c fx --condition '__fish_use_subcommand' --arguments "help" --description "Show help for COMMAND"
complete -c fx --condition "__fish_seen_subcommand_from help" -l "no-contrib" --description "Hide contrib commands (see //tools/devshell/README.md)"
complete -c fx --condition "__fish_seen_subcommand_from help" -l "deprecated" --description "Do not hide deprecated commands"

# Compute vendor directories using set to avoid getting a Fish error if
# $FUCHSIA_DIR/vendor does not exist.
# https://fishshell.com/docs/current/language.html?highlight=exceptions#wildcards-globbing
set -l vendor_devshell_dirs $FUCHSIA_DIR/vendor/*/scripts/devshell/

# Find fx subcommands and their descriptions
find $FUCHSIA_DIR/tools/devshell/ $vendor_devshell_dirs \
-maxdepth 2 -type f \( -executable -or -name '*.fx' \) \
| xargs grep -E '^### +' \
| while read -a -l line -d ':### '
    set -l command_name (basename -s '.fx' $line[1])
    set -l command_desc (string trim $line[2])
    # save subcommand name
    set -a fx_subcommands $command_name
    complete -c fx --condition '__fish_use_subcommand' --arguments $command_name --description $command_desc
    complete -c fx --condition "__fish_seen_subcommand_from help" --arguments $command_name --description $command_desc
end

# Returns true if no subcommand is on the command line
function __fish_fx_needs_subcommand
    not __fish_seen_subcommand_from $fx_subcommands
end

# global fx options
complete -c fx --condition "__fish_fx_needs_subcommand" -l "dir" -d "Path to the build directory to use when running COMMAND."
complete -c fx --condition "__fish_fx_needs_subcommand" -o "d" -d "Target a specific device."
complete -c fx --condition "__fish_fx_needs_subcommand" -o "i" -d "Iterative mode"
complete -c fx --condition "__fish_fx_needs_subcommand" -o "x" -d "Print commands and their arguments as they are executed."
complete -c fx --condition "__fish_fx_needs_subcommand" -o "xx" -d "Print extra logging of the fx tool itself (implies -x)"

# Find fx subcommand options
find $FUCHSIA_DIR/tools/devshell/ \
-maxdepth 2 -type f \( -executable -or -name '*.fx' \) \
| xargs grep -E '^## +-[^ ]' \
| while read -a -l line -d ':'
    set -l command_name (basename -s '.fx' $line[1])
    set -l command_desc $line[2]
    set -l shortoption
    set -l longoption
    set -l longoption2
    set -l argname
    set -l desc

    if set -l flgs (string match --regex '##\s+(-[^-])(?:, ?|\|)(--[^ ]+)(?:\s*)([^-].*)?' $command_desc)
        # short followed by long option
        set shortoption $flgs[2]
        set longoption $flgs[3]
        set desc $flgs[4]
    else if set -l flgs (string match --regex '##\s+(--[^ ,|]+)(?:, ?|\|)(-[^ ,|]+)(?:\s*)([^-].*)?' $command_desc)
        # long followed by short option
        set longoption $flgs[2]

        # is the second option another long?
        if printf "%s\n" $flgs[3] | string match --regex '(--[^ ,|]+).*'
            set longoption2 $flgs[3]
        else
            set shortoption $flgs[3]
        end
        set desc $flgs[4]
    else if set -l flgs (string match --regex '##\s+(--[^ ,|]+)(?:\s*)([^-].*)?' $command_desc)
        # single long option
        set longoption $flgs[2]
        set desc $flgs[3]
        if set -l la (printf "%s\n" $flgs[2] | string match --regex '(--[^=]+)=[<]?([^>\s]+)[>]?')
            set longoption $la[2]
            set argname $la[3]
        end
    else if set -l flgs (string match --regex '##\s+(-[^-\s]+)(?:\s*)([^-].*)?' $command_desc)
        # single short option
        set shortoption $flgs[2]
        set desc $flgs[3]
    end

    # extract the first word in the desc if it's all caps
    if set -l leadingargname (string match --regex '^\s*([A-Z_-][A-Z_-]+)\s+(.*)' $desc)
        set argname $leadingargname[2]
        set desc $leadingargname[3]
    end

    # trim desc
    set desc (printf "%s" "$desc" | string trim)
    # if no description set, create one so every completion has one
    if test -z "$desc"
        set desc "$shortoption $longoption"
    end

    # trim remaining options
    set shortoption (printf "%s" "$shortoption" | string trim | string trim -l -c '-')
    set longoption (printf "%s" "$longoption" | string trim | string trim -l -c '-')
    set longoption2 (printf "%s" "$longoption2" | string trim | string trim -l -c '-')
    set argname (printf "%s" "$argname" | string trim)

    # define completions
    if test -n "$shortoption" -a -n "$desc"
        # old long option style (single dash): -xx
        complete -c fx --condition "__fish_seen_subcommand_from $command_name" -o $shortoption -d $desc
    end
    if test -n "$longoption" -a -n "$desc"
        # long option style: --option
        complete -c fx --condition "__fish_seen_subcommand_from $command_name" -l $longoption -d $desc
    end
    if test -n "$longoption2" -a -n "$desc"
        # second long option, --goma|--no-goma
        complete -c fx --condition "__fish_seen_subcommand_from $command_name" -l $longoption2 -d $desc
    end
end
