// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <ctime>
#include <list>
#include <map>
#include <string>
#include <vector>
#include <fstream>

using std::string;

// ======================= generic parsing machinery =============================================

std::vector<string> tokenize_string(const string& str) {
    std::vector<string> tokens;
    string tok;

    for (auto& c : str) {
        if (isalnum(c) || c == '_') {
            tok += c;
        } else {
            if (!tok.empty())
                tokens.push_back(tok);
            tok.clear();
            if (ispunct(c))
                tokens.emplace_back(1, c);
        }
    }
    if (!tok.empty())
        tokens.push_back(tok);

    return tokens;
}

std::vector<string>& operator +=(std::vector<string>& v1, const std::vector<string>& v2) {
    v1.insert(v1.end(), v2.begin(), v2.end());
    return v1;
}

struct FileCtx {
    const char* file;
    const char* last_token;
    int line_start;
    int line_end;
    bool verbose;

    void print_error(const char* what, const string& extra) const {
        if (line_end) {
            fprintf(stderr, "error: %s : lines %d-%d : %s '%s' [near: %s]\n",
                file, line_start, line_end, what, extra.c_str(), last_token);
        } else {
            fprintf(stderr, "error: %s : line %d : %s '%s' [near: %s]\n",
                file, line_start, what, extra.c_str(), last_token);
        }
    }

    void print_info(const char* what) const {
        fprintf(stderr, "%s : line %d : %s\n", file, line_start, what);
    }

    FileCtx(const char* file, bool verbose)
        : file(file), last_token(nullptr),
          line_start(0), line_end(0),
          verbose(verbose) {}

    FileCtx(const FileCtx& src, int start)
        : file(src.file), last_token(src.last_token),
          line_start(start), line_end(src.line_start),
          verbose(src.verbose) {}
};

std::string eof_str;

class TokenStream {
public:
    TokenStream(const std::vector<string>& tokens, const FileCtx& fc)
        : fc_(fc), ix_(0u), tokens_(tokens) {}

    const string& curr() {
        if (ix_ >= tokens_.size())
            return eof_str;
        return tokens_[ix_];
    }

    const string& next() {
        ix_ += 1u;
        if (ix_ >= tokens_.size()) {
            fc_.print_error("unexpected end of file", "");
            return eof_str;
        }
        return tokens_[ix_];
    }

    const string& peek_next() const {
        auto n = ix_ + 1;
        return (n >= tokens_.size()) ? eof_str : tokens_[n];
    }

    const FileCtx& filectx() {
        fc_.last_token = curr().c_str();
        return fc_;
    }

private:
    FileCtx fc_;
    size_t ix_;
    const std::vector<string>& tokens_;
};

template<typename P>
using ProcFn = bool (*) (P* parser, TokenStream& ts);

template<typename P>
struct Dispatch {
    const char* first_token;
    const char* last_token;
    ProcFn<P> fn;
};

template<typename P>
bool process_line(P* parser, const Dispatch<P>* table,
                  const std::vector<string>& tokens,
                  const FileCtx& fc) {
    static std::vector<string> acc;
    static int start = 0;

    auto& first = acc.empty() ? tokens[0] : acc[0];
    auto& last = tokens.back();

    start = acc.empty() ? fc.line_start : start;

    size_t ix = 0;
    while (table[ix].fn) {
        auto& d = table[ix++];
        if (first == d.first_token) {

            TokenStream ts(tokens, fc);
            if (!d.last_token)
                return d.fn(parser, ts);

            if (last == d.last_token) {
                if (acc.empty()) {
                    // single line case.
                    return d.fn(parser, ts);
                } else {
                    // multiline case.
                    std::vector<string> t(std::move(acc));
                    t += tokens;
                    TokenStream mts(t, FileCtx(fc, start));
                    return d.fn(parser, mts);
                }
            } else {
                // more input is needed.
                acc += tokens;
                return true;
            }
        }
    }

    if (!acc.empty())
        fc.print_error("missing terminator", tokens[0]);
    else
        fc.print_error("unknown token", tokens[0]);
    return false;
}

template<typename P>
bool run_parser(P* parser, const Dispatch<P>* table, const char* input, bool verbose) {
    std::ifstream infile;
    infile.open(input, std::ifstream::in);

    if (!infile.good()) {
        fprintf(stderr, "error: unable to open %s\n", input);
        return false;
    }

    if (verbose)
        fprintf(stderr, "sysgen: processing file %s\n", input);

    bool error = false;
    FileCtx fc(input, verbose);
    string line;

    while (!infile.eof()) {
        getline(infile, line);
        ++fc.line_start;
        auto tokens = tokenize_string(line);
        if (tokens.empty())
            continue;

        if (!process_line(parser, table, tokens, fc)) {
            error = true;
            break;
        }
    }

    if (error) {
        fprintf(stderr, "** stopping at line %d. parsing %s failed.\n", fc.line_start, input);
        return false;
    }

    return true;
}

