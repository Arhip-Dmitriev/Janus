#include "lexer.hpp"
#include "error.hpp"

#include <unordered_map>

namespace janus {


//  Keyword table


// Returns the static keyword-to-TokenType mapping.  Every reserved word
// in the language is listed here.  Identifiers that match a keyword
// are emitted as the corresponding keyword token.

static const std::unordered_map<std::string, TokenType>& keyword_map() {
    static const std::unordered_map<std::string, TokenType> map = {

        // Type keywords
        {"qubit",        TokenType::KW_QUBIT},
        {"cbit",         TokenType::KW_CBIT},
        {"qnum",         TokenType::KW_QNUM},
        {"cnum",         TokenType::KW_CNUM},
        {"cstr",         TokenType::KW_CSTR},
        {"list",         TokenType::KW_LIST},
        {"matrix",       TokenType::KW_MATRIX},
        {"gate",         TokenType::KW_GATE},
        {"circ",         TokenType::KW_CIRC},
        {"block",        TokenType::KW_BLOCK},
        {"function",     TokenType::KW_FUNCTION},

        // Control flow
        {"if",           TokenType::KW_IF},
        {"else",         TokenType::KW_ELSE},
        {"for",          TokenType::KW_FOR},
        {"while",        TokenType::KW_WHILE},
        {"foreach",      TokenType::KW_FOREACH},
        {"in",           TokenType::KW_IN},
        {"where",        TokenType::KW_WHERE},
        {"from",         TokenType::KW_FROM},
        {"to",           TokenType::KW_TO},
        {"break",        TokenType::KW_BREAK},
        {"continue",     TokenType::KW_CONTINUE},
        {"return",       TokenType::KW_RETURN},

        // Literal value keywords
        {"true",         TokenType::KW_TRUE},
        {"false",        TokenType::KW_FALSE},
        {"null",         TokenType::KW_NULL},

        // Keyword operators
        {"and",          TokenType::KW_AND},
        {"nand",         TokenType::KW_NAND},
        {"or",           TokenType::KW_OR},
        {"nor",          TokenType::KW_NOR},
        {"xor",          TokenType::KW_XOR},
        {"xnor",         TokenType::KW_XNOR},
        {"not",          TokenType::KW_NOT},
        {"tensor",       TokenType::KW_TENSOR},

        // Built-in operand keywords (function-call style)
        {"measure",      TokenType::KW_MEASURE},
        {"peek",         TokenType::KW_PEEK},
        {"state",        TokenType::KW_STATE},
        {"expect",       TokenType::KW_EXPECT},
        {"ctrle",        TokenType::KW_CTRLE},
        {"run",          TokenType::KW_RUN},
        {"runh",         TokenType::KW_RUNH},
        {"isunitary",    TokenType::KW_ISUNITARY},
        {"sameoutput",   TokenType::KW_SAMEOUTPUT},
        {"print",        TokenType::KW_PRINT},
        {"delete",       TokenType::KW_DELETE},
        {"sin",          TokenType::KW_SIN},
        {"cos",          TokenType::KW_COS},
        {"numberofgates", TokenType::KW_NUMBEROFGATES},

        // Built-in operand keywords (unary prefix / call style)
        {"det",          TokenType::KW_DET},
        {"transpose",    TokenType::KW_TRANSPOSE},
        {"transposec",   TokenType::KW_TRANSPOSEC},
        {"evals",        TokenType::KW_EVALS},
        {"evecs",        TokenType::KW_EVECS},

        // Circuit inspection
        {"gates",        TokenType::KW_GATES},
        {"qubits",       TokenType::KW_QUBITS},
        {"depth",        TokenType::KW_DEPTH},

        // Built-in constants
        {"pi",           TokenType::KW_PI},
        {"e",            TokenType::KW_E},
    };
    return map;
}


//  Character classification helpers


static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z')
        || (c >= 'A' && c <= 'Z')
        || c == '_';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}


//  Lexer construction


Lexer::Lexer(std::string source)
    : source_(std::move(source)),
      pos_(0),
      line_(1),
      paren_depth_(0),
      bracket_depth_(0) {}


//  Character access


char Lexer::peek() const {
    if (pos_ >= source_.size()) return '\0';
    return source_[pos_];
}

