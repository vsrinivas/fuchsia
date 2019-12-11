#!/usr/bin/python3
"""
example:
find . \
    -path ./out -prune -o \
    -path ./garnet/public/lib/fidl/fuzz/input_corpus -prune -o \
    -path ./prebuilt -prune -o \
    -type f \
    -name \*.fidl -print0 \
  | xargs -0 tools/fidl/scripts/format_unions.py
"""

import sys


def starts_with_oneof(s, prefixes):
    return any(s.startswith(p) for p in prefixes)


def format_file(path, in_place):
    with open(path, 'r+') as f:
        result = format_lines(f)
        if in_place:
            f.seek(0)
            f.writelines(result)
        else:
            print(''.join(result))


def has_explicit_xunion(path):
    with open(path, 'r') as f:
        in_union = False
        for line in f:
            if starts_with_oneof(line, ['xunion ', 'strict xunion ']):
                in_union = True
            elif in_union and line.startswith('}'):
                in_union = False
            elif in_union and is_explicit(line.split()):
                return True
    return False


def format_lines(in_lines):
    out_lines = []
    current_union_lines = []
    in_union = False
    for line in in_lines:
        if starts_with_oneof(line, ['union ', 'xunion ', 'strict xunion ']):
            in_union = True
            out_lines.append(line)
        elif line.startswith('}') and in_union:
            out_lines += format_union_member_lines(current_union_lines)
            out_lines.append(line)
            current_union_lines = []
            in_union = False
        elif in_union:
            current_union_lines.append(line)
        else:
            out_lines.append(line)
    return out_lines


def format_union_member_lines(lines):
    out_lines = []
    ordinal_width = len(
        str(len([l for l in lines if is_union_field_line(l.split())])))
    index = 1
    for line in lines:
        tokens = line.split()
        if not is_union_field_line(tokens):
            out_lines.append(line)
        elif is_explicit(tokens):
            out_lines.append(line)
        else:  # implicit, update to be explicit
            extra_spaces = (ordinal_width - len(str(index))) * ' '
            indent = (len(line) - len(line.lstrip())) * ' '
            out_lines.append(
                '{0}{1}{2}: {3}'.format(
                    indent, extra_spaces, index, line.lstrip()))
            index += 1
    return out_lines


def is_union_field_line(tokens):
    return not (
        not tokens or tokens[0].startswith('//') or tokens[0].startswith('['))


def is_explicit(tokens):
    return tokens and tokens[0].endswith(':') and tokens[0][:-1].isnumeric()


def test():
    explicit_lines = [
        '    1: uint8 unused1;',
        '    2: uint8 unused2;',
        '    3: array<uint8>:6 variant;',
    ]
    print(format_union_member_lines(explicit_lines))

    implicit_lines = [
        '    uint32 before;',
        '    UnionSize12Alignment4 union;',
        '    uint32 after;',
    ]
    print(format_union_member_lines(implicit_lines))

    many_lines = [
        '    uint32 before;',
        '    UnionSize12Alignment4 union;',
        '    uint32 after;',
        '    uint32 before;',
        '    UnionSize12Alignment4 union;',
        '    uint32 after;',
        '    uint32 before;',
        '    UnionSize12Alignment4 union;',
        '    uint32 after;',
        '    uint32 before;',
        '    UnionSize12Alignment4 union;',
        '    uint32 after;',
    ]
    print(format_union_member_lines(many_lines))

    # doesn't currently work
    attribute_inline = [
        '   [Selector = "v2"] Foo foo;',
    ]
    print(format_union_member_lines(attribute_inline))


if __name__ == '__main__':
    for filename in sys.argv[1:]:
        try:
            format_file(filename, in_place=True)
            # if (has_explicit_xunion(filename)):
            #     print(filename)
        except:
            print('error in file: {0}'.format(filename))
            raise
