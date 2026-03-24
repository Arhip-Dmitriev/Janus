#include "parser.hpp"

#include <charconv>
#include <cstdlib>
#include <utility>

namespace janus {



//  Construction


Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens)), pos_(0) {}



//  Token navigation


const Token& Parser::peek() const {
    return tokens_[pos_];
}

const Token& Parser::peek_next() const {
    if (pos_ + 1 < tokens_.size()) {
        return tokens_[pos_ + 1];
    }
    return tokens_.back();
}

Token Parser::advance() {
    Token tok = tokens_[pos_];
    if (pos_ < tokens_.size() - 1) {
        ++pos_;
    }
    return tok;
}

bool Parser::check(TokenType type) const {
    return peek().type == type;
}

bool Parser::check_next(TokenType type) const {
    return peek_next().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

Token Parser::consume(TokenType type) {
    if (check(type)) {
        return advance();
    }
    report_error(current_line());
    // Unreachable; report_error is [[noreturn]].
    std::abort();
}

bool Parser::at_end() const {
    return peek().type == TokenType::EOF_TOKEN;
}

uint32_t Parser::current_line() const {
    return peek().line;
}

void Parser::skip_newlines() {
    while (check(TokenType::NEWLINE)) {
        advance();
    }
}

bool Parser::peek_past_newlines_is(TokenType type) const {
    std::size_t look = pos_;
    while (look < tokens_.size() && tokens_[look].type == TokenType::NEWLINE) {
        ++look;
    }
    if (look < tokens_.size()) {
        return tokens_[look].type == type;
    }
    return false;
}

void Parser::expect_statement_end() {
    if (check(TokenType::NEWLINE) || check(TokenType::EOF_TOKEN) ||
        check(TokenType::RIGHT_BRACE)) {
        return;
    }
    report_error(current_line());
}



//  Program and statement parsing


Program Parser::parse() {
    Program prog;
    skip_newlines();
    while (!at_end()) {
        prog.statements.push_back(parse_statement());
        // Consume the statement-terminating newline(s).
        skip_newlines();
    }
    return prog;
}

std::vector<StmtPtr> Parser::parse_block() {
    consume(TokenType::LEFT_BRACE);
    skip_newlines();

    std::vector<StmtPtr> stmts;
    while (!check(TokenType::RIGHT_BRACE) && !at_end()) {
        stmts.push_back(parse_statement());
        skip_newlines();
    }

    consume(TokenType::RIGHT_BRACE);
    return stmts;
}

StmtPtr Parser::parse_statement() {
    set_error_line(current_line());

    // break
    if (check(TokenType::KW_BREAK)) {
        uint32_t ln = current_line();
        advance();
        expect_statement_end();
        return std::make_unique<BreakStmt>(ln);
    }

    // continue
    if (check(TokenType::KW_CONTINUE)) {
        uint32_t ln = current_line();
        advance();
        expect_statement_end();
        return std::make_unique<ContinueStmt>(ln);
    }

    // return [value]
    if (check(TokenType::KW_RETURN)) {
        uint32_t ln = current_line();
        advance();

        ExprPtr value = nullptr;
        // If the next token is a valid statement terminator, there is no
        // return value.
        if (!check(TokenType::NEWLINE) && !check(TokenType::EOF_TOKEN) &&
            !check(TokenType::RIGHT_BRACE)) {
            value = parse_expression();
        }

        expect_statement_end();
        return std::make_unique<ReturnStmt>(ln, std::move(value));
    }

    // Expression statement (covers assignments, function calls, control
    // flow expressions, and all other expressions used as statements).
    uint32_t ln = current_line();
    auto expr = parse_expression();
    expect_statement_end();
    return std::make_unique<ExprStmt>(ln, std::move(expr));
}



//  Expression entry point


ExprPtr Parser::parse_expression() {
    return parse_assignment();
}



//  Level 17: Assignment (right-associative)


ExprPtr Parser::parse_assignment() {
    auto expr = parse_postfix_bang();

    if (check(TokenType::EQUAL)) {
        uint32_t ln = current_line();
        advance();
        auto value = parse_assignment();
        return std::make_unique<AssignExpr>(ln, std::move(expr), std::move(value));
    }

    return expr;
}



//  Level 14: Postfix boolean negation (expr!)


ExprPtr Parser::parse_postfix_bang() {
    auto expr = parse_bitwise_logic();

    while (check(TokenType::BANG)) {
        uint32_t ln = current_line();
        advance();
        expr = std::make_unique<PostfixBangExpr>(ln, std::move(expr));
    }

    return expr;
}



//  Level 13: Bitwise logic (and nand or nor xor xnor)


ExprPtr Parser::parse_bitwise_logic() {
    auto expr = parse_equality();

    while (check(TokenType::KW_AND)  || check(TokenType::KW_NAND) ||
           check(TokenType::KW_OR)   || check(TokenType::KW_NOR)  ||
           check(TokenType::KW_XOR)  || check(TokenType::KW_XNOR)) {
        uint32_t ln = current_line();
        Token op = advance();
        auto right = parse_equality();
        expr = std::make_unique<BinaryExpr>(ln, std::move(expr), op.type,
                                            std::move(right));
    }

    return expr;
}



//  Level 12: Equality (==)


ExprPtr Parser::parse_equality() {
    auto expr = parse_relational();

    while (check(TokenType::EQUAL_EQUAL)) {
        uint32_t ln = current_line();
        Token op = advance();
        auto right = parse_relational();
        expr = std::make_unique<BinaryExpr>(ln, std::move(expr), op.type,
                                            std::move(right));
    }

    return expr;
}



//  Level 11: Relational (< > <= >=)


ExprPtr Parser::parse_relational() {
    auto expr = parse_addition();

    while (check(TokenType::LESS)       || check(TokenType::GREATER) ||
           check(TokenType::LESS_EQUAL) || check(TokenType::GREATER_EQUAL)) {
        uint32_t ln = current_line();
        Token op = advance();
        auto right = parse_addition();
        expr = std::make_unique<BinaryExpr>(ln, std::move(expr), op.type,
                                            std::move(right));
    }

    return expr;
}



//  Level 10: Addition & Subtraction (+ -)


ExprPtr Parser::parse_addition() {
    auto expr = parse_multiplication();

    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        uint32_t ln = current_line();
        Token op = advance();
        auto right = parse_multiplication();
        expr = std::make_unique<BinaryExpr>(ln, std::move(expr), op.type,
                                            std::move(right));
    }

    return expr;
}



