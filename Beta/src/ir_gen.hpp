#ifndef JANUS_IR_GEN_HPP
#define JANUS_IR_GEN_HPP

#include "ast.hpp"
#include "ir.hpp"
#include "typechecker.hpp"
#include "types.hpp"
#include "error.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace janus {

// IRGen :: lowers the type-annotated AST to the Janus IR.
// The IR generator walks the AST produced by the parser and annotated by
// the type checker, producing an IRProgram that preserves all control
// flow structures (if/else, for, while, foreach) rather than lowering
// to basic blocks.  Both the QuEST execution backend and the Qiskit
// transpilation backend consume the same IR.

// The circuit synthesiser is invoked by the backends at execution or
// transpilation time through the IR nodes this module produces; the IR
// faithfully represents every quantum operation (gate applications,
// measure, run, etc.) so that both backends can call into the shared
// circuit synthesiser with identical gate sequences.

// On any unexpected AST structure, the single Beta error is reported via
// report_error and the process terminates.

class IRGen {
public:
    // Constructs the IR generator with a reference to the type checker
    // whose annotations will be used to populate result types on IR nodes.
    explicit IRGen(const TypeChecker& checker);

    // Generates the complete IR from a type-checked AST.
    // The TypeChecker::check() must have been called on the program before
    // this method is invoked.
    IRProgram generate(const Program& program);

private:
    const TypeChecker& checker_;

    // Retrieves the TypeInfo that the type checker annotated on an AST
    // expression node.  Calls report_error if the node was not annotated.
    TypeInfo type_of(const Expr& expr) const;

    // Statement lowering.
    IRStmtPtr lower_stmt(const Stmt& stmt);
    std::vector<IRStmtPtr> lower_stmts(const std::vector<StmtPtr>& stmts);

    // Expression lowering.
    IRExprPtr lower_expr(const Expr& expr);

    // Specific expression lowering methods.
    IRExprPtr lower_integer_literal(const IntegerLiteralExpr& e);
    IRExprPtr lower_float_literal(const FloatLiteralExpr& e);
    IRExprPtr lower_string_literal(const StringLiteralExpr& e);
    IRExprPtr lower_bool_literal(const BoolLiteralExpr& e);
    IRExprPtr lower_null_literal(const NullLiteralExpr& e);
    IRExprPtr lower_pi_literal(const PiLiteralExpr& e);
    IRExprPtr lower_e_literal(const ELiteralExpr& e);
    IRExprPtr lower_interpolated_string(const InterpolatedStringExpr& e);
    IRExprPtr lower_identifier(const IdentifierExpr& e);
    IRExprPtr lower_index(const IndexExpr& e);
    IRExprPtr lower_assign(const AssignExpr& e);
    IRExprPtr lower_binary(const BinaryExpr& e);
    IRExprPtr lower_unary(const UnaryExpr& e);
    IRExprPtr lower_postfix_bang(const PostfixBangExpr& e);
    IRExprPtr lower_type_cast(const TypeCastExpr& e);
    IRExprPtr lower_matrix_literal(const MatrixLiteralExpr& e);
    IRExprPtr lower_call(const CallExpr& e);
    IRExprPtr lower_gate_library(const GateLibraryExpr& e);
    IRExprPtr lower_type_construct(const TypeConstructExpr& e);
    IRExprPtr lower_function_expr(const FunctionExpr& e);
    IRExprPtr lower_builtin_call(const BuiltinCallExpr& e);
    IRExprPtr lower_if(const IfExpr& e);
    IRExprPtr lower_for(const ForExpr& e);
    IRExprPtr lower_while(const WhileExpr& e);
    IRExprPtr lower_foreach(const ForeachExpr& e);

    // Operator mapping helpers.
    static IRBinaryOp  map_binary_op(TokenType tok, uint32_t line);
    static IRUnaryOp   map_unary_op(TokenType tok, uint32_t line);
    static IRBuiltinOp map_builtin_op(TokenType tok, uint32_t line);
};

} // namespace janus

#endif // JANUS_IR_GEN_HPP
