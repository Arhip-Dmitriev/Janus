#include "typechecker.hpp"

namespace janus {


// Construction


TypeChecker::TypeChecker() {
    // Create the script-level scope (frame 0).
    frames_.emplace_back();
}


// Public interface


void TypeChecker::check(const Program& program) {
    // Forward-declare all top-level function assignments so that
    // functions may be called before their declaration in source order.
    forward_declare_functions(program.statements);

    // Walk every statement in program order.
    check_stmts(program.statements);
}

TypeInfo TypeChecker::get_type(const Expr* expr) const {
    auto it = type_map_.find(expr);
    if (it != type_map_.end()) {
        return it->second;
    }
    return make_null_type();
}


// Compile-time scope management


void TypeChecker::push_block() {
    frames_.emplace_back();
}

void TypeChecker::pop_block() {
    if (frames_.size() <= 1) {
        report_error(get_error_line());
    }
    frames_.pop_back();
}

void TypeChecker::push_function() {
    TypeFrame frame;
    frame.is_function_boundary = true;
    frames_.push_back(std::move(frame));
}

void TypeChecker::pop_function() {
    if (frames_.size() <= 1) {
        report_error(get_error_line());
    }
    frames_.pop_back();
}

TypeInfo* TypeChecker::lookup(const std::string& name) {
    for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
        auto var_it = it->vars.find(name);
        if (var_it != it->vars.end()) {
            return &var_it->second;
        }
        if (it->is_function_boundary) {
            return nullptr;
        }
    }
    return nullptr;
}

void TypeChecker::assign(const std::string& name, TypeInfo type,
                         uint32_t line) {
    auto& current = frames_.back().vars;
    auto it = current.find(name);

    if (it != current.end()) {
        // Re-assignment: check type compatibility.
        // If the existing type is NULL_TYPE, the variable was declared
        // with an unknown type (e.g. function parameter or foreach
        // element) and adopts the new type on first real assignment.
        if (it->second.type == JanusType::NULL_TYPE) {
            it->second = type;
        } else if (!is_assignable_without_cast(it->second.type, type.type)) {
            report_error(line);
        } else {
            // Compatible re-assignment.  Keep the existing type tag but
            // allow the metadata to update (e.g. width might change for
            // qnum assignments of different values).
            // Conservative choice: keep the original type tag.
        }
    } else {
        // New variable in the current frame (potentially shadowing outer).
        current.emplace(name, type);
    }
}

void TypeChecker::declare_loop_variable(const std::string& name,
                                        TypeInfo type, uint32_t line) {
    auto& current = frames_.back().vars;
    if (current.find(name) != current.end()) {
        // Collision: a variable with this name already exists at the
        // scope level where the loop variable would persist.
        report_error(line);
    }
    current.emplace(name, type);
}

bool TypeChecker::exists_in_current_frame(const std::string& name) const {
    return frames_.back().vars.find(name) != frames_.back().vars.end();
}


// Forward-declaration pre-pass


void TypeChecker::forward_declare_functions(
        const std::vector<StmtPtr>& stmts) {
    for (const auto& stmt : stmts) {
        auto* expr_stmt = dynamic_cast<const ExprStmt*>(stmt.get());
        if (!expr_stmt) continue;

        auto* assign_expr = dynamic_cast<const AssignExpr*>(
            expr_stmt->expr.get());
        if (!assign_expr) continue;

        auto* id = dynamic_cast<const IdentifierExpr*>(
            assign_expr->target.get());
        if (!id) continue;

        auto* func = dynamic_cast<const FunctionExpr*>(
            assign_expr->value.get());
        if (!func) continue;

        auto arity = static_cast<uint32_t>(func->params.size());
        // Only forward-declare if not already present in the current frame
        // to avoid overwriting a variable that was assigned earlier.
        auto& current = frames_.back().vars;
        if (current.find(id->name) == current.end()) {
            current.emplace(id->name, make_function_type(arity));
        }
    }
}


// Statement checking


void TypeChecker::check_stmts(const std::vector<StmtPtr>& stmts) {
    for (const auto& stmt : stmts) {
        check_stmt(*stmt);
    }
}

void TypeChecker::check_stmt(const Stmt& stmt) {
    set_error_line(stmt.line);

    if (auto* es = dynamic_cast<const ExprStmt*>(&stmt)) {
        check_expr(*es->expr);
        return;
    }

    if (auto* bs = dynamic_cast<const BreakStmt*>(&stmt)) {
        // break is only valid inside a loop.
        if (loop_depth_ == 0) {
            report_error(bs->line);
        }
        return;
    }

    if (auto* cs = dynamic_cast<const ContinueStmt*>(&stmt)) {
        // continue is only valid inside a loop.
        if (loop_depth_ == 0) {
            report_error(cs->line);
        }
        return;
    }

    if (auto* rs = dynamic_cast<const ReturnStmt*>(&stmt)) {
        if (rs->value) {
            TypeInfo val_type = check_expr(*rs->value);
            // At top level (not inside any function), return value must
            // be an integer (CNUM or CBIT or NULL_TYPE).
            if (function_depth_ == 0) {
                JanusType vt = val_type.type;
                if (vt != JanusType::CNUM && vt != JanusType::CBIT &&
                    vt != JanusType::NULL_TYPE) {
                    report_error(rs->line);
                }
            }
        }
        return;
    }

    // Unknown statement type should not occur with a well-formed AST.
    report_error(stmt.line);
}


// Expression checking dispatch


