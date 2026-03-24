#ifndef JANUS_LEXER_HPP
#define JANUS_LEXER_HPP

#include "token.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace janus {

// Lexer :: tokenises Janus source into a flat token stream.

// Handles: keywords, identifiers, integer and
// floating-point literals, string literals with ${} interpolation,
// single-line comments (#), multi-line comments (## ... ##), Dirac
// notation strings (treated as regular string literals at the lexer
// level), all operators and delimiters, and NEWLINE as a statement
// terminator.

// Newlines are suppressed (not emitted) when inside unclosed parentheses
// or brackets so that expressions may span multiple source lines.
// Consecutive newlines are collapsed into a single NEWLINE token.
// Whitespace other than newline is ignored everywhere except inside
// string literals.

class Lexer {
public:
    // Constructs a lexer for the given source text.  The source string is
    // copied and owned by the lexer.
    explicit Lexer(std::string source);

    // Tokenises the entire source and returns the complete token stream.
    // The stream always ends with an EOF_TOKEN.  On any lexical error the
    // single Beta error is reported via report_error and the process
    // terminates.
    std::vector<Token> tokenize();

private:
    std::string source_;
    std::size_t pos_;
    uint32_t    line_;

    // Grouping depth for newline suppression.
    // Newlines inside () or [] are not emitted as NEWLINE tokens.
    // Newlines inside {} ARE emitted because braces delimit statement
    // blocks that contain newline-separated commands.
    int paren_depth_;
    int bracket_depth_;

    // String interpolation tracking.
    // Each entry records the brace nesting depth inside one level of
    // ${...} interpolation so that the lexer knows when the closing }
    // belongs to the interpolation rather than to a nested block.
    struct InterpContext {
        int brace_depth;
    };
    std::vector<InterpContext> interp_stack_;

    // Character access helpers.
    char        peek() const;
    char        peek_next() const;
    char        advance();
    bool        at_end() const;
    bool        match(char expected);

    // Skips whitespace (excluding newline) and comments.
    void skip_whitespace_and_comments();

    // Scans a numeric literal (INTEGER_LITERAL or FLOAT_LITERAL).
    Token scan_number();

    // Scans an identifier or keyword.
    Token scan_identifier_or_keyword();

    // Scans string content starting immediately after the opening " or
    // the closing } of an interpolation expression.  Pushes one or more
    // tokens to the output vector.  When string interpolation is
    // encountered, pushes the interp stack so that subsequent calls to
    // the main tokenize loop will process the embedded expression.

    // continuation is true when resuming a string after a } that closed
    // an interpolation, and false when starting a fresh string from ".
    void scan_string_content(std::vector<Token>& tokens, bool continuation);
};

} // namespace janus

#endif // JANUS_LEXER_HPP