//  Level 9: Multiplication, Division, Modulus (* / // %)


ExprPtr Parser::parse_multiplication() {
    auto expr = parse_unary_minus();

    while (check(TokenType::STAR)   || check(TokenType::SLASH) ||
           check(TokenType::DOUBLE_SLASH) || check(TokenType::PERCENT)) {
        uint32_t ln = current_line();
        Token op = advance();
        auto right = parse_unary_minus();
        expr = std::make_unique<BinaryExpr>(ln, std::move(expr), op.type,
                                            std::move(right));
    }

    return expr;
}



//  Unary minus (between levels 9 and 8)
//  Placed here so that -5 ^ 2 parses as -(5 ^ 2) = -25, matching the
//  documented ambiguity resolution: "unary minus binds after
//  exponentiation".


ExprPtr Parser::parse_unary_minus() {
    if (check(TokenType::MINUS)) {
        uint32_t ln = current_line();
        advance();
        auto operand = parse_unary_minus();
        return std::make_unique<UnaryExpr>(ln, TokenType::MINUS,
                                           std::move(operand));
    }

    return parse_exponentiation();
}



//  Level 8: Exponentiation (^ right-associative)


ExprPtr Parser::parse_exponentiation() {
    auto expr = parse_tensor();

    if (check(TokenType::CARET)) {
        uint32_t ln = current_line();
        advance();
        auto right = parse_exponentiation();
        return std::make_unique<BinaryExpr>(ln, std::move(expr),
                                            TokenType::CARET, std::move(right));
    }

    return expr;
}



//  Level 7: Tensor product


ExprPtr Parser::parse_tensor() {
    auto expr = parse_type_cast();

    while (check(TokenType::KW_TENSOR)) {
        uint32_t ln = current_line();
        Token op = advance();
        auto right = parse_type_cast();
        expr = std::make_unique<BinaryExpr>(ln, std::move(expr), op.type,
                                            std::move(right));
    }

    return expr;
}