TypeInfo TypeChecker::check_expr(const Expr& expr) {
    set_error_line(expr.line);

    if (auto* e = dynamic_cast<const IntegerLiteralExpr*>(&expr))
        return check_integer_literal(*e);
    if (auto* e = dynamic_cast<const FloatLiteralExpr*>(&expr))
        return check_float_literal(*e);
    if (auto* e = dynamic_cast<const StringLiteralExpr*>(&expr))
        return check_string_literal(*e);
    if (auto* e = dynamic_cast<const BoolLiteralExpr*>(&expr))
        return check_bool_literal(*e);
    if (auto* e = dynamic_cast<const NullLiteralExpr*>(&expr))
        return check_null_literal(*e);
    if (auto* e = dynamic_cast<const PiLiteralExpr*>(&expr))
        return check_pi_literal(*e);
    if (auto* e = dynamic_cast<const ELiteralExpr*>(&expr))
        return check_e_literal(*e);
    if (auto* e = dynamic_cast<const InterpolatedStringExpr*>(&expr))
        return check_interpolated_string(*e);
    if (auto* e = dynamic_cast<const IdentifierExpr*>(&expr))
        return check_identifier(*e);
    if (auto* e = dynamic_cast<const IndexExpr*>(&expr))
        return check_index(*e);
    if (auto* e = dynamic_cast<const MemberAccessExpr*>(&expr))
        return check_member_access(*e);
    if (auto* e = dynamic_cast<const AssignExpr*>(&expr))
        return check_assign(*e);
    if (auto* e = dynamic_cast<const BinaryExpr*>(&expr))
        return check_binary(*e);
    if (auto* e = dynamic_cast<const UnaryExpr*>(&expr))
        return check_unary(*e);
    if (auto* e = dynamic_cast<const PostfixBangExpr*>(&expr))
        return check_postfix_bang(*e);
    if (auto* e = dynamic_cast<const TypeCastExpr*>(&expr))
        return check_type_cast(*e);
    if (auto* e = dynamic_cast<const MatrixLiteralExpr*>(&expr))
        return check_matrix_literal(*e);
    if (auto* e = dynamic_cast<const CallExpr*>(&expr))
        return check_call(*e);
    if (auto* e = dynamic_cast<const GateLibraryExpr*>(&expr))
        return check_gate_library(*e);
    if (auto* e = dynamic_cast<const TypeConstructExpr*>(&expr))
        return check_type_construct(*e);
    if (auto* e = dynamic_cast<const FunctionExpr*>(&expr))
        return check_function_expr(*e);
    if (auto* e = dynamic_cast<const BuiltinCallExpr*>(&expr))
        return check_builtin_call(*e);
    if (auto* e = dynamic_cast<const IfExpr*>(&expr))
        return check_if(*e);
    if (auto* e = dynamic_cast<const ForExpr*>(&expr))
        return check_for(*e);
    if (auto* e = dynamic_cast<const WhileExpr*>(&expr))
        return check_while(*e);
    if (auto* e = dynamic_cast<const ForeachExpr*>(&expr))
        return check_foreach(*e);

    // Unknown expression type.
    report_error(expr.line);
}


// Annotation


void TypeChecker::annotate(const Expr& expr, TypeInfo type) {
    type_map_[&expr] = type;
}


// Literal checkers


TypeInfo TypeChecker::check_integer_literal(const IntegerLiteralExpr& e) {
    TypeInfo ti = make_cnum_type();
    annotate(e, ti);
    return ti;
}

TypeInfo TypeChecker::check_float_literal(const FloatLiteralExpr& e) {
    TypeInfo ti = make_cnum_type();
    annotate(e, ti);
    return ti;
}

TypeInfo TypeChecker::check_string_literal(const StringLiteralExpr& e) {
    TypeInfo ti = make_cstr_type(static_cast<uint32_t>(e.value.size()));
    annotate(e, ti);
    return ti;
}

TypeInfo TypeChecker::check_bool_literal(const BoolLiteralExpr& e) {
    TypeInfo ti = make_cbit_type();
    annotate(e, ti);
    return ti;
}

TypeInfo TypeChecker::check_null_literal(const NullLiteralExpr& e) {
    TypeInfo ti = make_null_type();
    annotate(e, ti);
    return ti;
}

TypeInfo TypeChecker::check_pi_literal(const PiLiteralExpr& e) {
    TypeInfo ti = make_cnum_type();
    annotate(e, ti);
    return ti;
}

TypeInfo TypeChecker::check_e_literal(const ELiteralExpr& e) {
    TypeInfo ti = make_cnum_type();
    annotate(e, ti);
    return ti;
}

TypeInfo TypeChecker::check_interpolated_string(
        const InterpolatedStringExpr& e) {
    // All interpolated sub-expressions are evaluated; any type is
    // allowed since every Janus value has a string representation.
    for (const auto& sub : e.expressions) {
        check_expr(*sub);
    }
    TypeInfo ti = make_cstr_type();
    annotate(e, ti);
    return ti;
}


// Identifier


TypeInfo TypeChecker::check_identifier(const IdentifierExpr& e) {
    TypeInfo* found = lookup(e.name);
    if (!found) {
        report_error(e.line);
    }
    TypeInfo ti = *found;
    annotate(e, ti);
    return ti;
}


// Index access


