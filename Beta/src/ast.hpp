#ifndef JANUS_AST_HPP
#define JANUS_AST_HPP

#include "token.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace janus {


// Forward declarations


struct Expr;
struct Stmt;

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;


// Base classes


struct Expr {
    uint32_t line = 0;
    virtual ~Expr() = default;

protected:
    explicit Expr(uint32_t ln) : line(ln) {}
};

struct Stmt {
    uint32_t line = 0;
    virtual ~Stmt() = default;

protected:
    explicit Stmt(uint32_t ln) : line(ln) {}
};


// Expression nodes :: Literals


// Integer literal
struct IntegerLiteralExpr final : Expr {
    int64_t value;

    IntegerLiteralExpr(uint32_t ln, int64_t v)
        : Expr(ln), value(v) {}
};

// Floating-point literal
struct FloatLiteralExpr final : Expr {
    double value;

    FloatLiteralExpr(uint32_t ln, double v)
        : Expr(ln), value(v) {}
};

// Plain string literal with no interpolation
struct StringLiteralExpr final : Expr {
    std::string value;

    StringLiteralExpr(uint32_t ln, std::string v)
        : Expr(ln), value(std::move(v)) {}
};

// Boolean literal
struct BoolLiteralExpr final : Expr {
    bool value;

    BoolLiteralExpr(uint32_t ln, bool v)
        : Expr(ln), value(v) {}
};

// Null literal
struct NullLiteralExpr final : Expr {
    explicit NullLiteralExpr(uint32_t ln) : Expr(ln) {}
};

// Built-in constant pi 
struct PiLiteralExpr final : Expr {
    explicit PiLiteralExpr(uint32_t ln) : Expr(ln) {}
};

// Built-in constant e 
struct ELiteralExpr final : Expr {
    explicit ELiteralExpr(uint32_t ln) : Expr(ln) {}
};


// Expression nodes :: Interpolated string


// "text ${expr} more text ${expr} end"
// segments has exactly expressions.size() + 1 elements.
// segments[0] is text before first interpolation, segments[i] is text after
// expressions[i-1] and before expressions[i], and the last segment is the
// trailing text after the final interpolation.
struct InterpolatedStringExpr final : Expr {
    std::vector<std::string> segments;
    std::vector<ExprPtr>     expressions;

    InterpolatedStringExpr(uint32_t ln,
                           std::vector<std::string> segs,
                           std::vector<ExprPtr> exprs)
        : Expr(ln),
          segments(std::move(segs)),
          expressions(std::move(exprs)) {}
};


// Expression nodes :: Identifiers and access


// Variable reference
struct IdentifierExpr final : Expr {
    std::string name;

    IdentifierExpr(uint32_t ln, std::string n)
        : Expr(ln), name(std::move(n)) {}
};

// Indexing: collection[index] or matrix[row, col]
// indices contains 1 element for collection access and 2 for matrix access.
struct IndexExpr final : Expr {
    ExprPtr              object;
    std::vector<ExprPtr> indices;

    IndexExpr(uint32_t ln, ExprPtr obj, std::vector<ExprPtr> idx)
        : Expr(ln), object(std::move(obj)), indices(std::move(idx)) {}
};

// Member access
// Used exclusively for the gates namespace in Beta.
struct MemberAccessExpr final : Expr {
    ExprPtr     object;
    std::string member;

    MemberAccessExpr(uint32_t ln, ExprPtr obj, std::string mem)
        : Expr(ln), object(std::move(obj)), member(std::move(mem)) {}
};


// Expression nodes :: Operators


// Assignment: target = value (right-associative, precedence 17)
// target is an IdentifierExpr or an IndexExpr.
struct AssignExpr final : Expr {
    ExprPtr target;
    ExprPtr value;

    AssignExpr(uint32_t ln, ExprPtr tgt, ExprPtr val)
        : Expr(ln), target(std::move(tgt)), value(std::move(val)) {}
};

// Binary operator expression.
// Covers: +  -  *  /  //  %  ^  ==  <  >  <=  >=
//         and  nand  or  nor  xor  xnor  tensor
struct BinaryExpr final : Expr {
    ExprPtr   left;
    TokenType op;
    ExprPtr   right;

    BinaryExpr(uint32_t ln, ExprPtr lhs, TokenType o, ExprPtr rhs)
        : Expr(ln),
          left(std::move(lhs)),
          op(o),
          right(std::move(rhs)) {}
};