char Lexer::peek_next() const {
    if (pos_ + 1 >= source_.size()) return '\0';
    return source_[pos_ + 1];
}

char Lexer::advance() {
    char c = source_[pos_];
    ++pos_;
    return c;
}

bool Lexer::at_end() const {
    return pos_ >= source_.size();
}

bool Lexer::match(char expected) {
    if (at_end()) return false;
    if (source_[pos_] != expected) return false;
    ++pos_;
    return true;
}


//  Whitespace and comment skipping


// Skips spaces, tabs, carriage returns, and comments.  doesnt skip
// newlines because they are significant as statement terminators.

void Lexer::skip_whitespace_and_comments() {
    while (!at_end()) {
        char c = peek();

        // Spaces, tabs, carriage returns.
        if (c == ' ' || c == '\t' || c == '\r') {
            advance();
            continue;
        }

        // Comments.
        if (c == '#') {
            if (peek_next() == '#') {
                // Multi-line comment: ## ... ##
                advance(); // consume first #
                advance(); // consume second #
                bool closed = false;
                while (!at_end()) {
                    if (peek() == '#' && peek_next() == '#') {
                        advance(); // consume closing #
                        advance(); // consume closing #
                        closed = true;
                        break;
                    }
                    if (peek() == '\n') {
                        ++line_;
                    }
                    advance();
                }
                // Unterminated multi-line comment is an error.
                if (!closed) {
                    report_error(line_);
                }
                continue;
            }
            // Single-line comment: # ... (until end of line or EOF).
            advance(); // consume #
            while (!at_end() && peek() != '\n') {
                advance();
            }
            // The newline itself is NOT consumed here; it will be
            // handled by the main loop as a statement terminator.
            continue;
        }

        // Nothing else to skip.
        break;
    }
}


//  Numeric literal scanning


// Scans an integer or floating-point literal starting at the current
// position.  The first character must be a digit.

Token Lexer::scan_number() {
    uint32_t start_line = line_;
    std::size_t start = pos_;

    // Consume leading digits.
    while (!at_end() && is_digit(peek())) {
        advance();
    }

    bool is_float = false;

    // Check for a decimal point followed by at least one digit.
    if (!at_end() && peek() == '.' && peek_next() != '\0' && is_digit(peek_next())) {
        is_float = true;
        advance(); // consume '.'
        while (!at_end() && is_digit(peek())) {
            advance();
        }
    }

    // Check for scientific notation (e.g. 1e5, 3.14e-2).
    if (!at_end() && (peek() == 'e' || peek() == 'E')) {
        // Only consume if followed by digit or sign+digit, to avoid
        // consuming the constant keyword 'e' when it appears after a
        // number with whitespace (the whitespace would have been
        // consumed already, but the identifier scanner would handle 'e'
        // separately).  However, immediately adjacent 'e' after digits
        // with no whitespace is ambiguous.  We treat it as scientific
        // notation only if the next character is a digit or +/- followed
        // by a digit.
        char after_e = peek_next();
        bool sci = is_digit(after_e);
        if (!sci && (after_e == '+' || after_e == '-')) {
            // Check one more character ahead.
            if (pos_ + 2 < source_.size() && is_digit(source_[pos_ + 2])) {
                sci = true;
            }
        }
        if (sci) {
            is_float = true;
            advance(); // consume 'e' or 'E'
            if (!at_end() && (peek() == '+' || peek() == '-')) {
                advance(); // consume sign
            }
            while (!at_end() && is_digit(peek())) {
                advance();
            }
        }
    }

    std::string lexeme = source_.substr(start, pos_ - start);
    TokenType type = is_float ? TokenType::FLOAT_LITERAL
                              : TokenType::INTEGER_LITERAL;
    return Token{type, std::move(lexeme), start_line};
}


//  Identifier / keyword scanning


Token Lexer::scan_identifier_or_keyword() {
    uint32_t start_line = line_;
    std::size_t start = pos_;

    while (!at_end() && is_alnum(peek())) {
        advance();
    }

    std::string lexeme = source_.substr(start, pos_ - start);

    const auto& kw = keyword_map();
    auto it = kw.find(lexeme);
    if (it != kw.end()) {
        return Token{it->second, std::move(lexeme), start_line};
    }

    return Token{TokenType::IDENTIFIER, std::move(lexeme), start_line};
}