//  Level 6: Type casting  ((type) expr)
//  A type cast is recognised when the current token is LEFT_PAREN, the
//  next token is a type keyword, and the token after that is RIGHT_PAREN.
//  This three-token lookahead distinguishes casts from parenthesised
//  grouping expressions.


ExprPtr Parser::parse_type_cast() {
    if (check(TokenType::LEFT_PAREN)) {
        // Lookahead to determine if this is a type cast.
        const Token& after_paren = tokens_[pos_ + 1];

        if (is_type_keyword(after_paren.type)) {
            // Check that the token after the type keyword is RIGHT_PAREN.
            if (pos_ + 2 < tokens_.size() &&
                tokens_[pos_ + 2].type == TokenType::RIGHT_PAREN) {
                uint32_t ln = current_line();
                advance();                        // consume (
                Token type_tok = advance();        // consume type keyword
                advance();                        // consume )
                auto operand = parse_type_cast();  // right-recursive
                return std::make_unique<TypeCastExpr>(ln, type_tok.type,
                                                      std::move(operand));
            }
        }
        // Not a cast; fall through to unary prefix.
    }

    return parse_unary_prefix();
}



//  Level 5: Unary prefix
//  Handles: not  !  <<  >>  and the builtin prefix operands
//  (det, transpose, transposec, evals, evecs, gates, qubits, depth).
//  Unary minus isnt handled here; it is at the parse_unary_minus level
//  so that -x^2 = -(x^2).  However, minus IS handled here as well so
//  that constructs like 5 ^ -3 work (the exponentiation parser calls
//  down to here for the right-hand operand).


ExprPtr Parser::parse_unary_prefix() {

    // Unary minus in nested context (e.g. right-hand side of ^).
    if (check(TokenType::MINUS)) {
        uint32_t ln = current_line();
        advance();
        auto operand = parse_unary_prefix();
        return std::make_unique<UnaryExpr>(ln, TokenType::MINUS,
                                           std::move(operand));
    }

    // Bitwise negation: not
    if (check(TokenType::KW_NOT)) {
        uint32_t ln = current_line();
        advance();
        auto operand = parse_unary_prefix();
        return std::make_unique<UnaryExpr>(ln, TokenType::KW_NOT,
                                           std::move(operand));
    }

    // Boolean negation prefix: !
    if (check(TokenType::BANG)) {
        uint32_t ln = current_line();
        advance();
        auto operand = parse_unary_prefix();
        return std::make_unique<UnaryExpr>(ln, TokenType::BANG,
                                           std::move(operand));
    }

    // Shift left / shift right (always unary prefix, never binary).
    if (check(TokenType::SHIFT_LEFT) || check(TokenType::SHIFT_RIGHT)) {
        uint32_t ln = current_line();
        Token op = advance();
        auto operand = parse_unary_prefix();
        return std::make_unique<UnaryExpr>(ln, op.type, std::move(operand));
    }

    // gates: either gate library access (gates.name(args)) or circuit
    // inspection builtin (gates(circ)) or unary prefix (gates expr).
    if (check(TokenType::KW_GATES)) {
        uint32_t ln = current_line();

        if (check_next(TokenType::DOT)) {
            // Gate library access: gates.name(args...)
            advance();  // consume KW_GATES
            advance();  // consume DOT
            Token name_tok = consume(TokenType::IDENTIFIER);
            consume(TokenType::LEFT_PAREN);
            auto args = parse_arg_list();
            return std::make_unique<GateLibraryExpr>(
                ln, std::move(name_tok.lexeme), std::move(args));
        }

        if (check_next(TokenType::LEFT_PAREN)) {
            // Builtin call: gates(circuit)
            advance();  // consume KW_GATES
            advance();  // consume LEFT_PAREN
            auto args = parse_arg_list();
            return std::make_unique<BuiltinCallExpr>(
                ln, TokenType::KW_GATES, std::move(args));
        }

        // Unary prefix: gates expr
        advance();
        auto operand = parse_unary_prefix();
        std::vector<ExprPtr> args;
        args.push_back(std::move(operand));
        return std::make_unique<BuiltinCallExpr>(
            ln, TokenType::KW_GATES, std::move(args));
    }

    // qubits and depth: builtin call with parens or unary prefix.
    if (check(TokenType::KW_QUBITS) || check(TokenType::KW_DEPTH)) {
        uint32_t ln = current_line();
        TokenType op_type = peek().type;

        if (check_next(TokenType::LEFT_PAREN)) {
            advance();  // consume keyword
            advance();  // consume LEFT_PAREN
            auto args = parse_arg_list();
            return std::make_unique<BuiltinCallExpr>(ln, op_type,
                                                      std::move(args));
        }

        advance();  // consume keyword
        auto operand = parse_unary_prefix();
        std::vector<ExprPtr> args;
        args.push_back(std::move(operand));
        return std::make_unique<BuiltinCallExpr>(ln, op_type, std::move(args));
    }

    // det, transpose, transposec, evals, evecs: unary prefix builtins.
    // Can optionally use parentheses.
    if (is_unary_prefix_builtin(peek().type)) {
        uint32_t ln = current_line();
        Token op = advance();

        if (check(TokenType::LEFT_PAREN)) {
            advance();  // consume LEFT_PAREN
            auto args = parse_arg_list();
            return std::make_unique<BuiltinCallExpr>(ln, op.type,
                                                      std::move(args));
        }

        auto operand = parse_unary_prefix();
        std::vector<ExprPtr> args;
        args.push_back(std::move(operand));
        return std::make_unique<BuiltinCallExpr>(ln, op.type, std::move(args));
    }

    return parse_postfix();
}