TypeInfo TypeChecker::check_index(const IndexExpr& e) {
    TypeInfo obj_type = check_expr(*e.object);
    for (const auto& idx : e.indices) {
        check_expr(*idx);
    }

    // Quantum amplitude read: qnum[x] or qubit[x] with exactly 1 index.
    // Result is CNUM.  Index value validation is deferred to runtime.
    if ((obj_type.type == JanusType::QNUM ||
         obj_type.type == JanusType::QUBIT) &&
        e.indices.size() == 1) {
        TypeInfo ti = make_cnum_type();
        annotate(e, ti);
        return ti;
    }

    // Matrix access requires exactly 2 indices.
    if (obj_type.type == JanusType::MATRIX ||
        obj_type.type == JanusType::GATE) {
        if (e.indices.size() != 2) {
            report_error(e.line);
        }
        // Element of a matrix is CNUM (complex scalar).
        TypeInfo ti = make_cnum_type(true);
        annotate(e, ti);
        return ti;
    }

    // List access requires exactly 1 index.
    if (obj_type.type == JanusType::LIST) {
        if (e.indices.size() != 1) {
            report_error(e.line);
        }
        // Element type of a list is unknown at compile time.
        TypeInfo ti = make_null_type();
        annotate(e, ti);
        return ti;
    }

    // CSTR access (character at index) requires 1 index, returns CSTR.
    if (obj_type.type == JanusType::CSTR) {
        if (e.indices.size() != 1) {
            report_error(e.line);
        }
        TypeInfo ti = make_cstr_type(1);
        annotate(e, ti);
        return ti;
    }

    // Circ/block access returns a gate or null.
    if (obj_type.type == JanusType::CIRC ||
        obj_type.type == JanusType::BLOCK) {
        if (e.indices.size() != 2) {
            report_error(e.line);
        }
        TypeInfo ti = make_gate_type();
        annotate(e, ti);
        return ti;
    }

    // For NULL_TYPE (type unknown at compile time), allow indexing and
    // defer full validation to runtime.
    if (obj_type.type == JanusType::NULL_TYPE) {
        TypeInfo ti = make_null_type();
        annotate(e, ti);
        return ti;
    }

    // Other types are not indexable.
    report_error(e.line);
}


// Member access (gates namespace only in Beta)


TypeInfo TypeChecker::check_member_access(const MemberAccessExpr& e) {
    // In Beta, member access is only used for the gates namespace.
    // The object should be an identifier "gates".  The type checker does
    // not enforce this structurally because the parser already produces
    // GateLibraryExpr for valid gates.name() calls.  A MemberAccessExpr
    // reaching the type checker means a bare gates.name without the call
    // parentheses, which is an error.
    report_error(e.line);
}


// Assignment


TypeInfo TypeChecker::check_assign(const AssignExpr& e) {
    // Special case: when the value is a FunctionExpr and the target is
    // an identifier, handle recursion by injecting the function name
    // into the function's own scope so it can call itself.
    if (auto* id = dynamic_cast<const IdentifierExpr*>(e.target.get())) {
        if (auto* func = dynamic_cast<const FunctionExpr*>(e.value.get())) {
            TypeInfo func_type = make_function_type(
                static_cast<uint32_t>(func->params.size()));

            // Register the function name in the enclosing scope.
            assign(id->name, func_type, e.line);

            // Push an isolated function scope.
            push_function();
            ++function_depth_;

            // Inject the function name into its own scope for recursion.
            frames_.back().vars.emplace(id->name, func_type);

            // Declare parameters with NULL_TYPE (no type annotations).
            for (const auto& param : func->params) {
                frames_.back().vars.emplace(param, make_null_type());
            }

            // Forward-declare nested function assignments.
            forward_declare_functions(func->body);

            // Check the function body.
            check_stmts(func->body);

            --function_depth_;
            pop_function();

            annotate(*func, func_type);
            annotate(*id, func_type);
            annotate(e, func_type);
            return func_type;
        }
    }

    // General case: check the value expression first.
    TypeInfo val_type = check_expr(*e.value);

    if (auto* id = dynamic_cast<const IdentifierExpr*>(e.target.get())) {
        // Variable assignment.
        assign(id->name, val_type, e.line);
        annotate(*e.target, val_type);
        annotate(e, val_type);
        return val_type;
    }

    if (auto* idx = dynamic_cast<const IndexExpr*>(e.target.get())) {
        // Check the object expression of the index target.
        TypeInfo obj_type = check_expr(*idx->object);
        for (const auto& index : idx->indices) {
            check_expr(*index);
        }

        // Quantum amplitude write: register[x] = v.
        if (obj_type.type == JanusType::QNUM ||
            obj_type.type == JanusType::QUBIT) {
            // The target of a quantum amplitude write must be a plain
            // identifier at compile time.
            if (!dynamic_cast<const IdentifierExpr*>(idx->object.get())) {
                report_error(e.line);
            }
            // Right-hand side must be CNUM or QNUM.
            if (val_type.type != JanusType::CNUM &&
                val_type.type != JanusType::QNUM) {
                report_error(e.line);
            }
            TypeInfo ti = make_cnum_type();
            annotate(*idx, ti);
            annotate(e, ti);
            return ti;
        }

        // General index assignment: collection[i] = value or matrix[r,c] = value.
        annotate(*idx, val_type);
        annotate(e, val_type);
        return val_type;
    }

    // Assignment target must be an identifier or index expression.
    report_error(e.line);
}


// Binary operators


