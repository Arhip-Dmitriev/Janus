#ifndef JANUS_IR_HPP
#define JANUS_IR_HPP

#include "types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace janus {


// Forward declarations


struct IRExpr;
struct IRStmt;

using IRExprPtr = std::unique_ptr<IRExpr>;
using IRStmtPtr = std::unique_ptr<IRStmt>;



// IR operation enumerations

// These are IR-specific and independent of TokenType so that both backends
// can consume the IR without depending on the lexer.


// Binary operator codes.
// Covers every binary operation available in the Beta operand list.
enum class IRBinaryOp : uint8_t {
    ADD,            // +   (1)  addition / concatenation
    SUB,            // -   (2)  subtraction
    MUL,            // *   (3)  multiplication
    DIV,            // /   (4)  division
    INT_DIV,        // //  (20) integer division
    MOD,            // %   (6)  modulus
    EXP,            // ^   (7)  exponentiation (right-associative)
    EQ,             // ==  (11) equality
    LT,             // <   (16) less-than
    GT,             // >   (17) greater-than
    LE,             // <=       less-than-or-equal
    GE,             // >=       greater-than-or-equal
    AND,            // and      bitwise and
    NAND,           // nand     bitwise nand
    OR,             // or       bitwise or
    NOR,            // nor      bitwise nor
    XOR,            // xor      bitwise xor
    XNOR,           // xnor     bitwise xnor
    TENSOR          // tensor   (30) tensor product
};

// Returns a human-readable name for a binary operator.
constexpr std::string_view ir_binary_op_name(IRBinaryOp op) noexcept {
    switch (op) {
        case IRBinaryOp::ADD:     return "add";
        case IRBinaryOp::SUB:     return "sub";
        case IRBinaryOp::MUL:     return "mul";
        case IRBinaryOp::DIV:     return "div";
        case IRBinaryOp::INT_DIV: return "int_div";
        case IRBinaryOp::MOD:     return "mod";
        case IRBinaryOp::EXP:     return "exp";
        case IRBinaryOp::EQ:      return "eq";
        case IRBinaryOp::LT:      return "lt";
        case IRBinaryOp::GT:      return "gt";
        case IRBinaryOp::LE:      return "le";
        case IRBinaryOp::GE:      return "ge";
        case IRBinaryOp::AND:     return "and";
        case IRBinaryOp::NAND:    return "nand";
        case IRBinaryOp::OR:      return "or";
        case IRBinaryOp::NOR:     return "nor";
        case IRBinaryOp::XOR:     return "xor";
        case IRBinaryOp::XNOR:   return "xnor";
        case IRBinaryOp::TENSOR:  return "tensor";
    }
    return "unknown";
}


// Unary operator codes.
enum class IRUnaryOp : uint8_t {
    NEG,            // -   unary negation
    BITWISE_NOT,    // not bitwise negation (precedence 5)
    BOOL_NOT,       // !   boolean negation prefix (precedence 5)
    SHIFT_LEFT,     // <<  unary prefix shift left
    SHIFT_RIGHT     // >>  unary prefix shift right
};

// Returns a human-readable name for a unary operator.
constexpr std::string_view ir_unary_op_name(IRUnaryOp op) noexcept {
    switch (op) {
        case IRUnaryOp::NEG:         return "neg";
        case IRUnaryOp::BITWISE_NOT: return "not";
        case IRUnaryOp::BOOL_NOT:    return "bool_not";
        case IRUnaryOp::SHIFT_LEFT:  return "shift_left";
        case IRUnaryOp::SHIFT_RIGHT: return "shift_right";
    }
    return "unknown";
}


// Built-in operand codes.
enum class IRBuiltinOp : uint8_t {
    MEASURE,        // measure(Register) or measure(Register, Basis)  (35)
    PEEK,           // peek(Register)                                 (37)
    STATE,          // state(Register)                                (55)
    EXPECT,         // expect(Matrix, Register)                       (38)
    CTRLE,          // ctrle(Gate, ControlQubits)                     (57)
    RUN,            // run(Circuit, ...)                              (64)
    RUNH,           // runh(Circuit, Shots)                           (65)
    ISUNITARY,      // isunitary(Matrix)                              (97)
    SAMEOUTPUT,     // sameoutput(Circ1, Circ2)                      (96)
    PRINT,          // print(...)                                     (54)
    DELETE,         // delete(Collection, Index)                      (43)
    SIN,            // sin(x)                                         (76)
    COS,            // cos(x)                                         (77)
    NUMBEROFGATES,  // numberofgates(Circuit)                        (103)
    DET,            // det(Matrix)                                    (47)
    TRANSPOSE,      // transpose(Matrix)                              (60)
    TRANSPOSEC,     // transposec(Matrix)                             (61)
    EVALS,          // evals(Matrix)                                  (58)
    EVECS,          // evecs(Matrix)                                  (59)
    GATES,          // gates(Circuit) list of gates                   (68)
    QUBITS,         // qubits(Circuit) qubit count/list              (69)
    DEPTH,          // depth(Circuit) circuit depth                   (70)
    BITLENGTH       // bitlength(Register) qubit count of qnum/qubit
};