//  Level 4: Postfix access & indexing
//
//  collection[i]   matrix[r,c]   f(args)   obj.member


ExprPtr Parser::parse_postfix() {
    auto expr = parse_primary();

    for (;;) {
        if (check(TokenType::LEFT_BRACKET)) {
            // Indexing: expr[index] or expr[row, col]
            uint32_t ln = current_line();
            advance();  // consume [

            std::vector<ExprPtr> indices;
            indices.push_back(parse_expression());
            while (match(TokenType::COMMA)) {
                indices.push_back(parse_expression());
            }
            consume(TokenType::RIGHT_BRACKET);

            expr = std::make_unique<IndexExpr>(ln, std::move(expr),
                                               std::move(indices));

        } else if (check(TokenType::LEFT_PAREN)) {
            // Function call / map access: expr(args)
            uint32_t ln = current_line();
            advance();  // consume (
            auto args = parse_arg_list();
            expr = std::make_unique<CallExpr>(ln, std::move(expr),
                                              std::move(args));

        } else if (check(TokenType::DOT)) {
            // Member access: expr.member
            uint32_t ln = current_line();
            advance();  // consume .
            Token member = consume(TokenType::IDENTIFIER);
            expr = std::make_unique<MemberAccessExpr>(
                ln, std::move(expr), std::move(member.lexeme));

        } else {
            break;
        }
    }

    return expr;
}



//  Levels 1-3: Primary expressions