// Unary prefix operator expression.
// Covers: -  not  !  <<  >>
struct UnaryExpr final : Expr {
    TokenType op;
    ExprPtr   operand;

    UnaryExpr(uint32_t ln, TokenType o, ExprPtr opnd)
        : Expr(ln), op(o), operand(std::move(opnd)) {}
};

// Postfix boolean negation: Expression! (precedence 14)
struct PostfixBangExpr final : Expr {
    ExprPtr operand;

    PostfixBangExpr(uint32_t ln, ExprPtr opnd)
        : Expr(ln), operand(std::move(opnd)) {}
};


// Expression nodes – Type cast


// Type cast: (type) Expression (precedence 6)
// target_type is the TokenType of the type keyword (e.g. KW_CNUM, KW_QUBIT).
struct TypeCastExpr final : Expr {
    TokenType target_type;
    ExprPtr   operand;

    TypeCastExpr(uint32_t ln, TokenType tt, ExprPtr opnd)
        : Expr(ln), target_type(tt), operand(std::move(opnd)) {}
};


// Expression nodes – Matrix literal


// Matrix literal: [a, b; c, d]
// rows is a 2D grid.  A single-row literal like [a, b, c] has exactly one
// inner vector.  Also used as the bracket expression inside type constructor
// arguments such as list([1,2,3]).
struct MatrixLiteralExpr final : Expr {
    std::vector<std::vector<ExprPtr>> rows;

    MatrixLiteralExpr(uint32_t ln, std::vector<std::vector<ExprPtr>> r)
        : Expr(ln), rows(std::move(r)) {}
};


// Expression nodes – Function call


// User-defined function call: f(args...)
// Also used for map-key access when the callee is an identifier and the
// parser cannot yet distinguish it from a function call; the type checker
// resolves it later.
struct CallExpr final : Expr {
    ExprPtr              callee;
    std::vector<ExprPtr> args;

    CallExpr(uint32_t ln, ExprPtr cal, std::vector<ExprPtr> a)
        : Expr(ln), callee(std::move(cal)), args(std::move(a)) {}
};


// Expression nodes – Gate library access


// gates.name() or gates.name(params...)
// Represents a call into the predefined gate namespace.
struct GateLibraryExpr final : Expr {
    std::string          gate_name;
    std::vector<ExprPtr> args;

    GateLibraryExpr(uint32_t ln, std::string gn, std::vector<ExprPtr> a)
        : Expr(ln), gate_name(std::move(gn)), args(std::move(a)) {}
};


// Expression nodes – Type constructors


// Type constructor call: qubit(), cbit(), qnum(5), cnum("i",a,b),
//   cstr("hello"), list([1,2,3]), matrix([1,2;3,4]), gate(matrix),
//   circ([qubits], [gates]), block([gates]).
// type_keyword is the TokenType for the type (e.g. KW_QUBIT, KW_LIST).
struct TypeConstructExpr final : Expr {
    TokenType            type_keyword;
    std::vector<ExprPtr> args;

    TypeConstructExpr(uint32_t ln, TokenType tk, std::vector<ExprPtr> a)
        : Expr(ln), type_keyword(tk), args(std::move(a)) {}
};


// Expression nodes – Function definition


// function(param1, param2, ...) { body }
// Parameters are named by their identifier strings only; Janus has no
// explicit parameter type annotation.
struct FunctionExpr final : Expr {
    std::vector<std::string> params;
    std::vector<StmtPtr>     body;

    FunctionExpr(uint32_t ln, std::vector<std::string> p,
                 std::vector<StmtPtr> b)
        : Expr(ln), params(std::move(p)), body(std::move(b)) {}
};


// Expression nodes – Built-in operand calls


// Covers every built-in operand that uses function-call syntax or unary
// prefix syntax in the Beta operand list.
//
// Function-call style (precedence 3):
//   measure, peek, state, expect, ctrle, run, runh,
//   isunitary, sameoutput, print, delete, sin, cos, numberofgates
//
// Unary prefix / call style (precedence 5):
//   det, transpose, transposec, evals, evecs,
//   gates (circuit inspection), qubits, depth
//
// op stores the TokenType of the keyword (e.g. KW_MEASURE, KW_DET).
// args holds all argument expressions.  For unary prefix operators
// parsed without parentheses (e.g. transpose Matrix) args contains a
// single element.
struct BuiltinCallExpr final : Expr {
    TokenType            op;
    std::vector<ExprPtr> args;