//  String literal and interpolation scanning


// Scans the body of a string starting at the current position.  The
// opening " has already been consumed if continuation is false; the
// closing } of a previous interpolation has already been consumed if
// continuation is true.
// Emits one token:
//   STRING_LITERAL      if no interpolation was encountered and we
//                       reached the closing "  (only when !continuation)
//   STRING_INTERP_BEGIN if interpolation was found and this is the first
//                       segment  (only when !continuation)
//   STRING_INTERP_MID   if interpolation was found and we are continuing
//                       after a previous interpolation
//   STRING_INTERP_END   if we reached the closing " after a previous
//                       interpolation  (only when continuation)
// When interpolation is found (${), an InterpContext is pushed so that
// the main tokenize loop knows that subsequent } tokens may close the
// interpolation rather than being normal RIGHT_BRACE tokens.

void Lexer::scan_string_content(std::vector<Token>& tokens,
                                bool continuation) {
    uint32_t start_line = line_;
    std::string text;

    while (!at_end()) {
        char c = peek();

        // End of string.
        if (c == '"') {
            advance(); // consume closing "
            if (continuation) {
                tokens.push_back(
                    Token{TokenType::STRING_INTERP_END,
                          std::move(text), start_line});
            } else {
                tokens.push_back(
                    Token{TokenType::STRING_LITERAL,
                          std::move(text), start_line});
            }
            return;
        }

        // Interpolation start.
        if (c == '$' && peek_next() == '{') {
            advance(); // consume $
            advance(); // consume {
            if (continuation) {
                tokens.push_back(
                    Token{TokenType::STRING_INTERP_MID,
                          std::move(text), start_line});
            } else {
                tokens.push_back(
                    Token{TokenType::STRING_INTERP_BEGIN,
                          std::move(text), start_line});
            }
            // Push an interpolation context so the main loop tracks
            // brace nesting inside the expression.
            interp_stack_.push_back(InterpContext{0});
            return;
        }

        // Track newlines inside the string for accurate line counting.
        if (c == '\n') {
            ++line_;
        }

        text += c;
        advance();
    }

    // Reached end of file inside a string literal: error.
    report_error(start_line);
}


//  Main tokenization loop