// Returns a human-readable name for a built-in operand.
constexpr std::string_view ir_builtin_op_name(IRBuiltinOp op) noexcept {
    switch (op) {
        case IRBuiltinOp::MEASURE:       return "measure";
        case IRBuiltinOp::PEEK:          return "peek";
        case IRBuiltinOp::STATE:         return "state";
        case IRBuiltinOp::EXPECT:        return "expect";
        case IRBuiltinOp::CTRLE:         return "ctrle";
        case IRBuiltinOp::RUN:           return "run";
        case IRBuiltinOp::RUNH:          return "runh";
        case IRBuiltinOp::ISUNITARY:     return "isunitary";
        case IRBuiltinOp::SAMEOUTPUT:    return "sameoutput";
        case IRBuiltinOp::PRINT:         return "print";
        case IRBuiltinOp::DELETE:        return "delete";
        case IRBuiltinOp::SIN:           return "sin";
        case IRBuiltinOp::COS:           return "cos";
        case IRBuiltinOp::NUMBEROFGATES: return "numberofgates";
        case IRBuiltinOp::DET:           return "det";
        case IRBuiltinOp::TRANSPOSE:     return "transpose";
        case IRBuiltinOp::TRANSPOSEC:    return "transposec";
        case IRBuiltinOp::EVALS:         return "evals";
        case IRBuiltinOp::EVECS:         return "evecs";
        case IRBuiltinOp::GATES:         return "gates";
        case IRBuiltinOp::QUBITS:        return "qubits";
        case IRBuiltinOp::DEPTH:         return "depth";
        case IRBuiltinOp::BITLENGTH:     return "bitlength";
    }
    return "unknown";
}



// IR base classes



// Base class for all IR expression nodes.
// Every expression carries a resolved TypeInfo describing the result type
// and the source line number for error reporting.

struct IRExpr {
    TypeInfo result_type;
    uint32_t line = 0;

    virtual ~IRExpr() = default;

protected:
    IRExpr(TypeInfo ti, uint32_t ln) : result_type(ti), line(ln) {}
};


// Base class for all IR statement nodes.

struct IRStmt {
    uint32_t line = 0;

    virtual ~IRStmt() = default;

protected:
    explicit IRStmt(uint32_t ln) : line(ln) {}
};



// IR expression nodes :: Literals



// Integer literal.  The result type is CNUM (64-bit numeric).
struct IRIntegerLiteral final : IRExpr {
    int64_t value;

    IRIntegerLiteral(uint32_t ln, int64_t v, TypeInfo ti)
        : IRExpr(ti, ln), value(v) {}
};

// Floating-point literal.  The result type is CNUM.
struct IRFloatLiteral final : IRExpr {
    double value;

    IRFloatLiteral(uint32_t ln, double v, TypeInfo ti)
        : IRExpr(ti, ln), value(v) {}
};

// Plain string literal (no interpolation).  The result type is CSTR.
struct IRStringLiteral final : IRExpr {
    std::string value;

    IRStringLiteral(uint32_t ln, std::string v, TypeInfo ti)
        : IRExpr(ti, ln), value(std::move(v)) {}
};

// Boolean literal.  The result type is CBIT (true = 1, false = 0).
struct IRBoolLiteral final : IRExpr {
    bool value;

    IRBoolLiteral(uint32_t ln, bool v, TypeInfo ti)
        : IRExpr(ti, ln), value(v) {}
};

// Null literal.  The result type is NULL_TYPE.
struct IRNullLiteral final : IRExpr {
    explicit IRNullLiteral(uint32_t ln, TypeInfo ti)
        : IRExpr(ti, ln) {}
};

// Built-in constant pi.  The result type is CNUM (maximum double precision).
struct IRPiLiteral final : IRExpr {
    explicit IRPiLiteral(uint32_t ln, TypeInfo ti)
        : IRExpr(ti, ln) {}
};

