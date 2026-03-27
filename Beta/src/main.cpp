#include "lexer.hpp"
#include "parser.hpp"
#include "typechecker.hpp"
#include "ir_gen.hpp"
#include "backend_quest.hpp"
#include "backend_qiskit.hpp"
#include "error.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

static constexpr const char* JANUS_VERSION = "Janus Beta 0.1.0";

// Derives the default Qiskit output path from the source file path.
// Replaces the .jan extension with .py.  If the source has no .jan
// extension, appends .py to the full name.
static std::string default_qiskit_output(const std::string& source_path) {
    std::string base = source_path;

    // Strip directory components to produce the output in the current
    // working directory, matching C compiler conventions.
    std::size_t sep = base.find_last_of("/\\");
    if (sep != std::string::npos) {
        base = base.substr(sep + 1);
    }

    std::string_view sv(base);
    if (sv.size() > 4 && sv.substr(sv.size() - 4) == ".jan") {
        return std::string(sv.substr(0, sv.size() - 4)) + ".py";
    }
    return base + ".py";
}

// Reads the entire contents of a file into a string.
// On failure, error with line 0
static std::string read_source_file(const std::string& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        janus::report_error(0);
    }

    std::ostringstream contents;
    contents << file.rdbuf();

    if (file.bad()) {
        janus::report_error(0);
    }

    return contents.str();
}

int main(int argc, char* argv[]) {
    bool        qiskit_mode = false;
    bool        use_32bit   = false;
    bool        have_output = false;
    std::string output_name;
    std::string source_path;

    // Parse command-line arguments.  Flags and the source file may
    // appear in any order.
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);

        if (arg == "-v") {
            std::puts(JANUS_VERSION);
            return 0;
        }

        if (arg == "-qiskit") {
            qiskit_mode = true;
            continue;
        }

        if (arg == "-p32") {
            use_32bit = true;
            continue;
        }

        if (arg == "-o") {
            if (i + 1 >= argc) {
                // -o requires an argument.
                janus::report_error(0);
            }
            ++i;
            have_output = true;
            output_name = argv[i];
            continue;
        }

        // Any argument starting with '-' that is not a recognised flag
        // is an error.
        if (!arg.empty() && arg[0] == '-') {
            janus::report_error(0);
        }

        // Non-flag argument: treat as the source file.
        if (!source_path.empty()) {
            // Multiple source files are not supported.
            janus::report_error(0);
        }
        source_path = arg;
    }

    // A source file is required.
    if (source_path.empty()) {
        janus::report_error(0);
    }

    // -o is only meaningful alongside -qiskit.  When used without
    // -qiskit, it is silently ignored

    // Read the source file.
    std::string source = read_source_file(source_path);

    // Lexing
    janus::Lexer lexer(std::move(source));
    std::vector<janus::Token> tokens = lexer.tokenize();

    // Parsing
    janus::Parser parser(std::move(tokens));
    janus::Program ast = parser.parse();

    // Type checking
    janus::TypeChecker checker;
    checker.check(ast);

    // IR generation
    janus::IRGen ir_gen(checker);
    janus::IRProgram ir = ir_gen.generate(ast);

    // Backend dispatch
    if (qiskit_mode) {
        // Determine the output file path.
        std::string out_path;
        if (have_output) {
            out_path = output_name;
        } else {
            out_path = default_qiskit_output(source_path);
        }

        // -p32 has no effect on the Qiskit backend and is silently
        // ignored
        janus::qiskit_transpile(ir, out_path);
        return 0;
    }

    // Direct execution via the QuEST simulation backend.
    // Produces no output file.  -o is silently ignored.
    int exit_code = janus::quest_execute(ir, use_32bit);
    return exit_code;
}