std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        skip_whitespace_and_comments();

        if (at_end()) break;

        char c = peek();

        // Newline handling.
        if (c == '\n') {
            ++line_;
            advance();

            // Suppress newlines inside () or [].
            if (paren_depth_ > 0 || bracket_depth_ > 0) {
                continue;
            }

            // Suppress newlines inside string interpolation expressions
            // when the interpolation is inside () or [].  However the
            // interp context itself does not suppress newlines; only
            // the grouping tokens do.  This is handled by the depth
            // checks above.

            // Collapse consecutive NEWLINE tokens.
            if (!tokens.empty()
                && tokens.back().type == TokenType::NEWLINE) {
                continue;
            }

            // Do not emit a leading NEWLINE at the very start.
            if (tokens.empty()) {
                continue;
            }

            tokens.push_back(
                Token{TokenType::NEWLINE, "\\n", line_ - 1});
            continue;
        }

        // String literal / interpolation.
        if (c == '"') {
            advance(); // consume opening "
            scan_string_content(tokens, false);
            continue;
        }

        // Numeric literals (must start with a digit).
        if (is_digit(c)) {
            tokens.push_back(scan_number());
            continue;
        }

        // Identifiers and keywords.
        if (is_alpha(c)) {
            tokens.push_back(scan_identifier_or_keyword());
            continue;
        }

        // Operators and delimiters.
        uint32_t tok_line = line_;
        advance(); // consume the character

        switch (c) {

            // Arithmetic
            case '+':
                tokens.push_back(
                    Token{TokenType::PLUS, "+", tok_line});
                break;

            case '-':
                tokens.push_back(
                    Token{TokenType::MINUS, "-", tok_line});
                break;

            case '*':
                tokens.push_back(
                    Token{TokenType::STAR, "*", tok_line});
                break;

            case '/':
                if (match('/')) {
                    tokens.push_back(
                        Token{TokenType::DOUBLE_SLASH, "//", tok_line});
                } else {
                    tokens.push_back(
                        Token{TokenType::SLASH, "/", tok_line});
                }
                break;

            case '%':
                tokens.push_back(
                    Token{TokenType::PERCENT, "%", tok_line});
                break;

            case '^':
                tokens.push_back(
                    Token{TokenType::CARET, "^", tok_line});
                break;

            // Comparison and assignment
            case '<':
                if (match('<')) {
                    tokens.push_back(
                        Token{TokenType::SHIFT_LEFT, "<<", tok_line});
                } else if (match('=')) {
                    tokens.push_back(
                        Token{TokenType::LESS_EQUAL, "<=", tok_line});
                } else {
                    tokens.push_back(
                        Token{TokenType::LESS, "<", tok_line});
                }
                break;

            case '>':
                if (match('>')) {
                    tokens.push_back(
                        Token{TokenType::SHIFT_RIGHT, ">>", tok_line});
                } else if (match('=')) {
                    tokens.push_back(
                        Token{TokenType::GREATER_EQUAL, ">=", tok_line});
                } else {
                    tokens.push_back(
                        Token{TokenType::GREATER, ">", tok_line});
                }
                break;

            case '=':
                if (match('=')) {
                    tokens.push_back(
                        Token{TokenType::EQUAL_EQUAL, "==", tok_line});
                } else {
                    tokens.push_back(
                        Token{TokenType::EQUAL, "=", tok_line});
                }
                break;

            // Boolean negation
            case '!':
                tokens.push_back(
                    Token{TokenType::BANG, "!", tok_line});
                break;

            // Delimiters
            case '(':
                ++paren_depth_;
                tokens.push_back(
                    Token{TokenType::LEFT_PAREN, "(", tok_line});
                break;

            case ')':
                if (paren_depth_ > 0) --paren_depth_;
                tokens.push_back(
                    Token{TokenType::RIGHT_PAREN, ")", tok_line});
                break;

            case '{':
                // If we are inside a string interpolation expression,
                // a { increases the brace nesting depth so we know that
                // the next } does not close the interpolation.
                if (!interp_stack_.empty()) {
                    ++interp_stack_.back().brace_depth;
                }
                tokens.push_back(
                    Token{TokenType::LEFT_BRACE, "{", tok_line});
                break;

            case '}':
                if (!interp_stack_.empty()) {
                    if (interp_stack_.back().brace_depth > 0) {
                        // This } closes a nested block inside the
                        // interpolation expression, not the
                        // interpolation itself.
                        --interp_stack_.back().brace_depth;
                        tokens.push_back(
                            Token{TokenType::RIGHT_BRACE, "}",
                                  tok_line});
                    } else {
                        // This } closes the interpolation expression.
                        // Pop the interp context and continue scanning
                        // the rest of the string.
                        interp_stack_.pop_back();
                        scan_string_content(tokens, true);
                    }
                } else {
                    tokens.push_back(
                        Token{TokenType::RIGHT_BRACE, "}", tok_line});
                }
                break;

            case '[':
                ++bracket_depth_;
                tokens.push_back(
                    Token{TokenType::LEFT_BRACKET, "[", tok_line});
                break;

            case ']':
                if (bracket_depth_ > 0) --bracket_depth_;
                tokens.push_back(
                    Token{TokenType::RIGHT_BRACKET, "]", tok_line});
                break;

            case ',':
                tokens.push_back(
                    Token{TokenType::COMMA, ",", tok_line});
                break;

            case ';':
                tokens.push_back(
                    Token{TokenType::SEMICOLON, ";", tok_line});
                break;

            case '.':
                tokens.push_back(
                    Token{TokenType::DOT, ".", tok_line});
                break;

            default:
                // Any unrecognised character is a lexical error.
                report_error(tok_line);
                break;
        }
    }

    // Remove a trailing NEWLINE immediately before EOF for cleanliness.
    if (!tokens.empty() && tokens.back().type == TokenType::NEWLINE) {
        tokens.pop_back();
    }

    tokens.push_back(Token{TokenType::EOF_TOKEN, "", line_});
    return tokens;
}

} // namespace janus