// Built-in constant e.  The result type is CNUM (maximum double precision).
struct IRELiteral final : IRExpr {
    explicit IRELiteral(uint32_t ln, TypeInfo ti)
        : IRExpr(ti, ln) {}
};



// IR expression nodes :: Interpolated string



// "text ${expr} more ${expr} end"
// segments has exactly expressions.size() + 1 elements (same layout as
// the AST InterpolatedStringExpr).  The result type is CSTR.
struct IRInterpolatedString final : IRExpr {
    std::vector<std::string> segments;
    std::vector<IRExprPtr>   expressions;

    IRInterpolatedString(uint32_t ln,
                         std::vector<std::string> segs,
                         std::vector<IRExprPtr> exprs,
                         TypeInfo ti)
        : IRExpr(ti, ln),
          segments(std::move(segs)),
          expressions(std::move(exprs)) {}
};



// IR expression nodes :: Variables and access



// Variable reference.  The result type is the resolved type of the variable.
struct IRVariable final : IRExpr {
    std::string name;

    IRVariable(uint32_t ln, std::string n, TypeInfo ti)
        : IRExpr(ti, ln), name(std::move(n)) {}
};

// Indexing: collection[index] or matrix[row, col].
// indices contains 1 element for collection/list access and 2 for matrix.
struct IRIndex final : IRExpr {
    IRExprPtr              object;
    std::vector<IRExprPtr> indices;

    IRIndex(uint32_t ln, IRExprPtr obj, std::vector<IRExprPtr> idx,
            TypeInfo ti)
        : IRExpr(ti, ln),
          object(std::move(obj)),
          indices(std::move(idx)) {}
};

// Quantum amplitude read: qnum_or_qubit[index].
// Distinct from IRIndex so both backends can dispatch on quantum amplitude
// access specifically.  The index is a cnum integer or a cstr binary
// string at runtime.  The result type is CNUM because a raw amplitude is
// a complex scalar; coercion to QNUM when the read is used in a quantum
// context is handled downstream by the existing numeric-to-quantum
// assignment path.
struct IRQnumIndex final : IRExpr {
    IRExprPtr object;
    IRExprPtr index;

    IRQnumIndex(uint32_t ln, IRExprPtr obj, IRExprPtr idx, TypeInfo ti)
        : IRExpr(ti, ln),
          object(std::move(obj)),
          index(std::move(idx)) {}
};

// Quantum amplitude write: qnum_or_qubit[index] = value.
// Distinct from IRAssign so both backends can dispatch on quantum
// amplitude write specifically.  The value may be CNUM or QNUM at the
// IR level; if QNUM, the backend measures it to produce a scalar
// amplitude.  After the write the register is automatically renormalised.
// The result type is CNUM, representing the scalar amplitude that was
// written (after measuring value if it was QNUM, before renormalisation
// of the register).
struct IRQnumIndexAssign final : IRExpr {
    IRExprPtr object;
    IRExprPtr index;
    IRExprPtr value;

    IRQnumIndexAssign(uint32_t ln, IRExprPtr obj, IRExprPtr idx,
                      IRExprPtr val, TypeInfo ti)
        : IRExpr(ti, ln),
          object(std::move(obj)),
          index(std::move(idx)),
          value(std::move(val)) {}
};



// IR expression nodes :: Assignment



// Assignment: target = value.
// target is an IRVariable or an IRIndex.  The result type is the type of
// the assigned value (assignments in Janus are expressions).
struct IRAssign final : IRExpr {
    IRExprPtr target;
    IRExprPtr value;

    IRAssign(uint32_t ln, IRExprPtr tgt, IRExprPtr val, TypeInfo ti)
        : IRExpr(ti, ln),
          target(std::move(tgt)),
          value(std::move(val)) {}
};



// IR expression nodes :: Operators



// Binary operator expression.
struct IRBinary final : IRExpr {
    IRExprPtr  left;
    IRBinaryOp op;
    IRExprPtr  right;

    IRBinary(uint32_t ln, IRExprPtr lhs, IRBinaryOp o, IRExprPtr rhs,
             TypeInfo ti)
        : IRExpr(ti, ln),
          left(std::move(lhs)),
          op(o),
          right(std::move(rhs)) {}
};

// Unary prefix operator expression.
struct IRUnary final : IRExpr {
    IRUnaryOp op;
    IRExprPtr operand;

    IRUnary(uint32_t ln, IRUnaryOp o, IRExprPtr opnd, TypeInfo ti)
        : IRExpr(ti, ln), op(o), operand(std::move(opnd)) {}
};