// ====================== sysgen specific parsing and generation =================================

// TODO(cpu): put the 2 and 8 below as pragmas on the file?
constexpr size_t kMaxReturnArgs = 2;
constexpr size_t kMaxInputArgs = 8;

constexpr char kAuthors[] = "The Fuchsia Authors";

struct ArraySpec {
    enum Kind : uint32_t {
        IN,
        OUT,
        INOUT
    };

    Kind kind;
    uint32_t count;
    string name;

    string kind_str() const {
        switch (kind) {
        case IN: return "IN";
        case OUT: return "OUT";
        default: return "INOUT";
        }
    }

    string to_string() const {
        return "[]" + kind_str();
    }
};

struct TypeSpec {
    string name;
    string type;
    ArraySpec* arr_spec = nullptr;

    void debug_dump() const {
        fprintf(stderr, "  + %s %s\n", type.c_str(), name.c_str());
        if (arr_spec) {
            if (arr_spec->count)
                fprintf(stderr, "      [%u] (explicit)\n", arr_spec->count);
            else
                fprintf(stderr, "      [%s]\n", arr_spec->name.c_str());
        }
    }

    string to_string() const {
        return type + (arr_spec ? arr_spec->to_string() : string());
    }
};

struct Syscall {
    enum Atrributes : uint32_t {
        NORETURN,           // user mode: syscall does not return.
        VDSOPURE,           // user mode: syscall is handled by the vdso.
    };

    FileCtx fc;
    string name;
    std::vector<TypeSpec> ret_spec;
    std::vector<TypeSpec> arg_spec;
    std::vector<Atrributes> attributes;

    bool validate() const {
        if (ret_spec.size() > kMaxReturnArgs) {
            print_error("invalid number of return arguments");
            return false;
        } else if (ret_spec.size() == 1) {
            if (!ret_spec[0].name.empty()) {
                print_error("single return arguments cannot be named");
                return false;
            }
        }
        if (arg_spec.size() > kMaxInputArgs) {
            print_error("invalid number of input arguments");
            return false;
        }
        for (const auto& arg : arg_spec) {
            if (arg.name.empty()) {
                print_error("all input arguments need to be named");
                return false;
            }
            if (arg.arr_spec) {
                if (!valid_array_count(arg)) {
                    std::string err = "invalid array spec for " + arg.name;
                    print_error(err.c_str());
                    return false;
                }
            }
        }
        return true;
    }

    bool valid_array_count(const TypeSpec& ts) const {
        if (ts.arr_spec->count > 0)
            return true;
        // find the argument that represents the array count.
        for (const auto& arg : arg_spec) {
            if (ts.arr_spec->name == arg.name) {
                if (!arg.arr_spec)
                    return true;
                // if the count itself is an array it can only be "[1]".
                // TODO:cpu also enforce INOUT here.
                return (arg.arr_spec->count == 1u);
            }
        }
        return false;
    }

    void print_error(const char* what) const {
        fprintf(stderr, "error: %s  : %s\n", name.c_str(), what);
    }

    void debug_dump() const {
        fprintf(stderr, "line %d: syscall {%s}\n", fc.line_start, name.c_str());
        fprintf(stderr, "- return(s)\n");
        for (auto& r : ret_spec) {
            r.debug_dump();
        }
        fprintf(stderr, "- args(s)\n");
        for (auto& a : arg_spec) {
            a.debug_dump();
        }
    }
};

bool vet_identifier(const string& iden, const FileCtx& fc) {
    if (iden.empty()) {
        fc.print_error("expecting idenfier", "");
        return false;
    }

    if (iden == "syscall" ||
        iden == "returns" || iden == "noreturn" ||
        iden == "IN" || iden == "OUT" || iden == "INOUT") {
        fc.print_error("identifier cannot be keyword or attribute", iden);
        return false;
    }
    if (!isalpha(iden[0])) {
        fc.print_error("identifier should start with a-z|A-Z", string(iden));
        return false;
    }
    return true;
}