ExprPtr Parser::parse_primary() {
    set_error_line(current_line());

    // -- Level 2: Literals 

    // Integer literal.
    if (check(TokenType::INTEGER_LITERAL)) {
        uint32_t ln = current_line();
        Token tok = advance();
        int64_t val = 0;
        auto [ptr, ec] = std::from_chars(
            tok.lexeme.data(), tok.lexeme.data() + tok.lexeme.size(), val);
        if (ec != std::errc{}) {
            report_error(ln);
        }
        return std::make_unique<IntegerLiteralExpr>(ln, val);
    }

    // Floating-point literal.
    if (check(TokenType::FLOAT_LITERAL)) {
        uint32_t ln = current_line();
        Token tok = advance();
        double val = 0.0;
        auto [ptr, ec] = std::from_chars(
            tok.lexeme.data(), tok.lexeme.data() + tok.lexeme.size(), val);
        if (ec != std::errc{}) {
            // Fallback for platforms where from_chars does not support double.
            char* end = nullptr;
            val = std::strtod(tok.lexeme.c_str(), &end);
            if (end == tok.lexeme.c_str()) {
                report_error(ln);
            }
        }
        return std::make_unique<FloatLiteralExpr>(ln, val);
    }

    // Plain string literal.
    if (check(TokenType::STRING_LITERAL)) {
        uint32_t ln = current_line();
        Token tok = advance();
        return std::make_unique<StringLiteralExpr>(ln, std::move(tok.lexeme));
    }

    // Interpolated string.
    if (check(TokenType::STRING_INTERP_BEGIN)) {
        return parse_interpolated_string();
    }

    // Boolean literals.
    if (check(TokenType::KW_TRUE)) {
        uint32_t ln = current_line();
        advance();
        return std::make_unique<BoolLiteralExpr>(ln, true);
    }
    if (check(TokenType::KW_FALSE)) {
        uint32_t ln = current_line();
        advance();
        return std::make_unique<BoolLiteralExpr>(ln, false);
    }

    // Null literal.
    if (check(TokenType::KW_NULL)) {
        uint32_t ln = current_line();
        advance();
        return std::make_unique<NullLiteralExpr>(ln);
    }

    // Built-in constants.
    if (check(TokenType::KW_PI)) {
        uint32_t ln = current_line();
        advance();
        return std::make_unique<PiLiteralExpr>(ln);
    }
    if (check(TokenType::KW_E)) {
        uint32_t ln = current_line();
        advance();
        return std::make_unique<ELiteralExpr>(ln);
    }

    // -- Level 3: Function-call style built-in operands 

    if (is_function_call_builtin(peek().type)) {
        uint32_t ln = current_line();
        Token op = advance();
        consume(TokenType::LEFT_PAREN);
        auto args = parse_arg_list();
        return std::make_unique<BuiltinCallExpr>(ln, op.type, std::move(args));
    }

    // -- Type constructors 
    // type_keyword ( args )
    // Except KW_FUNCTION which is a function definition.

    if (check(TokenType::KW_FUNCTION)) {
        return parse_function_expr();
    }

    if (is_type_keyword(peek().type) && check_next(TokenType::LEFT_PAREN)) {
        uint32_t ln = current_line();
        Token type_tok = advance();   // consume type keyword
        advance();                    // consume (
        auto args = parse_arg_list();
        return std::make_unique<TypeConstructExpr>(ln, type_tok.type,
                                                    std::move(args));
    }

    // -- Control flow expressions 

    if (check(TokenType::KW_IF))      return parse_if_expr();
    if (check(TokenType::KW_FOR))     return parse_for_expr();
    if (check(TokenType::KW_WHILE))   return parse_while_expr();
    if (check(TokenType::KW_FOREACH)) return parse_foreach_expr();

    // -- Identifier 

    if (check(TokenType::IDENTIFIER)) {
        uint32_t ln = current_line();
        Token tok = advance();
        return std::make_unique<IdentifierExpr>(ln, std::move(tok.lexeme));
    }

    // -- Level 1: Parenthesised grouping 

    if (check(TokenType::LEFT_PAREN)) {
        advance();  // consume (
        auto expr = parse_expression();
        consume(TokenType::RIGHT_PAREN);
        return expr;
    }

    // -- Matrix / list literal  [ ... ] -

    if (check(TokenType::LEFT_BRACKET)) {
        return parse_matrix_literal();
    }

    // Nothing matched; this is a syntax error.
    report_error(current_line());
    // Unreachable.
    std::abort();
}



//  if(Condition) { body } [else if(Condition) { body }]* [else { body }]


ExprPtr Parser::parse_if_expr() {
    uint32_t ln = current_line();
    consume(TokenType::KW_IF);
    consume(TokenType::LEFT_PAREN);
    auto condition = parse_expression();
    consume(TokenType::RIGHT_PAREN);
    auto then_body = parse_block();

    std::vector<ElseIfClause> else_ifs;
    std::vector<StmtPtr>      else_body;

    // After the closing } of the then-body (or the latest else-if body),
    // look past any newlines for a following else keyword.
    while (peek_past_newlines_is(TokenType::KW_ELSE)) {
        skip_newlines();
        advance();  // consume KW_ELSE

        if (check(TokenType::KW_IF)) {
            // else if
            uint32_t eif_ln = current_line();
            advance();  // consume KW_IF
            consume(TokenType::LEFT_PAREN);
            auto eif_cond = parse_expression();
            consume(TokenType::RIGHT_PAREN);
            auto eif_body = parse_block();
            else_ifs.push_back(
                ElseIfClause{eif_ln, std::move(eif_cond), std::move(eif_body)});
        } else {
            // else
            else_body = parse_block();
            break;
        }
    }

    return std::make_unique<IfExpr>(ln, std::move(condition),
                                     std::move(then_body),
                                     std::move(else_ifs),
                                     std::move(else_body));
}



