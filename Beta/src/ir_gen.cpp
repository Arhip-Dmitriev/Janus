#include "ir_gen.hpp"

namespace janus {


// Construction


IRGen::IRGen(const TypeChecker& checker)
    : checker_(checker) {}


// Public interface


IRProgram IRGen::generate(const Program& program) {
    IRProgram ir;
    ir.statements = lower_stmts(program.statements);
    return ir;
}


// Type annotation lookup


TypeInfo IRGen::type_of(const Expr& expr) const {
    return checker_.get_type(&expr);
}


// Statement lowering


std::vector<IRStmtPtr> IRGen::lower_stmts(
        const std::vector<StmtPtr>& stmts) {
    std::vector<IRStmtPtr> result;
    result.reserve(stmts.size());
    for (const auto& stmt : stmts) {
        result.push_back(lower_stmt(*stmt));
    }
    return result;
}


IRStmtPtr IRGen::lower_stmt(const Stmt& stmt) {
    set_error_line(stmt.line);

    // Expression statement.
    if (const auto* es = dynamic_cast<const ExprStmt*>(&stmt)) {
        auto ir_expr = lower_expr(*es->expr);
        return std::make_unique<IRExprStmt>(es->line, std::move(ir_expr));
    }

    // break.
    if (const auto* bs = dynamic_cast<const BreakStmt*>(&stmt)) {
        return std::make_unique<IRBreakStmt>(bs->line);
    }

    // continue.
    if (const auto* cs = dynamic_cast<const ContinueStmt*>(&stmt)) {
        return std::make_unique<IRContinueStmt>(cs->line);
    }

    // return [value].
    if (const auto* rs = dynamic_cast<const ReturnStmt*>(&stmt)) {
        IRExprPtr val = nullptr;
        if (rs->value) {
            val = lower_expr(*rs->value);
        }
        return std::make_unique<IRReturnStmt>(rs->line, std::move(val));
    }

    // Unknown statement type should not occur with a well-formed AST.
    report_error(stmt.line);
}


// Expression lowering dispatch


IRExprPtr IRGen::lower_expr(const Expr& expr) {
    set_error_line(expr.line);

    // Literals.
    if (const auto* e = dynamic_cast<const IntegerLiteralExpr*>(&expr))
        return lower_integer_literal(*e);
    if (const auto* e = dynamic_cast<const FloatLiteralExpr*>(&expr))
        return lower_float_literal(*e);
    if (const auto* e = dynamic_cast<const StringLiteralExpr*>(&expr))
        return lower_string_literal(*e);
    if (const auto* e = dynamic_cast<const BoolLiteralExpr*>(&expr))
        return lower_bool_literal(*e);
    if (const auto* e = dynamic_cast<const NullLiteralExpr*>(&expr))
        return lower_null_literal(*e);
    if (const auto* e = dynamic_cast<const PiLiteralExpr*>(&expr))
        return lower_pi_literal(*e);
    if (const auto* e = dynamic_cast<const ELiteralExpr*>(&expr))
        return lower_e_literal(*e);

    // Interpolated string.
    if (const auto* e = dynamic_cast<const InterpolatedStringExpr*>(&expr))
        return lower_interpolated_string(*e);

    // Identifiers and access.
    if (const auto* e = dynamic_cast<const IdentifierExpr*>(&expr))
        return lower_identifier(*e);
    if (const auto* e = dynamic_cast<const IndexExpr*>(&expr))
        return lower_index(*e);

    // Assignment.
    if (const auto* e = dynamic_cast<const AssignExpr*>(&expr))
        return lower_assign(*e);

    // Operators.
    if (const auto* e = dynamic_cast<const BinaryExpr*>(&expr))
        return lower_binary(*e);
    if (const auto* e = dynamic_cast<const UnaryExpr*>(&expr))
        return lower_unary(*e);
    if (const auto* e = dynamic_cast<const PostfixBangExpr*>(&expr))
        return lower_postfix_bang(*e);

    // Type cast.
    if (const auto* e = dynamic_cast<const TypeCastExpr*>(&expr))
        return lower_type_cast(*e);

    // Matrix literal.
    if (const auto* e = dynamic_cast<const MatrixLiteralExpr*>(&expr))
        return lower_matrix_literal(*e);

    // Calls.
    if (const auto* e = dynamic_cast<const CallExpr*>(&expr))
        return lower_call(*e);
    if (const auto* e = dynamic_cast<const GateLibraryExpr*>(&expr))
        return lower_gate_library(*e);
    if (const auto* e = dynamic_cast<const BuiltinCallExpr*>(&expr))
        return lower_builtin_call(*e);

    // Type constructors.
    if (const auto* e = dynamic_cast<const TypeConstructExpr*>(&expr))
        return lower_type_construct(*e);

    // Function definition.
    if (const auto* e = dynamic_cast<const FunctionExpr*>(&expr))
        return lower_function_expr(*e);

    // Control flow.
    if (const auto* e = dynamic_cast<const IfExpr*>(&expr))
        return lower_if(*e);
    if (const auto* e = dynamic_cast<const ForExpr*>(&expr))
        return lower_for(*e);
    if (const auto* e = dynamic_cast<const WhileExpr*>(&expr))
        return lower_while(*e);
    if (const auto* e = dynamic_cast<const ForeachExpr*>(&expr))
        return lower_foreach(*e);

    // MemberAccessExpr is used exclusively for the gates namespace in
    // Beta.  The parser produces GateLibraryExpr nodes for gates.name()
    // calls, so a bare MemberAccessExpr reaching this point is an error.
    if (dynamic_cast<const MemberAccessExpr*>(&expr)) {
        report_error(expr.line);
    }

    // Unknown expression type should not occur with a well-formed AST.
    report_error(expr.line);
}


// Literal lowering


IRExprPtr IRGen::lower_integer_literal(const IntegerLiteralExpr& e) {
    TypeInfo ti = type_of(e);
    return std::make_unique<IRIntegerLiteral>(e.line, e.value, ti);
}

IRExprPtr IRGen::lower_float_literal(const FloatLiteralExpr& e) {
    TypeInfo ti = type_of(e);
    return std::make_unique<IRFloatLiteral>(e.line, e.value, ti);
}

IRExprPtr IRGen::lower_string_literal(const StringLiteralExpr& e) {
    TypeInfo ti = type_of(e);
    return std::make_unique<IRStringLiteral>(e.line, e.value, ti);
}

IRExprPtr IRGen::lower_bool_literal(const BoolLiteralExpr& e) {
    TypeInfo ti = type_of(e);
    return std::make_unique<IRBoolLiteral>(e.line, e.value, ti);
}

IRExprPtr IRGen::lower_null_literal(const NullLiteralExpr& e) {
    TypeInfo ti = type_of(e);
    return std::make_unique<IRNullLiteral>(e.line, ti);
}

IRExprPtr IRGen::lower_pi_literal(const PiLiteralExpr& e) {
    TypeInfo ti = type_of(e);
    return std::make_unique<IRPiLiteral>(e.line, ti);
}

IRExprPtr IRGen::lower_e_literal(const ELiteralExpr& e) {
    TypeInfo ti = type_of(e);
    return std::make_unique<IRELiteral>(e.line, ti);
}


// Interpolated string lowering


IRExprPtr IRGen::lower_interpolated_string(
        const InterpolatedStringExpr& e) {
    TypeInfo ti = type_of(e);

    std::vector<IRExprPtr> ir_exprs;
    ir_exprs.reserve(e.expressions.size());
    for (const auto& sub : e.expressions) {
        ir_exprs.push_back(lower_expr(*sub));
    }

    // Copy the string segments verbatim.
    std::vector<std::string> segs = e.segments;

    return std::make_unique<IRInterpolatedString>(
        e.line, std::move(segs), std::move(ir_exprs), ti);
}


// Identifier and access lowering


IRExprPtr IRGen::lower_identifier(const IdentifierExpr& e) {
    TypeInfo ti = type_of(e);
    return std::make_unique<IRVariable>(e.line, e.name, ti);
}

IRExprPtr IRGen::lower_index(const IndexExpr& e) {
    TypeInfo ti = type_of(e);

    auto ir_obj = lower_expr(*e.object);

    std::vector<IRExprPtr> ir_indices;
    ir_indices.reserve(e.indices.size());
    for (const auto& idx : e.indices) {
        ir_indices.push_back(lower_expr(*idx));
    }

    return std::make_unique<IRIndex>(
        e.line, std::move(ir_obj), std::move(ir_indices), ti);
}


// Assignment lowering


IRExprPtr IRGen::lower_assign(const AssignExpr& e) {
    TypeInfo ti = type_of(e);

    auto ir_target = lower_expr(*e.target);
    auto ir_value  = lower_expr(*e.value);

    return std::make_unique<IRAssign>(
        e.line, std::move(ir_target), std::move(ir_value), ti);
}


// Binary operator lowering


IRExprPtr IRGen::lower_binary(const BinaryExpr& e) {
    TypeInfo ti = type_of(e);

    auto ir_left  = lower_expr(*e.left);
    IRBinaryOp op = map_binary_op(e.op, e.line);
    auto ir_right = lower_expr(*e.right);

    return std::make_unique<IRBinary>(
        e.line, std::move(ir_left), op, std::move(ir_right), ti);
}


// Unary operator lowering


IRExprPtr IRGen::lower_unary(const UnaryExpr& e) {
    TypeInfo ti = type_of(e);

    IRUnaryOp op      = map_unary_op(e.op, e.line);
    auto      ir_opnd = lower_expr(*e.operand);

    return std::make_unique<IRUnary>(
        e.line, op, std::move(ir_opnd), ti);
}


// Postfix bang lowering


IRExprPtr IRGen::lower_postfix_bang(const PostfixBangExpr& e) {
    TypeInfo ti = type_of(e);

    auto ir_opnd = lower_expr(*e.operand);

    return std::make_unique<IRPostfixBang>(
        e.line, std::move(ir_opnd), ti);
}


// Type cast lowering


IRExprPtr IRGen::lower_type_cast(const TypeCastExpr& e) {
    TypeInfo ti = type_of(e);

    auto target_opt = janus_type_from_keyword(e.target_type);
    if (!target_opt.has_value()) {
        report_error(e.line);
    }
    JanusType target = target_opt.value();

    auto ir_opnd = lower_expr(*e.operand);

    return std::make_unique<IRTypeCast>(
        e.line, target, std::move(ir_opnd), ti);
}


// Matrix literal lowering


IRExprPtr IRGen::lower_matrix_literal(const MatrixLiteralExpr& e) {
    TypeInfo ti = type_of(e);

    std::vector<std::vector<IRExprPtr>> ir_rows;
    ir_rows.reserve(e.rows.size());
    for (const auto& row : e.rows) {
        std::vector<IRExprPtr> ir_row;
        ir_row.reserve(row.size());
        for (const auto& elem : row) {
            ir_row.push_back(lower_expr(*elem));
        }
        ir_rows.push_back(std::move(ir_row));
    }

    return std::make_unique<IRMatrixLiteral>(
        e.line, std::move(ir_rows), ti);
}


// User-defined function call lowering


IRExprPtr IRGen::lower_call(const CallExpr& e) {
    TypeInfo ti = type_of(e);

    auto ir_callee = lower_expr(*e.callee);

    std::vector<IRExprPtr> ir_args;
    ir_args.reserve(e.args.size());
    for (const auto& arg : e.args) {
        ir_args.push_back(lower_expr(*arg));
    }

    return std::make_unique<IRCall>(
        e.line, std::move(ir_callee), std::move(ir_args), ti);
}


// Gate library access lowering


IRExprPtr IRGen::lower_gate_library(const GateLibraryExpr& e) {
    TypeInfo ti = type_of(e);

    std::vector<IRExprPtr> ir_args;
    ir_args.reserve(e.args.size());
    for (const auto& arg : e.args) {
        ir_args.push_back(lower_expr(*arg));
    }

    return std::make_unique<IRGateLibrary>(
        e.line, e.gate_name, std::move(ir_args), ti);
}


// Type constructor lowering


IRExprPtr IRGen::lower_type_construct(const TypeConstructExpr& e) {
    TypeInfo ti = type_of(e);

    auto type_opt = janus_type_from_keyword(e.type_keyword);
    if (!type_opt.has_value()) {
        report_error(e.line);
    }
    JanusType constructed = type_opt.value();

    std::vector<IRExprPtr> ir_args;
    ir_args.reserve(e.args.size());
    for (const auto& arg : e.args) {
        ir_args.push_back(lower_expr(*arg));
    }

    return std::make_unique<IRTypeConstruct>(
        e.line, constructed, std::move(ir_args), ti);
}


// Function definition lowering


IRExprPtr IRGen::lower_function_expr(const FunctionExpr& e) {
    TypeInfo ti = type_of(e);

    // Copy parameter names.
    std::vector<std::string> params = e.params;

    // Recursively lower the function body statements.
    std::vector<IRStmtPtr> ir_body = lower_stmts(e.body);

    return std::make_unique<IRFunctionDef>(
        e.line, std::move(params), std::move(ir_body), ti);
}


// Built-in operand call lowering


IRExprPtr IRGen::lower_builtin_call(const BuiltinCallExpr& e) {
    TypeInfo ti = type_of(e);

    IRBuiltinOp op = map_builtin_op(e.op, e.line);

    std::vector<IRExprPtr> ir_args;
    ir_args.reserve(e.args.size());
    for (const auto& arg : e.args) {
        ir_args.push_back(lower_expr(*arg));
    }

    return std::make_unique<IRBuiltinCall>(
        e.line, op, std::move(ir_args), ti);
}


// Control flow lowering


IRExprPtr IRGen::lower_if(const IfExpr& e) {
    TypeInfo ti = type_of(e);

    auto ir_cond = lower_expr(*e.condition);
    auto ir_then = lower_stmts(e.then_body);

    // Lower else-if clauses.
    std::vector<IRElseIfClause> ir_else_ifs;
    ir_else_ifs.reserve(e.else_ifs.size());
    for (const auto& eif : e.else_ifs) {
        IRElseIfClause ir_eif;
        ir_eif.line      = eif.line;
        ir_eif.condition  = lower_expr(*eif.condition);
        ir_eif.body       = lower_stmts(eif.body);
        ir_else_ifs.push_back(std::move(ir_eif));
    }

    // Lower else body (may be empty).
    auto ir_else = lower_stmts(e.else_body);

    return std::make_unique<IRIf>(
        e.line, std::move(ir_cond), std::move(ir_then),
        std::move(ir_else_ifs), std::move(ir_else), ti);
}


IRExprPtr IRGen::lower_for(const ForExpr& e) {
    TypeInfo ti = type_of(e);

    auto ir_init   = lower_expr(*e.init);
    auto ir_cond   = lower_expr(*e.condition);
    auto ir_update = lower_expr(*e.update);
    auto ir_body   = lower_stmts(e.body);

    return std::make_unique<IRFor>(
        e.line, std::move(ir_init), std::move(ir_cond),
        std::move(ir_update), std::move(ir_body), ti);
}


IRExprPtr IRGen::lower_while(const WhileExpr& e) {
    TypeInfo ti = type_of(e);

    auto ir_cond = lower_expr(*e.condition);
    auto ir_body = lower_stmts(e.body);

    return std::make_unique<IRWhile>(
        e.line, std::move(ir_cond), std::move(ir_body), ti);
}


IRExprPtr IRGen::lower_foreach(const ForeachExpr& e) {
    TypeInfo ti = type_of(e);

    auto ir_coll = lower_expr(*e.collection);

    // Optional clauses: lower only if present; otherwise nullptr.
    IRExprPtr ir_where = nullptr;
    if (e.where_cond) {
        ir_where = lower_expr(*e.where_cond);
    }

    IRExprPtr ir_from = nullptr;
    if (e.from_bound) {
        ir_from = lower_expr(*e.from_bound);
    }

    IRExprPtr ir_to = nullptr;
    if (e.to_bound) {
        ir_to = lower_expr(*e.to_bound);
    }

    auto ir_body = lower_stmts(e.body);

    return std::make_unique<IRForeach>(
        e.line, e.element, std::move(ir_coll),
        std::move(ir_where), std::move(ir_from), std::move(ir_to),
        std::move(ir_body), ti);
}


// Operator mapping: TokenType -> IRBinaryOp


IRBinaryOp IRGen::map_binary_op(TokenType tok, uint32_t line) {
    switch (tok) {
        case TokenType::PLUS:         return IRBinaryOp::ADD;
        case TokenType::MINUS:        return IRBinaryOp::SUB;
        case TokenType::STAR:         return IRBinaryOp::MUL;
        case TokenType::SLASH:        return IRBinaryOp::DIV;
        case TokenType::DOUBLE_SLASH: return IRBinaryOp::INT_DIV;
        case TokenType::PERCENT:      return IRBinaryOp::MOD;
        case TokenType::CARET:        return IRBinaryOp::EXP;
        case TokenType::EQUAL_EQUAL:  return IRBinaryOp::EQ;
        case TokenType::LESS:         return IRBinaryOp::LT;
        case TokenType::GREATER:      return IRBinaryOp::GT;
        case TokenType::LESS_EQUAL:   return IRBinaryOp::LE;
        case TokenType::GREATER_EQUAL:return IRBinaryOp::GE;
        case TokenType::KW_AND:       return IRBinaryOp::AND;
        case TokenType::KW_NAND:      return IRBinaryOp::NAND;
        case TokenType::KW_OR:        return IRBinaryOp::OR;
        case TokenType::KW_NOR:       return IRBinaryOp::NOR;
        case TokenType::KW_XOR:       return IRBinaryOp::XOR;
        case TokenType::KW_XNOR:      return IRBinaryOp::XNOR;
        case TokenType::KW_TENSOR:    return IRBinaryOp::TENSOR;
        default:
            report_error(line);
    }
}


// Operator mapping: TokenType -> IRUnaryOp


IRUnaryOp IRGen::map_unary_op(TokenType tok, uint32_t line) {
    switch (tok) {
        case TokenType::MINUS:       return IRUnaryOp::NEG;
        case TokenType::KW_NOT:      return IRUnaryOp::BITWISE_NOT;
        case TokenType::BANG:        return IRUnaryOp::BOOL_NOT;
        case TokenType::SHIFT_LEFT:  return IRUnaryOp::SHIFT_LEFT;
        case TokenType::SHIFT_RIGHT: return IRUnaryOp::SHIFT_RIGHT;
        default:
            report_error(line);
    }
}


// Operator mapping: TokenType -> IRBuiltinOp


IRBuiltinOp IRGen::map_builtin_op(TokenType tok, uint32_t line) {
    switch (tok) {
        case TokenType::KW_MEASURE:       return IRBuiltinOp::MEASURE;
        case TokenType::KW_PEEK:          return IRBuiltinOp::PEEK;
        case TokenType::KW_STATE:         return IRBuiltinOp::STATE;
        case TokenType::KW_EXPECT:        return IRBuiltinOp::EXPECT;
        case TokenType::KW_CTRLE:         return IRBuiltinOp::CTRLE;
        case TokenType::KW_RUN:           return IRBuiltinOp::RUN;
        case TokenType::KW_RUNH:          return IRBuiltinOp::RUNH;
        case TokenType::KW_ISUNITARY:     return IRBuiltinOp::ISUNITARY;
        case TokenType::KW_SAMEOUTPUT:    return IRBuiltinOp::SAMEOUTPUT;
        case TokenType::KW_PRINT:         return IRBuiltinOp::PRINT;
        case TokenType::KW_DELETE:        return IRBuiltinOp::DELETE;
        case TokenType::KW_SIN:           return IRBuiltinOp::SIN;
        case TokenType::KW_COS:           return IRBuiltinOp::COS;
        case TokenType::KW_NUMBEROFGATES: return IRBuiltinOp::NUMBEROFGATES;
        case TokenType::KW_DET:           return IRBuiltinOp::DET;
        case TokenType::KW_TRANSPOSE:     return IRBuiltinOp::TRANSPOSE;
        case TokenType::KW_TRANSPOSEC:    return IRBuiltinOp::TRANSPOSEC;
        case TokenType::KW_EVALS:         return IRBuiltinOp::EVALS;
        case TokenType::KW_EVECS:         return IRBuiltinOp::EVECS;
        case TokenType::KW_GATES:         return IRBuiltinOp::GATES;
        case TokenType::KW_QUBITS:        return IRBuiltinOp::QUBITS;
        case TokenType::KW_DEPTH:         return IRBuiltinOp::DEPTH;
        default:
            report_error(line);
    }
}


} // namespace janus