TypeInfo TypeChecker::check_binary(const BinaryExpr& e) {
    TypeInfo lhs_ti = check_expr(*e.left);
    TypeInfo rhs_ti = check_expr(*e.right);

    JanusType lt = lhs_ti.type;
    JanusType rt = rhs_ti.type;

    TypeInfo result;

    switch (e.op) {
        // Arithmetic operators.
        case TokenType::PLUS:
        case TokenType::MINUS:
        case TokenType::STAR:
        case TokenType::SLASH:
        case TokenType::DOUBLE_SLASH:
        case TokenType::PERCENT:
        case TokenType::CARET: {
            // For +, if either operand is LIST, result is LIST (concatenation).
            // For + with two CSTR operands and no list, the docs include
            // cstr in the allowed types list for addition; non-numeric types
            // are treated as 2's complement register values (result is CNUM).
            if (!is_arithmetic_allowed(lt) || !is_arithmetic_allowed(rt)) {
                report_error(e.line);
            }
            result = resolve_arithmetic_type(lt, rt, e.line);
            break;
        }

        // Comparison operators.
        case TokenType::LESS:
        case TokenType::GREATER:
        case TokenType::LESS_EQUAL:
        case TokenType::GREATER_EQUAL: {
            if (!is_comparison_allowed(lt) || !is_comparison_allowed(rt)) {
                report_error(e.line);
            }
            JanusType res = comparison_result_type(lt, rt);
            result.type = res;
            result.width = 1;
            break;
        }

        // Equality operator.
        case TokenType::EQUAL_EQUAL: {
            if (!is_equality_allowed(lt) || !is_equality_allowed(rt)) {
                report_error(e.line);
            }
            JanusType res = comparison_result_type(lt, rt);
            result.type = res;
            result.width = 1;
            break;
        }

        // Bitwise logic operators.
        case TokenType::KW_AND:
        case TokenType::KW_NAND:
        case TokenType::KW_OR:
        case TokenType::KW_NOR:
        case TokenType::KW_XOR:
        case TokenType::KW_XNOR: {
            if (!is_bitwise_allowed(lt) || !is_bitwise_allowed(rt)) {
                report_error(e.line);
            }
            auto opt = bitwise_result_type(lt, rt);
            if (!opt.has_value()) {
                // Fallback for CSTR operands: treat as CNUM.
                JanusType elt = (lt == JanusType::CSTR) ? JanusType::CNUM : lt;
                JanusType ert = (rt == JanusType::CSTR) ? JanusType::CNUM : rt;
                opt = bitwise_result_type(elt, ert);
                if (!opt.has_value()) {
                    report_error(e.line);
                }
            }
            result.type = opt.value();
            break;
        }

        // Tensor product.
        case TokenType::KW_TENSOR: {
            if (!is_tensor_allowed(lt) || !is_tensor_allowed(rt)) {
                report_error(e.line);
            }
            result = resolve_tensor_type(lt, rt);
            break;
        }

        default:
            report_error(e.line);
    }

    annotate(e, result);
    return result;
}


// Unary operators


TypeInfo TypeChecker::check_unary(const UnaryExpr& e) {
    TypeInfo operand_ti = check_expr(*e.operand);
    JanusType ot = operand_ti.type;

    TypeInfo result;

    switch (e.op) {
        // Unary negation.
        case TokenType::MINUS: {
            if (!is_arithmetic_allowed(ot)) {
                report_error(e.line);
            }
            // Unary negation preserves the type.
            // For CSTR, the result is CNUM since the string is treated
            // as a numeric register value.
            if (ot == JanusType::CSTR) {
                result = make_cnum_type();
            } else if (ot == JanusType::NULL_TYPE) {
                result = make_cnum_type();
            } else {
                result = operand_ti;
            }
            break;
        }

        // Bitwise not.
        case TokenType::KW_NOT: {
            if (!is_bitwise_allowed(ot)) {
                report_error(e.line);
            }
            auto opt = unary_bitwise_result_type(ot);
            if (!opt.has_value()) {
                // CSTR fallback.
                if (ot == JanusType::CSTR) {
                    result = make_cnum_type();
                } else {
                    report_error(e.line);
                }
            } else {
                result.type = opt.value();
            }
            break;
        }

        // Boolean negation (prefix !).
        case TokenType::BANG: {
            if (!is_bool_negation_allowed(ot)) {
                report_error(e.line);
            }
            result = resolve_bool_negation_type(ot);
            break;
        }

        // Left shift (<<) and right shift (>>).
        case TokenType::SHIFT_LEFT:
        case TokenType::SHIFT_RIGHT: {
            if (!is_bitwise_allowed(ot)) {
                report_error(e.line);
            }
            auto opt = unary_bitwise_result_type(ot);
            if (!opt.has_value()) {
                if (ot == JanusType::CSTR) {
                    result = make_cnum_type();
                } else {
                    report_error(e.line);
                }
            } else {
                result.type = opt.value();
            }
            break;
        }

        default:
            report_error(e.line);
    }

    annotate(e, result);
    return result;
}


// Postfix boolean negation (expr!)


TypeInfo TypeChecker::check_postfix_bang(const PostfixBangExpr& e) {
    TypeInfo operand_ti = check_expr(*e.operand);
    JanusType ot = operand_ti.type;

    if (!is_bool_negation_allowed(ot)) {
        report_error(e.line);
    }

    TypeInfo result = resolve_bool_negation_type(ot);
    annotate(e, result);
    return result;
}


// Type cast