bool parse_arrayspec(TokenStream& ts, TypeSpec* type_spec) {
    std::string name;
    uint32_t count = 0;

    if (ts.next() != "[")
        return false;

    if (ts.next().empty())
        return false;

    auto c = ts.curr()[0];

    if (isalpha(c)) {
        if (!vet_identifier(ts.curr(), ts.filectx()))
            return false;
        name = ts.curr();

    } else if (isdigit(c)) {
        count = c - '0';
        if (ts.curr().size() > 1 || count == 0 || count > 9) {
            ts.filectx().print_error("only 1-9 explicit array count allowed", "");
            return false;
        }
    } else {
        ts.filectx().print_error("expected array specifier", "");
        return false;
    }

    if (name == type_spec->name) {
        ts.filectx().print_error("invalid name for an array specifier", name);
        return false;
    }

    if (ts.next() != "]") {
        ts.filectx().print_error("expected", "]");
        return false;
    }

    auto attr = ts.next();
    ArraySpec::Kind kind;

    if (attr == "IN") {
        kind = ArraySpec::IN;
    } else if (attr == "OUT") {
        kind = ArraySpec::OUT;
    } else if (attr == "INOUT") {
        kind = ArraySpec::INOUT;
    } else {
        ts.filectx().print_error("invalid array attribute", attr);
        return false;
    }

    type_spec->arr_spec = new ArraySpec {kind, count, name};
    return true;
}

bool parse_typespec(TokenStream& ts, TypeSpec* type_spec) {
    if (ts.peek_next() == ":") {
        auto name = ts.curr();
        if (!vet_identifier(name, ts.filectx()))
            return false;

        type_spec->name = name;

        ts.next();
        if (ts.next().empty())
            return false;
    }

    auto type = ts.curr();
    if (!vet_identifier(type, ts.filectx()))
        return false;

    type_spec->type = type;

    if (ts.peek_next() != "[")
        return true;

    return parse_arrayspec(ts, type_spec);
}

bool parse_argpack(TokenStream& ts, std::vector<TypeSpec>* v) {
    if (ts.curr() != "(") {
        ts.filectx().print_error("expected", "(");
        return false;
    }

    while (true) {
        if (ts.next() == ")")
            break;

        if (v->size() > 0) {
            if (ts.curr() != ",") {
                ts.filectx().print_error("expected", ", or :");
                return false;
            }
            ts.next();
        }

        TypeSpec type_spec;

        if (!parse_typespec(ts, &type_spec))
            return false;
        v->emplace_back(type_spec);
    }
    return true;
}

enum GenType : uint32_t {
    UserHeaderC,
    KernelHeaderCPP,
    Max
};

struct GenParams {
    const char* file_postfix;
    const char* entry_prefix;
    const char* name_prefix;
    const char* empty_args;
    uint32_t indent_spaces;
    std::map<string, string> overrides;
    std::map<uint32_t, string> attributes;
};

const std::map<string, string> c_overrides = {
    {"", "void"},
    {"any[]IN", "const void*"},
    {"any[]OUT", "void*"},
    {"any[]INOUT", "void*"}
};

const GenParams gen_params[] = {
    // The user header, pure C.  (UserHeaderC)
    {
        ".user.h",          // file postfix.
        "extern",           // function prefix.
        "mx_",              // function name prefix.
        "void",             // no args special type.
        4u,                 // argument indent.
        c_overrides,
        {{Syscall::NORETURN, "__attribute__((noreturn))"}},
    },
    // The kernel header, C++.  (KernelHeaderCPP)
    {
        ".kernel.h",        // file postix.
        nullptr,            // no function prefix
        "sys_",             // function name prefix.
        nullptr,            // no args.
        4u,                 // argument indent.
        c_overrides,
        {},
    }
};

bool generate_file_header(std::ofstream& os) {
    auto t = std::time(nullptr);
    auto ltime = std::localtime(&t);

    os << "// Copyright " << ltime->tm_year + 1900
       << " " << kAuthors << ". All rights reserved.\n";
    os << "// This is a GENERATED file. The license governing this file can be ";
    os << "found in the LICENSE file.\n\n";
    return os.good();
}

const string override_type(const GenParams& gp, const string& type_name) {
    auto ft = gp.overrides.find(type_name);
    return (ft == gp.overrides.end()) ? type_name : ft->second;
}

const string add_attribute(const GenParams& gp, uint32_t attribute) {
    auto ft = gp.attributes.find(attribute);
    return (ft == gp.attributes.end()) ? string() : ft->second;
}

bool generate_legacy_c(const GenParams& gp, std::ofstream& os, const Syscall& sc) {
    if (gp.entry_prefix) {
        os << gp.entry_prefix << " ";
    }

    // writes "[return-type] prefix_[syscall-name]("
    os << override_type(gp, (sc.ret_spec.empty() ? string() : sc.ret_spec[0].to_string()));
    os << " " << gp.name_prefix << sc.name << "(";

    // Writes all arguments.
    for (const auto& arg : sc.arg_spec) {
        if (!os.good())
            return false;
        // writes each parameter in its own line.
        os << "\n" << string(gp.indent_spaces, ' ');

        auto overrided = override_type(gp, arg.to_string());

        if (overrided != arg.to_string()) {
            os << overrided << " " << arg.name;
        } else if (!arg.arr_spec) {
            os << arg.type << " " << arg.name;
        } else {
            if (arg.arr_spec->kind == ArraySpec::IN)
                os << "const ";

            os << arg.type << " " << arg.name;
            os << "[";
            if (arg.arr_spec->count)
                os << arg.arr_spec->count;
            os << "]";
        }

        os << ",";
    }

    if (!sc.arg_spec.empty()) {
        // remove the comma.
        os.seekp(-1, std::ios_base::end);
    } else {
        // empty args might have a special type.
        if (gp.empty_args)
            os << gp.empty_args;
    }

    os << ") ";

    // Writes attributes after arguments.
    for (const auto& attr : sc.attributes) {
        auto a = add_attribute(gp, attr);
        if (!a.empty())
            os << a << " ";
    }

    os.seekp(-1, std::ios_base::end);

    os << ";\n\n";
    return os.good();
}