    BuiltinCallExpr(uint32_t ln, TokenType o, std::vector<ExprPtr> a)
        : Expr(ln), op(o), args(std::move(a)) {}
};


// Expression nodes – Control flow (these are expressions in Janus because
// they can appear on the RHS of an assignment)


// Helper: one "else if" branch inside an if-chain.
struct ElseIfClause {
    uint32_t             line;
    ExprPtr              condition;
    std::vector<StmtPtr> body;
};

// if(Expression) { ... } else if (Expression) { ... } else { ... }
// Returns a boolean indicating which branch executed.
struct IfExpr final : Expr {
    ExprPtr                    condition;
    std::vector<StmtPtr>       then_body;
    std::vector<ElseIfClause>  else_ifs;
    std::vector<StmtPtr>       else_body;   // empty if no else clause

    IfExpr(uint32_t ln, ExprPtr cond, std::vector<StmtPtr> then_b,
           std::vector<ElseIfClause> eifs, std::vector<StmtPtr> else_b)
        : Expr(ln),
          condition(std::move(cond)),
          then_body(std::move(then_b)),
          else_ifs(std::move(eifs)),
          else_body(std::move(else_b)) {}
};

// for(Init; Condition; Update) { ... }
// init is an assignment expression (e.g. i = 0).
// update is an assignment expression (e.g. i = i + 1).
// Returns iterations completed + 1, or 0 if none.
struct ForExpr final : Expr {
    ExprPtr              init;
    ExprPtr              condition;
    ExprPtr              update;
    std::vector<StmtPtr> body;

    ForExpr(uint32_t ln, ExprPtr ini, ExprPtr cond, ExprPtr upd,
            std::vector<StmtPtr> b)
        : Expr(ln),
          init(std::move(ini)),
          condition(std::move(cond)),
          update(std::move(upd)),
          body(std::move(b)) {}
};

// while(Condition) { ... }
// Returns iterations completed + 1, or 0 if none.
struct WhileExpr final : Expr {
    ExprPtr              condition;
    std::vector<StmtPtr> body;

    WhileExpr(uint32_t ln, ExprPtr cond, std::vector<StmtPtr> b)
        : Expr(ln), condition(std::move(cond)), body(std::move(b)) {}
};

// foreach(Element in Collection [where Condition] [from Lower] [to Upper])
// { ... }
// Returns iterations completed + 1, or 0 if none.
// element is the name of the iteration variable.
// where_cond, from_bound, and to_bound are all optional.
struct ForeachExpr final : Expr {
    std::string          element;
    ExprPtr              collection;
    ExprPtr              where_cond;    // nullptr if absent
    ExprPtr              from_bound;    // nullptr if absent
    ExprPtr              to_bound;      // nullptr if absent
    std::vector<StmtPtr> body;

    ForeachExpr(uint32_t ln, std::string elem, ExprPtr coll,
                ExprPtr whr, ExprPtr frm, ExprPtr to_b,
                std::vector<StmtPtr> b)
        : Expr(ln),
          element(std::move(elem)),
          collection(std::move(coll)),
          where_cond(std::move(whr)),
          from_bound(std::move(frm)),
          to_bound(std::move(to_b)),
          body(std::move(b)) {}
};


// Statement nodes


// Expression used as a statement (the most common statement form in Janus
// since assignments, function calls, print, control flow, etc. are all
// expressions).
struct ExprStmt final : Stmt {
    ExprPtr expr;

    ExprStmt(uint32_t ln, ExprPtr e)
        : Stmt(ln), expr(std::move(e)) {}
};

// break (valid only inside for, while, foreach)
struct BreakStmt final : Stmt {
    explicit BreakStmt(uint32_t ln) : Stmt(ln) {}
};

// continue (valid only inside for, while, foreach)
struct ContinueStmt final : Stmt {
    explicit ContinueStmt(uint32_t ln) : Stmt(ln) {}
};

// return [value]
// A top-level return with an integer sets the process exit code (mod 256).
// A top-level return with a non-integer is an error.
// value is nullptr when return has no expression.
struct ReturnStmt final : Stmt {
    ExprPtr value;

    ReturnStmt(uint32_t ln, ExprPtr v)
        : Stmt(ln), value(std::move(v)) {}
};


// Top-level program


// The root of the AST.  A Janus source file is a flat sequence of
// statements executed from top to bottom.
struct Program {
    std::vector<StmtPtr> statements;
};

} // namespace janus

#endif // JANUS_AST_HPP