TypeInfo TypeChecker::check_type_cast(const TypeCastExpr& e) {
    TypeInfo operand_ti = check_expr(*e.operand);

    auto target_opt = janus_type_from_keyword(e.target_type);
    if (!target_opt.has_value()) {
        report_error(e.line);
    }
    JanusType target = target_opt.value();

    if (!is_cast_allowed(target, operand_ti.type)) {
        report_error(e.line);
    }

    // Build a TypeInfo for the target type with default metadata.
    TypeInfo result;
    result.type = target;
    switch (target) {
        case JanusType::QUBIT:
            result = make_qubit_type();
            break;
        case JanusType::CBIT:
            result = make_cbit_type();
            break;
        case JanusType::QNUM:
            result = make_qnum_type();
            break;
        case JanusType::CNUM:
            result = make_cnum_type();
            break;
        case JanusType::CSTR:
            result = make_cstr_type();
            break;
        case JanusType::LIST:
            result = make_list_type();
            break;
        case JanusType::MATRIX:
            result = make_matrix_type();
            break;
        case JanusType::GATE:
            result = make_gate_type();
            break;
        case JanusType::CIRC:
            result = make_circ_type();
            break;
        case JanusType::BLOCK:
            result = make_block_type();
            break;
        case JanusType::FUNCTION:
            result = make_function_type();
            break;
        case JanusType::NULL_TYPE:
            // is_cast_allowed already rejects NULL_TYPE as target.
            report_error(e.line);
    }

    annotate(e, result);
    return result;
}


// Matrix literal


TypeInfo TypeChecker::check_matrix_literal(const MatrixLiteralExpr& e) {
    uint32_t rows = static_cast<uint32_t>(e.rows.size());
    uint32_t cols = 0;

    for (const auto& row : e.rows) {
        if (cols == 0) {
            cols = static_cast<uint32_t>(row.size());
        } else if (static_cast<uint32_t>(row.size()) != cols) {
            // All rows must have the same number of columns.
            report_error(e.line);
        }
        for (const auto& elem : row) {
            check_expr(*elem);
        }
    }

    TypeInfo ti = make_matrix_type(rows, cols);
    annotate(e, ti);
    return ti;
}


// User-defined function call


TypeInfo TypeChecker::check_call(const CallExpr& e) {
    TypeInfo callee_ti = check_expr(*e.callee);

    // Check all argument expressions.
    for (const auto& arg : e.args) {
        check_expr(*arg);
    }

    // If the callee is known to be a function, validate arity.
    if (callee_ti.type == JanusType::FUNCTION) {
        if (callee_ti.arity != 0 &&
            callee_ti.arity != static_cast<uint32_t>(e.args.size())) {
            report_error(e.line);
        }
    }

    // The return type of a user-defined function call is not known at
    // compile time since Janus has no return type annotations.
    // Use NULL_TYPE as a placeholder; the runtime determines the actual
    // return type.
    TypeInfo ti = make_null_type();
    annotate(e, ti);
    return ti;
}


// Gate library access


TypeInfo TypeChecker::check_gate_library(const GateLibraryExpr& e) {
    // Check all parameter expressions (for parameterised gates).
    for (const auto& arg : e.args) {
        TypeInfo arg_ti = check_expr(*arg);
        // Gate parameters must be numeric.
        JanusType at = arg_ti.type;
        if (!is_numeric(at) && at != JanusType::NULL_TYPE) {
            report_error(e.line);
        }
    }

    // Determine the qubit width of the gate from its name.
    uint32_t width = 1;
    const std::string& gn = e.gate_name;

    // Two-qubit gates.
    if (gn == "cnot" || gn == "cy" || gn == "cz" || gn == "ch" ||
        gn == "swap" || gn == "iswap" ||
        gn == "crx" || gn == "cry" || gn == "crz" || gn == "cp" ||
        gn == "xx" || gn == "yy" || gn == "zz") {
        width = 2;
    }
    // Three-qubit gates.
    else if (gn == "toffoli" || gn == "cswap") {
        width = 3;
    }
    // Single-qubit gates (i, x, y, z, h, s, sdg, t, tdg, sx, sxdg,
    // rx, ry, rz, p, u) default to width = 1.

    TypeInfo ti = make_gate_type(width);
    annotate(e, ti);
    return ti;
}


// Type constructor


TypeInfo TypeChecker::check_type_construct(const TypeConstructExpr& e) {
    // Check all argument expressions.
    for (const auto& arg : e.args) {
        check_expr(*arg);
    }

    auto type_opt = janus_type_from_keyword(e.type_keyword);
    if (!type_opt.has_value()) {
        report_error(e.line);
    }
    JanusType constructed = type_opt.value();

    TypeInfo ti;
    switch (constructed) {
        case JanusType::QUBIT:
            ti = make_qubit_type();
            break;
        case JanusType::CBIT:
            ti = make_cbit_type();
            break;
        case JanusType::QNUM:
            ti = make_qnum_type();
            break;
        case JanusType::CNUM:
            ti = make_cnum_type();
            break;
        case JanusType::CSTR:
            ti = make_cstr_type();
            break;
        case JanusType::LIST:
            ti = make_list_type();
            break;
        case JanusType::MATRIX:
            ti = make_matrix_type();
            break;
        case JanusType::GATE:
            ti = make_gate_type();
            break;
        case JanusType::CIRC:
            ti = make_circ_type();
            break;
        case JanusType::BLOCK:
            ti = make_block_type();
            break;
        case JanusType::FUNCTION:
            // function() as a type constructor is not valid syntax;
            // functions are created via function(params) { body }.
            report_error(e.line);
        case JanusType::NULL_TYPE:
            report_error(e.line);
    }

    annotate(e, ti);
    return ti;
}


// Function expression


TypeInfo TypeChecker::check_function_expr(const FunctionExpr& e) {
    // Push an isolated function scope.
    push_function();
    ++function_depth_;

    // Declare parameters with NULL_TYPE since Janus has no parameter
    // type annotations; the actual types are determined at runtime.
    for (const auto& param : e.params) {
        frames_.back().vars.emplace(param, make_null_type());
    }

    // Forward-declare nested function assignments.
    forward_declare_functions(e.body);

    // Check the body.
    check_stmts(e.body);

    // Pop the function scope.
    --function_depth_;
    pop_function();

    TypeInfo ti = make_function_type(
        static_cast<uint32_t>(e.params.size()));
    annotate(e, ti);
    return ti;
}


