#ifndef JANUS_PARSER_HPP
#define JANUS_PARSER_HPP

#include "token.hpp"
#include "ast.hpp"
#include "error.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace janus {

// Parser :: hand-written recursive descent parser for the Janus Beta.
// Consumes a flat token stream produced by the Lexer and produces an AST
// rooted at a Program node.  Implements the full operator precedence table
// defined in the documentation:
//   1.  Parentheses & Grouping          ( )  { }  [ ]
//   2.  Literals                         integers  floats  strings  true  false  null  pi  e
//   3.  Function calls & operands        measure()  sin()  print()  ...
//   4.  Postfix access & indexing        collection[i]  matrix[r,c]  f(args)  obj.member
//   5.  Unary prefix                     not  !  -  <<  >>  det  transpose  transposec  evals  evecs  gates  qubits  depth
//   6.  Type casting                     (type) expr
//   7.  Tensor product                   tensor
//   8.  Exponentiation                   ^  (right-associative)
//   9.  Multiplication / Division / Mod  *  /  //  %
//  10.  Addition & Subtraction           +  -
//  11.  Relational                       <  >  <=  >=
//  12.  Equality                         ==
//  13.  Bitwise logic                    and  nand  or  nor  xor  xnor
//  14.  Boolean negation (postfix)       expr!
//  17.  Assignment                       =  (right-associative)
// Levels 15 (diff/diffs) and 16 (reference swap <->) are not in Beta.
// Unary minus is handled at a precedence level between exponentiation
// and multiplication so that -5 ^ 2 parses as -(5 ^ 2) = -25,
// matching the documented ambiguity resolution.

class Parser {
public:
    // Constructs a parser from a token stream.  The stream must end with
    // an EOF_TOKEN.
    explicit Parser(std::vector<Token> tokens);

    // Parses the entire token stream and returns the AST.
    // On any syntax error the single Beta error is reported via
    // report_error and the process terminates.
    Program parse();

private:

    // Token stream and cursor.
    std::vector<Token> tokens_;
    std::size_t        pos_;


    // Token navigation.

    const Token& peek() const;
    const Token& peek_next() const;
    Token        advance();
    bool         check(TokenType type) const;
    bool         check_next(TokenType type) const;
    bool         match(TokenType type);
    Token        consume(TokenType type);
    bool         at_end() const;
    uint32_t     current_line() const;

    // Skips zero or more consecutive NEWLINE tokens.
    void skip_newlines();

    // Returns true if, after skipping any NEWLINE tokens at the current
    // position, the next non-newline token has the given type.  Does NOT
    // consume anything.
    bool peek_past_newlines_is(TokenType type) const;

    // Expects the current position to be at a valid statement boundary:
    // NEWLINE, EOF_TOKEN, or RIGHT_BRACE.  If not, reports an error.
    // Does NOT consume the token.
    void expect_statement_end();


    // Program and statement parsing.

    std::vector<StmtPtr> parse_block();
    StmtPtr              parse_statement();


    // Expression parsing by precedence level.

    ExprPtr parse_expression();

    // Level 17: assignment (right-associative).
    ExprPtr parse_assignment();

    // Level 14: postfix boolean negation (expr!).
    ExprPtr parse_postfix_bang();

    // Level 13: bitwise logic (and nand or nor xor xnor).
    ExprPtr parse_bitwise_logic();

    // Level 12: equality (==).
    ExprPtr parse_equality();

    // Level 11: relational (< > <= >=).
    ExprPtr parse_relational();

    // Level 10: addition and subtraction (+ -).
    ExprPtr parse_addition();

    // Level 9: multiplication, division, modulus (* / // %).
    ExprPtr parse_multiplication();

    // Unary minus: sits between levels 9 and 8 so that -x^2 = -(x^2).
    ExprPtr parse_unary_minus();

    // Level 8: exponentiation (^, right-associative).
    ExprPtr parse_exponentiation();

    // Level 7: tensor product.
    ExprPtr parse_tensor();

    // Level 6: type casting ((type) expr).
    ExprPtr parse_type_cast();

    // Level 5: unary prefix (not ! << >> and builtin prefix operands).
    ExprPtr parse_unary_prefix();

    // Level 4: postfix access and indexing ([] () .).
    ExprPtr parse_postfix();

    // Levels 1-3: primary expressions (literals, identifiers, grouping,
    //             function-call builtins, type constructors, control flow).
    ExprPtr parse_primary();


    // Special expression parsers.

    ExprPtr parse_if_expr();
    ExprPtr parse_for_expr();
    ExprPtr parse_while_expr();
    ExprPtr parse_foreach_expr();
    ExprPtr parse_function_expr();
    ExprPtr parse_matrix_literal();
    ExprPtr parse_interpolated_string();

    // Parses a parenthesised argument list (already past the opening paren).
    // Stops at RIGHT_PAREN and consumes it.
    std::vector<ExprPtr> parse_arg_list();


    // Predicate helpers.

    static bool is_type_keyword(TokenType type);
    static bool is_function_call_builtin(TokenType type);
    static bool is_unary_prefix_builtin(TokenType type);
};

} // namespace janus

#endif // JANUS_PARSER_HPP