// Postfix boolean negation: Expression!  (precedence 14).
// Semantically equivalent to prefix boolean negation but distinguished
// in the IR to allow faithful Qiskit transpilation of the original source
// structure.
struct IRPostfixBang final : IRExpr {
    IRExprPtr operand;

    IRPostfixBang(uint32_t ln, IRExprPtr opnd, TypeInfo ti)
        : IRExpr(ti, ln), operand(std::move(opnd)) {}
};



// IR expression nodes :: Type cast



// Explicit type cast: (Type) Expression.
// target_type is the JanusType the expression is being cast to.
struct IRTypeCast final : IRExpr {
    JanusType target_type;
    IRExprPtr operand;

    IRTypeCast(uint32_t ln, JanusType tt, IRExprPtr opnd, TypeInfo ti)
        : IRExpr(ti, ln), target_type(tt), operand(std::move(opnd)) {}
};



// IR expression nodes :: Matrix literal



// Matrix literal: [a, b; c, d].
// rows is a 2D grid of expressions.  A single-row literal like [a, b, c]
// has one inner vector.  Also used for bracket expressions inside type
// constructor arguments such as list([1,2,3]).
struct IRMatrixLiteral final : IRExpr {
    std::vector<std::vector<IRExprPtr>> rows;

    IRMatrixLiteral(uint32_t ln, std::vector<std::vector<IRExprPtr>> r,
                    TypeInfo ti)
        : IRExpr(ti, ln), rows(std::move(r)) {}
};



// IR expression nodes :: Calls



// User-defined function call: callee(args...).
struct IRCall final : IRExpr {
    IRExprPtr              callee;
    std::vector<IRExprPtr> args;

    IRCall(uint32_t ln, IRExprPtr cal, std::vector<IRExprPtr> a, TypeInfo ti)
        : IRExpr(ti, ln),
          callee(std::move(cal)),
          args(std::move(a)) {}
};

// Built-in operand call.
// Covers every Beta built-in that uses function-call or unary-prefix
// syntax at the source level (measure, peek, state, expect, ctrle, run,
// runh, isunitary, sameoutput, print, delete, sin, cos, numberofgates,
// det, transpose, transposec, evals, evecs, gates, qubits, depth,
// bitlength).
struct IRBuiltinCall final : IRExpr {
    IRBuiltinOp            op;
    std::vector<IRExprPtr> args;

    IRBuiltinCall(uint32_t ln, IRBuiltinOp o, std::vector<IRExprPtr> a,
                  TypeInfo ti)
        : IRExpr(ti, ln), op(o), args(std::move(a)) {}
};

// Gate library access: gates.name() or gates.name(params...).
// gate_name is the unqualified gate identifier (e.g. "h", "cnot", "rx").
// args holds parameter expressions for parameterised gates; empty for
// fixed gates.
struct IRGateLibrary final : IRExpr {
    std::string            gate_name;
    std::vector<IRExprPtr> args;

    IRGateLibrary(uint32_t ln, std::string gn, std::vector<IRExprPtr> a,
                  TypeInfo ti)
        : IRExpr(ti, ln),
          gate_name(std::move(gn)),
          args(std::move(a)) {}
};



// IR expression nodes :: Type constructors



// Type constructor call: qubit(), cbit(), qnum(5), cnum("i",a,b),
// cstr("hello"), list([1,2,3]), matrix([...]), gate(matrix),
// circ([qubits], [gates]), block([gates]).
// constructed_type is the JanusType being constructed.
struct IRTypeConstruct final : IRExpr {
    JanusType              constructed_type;
    std::vector<IRExprPtr> args;

    IRTypeConstruct(uint32_t ln, JanusType ct, std::vector<IRExprPtr> a,
                    TypeInfo ti)
        : IRExpr(ti, ln),
          constructed_type(ct),
          args(std::move(a)) {}
};



// IR expression nodes :: Function definition



// function(param1, param2, ...) { body }
// The result type is FUNCTION with arity set to the parameter count.
struct IRFunctionDef final : IRExpr {
    std::vector<std::string> params;
    std::vector<IRStmtPtr>   body;

    IRFunctionDef(uint32_t ln, std::vector<std::string> p,
                  std::vector<IRStmtPtr> b, TypeInfo ti)
        : IRExpr(ti, ln),
          params(std::move(p)),
          body(std::move(b)) {}
};



// IR expression nodes :: Control flow