// Built-in operand calls


TypeInfo TypeChecker::check_builtin_call(const BuiltinCallExpr& e) {
    // Check all argument expressions first.
    std::vector<TypeInfo> arg_types;
    arg_types.reserve(e.args.size());
    for (const auto& arg : e.args) {
        arg_types.push_back(check_expr(*arg));
    }

    TypeInfo result;

    switch (e.op) {

        // measure(Register) or measure(Register, Basis)
        case TokenType::KW_MEASURE: {
            if (e.args.size() < 1 || e.args.size() > 2) {
                report_error(e.line);
            }
            JanusType reg_type = arg_types[0].type;
            // Result is the classical counterpart of the register type.
            result.type = classical_counterpart(reg_type);
            if (result.type == JanusType::NULL_TYPE) {
                result.type = JanusType::CNUM;
            }
            result.width = 1;
            break;
        }

        // peek(Register)
        case TokenType::KW_PEEK: {
            if (e.args.size() != 1) {
                report_error(e.line);
            }
            JanusType reg_type = arg_types[0].type;
            result.type = classical_counterpart(reg_type);
            if (result.type == JanusType::NULL_TYPE) {
                result.type = JanusType::CNUM;
            }
            result.width = 1;
            break;
        }

        // state(Register)
        case TokenType::KW_STATE: {
            if (e.args.size() != 1) {
                report_error(e.line);
            }
            // Returns the state vector; represented as LIST at compile time.
            result = make_list_type();
            break;
        }

        // expect(Matrix, Register)
        case TokenType::KW_EXPECT: {
            if (e.args.size() != 2) {
                report_error(e.line);
            }
            // Expectation value is a real number.
            result = make_cnum_type();
            break;
        }

        // ctrle(Gate, ControlQubits)
        case TokenType::KW_CTRLE: {
            if (e.args.size() != 2) {
                report_error(e.line);
            }
            // Result is a gate with increased qubit width.
            result = make_gate_type();
            break;
        }

        // run(Circuit, ...)
        case TokenType::KW_RUN: {
            if (e.args.size() < 1) {
                report_error(e.line);
            }
            // Returns resulting register; type depends on circuit.
            // Conservative compile-time result: CNUM.
            result = make_cnum_type();
            break;
        }

        // runh(Circuit, ...) or runh(Shots, Circuit, ...)
        case TokenType::KW_RUNH: {
            if (e.args.size() < 1) {
                report_error(e.line);
            }
            // Returns a histogram; represented as LIST at compile time.
            result = make_list_type();
            break;
        }

        // isunitary(Matrix)
        case TokenType::KW_ISUNITARY: {
            if (e.args.size() != 1) {
                report_error(e.line);
            }
            result = make_cbit_type();
            break;
        }

        // sameoutput(Circ1, Circ2)
        case TokenType::KW_SAMEOUTPUT: {
            if (e.args.size() != 2) {
                report_error(e.line);
            }
            result = make_cbit_type();
            break;
        }

        // print(...)
        case TokenType::KW_PRINT: {
            if (e.args.size() < 1) {
                report_error(e.line);
            }
            // Returns the printed string.
            result = make_cstr_type();
            break;
        }

        // delete(Collection, Index) or delete(Collection, Element)
        case TokenType::KW_DELETE: {
            if (e.args.size() != 2) {
                report_error(e.line);
            }
            // Result type matches the collection type.
            result = arg_types[0];
            break;
        }

        // sin(x)
        case TokenType::KW_SIN: {
            if (e.args.size() != 1) {
                report_error(e.line);
            }
            result = make_cnum_type();
            break;
        }

        // cos(x)
        case TokenType::KW_COS: {
            if (e.args.size() != 1) {
                report_error(e.line);
            }
            result = make_cnum_type();
            break;
        }

        // numberofgates(Circuit)
        case TokenType::KW_NUMBEROFGATES: {
            if (e.args.size() != 1) {
                report_error(e.line);
            }
            result = make_cnum_type();
            break;
        }

        // det(Matrix)
        case TokenType::KW_DET: {
            if (e.args.size() != 1) {
                report_error(e.line);
            }
            // Determinant is a complex scalar.
            result = make_cnum_type(true);
            break;
        }

        // transpose(Matrix)
        case TokenType::KW_TRANSPOSE: {
            if (e.args.size() != 1) {
                report_error(e.line);
            }
            // Result is a matrix with transposed dimensions.
            result = make_matrix_type(arg_types[0].matrix_cols,
                                      arg_types[0].matrix_rows);
            break;
        }

        // transposec(Matrix) (conjugate transpose)
        case TokenType::KW_TRANSPOSEC: {
            if (e.args.size() != 1) {
                report_error(e.line);
            }
            result = make_matrix_type(arg_types[0].matrix_cols,
                                      arg_types[0].matrix_rows);
            break;
        }

        // evals(Matrix) (eigenvalues)
        case TokenType::KW_EVALS: {
            if (e.args.size() != 1) {
                report_error(e.line);
            }
            // Eigenvalues returned as a list.
            result = make_list_type();
            break;
        }

        // evecs(Matrix) (eigenvectors)
        case TokenType::KW_EVECS: {
            if (e.args.size() != 1) {
                report_error(e.line);
            }
            // Eigenvectors returned as a matrix.
            result = make_matrix_type();
            break;
        }

        // gates(Circuit) (list of gates)
        case TokenType::KW_GATES: {
            if (e.args.size() != 1) {
                report_error(e.line);
            }
            result = make_list_type();
            break;
        }

        // qubits(Circuit) (qubit count)
        case TokenType::KW_QUBITS: {
            if (e.args.size() != 1) {
                report_error(e.line);
            }
            result = make_cnum_type();
            break;
        }

        // depth(Circuit) (circuit depth)
        case TokenType::KW_DEPTH: {
            if (e.args.size() != 1) {
                report_error(e.line);
            }
            result = make_cnum_type();
            break;
        }

        // bitlength(Register) (qubit count of qnum or qubit)
        case TokenType::KW_BITLENGTH: {
            if (e.args.size() != 1) {
                report_error(e.line);
            }
            JanusType arg_t = arg_types[0].type;
            if (arg_t != JanusType::QNUM && arg_t != JanusType::QUBIT) {
                report_error(e.line);
            }
            result = make_cnum_type();
            break;
        }

        default:
            report_error(e.line);
    }

    annotate(e, result);
    return result;
}