//  for(Init; Condition; Update) { body }


ExprPtr Parser::parse_for_expr() {
    uint32_t ln = current_line();
    consume(TokenType::KW_FOR);
    consume(TokenType::LEFT_PAREN);

    auto init = parse_expression();
    consume(TokenType::SEMICOLON);
    auto condition = parse_expression();
    consume(TokenType::SEMICOLON);
    auto update = parse_expression();

    consume(TokenType::RIGHT_PAREN);
    auto body = parse_block();

    return std::make_unique<ForExpr>(ln, std::move(init), std::move(condition),
                                      std::move(update), std::move(body));
}



//  while(Condition) { body }


ExprPtr Parser::parse_while_expr() {
    uint32_t ln = current_line();
    consume(TokenType::KW_WHILE);
    consume(TokenType::LEFT_PAREN);
    auto condition = parse_expression();
    consume(TokenType::RIGHT_PAREN);
    auto body = parse_block();

    return std::make_unique<WhileExpr>(ln, std::move(condition),
                                        std::move(body));
}



//  foreach(Element in Collection [where Cond] [from Lower] [to Upper])
//  { body }


ExprPtr Parser::parse_foreach_expr() {
    uint32_t ln = current_line();
    consume(TokenType::KW_FOREACH);
    consume(TokenType::LEFT_PAREN);

    Token elem_tok = consume(TokenType::IDENTIFIER);
    std::string element = std::move(elem_tok.lexeme);
    consume(TokenType::KW_IN);
    auto collection = parse_expression();

    ExprPtr where_cond = nullptr;
    ExprPtr from_bound = nullptr;
    ExprPtr to_bound   = nullptr;

    // The optional clauses may appear in any order, each at most once.
    // They are structural keywords inside the foreach header.
    for (;;) {
        if (match(TokenType::KW_WHERE)) {
            if (where_cond) report_error(current_line());
            where_cond = parse_expression();
        } else if (match(TokenType::KW_FROM)) {
            if (from_bound) report_error(current_line());
            from_bound = parse_expression();
        } else if (match(TokenType::KW_TO)) {
            if (to_bound) report_error(current_line());
            to_bound = parse_expression();
        } else {
            break;
        }
    }

    consume(TokenType::RIGHT_PAREN);
    auto body = parse_block();

    return std::make_unique<ForeachExpr>(
        ln, std::move(element), std::move(collection),
        std::move(where_cond), std::move(from_bound), std::move(to_bound),
        std::move(body));
}



//  function(param1, param2, ...) { body }


ExprPtr Parser::parse_function_expr() {
    uint32_t ln = current_line();
    consume(TokenType::KW_FUNCTION);
    consume(TokenType::LEFT_PAREN);

    std::vector<std::string> params;
    if (!check(TokenType::RIGHT_PAREN)) {
        Token p = consume(TokenType::IDENTIFIER);
        params.push_back(std::move(p.lexeme));
        while (match(TokenType::COMMA)) {
            Token p2 = consume(TokenType::IDENTIFIER);
            params.push_back(std::move(p2.lexeme));
        }
    }

    consume(TokenType::RIGHT_PAREN);
    auto body = parse_block();

    return std::make_unique<FunctionExpr>(ln, std::move(params),
                                           std::move(body));
}



//  Matrix / list literal: [ expr, expr ; expr, expr ]
//  Commas separate elements within a row.  Semicolons separate rows.
//  A single-row literal [a, b, c] produces one inner vector.
//  An empty literal [] produces zero rows.