class SygenGenerator {
public:
    SygenGenerator(bool verbose) : verbose_(verbose) {}

    bool AddSyscall(const Syscall& syscall) {
        if (!syscall.validate())
            return false;
        calls_.push_back(syscall);
        return true;
    }

    bool Generate(const GenType type, const char* output_prefix) {
        auto gp = &gen_params[type];

        string output_file = string(output_prefix) + gp->file_postfix;

        std::ofstream ofile;
        ofile.open(output_file.c_str(), std::ofstream::out);

        if (!ofile.good()) {
            print_error("unable to open", output_file);
            return false;
        }

        if (!generate_file_header(ofile)) {
            print_error("i/o error", output_file);
            return false;
        }

        for (const auto& sc : calls_) {
            if (verbose_)
                sc.debug_dump();
            if (!generate_legacy_c(*gp, ofile, sc)) {
                print_error("generation failed", output_file);
                return false;
            }
        }

        ofile << "\n";
        return true;
    }

private:
    void print_error(const char* what, const string& file) {
        fprintf(stderr, "error: %s for %s\n", what, file.c_str());
    }

    std::list<Syscall> calls_;
    const bool verbose_;
};

bool process_comment(SygenGenerator* parser, TokenStream& ts) {
    return true;
}

bool process_syscall(SygenGenerator* parser, TokenStream& ts) {
    auto name = ts.next();

    if (!vet_identifier(name, ts.filectx()))
        return false;

    Syscall syscall { ts.filectx(), name };

    ts.next();

    if (!parse_argpack(ts, &syscall.arg_spec))
        return false;

    auto return_spec = ts.next();

    if (return_spec == "noreturn") {
        // nothing else follows except terminator.
        syscall.attributes.push_back(Syscall::Atrributes::NORETURN);
    } else if (return_spec == "returns") {
        ts.next();

        if (!parse_argpack(ts, &syscall.ret_spec))
            return false;
        if (syscall.ret_spec.empty()) {
            ts.filectx().print_error("empty return, expected", "noreturn");
            return false;
        }
    } else {
        ts.filectx().print_error("expected", "returns");
        return false;
    }

    if (ts.next() != ";") {
        ts.filectx().print_error("expected", ";");
        return false;
    }

    return parser->AddSyscall(syscall);
}

constexpr Dispatch<SygenGenerator> sysgen_table[] = {
    // comments start with '#' and terminate at the end of line.
    { "#", nullptr, process_comment },
    // sycalls start with 'syscall' and terminate with ';'.
    { "syscall", ";", process_syscall },
    // table terminator.
    { nullptr, nullptr, nullptr }
};

// =================================== driver ====================================================

int main(int argc, char* argv[]) {
    const char* output_prefix = "generated";
    bool verbose = false;

    argc--;
    argv++;
    while (argc > 0) {
        const char *cmd = argv[0];
        if (cmd[0] != '-')
            break;
        if (!strcmp(cmd,"-v")) {
            verbose = true;
        } else if (!strcmp(cmd,"-o")) {
            if (argc < 2) {
              fprintf(stderr, "no output prefix given\n");
              return -1;
            }
            output_prefix = argv[1];
            argc--;
            argv++;
        } else if (!strcmp(cmd,"-h")) {
            fprintf(stderr, "usage: sysgen [-v] [-o output_prefix] file1 ... fileN\n");
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", cmd);
            return -1;
        }
        argc--;
        argv++;
    }
    if (argc < 1) {
        fprintf(stderr, "no syscall-spec input given\n");
        return -1;
    }

    SygenGenerator generator(verbose);

    for (int ix = 0; ix < argc; ix++) {
        if (!run_parser(&generator, sysgen_table, argv[ix], verbose))
            return 1;
    }

    if (!generator.Generate(GenType::UserHeaderC, output_prefix))
        return 1;
    if (!generator.Generate(GenType::KernelHeaderCPP, output_prefix))
        return 1;

    return 0;
}