// Control flow expressions


TypeInfo TypeChecker::check_if(const IfExpr& e) {
    // Check the condition (any type is valid; truthiness is evaluated).
    check_expr(*e.condition);

    // Then body in its own block scope.
    push_block();
    check_stmts(e.then_body);
    pop_block();

    // Else-if branches.
    for (const auto& eif : e.else_ifs) {
        check_expr(*eif.condition);
        push_block();
        check_stmts(eif.body);
        pop_block();
    }

    // Else branch.
    if (!e.else_body.empty()) {
        push_block();
        check_stmts(e.else_body);
        pop_block();
    }

    // If expression returns CBIT (boolean indicating which branch ran).
    TypeInfo ti = make_cbit_type();
    annotate(e, ti);
    return ti;
}


TypeInfo TypeChecker::check_for(const ForExpr& e) {
    // The init must be an assignment to create the loop variable.
    auto* init_assign = dynamic_cast<const AssignExpr*>(e.init.get());
    if (!init_assign) {
        report_error(e.line);
    }
    auto* init_id = dynamic_cast<const IdentifierExpr*>(
        init_assign->target.get());
    if (!init_id) {
        report_error(e.line);
    }

    // Check the init value expression.
    TypeInfo init_val_type = check_expr(*init_assign->value);

    // Declare the loop variable in the ENCLOSING (current) scope frame.
    // This makes it persist after the loop body scope is popped.
    declare_loop_variable(init_id->name, init_val_type, e.line);

    // Annotate the init expressions.
    annotate(*init_id, init_val_type);
    annotate(*init_assign, init_val_type);

    // Push a block scope for the loop body.
    push_block();
    ++loop_depth_;

    // Check the condition expression (any type; truthiness evaluated).
    check_expr(*e.condition);

    // Check the body statements.
    check_stmts(e.body);

    // Check the update expression.
    check_expr(*e.update);

    // Pop the loop body scope.
    --loop_depth_;
    pop_block();

    // For expression returns CNUM (iterations completed + 1, or 0).
    TypeInfo ti = make_cnum_type();
    annotate(e, ti);
    return ti;
}


TypeInfo TypeChecker::check_while(const WhileExpr& e) {
    // Check the condition (any type; truthiness evaluated).
    check_expr(*e.condition);

    // Push a block scope for the loop body.
    push_block();
    ++loop_depth_;

    check_stmts(e.body);

    --loop_depth_;
    pop_block();

    // While expression returns CNUM (iterations completed + 1, or 0).
    TypeInfo ti = make_cnum_type();
    annotate(e, ti);
    return ti;
}


TypeInfo TypeChecker::check_foreach(const ForeachExpr& e) {
    // Check the collection expression.
    TypeInfo coll_type = check_expr(*e.collection);

    // Determine the element type from the collection.
    // For LIST: elements are heterogeneous; compile-time type is NULL_TYPE.
    // For MATRIX: elements are complex scalars (CNUM).
    // For CIRC/BLOCK: elements are gates (GATE).
    // For GATE: elements are complex scalars (CNUM).
    // For other types: NULL_TYPE (defer to runtime).
    TypeInfo elem_type;
    switch (coll_type.type) {
        case JanusType::MATRIX:
            elem_type = make_cnum_type(true);
            break;
        case JanusType::GATE:
            elem_type = make_cnum_type(true);
            break;
        case JanusType::CIRC:
        case JanusType::BLOCK:
            elem_type = make_gate_type();
            break;
        default:
            elem_type = make_null_type();
            break;
    }

    // Declare the element variable as a loop variable in the enclosing
    // (current) scope frame so that it persists after the loop.
    declare_loop_variable(e.element, elem_type, e.line);

    // Check optional clauses.
    if (e.where_cond) {
        check_expr(*e.where_cond);
    }
    if (e.from_bound) {
        check_expr(*e.from_bound);
    }
    if (e.to_bound) {
        check_expr(*e.to_bound);
    }

    // Push a block scope for the loop body.
    push_block();
    ++loop_depth_;

    check_stmts(e.body);

    --loop_depth_;
    pop_block();

    // Foreach expression returns CNUM (iterations completed + 1, or 0).
    TypeInfo ti = make_cnum_type();
    annotate(e, ti);
    return ti;
}


// Operator type validation helpers