ExprPtr Parser::parse_matrix_literal() {
    uint32_t ln = current_line();
    consume(TokenType::LEFT_BRACKET);

    std::vector<std::vector<ExprPtr>> rows;

    if (!check(TokenType::RIGHT_BRACKET)) {
        // First row.
        std::vector<ExprPtr> row;
        row.push_back(parse_expression());
        while (match(TokenType::COMMA)) {
            row.push_back(parse_expression());
        }
        rows.push_back(std::move(row));

        // Additional rows separated by semicolons.
        while (match(TokenType::SEMICOLON)) {
            std::vector<ExprPtr> next_row;
            next_row.push_back(parse_expression());
            while (match(TokenType::COMMA)) {
                next_row.push_back(parse_expression());
            }
            rows.push_back(std::move(next_row));
        }
    }

    consume(TokenType::RIGHT_BRACKET);
    return std::make_unique<MatrixLiteralExpr>(ln, std::move(rows));
}



//  Interpolated string: "text ${expr} more ${expr} end"
//  The lexer produces: STRING_INTERP_BEGIN, <expr tokens>,
//  STRING_INTERP_MID (zero or more), <expr tokens>, STRING_INTERP_END.


ExprPtr Parser::parse_interpolated_string() {
    uint32_t ln = current_line();
    Token begin = consume(TokenType::STRING_INTERP_BEGIN);

    std::vector<std::string> segments;
    std::vector<ExprPtr>     expressions;

    // First segment (text before the first ${).
    segments.push_back(std::move(begin.lexeme));

    // First interpolated expression.
    expressions.push_back(parse_expression());

    // Middle segments: each STRING_INTERP_MID carries the text between
    // the closing } of one interpolation and the opening ${ of the next.
    while (check(TokenType::STRING_INTERP_MID)) {
        Token mid = advance();
        segments.push_back(std::move(mid.lexeme));
        expressions.push_back(parse_expression());
    }

    // Closing segment.
    Token end_tok = consume(TokenType::STRING_INTERP_END);
    segments.push_back(std::move(end_tok.lexeme));

    return std::make_unique<InterpolatedStringExpr>(
        ln, std::move(segments), std::move(expressions));
}



//  Argument list helper
//  Parses a comma-separated list of expressions ending at RIGHT_PAREN.
//  The opening LEFT_PAREN must already have been consumed by the caller.
//  Consumes the closing RIGHT_PAREN.


std::vector<ExprPtr> Parser::parse_arg_list() {
    std::vector<ExprPtr> args;

    if (!check(TokenType::RIGHT_PAREN)) {
        args.push_back(parse_expression());
        while (match(TokenType::COMMA)) {
            args.push_back(parse_expression());
        }
    }

    consume(TokenType::RIGHT_PAREN);
    return args;
}



//  Predicate helpers


bool Parser::is_type_keyword(TokenType type) {
    switch (type) {
        case TokenType::KW_QUBIT:
        case TokenType::KW_CBIT:
        case TokenType::KW_QNUM:
        case TokenType::KW_CNUM:
        case TokenType::KW_CSTR:
        case TokenType::KW_LIST:
        case TokenType::KW_MATRIX:
        case TokenType::KW_GATE:
        case TokenType::KW_CIRC:
        case TokenType::KW_BLOCK:
        case TokenType::KW_FUNCTION:
            return true;
        default:
            return false;
    }
}

bool Parser::is_function_call_builtin(TokenType type) {
    switch (type) {
        case TokenType::KW_MEASURE:
        case TokenType::KW_PEEK:
        case TokenType::KW_STATE:
        case TokenType::KW_EXPECT:
        case TokenType::KW_CTRLE:
        case TokenType::KW_RUN:
        case TokenType::KW_RUNH:
        case TokenType::KW_ISUNITARY:
        case TokenType::KW_SAMEOUTPUT:
        case TokenType::KW_PRINT:
        case TokenType::KW_DELETE:
        case TokenType::KW_SIN:
        case TokenType::KW_COS:
        case TokenType::KW_NUMBEROFGATES:
            return true;
        default:
            return false;
    }
}

bool Parser::is_unary_prefix_builtin(TokenType type) {
    switch (type) {
        case TokenType::KW_DET:
        case TokenType::KW_TRANSPOSE:
        case TokenType::KW_TRANSPOSEC:
        case TokenType::KW_EVALS:
        case TokenType::KW_EVECS:
            return true;
        default:
            return false;
    }
}

} // namespace janus