// Control flow constructs are expressions in Janus (they return values)
// and the IR preserves this structure for both backends.


// Helper: one "else if" branch in an if-chain.
struct IRElseIfClause {
    uint32_t               line;
    IRExprPtr              condition;
    std::vector<IRStmtPtr> body;
};

// if(Condition) { ... } else if(Condition) { ... } else { ... }
// The result type is CBIT (boolean indicating which branch executed).
struct IRIf final : IRExpr {
    IRExprPtr                    condition;
    std::vector<IRStmtPtr>       then_body;
    std::vector<IRElseIfClause>  else_ifs;
    std::vector<IRStmtPtr>       else_body;

    IRIf(uint32_t ln, IRExprPtr cond, std::vector<IRStmtPtr> then_b,
         std::vector<IRElseIfClause> eifs, std::vector<IRStmtPtr> else_b,
         TypeInfo ti)
        : IRExpr(ti, ln),
          condition(std::move(cond)),
          then_body(std::move(then_b)),
          else_ifs(std::move(eifs)),
          else_body(std::move(else_b)) {}
};

// for(Init; Condition; Update) { ... }
// init is an assignment expression (e.g. i = 0).
// update is an assignment expression (e.g. i = i + 1).
// The result type is CNUM (iterations completed + 1, or 0 if none).
struct IRFor final : IRExpr {
    IRExprPtr              init;
    IRExprPtr              condition;
    IRExprPtr              update;
    std::vector<IRStmtPtr> body;

    IRFor(uint32_t ln, IRExprPtr ini, IRExprPtr cond, IRExprPtr upd,
          std::vector<IRStmtPtr> b, TypeInfo ti)
        : IRExpr(ti, ln),
          init(std::move(ini)),
          condition(std::move(cond)),
          update(std::move(upd)),
          body(std::move(b)) {}
};

// while(Condition) { ... }
// The result type is CNUM (iterations completed + 1, or 0 if none).
struct IRWhile final : IRExpr {
    IRExprPtr              condition;
    std::vector<IRStmtPtr> body;

    IRWhile(uint32_t ln, IRExprPtr cond, std::vector<IRStmtPtr> b,
            TypeInfo ti)
        : IRExpr(ti, ln),
          condition(std::move(cond)),
          body(std::move(b)) {}
};

// foreach(Element in Collection [where Condition] [from Lower] [to Upper])
// { ... }
// element is the name of the iteration variable.
// where_cond, from_bound, and to_bound are nullptr when absent.
// The result type is CNUM (iterations completed + 1, or 0 if none).
struct IRForeach final : IRExpr {
    std::string            element;
    IRExprPtr              collection;
    IRExprPtr              where_cond;
    IRExprPtr              from_bound;
    IRExprPtr              to_bound;
    std::vector<IRStmtPtr> body;

    IRForeach(uint32_t ln, std::string elem, IRExprPtr coll,
              IRExprPtr whr, IRExprPtr frm, IRExprPtr to_b,
              std::vector<IRStmtPtr> b, TypeInfo ti)
        : IRExpr(ti, ln),
          element(std::move(elem)),
          collection(std::move(coll)),
          where_cond(std::move(whr)),
          from_bound(std::move(frm)),
          to_bound(std::move(to_b)),
          body(std::move(b)) {}
};



// IR statement nodes



// Expression used as a statement.
struct IRExprStmt final : IRStmt {
    IRExprPtr expr;

    IRExprStmt(uint32_t ln, IRExprPtr e)
        : IRStmt(ln), expr(std::move(e)) {}
};

// break (valid only inside for, while, foreach).
struct IRBreakStmt final : IRStmt {
    explicit IRBreakStmt(uint32_t ln) : IRStmt(ln) {}
};

// continue (valid only inside for, while, foreach).
struct IRContinueStmt final : IRStmt {
    explicit IRContinueStmt(uint32_t ln) : IRStmt(ln) {}
};

// return [value]
// A top-level return with an integer sets the process exit code (mod 256).
// A top-level return with a non-integer is an error.
// value is nullptr when return has no expression.
struct IRReturnStmt final : IRStmt {
    IRExprPtr value;

    IRReturnStmt(uint32_t ln, IRExprPtr v)
        : IRStmt(ln), value(std::move(v)) {}
};



// Top-level program



// The root of the IR.  A Janus source file is a flat sequence of
// statements executed from top to bottom, mirroring the AST Program.
struct IRProgram {
    std::vector<IRStmtPtr> statements;
};


} // namespace janus

#endif // JANUS_IR_HPP