bool TypeChecker::is_arithmetic_allowed(JanusType t) {
    // Beta allowed types for arithmetic: qubit, cbit, qnum, cnum, cstr,
    // list, matrix.  NULL_TYPE is allowed (null == 0).
    switch (t) {
        case JanusType::QUBIT:
        case JanusType::CBIT:
        case JanusType::QNUM:
        case JanusType::CNUM:
        case JanusType::CSTR:
        case JanusType::LIST:
        case JanusType::MATRIX:
        case JanusType::NULL_TYPE:
            return true;
        default:
            return false;
    }
}

bool TypeChecker::is_comparison_allowed(JanusType t) {
    // Beta allowed operand types for < > <= >=:
    // qubit, cbit, qnum, cnum, cstr, list, matrix, block, circ, gate.
    switch (t) {
        case JanusType::QUBIT:
        case JanusType::CBIT:
        case JanusType::QNUM:
        case JanusType::CNUM:
        case JanusType::CSTR:
        case JanusType::LIST:
        case JanusType::MATRIX:
        case JanusType::BLOCK:
        case JanusType::CIRC:
        case JanusType::GATE:
        case JanusType::NULL_TYPE:
            return true;
        default:
            return false;
    }
}

bool TypeChecker::is_equality_allowed(JanusType t) {
    // Beta allowed operand types for ==:
    // qubit, cbit, qnum, cnum, list, matrix, block, circ, gate.
    switch (t) {
        case JanusType::QUBIT:
        case JanusType::CBIT:
        case JanusType::QNUM:
        case JanusType::CNUM:
        case JanusType::LIST:
        case JanusType::MATRIX:
        case JanusType::BLOCK:
        case JanusType::CIRC:
        case JanusType::GATE:
        case JanusType::NULL_TYPE:
            return true;
        default:
            return false;
    }
}

bool TypeChecker::is_bitwise_allowed(JanusType t) {
    // Beta allowed types for bitwise ops: qubit, cbit, qnum, cnum, cstr,
    // list, matrix.
    switch (t) {
        case JanusType::QUBIT:
        case JanusType::CBIT:
        case JanusType::QNUM:
        case JanusType::CNUM:
        case JanusType::CSTR:
        case JanusType::LIST:
        case JanusType::MATRIX:
        case JanusType::NULL_TYPE:
            return true;
        default:
            return false;
    }
}

bool TypeChecker::is_bool_negation_allowed(JanusType t) {
    // Beta allowed types for boolean negation (! prefix/postfix):
    // qubit, cbit, qnum, cnum, cstr, list, matrix.
    switch (t) {
        case JanusType::QUBIT:
        case JanusType::CBIT:
        case JanusType::QNUM:
        case JanusType::CNUM:
        case JanusType::CSTR:
        case JanusType::LIST:
        case JanusType::MATRIX:
        case JanusType::NULL_TYPE:
            return true;
        default:
            return false;
    }
}

bool TypeChecker::is_tensor_allowed(JanusType t) {
    // Beta allowed operand types for tensor product:
    // qubit, cbit, qnum, cnum, cstr, list, matrix.
    switch (t) {
        case JanusType::QUBIT:
        case JanusType::CBIT:
        case JanusType::QNUM:
        case JanusType::CNUM:
        case JanusType::CSTR:
        case JanusType::LIST:
        case JanusType::MATRIX:
        case JanusType::NULL_TYPE:
            return true;
        default:
            return false;
    }
}


// Result type resolution helpers


TypeInfo TypeChecker::resolve_arithmetic_type(JanusType lhs, JanusType rhs,
                                               uint32_t line) {
    // Extend arithmetic_result_type to handle CSTR by promoting it to
    // CNUM.  Non-numeric types in arithmetic are treated as their 2's
    // complement register values, which are numeric.
    JanusType eff_lhs = lhs;
    JanusType eff_rhs = rhs;

    if (eff_lhs == JanusType::CSTR) eff_lhs = JanusType::CNUM;
    if (eff_rhs == JanusType::CSTR) eff_rhs = JanusType::CNUM;

    auto opt = arithmetic_result_type(eff_lhs, eff_rhs);
    if (!opt.has_value()) {
        report_error(line);
    }

    TypeInfo ti;
    ti.type = opt.value();
    return ti;
}

TypeInfo TypeChecker::resolve_bool_negation_type(JanusType operand) {
    // Boolean negation returns a 1-bit value.
    // If the operand is quantum, the result is QUBIT.
    // Otherwise the result is CBIT.
    if (is_quantum(operand)) {
        return make_qubit_type();
    }
    return make_cbit_type();
}

TypeInfo TypeChecker::resolve_tensor_type(JanusType lhs, JanusType rhs) {
    // Tensor product result type:
    // If either operand is a list, result is LIST.
    if (lhs == JanusType::LIST || rhs == JanusType::LIST) {
        return make_list_type();
    }
    // If either operand is a matrix, result is MATRIX.
    if (lhs == JanusType::MATRIX || rhs == JanusType::MATRIX) {
        return make_matrix_type();
    }
    // For gate operands (not in operand types for tensor in Beta, but
    // gate is a matrix-adjacent type), result is GATE.
    if (lhs == JanusType::GATE || rhs == JanusType::GATE) {
        return make_gate_type();
    }
    // If both operands are quantum, the result is QNUM.
    if (is_quantum(lhs) && is_quantum(rhs)) {
        return make_qnum_type();
    }
    // If either operand is quantum, the result is QNUM.
    if (is_quantum(lhs) || is_quantum(rhs)) {
        return make_qnum_type();
    }
    // Both operands are classical scalars; result is CNUM.
    return make_cnum_type();
}


} // namespace janus
