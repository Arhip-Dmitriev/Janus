#include "backend_quest.hpp"
#include "gate_library.hpp"
#include "circuit_synthesiser.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace janus {

// Threshold below which a double is considered zero. 
static constexpr double ZERO_TOL = 1.0e-15;


// Construction


BackendQuEST::BackendQuEST(bool precision_32)
    : p32_(precision_32) {}


// Public interface


int BackendQuEST::execute(const IRProgram& program) {
    forward_declare_functions(program.statements);

    try {
        exec_stmts(program.statements);
    } catch (ReturnSignal& rs) {
        if (rs.has_value) {
            JanusType rt = rs.value.type_info.type;
            if (rt == JanusType::CNUM || rt == JanusType::CBIT ||
                rt == JanusType::NULL_TYPE) {
                int64_t code = rs.value.as_integer(0);
                return static_cast<int>(code % 256);
            }
            // Top-level return with non-integer is an error.
            report_error(0);
        }
        return 0;
    }

    return 0;
}


// Forward declaration pre-pass


void BackendQuEST::forward_declare_functions(
        const std::vector<IRStmtPtr>& stmts) {
    for (const auto& stmt : stmts) {
        auto* expr_stmt = dynamic_cast<const IRExprStmt*>(stmt.get());
        if (!expr_stmt) continue;

        auto* assign = dynamic_cast<const IRAssign*>(expr_stmt->expr.get());
        if (!assign) continue;

        auto* var = dynamic_cast<const IRVariable*>(assign->target.get());
        if (!var) continue;

        auto* func_def = dynamic_cast<const IRFunctionDef*>(assign->value.get());
        if (!func_def) continue;

        if (!scope_.exists_in_current_frame(var->name)) {
            // Store the IR body pointer through FunctionData::body which
            // is typed as const vector<unique_ptr<Stmt>>*.  The IR uses
            // vector<unique_ptr<IRStmt>> instead; we store the address
            // via reinterpret_cast and always read it back through the
            // matching reinterpret_cast in eval_call.  Both vector
            // instantiations have identical layout, and the pointer is
            // never dereferenced through the Stmt* type.
            auto fval = JanusValue::make_function(
                func_def->params,
                reinterpret_cast<const std::vector<std::unique_ptr<Stmt>>*>(
                    &func_def->body));
            scope_.assign(var->name, std::move(fval), stmt->line);
        }
    }
}


// Statement execution


void BackendQuEST::exec_stmts(const std::vector<IRStmtPtr>& stmts) {
    for (const auto& stmt : stmts) {
        exec_stmt(*stmt);
    }
}


void BackendQuEST::exec_stmt(const IRStmt& stmt) {
    set_error_line(stmt.line);

    if (auto* es = dynamic_cast<const IRExprStmt*>(&stmt)) {
        eval_expr(*es->expr);
        return;
    }
    if (dynamic_cast<const IRBreakStmt*>(&stmt)) {
        throw BreakSignal{};
    }
    if (dynamic_cast<const IRContinueStmt*>(&stmt)) {
        throw ContinueSignal{};
    }
    if (auto* rs = dynamic_cast<const IRReturnStmt*>(&stmt)) {
        ReturnSignal sig;
        if (rs->value) {
            sig.value = eval_expr(*rs->value);
            sig.has_value = true;
        }
        throw sig;
    }

    report_error(stmt.line);
}


// Expression evaluation dispatch


JanusValue BackendQuEST::eval_expr(const IRExpr& expr) {
    set_error_line(expr.line);

    if (auto* e = dynamic_cast<const IRIntegerLiteral*>(&expr))
        return eval_integer_literal(*e);
    if (auto* e = dynamic_cast<const IRFloatLiteral*>(&expr))
        return eval_float_literal(*e);
    if (auto* e = dynamic_cast<const IRStringLiteral*>(&expr))
        return eval_string_literal(*e);
    if (auto* e = dynamic_cast<const IRBoolLiteral*>(&expr))
        return eval_bool_literal(*e);
    if (auto* e = dynamic_cast<const IRNullLiteral*>(&expr))
        return eval_null_literal(*e);
    if (auto* e = dynamic_cast<const IRPiLiteral*>(&expr))
        return eval_pi_literal(*e);
    if (auto* e = dynamic_cast<const IRELiteral*>(&expr))
        return eval_e_literal(*e);
    if (auto* e = dynamic_cast<const IRInterpolatedString*>(&expr))
        return eval_interpolated_string(*e);
    if (auto* e = dynamic_cast<const IRVariable*>(&expr))
        return eval_variable(*e);
    if (auto* e = dynamic_cast<const IRIndex*>(&expr))
        return eval_index(*e);

   // Quantum amplitude read: qnum_or_qubit[index].
    if (auto* e = dynamic_cast<const IRQnumIndex*>(&expr)) {
        JanusValue obj = eval_expr(*e->object);
        JanusValue idx_val = eval_expr(*e->index);
        if (!obj.quantum_val) report_error(e->line);
        uint32_t nq = obj.quantum_val->num_qubits();
        uint64_t basis_index = 0;
        if (idx_val.type_info.type == JanusType::CNUM) {
            if (idx_val.real_val < 0) report_error(e->line);
            basis_index = static_cast<uint64_t>(idx_val.real_val);
        } else if (idx_val.type_info.type == JanusType::CSTR) {
            const std::string& bstr = idx_val.str_val;
            if (bstr.empty()) report_error(e->line);
            bool all_binary = true;
            for (char ch : bstr) {
                if (ch != '0' && ch != '1') { all_binary = false; break; }
            }
            if (all_binary) {
                basis_index = 0;
                for (char ch : bstr) {
                    basis_index = (basis_index << 1) |
                                  static_cast<uint64_t>(ch == '1' ? 1 : 0);
                }
            } else {
                const char* first = bstr.data();
                const char* last = bstr.data() + bstr.size();
                auto res = std::from_chars(first, last, basis_index);
                if (res.ec != std::errc() || res.ptr != last)
                    report_error(e->line);
            }
        } else {
            report_error(e->line);
        }
        uint64_t dim = uint64_t{1} << nq;
        if (basis_index >= dim) report_error(e->line);
        std::complex<double> amp = obj.quantum_val->amplitude(basis_index);
        return JanusValue::make_cnum_complex(
            static_cast<float>(amp.real()),
            static_cast<float>(amp.imag()));
    }

    // Quantum amplitude write: qnum_or_qubit[index] = value.
    if (auto* e = dynamic_cast<const IRQnumIndexAssign*>(&expr)) {
        // Resolve the basis index from the index expression.
        JanusValue idx_val = eval_expr(*e->index);

        // The object is always an IRVariable (enforced by the type checker).
        auto* target_var = dynamic_cast<const IRVariable*>(e->object.get());
        if (!target_var) report_error(e->line);

        JanusValue* target_ptr = scope_.lookup(target_var->name);
        if (!target_ptr) report_error(e->line);
        if (!target_ptr->quantum_val) report_error(e->line);

        uint32_t nq = target_ptr->quantum_val->num_qubits();
        uint64_t basis_index = 0;
        if (idx_val.type_info.type == JanusType::CNUM) {
            if (idx_val.real_val < 0) report_error(e->line);
            basis_index = static_cast<uint64_t>(idx_val.real_val);
        } else if (idx_val.type_info.type == JanusType::CSTR) {
            const std::string& bstr = idx_val.str_val;
            if (bstr.empty()) report_error(e->line);
            bool all_binary = true;
            for (char ch : bstr) {
                if (ch != '0' && ch != '1') { all_binary = false; break; }
            }
            if (all_binary) {
                basis_index = 0;
                for (char ch : bstr) {
                    basis_index = (basis_index << 1) |
                                  static_cast<uint64_t>(ch == '1' ? 1 : 0);
                }
            } else {
                const char* first = bstr.data();
                const char* last = bstr.data() + bstr.size();
                auto res = std::from_chars(first, last, basis_index);
                if (res.ec != std::errc() || res.ptr != last)
                    report_error(e->line);
            }
        } else {
            report_error(e->line);
        }
        uint64_t dim = uint64_t{1} << nq;
        if (basis_index >= dim) report_error(e->line);

        // Evaluate the value to assign.
        JanusValue val = eval_expr(*e->value);

        std::complex<double> scalar_amp;
        if (val.type_info.type == JanusType::QNUM ||
            val.type_info.type == JanusType::QUBIT) {
            // Quantum value: measure to obtain a classical outcome,
            // consistent with Janus rule that assigning quantum into
            // classical context causes measurement.
            if (!val.quantum_val) report_error(e->line);
            uint64_t outcome = val.quantum_val->measure(e->line);
            scalar_amp = std::complex<double>(
                static_cast<double>(outcome), 0.0);
        } else if (val.type_info.type == JanusType::CNUM) {
            scalar_amp = std::complex<double>(val.real_val, val.imag_val);
        } else {
            report_error(e->line);
        }

        // Copy the target value, modify the amplitude, normalise, and
        // write back to the scope.
        JanusValue target_copy = *target_ptr;
        target_copy.quantum_val->amplitude(basis_index) = scalar_amp;
        target_copy.quantum_val->normalise(e->line);
        scope_.assign(target_var->name, std::move(target_copy), e->line);

        // Return a CNUM representing the amplitude that was written
        // (before renormalisation).
        return JanusValue::make_cnum_complex(
            static_cast<float>(scalar_amp.real()),
            static_cast<float>(scalar_amp.imag()));
    }

    if (auto* e = dynamic_cast<const IRAssign*>(&expr))
        return eval_assign(*e);
    if (auto* e = dynamic_cast<const IRBinary*>(&expr))
        return eval_binary(*e);
    if (auto* e = dynamic_cast<const IRUnary*>(&expr))
        return eval_unary(*e);
    if (auto* e = dynamic_cast<const IRPostfixBang*>(&expr))
        return eval_postfix_bang(*e);
    if (auto* e = dynamic_cast<const IRTypeCast*>(&expr))
        return eval_type_cast(*e);
    if (auto* e = dynamic_cast<const IRMatrixLiteral*>(&expr))
        return eval_matrix_literal(*e);
    if (auto* e = dynamic_cast<const IRCall*>(&expr))
        return eval_call(*e);
    if (auto* e = dynamic_cast<const IRBuiltinCall*>(&expr))
        return eval_builtin_call(*e);
    if (auto* e = dynamic_cast<const IRGateLibrary*>(&expr))
        return eval_gate_library(*e);
    if (auto* e = dynamic_cast<const IRTypeConstruct*>(&expr))
        return eval_type_construct(*e);
    if (auto* e = dynamic_cast<const IRFunctionDef*>(&expr))
        return eval_function_def(*e);
    if (auto* e = dynamic_cast<const IRIf*>(&expr))
        return eval_if(*e);
    if (auto* e = dynamic_cast<const IRFor*>(&expr))
        return eval_for(*e);
    if (auto* e = dynamic_cast<const IRWhile*>(&expr))
        return eval_while(*e);
    if (auto* e = dynamic_cast<const IRForeach*>(&expr))
        return eval_foreach(*e);

    report_error(expr.line);
}


// Literal evaluators


JanusValue BackendQuEST::eval_integer_literal(const IRIntegerLiteral& e) {
    return JanusValue::make_cnum(static_cast<double>(e.value));
}

JanusValue BackendQuEST::eval_float_literal(const IRFloatLiteral& e) {
    return JanusValue::make_cnum(e.value);
}

JanusValue BackendQuEST::eval_string_literal(const IRStringLiteral& e) {
    return JanusValue::make_cstr(e.value);
}

JanusValue BackendQuEST::eval_bool_literal(const IRBoolLiteral& e) {
    return JanusValue::make_cbit(e.value ? 1 : 0);
}

JanusValue BackendQuEST::eval_null_literal(const IRNullLiteral&) {
    return JanusValue::make_null();
}

JanusValue BackendQuEST::eval_pi_literal(const IRPiLiteral&) {
    return JanusValue::make_cnum(std::numbers::pi);
}

JanusValue BackendQuEST::eval_e_literal(const IRELiteral&) {
    return JanusValue::make_cnum(std::numbers::e);
}


// Interpolated string


JanusValue BackendQuEST::eval_interpolated_string(
        const IRInterpolatedString& e) {
    std::string result;
    for (std::size_t i = 0; i < e.segments.size(); ++i) {
        result += e.segments[i];
        if (i < e.expressions.size()) {
            JanusValue val = eval_expr(*e.expressions[i]);
            result += val.to_string();
        }
    }
    return JanusValue::make_cstr(std::move(result));
}


// Variable lookup


JanusValue BackendQuEST::eval_variable(const IRVariable& e) {
    JanusValue* found = scope_.lookup(e.name);
    if (!found) {
        report_error(e.line);
    }
    return *found;
}


// Index access


JanusValue BackendQuEST::eval_index(const IRIndex& e) {
    JanusValue obj = eval_expr(*e.object);

    if (obj.type_info.type == JanusType::LIST) {
        if (e.indices.size() != 1) report_error(e.line);
        JanusValue idx_val = eval_expr(*e.indices[0]);
        int64_t idx = to_integer(idx_val, e.line);
        if (!obj.list_data) report_error(e.line);
        if (idx < 0) idx += static_cast<int64_t>(obj.list_data->size());
        if (idx < 0 || static_cast<uint64_t>(idx) >= obj.list_data->size())
            report_error(e.line);
        return (*obj.list_data)[static_cast<std::size_t>(idx)];
    }

    if (obj.type_info.type == JanusType::MATRIX ||
        obj.type_info.type == JanusType::GATE) {
        if (e.indices.size() != 2) report_error(e.line);
        JanusValue r_val = eval_expr(*e.indices[0]);
        JanusValue c_val = eval_expr(*e.indices[1]);
        int64_t r = to_integer(r_val, e.line);
        int64_t c = to_integer(c_val, e.line);
        uint32_t rows = obj.type_info.matrix_rows;
        uint32_t cols = obj.type_info.matrix_cols;
        if (r < 0 || static_cast<uint32_t>(r) >= rows) report_error(e.line);
        if (c < 0 || static_cast<uint32_t>(c) >= cols) report_error(e.line);
        if (!obj.matrix_data) report_error(e.line);
        auto elem = (*obj.matrix_data)[static_cast<uint32_t>(r) * cols +
                                       static_cast<uint32_t>(c)];
        return JanusValue::make_cnum_complex(
            static_cast<float>(elem.real()),
            static_cast<float>(elem.imag()));
    }

    if (obj.type_info.type == JanusType::CSTR) {
        if (e.indices.size() != 1) report_error(e.line);
        JanusValue idx_val = eval_expr(*e.indices[0]);
        int64_t idx = to_integer(idx_val, e.line);
        if (idx < 0) idx += static_cast<int64_t>(obj.str_val.size());
        if (idx < 0 || static_cast<uint64_t>(idx) >= obj.str_val.size())
            report_error(e.line);
        return JanusValue::make_cstr(
            std::string(1, obj.str_val[static_cast<std::size_t>(idx)]));
    }

    report_error(e.line);
}


// Assignment


JanusValue BackendQuEST::eval_assign(const IRAssign& e) {
    // Simple variable assignment.
    if (auto* var = dynamic_cast<const IRVariable*>(e.target.get())) {
        JanusValue val = eval_expr(*e.value);
        JanusValue result = val;
        scope_.assign(var->name, std::move(val), e.line);
        return result;
    }

    // Indexed assignment: collection[i] = val or matrix[r,c] = val.
    if (auto* idx = dynamic_cast<const IRIndex*>(e.target.get())) {
        // Resolve the target variable name from the index object.
        auto* target_var = dynamic_cast<const IRVariable*>(idx->object.get());
        if (!target_var) report_error(e.line);

        JanusValue* target_ptr = scope_.lookup(target_var->name);
        if (!target_ptr) report_error(e.line);

        JanusValue val = eval_expr(*e.value);

        if (target_ptr->type_info.type == JanusType::LIST) {
            if (idx->indices.size() != 1) report_error(e.line);
            JanusValue idx_val = eval_expr(*idx->indices[0]);
            int64_t i = to_integer(idx_val, e.line);
            if (!target_ptr->list_data) report_error(e.line);
            auto& list = *target_ptr->list_data;
            if (i < 0) i += static_cast<int64_t>(list.size());
            if (i < 0 || static_cast<uint64_t>(i) >= list.size())
                report_error(e.line);
            list[static_cast<std::size_t>(i)] = val;
            return val;
        }

        if (target_ptr->type_info.type == JanusType::MATRIX ||
            target_ptr->type_info.type == JanusType::GATE) {
            if (idx->indices.size() != 2) report_error(e.line);
            JanusValue r_val = eval_expr(*idx->indices[0]);
            JanusValue c_val = eval_expr(*idx->indices[1]);
            int64_t r = to_integer(r_val, e.line);
            int64_t c = to_integer(c_val, e.line);
            uint32_t rows = target_ptr->type_info.matrix_rows;
            uint32_t cols = target_ptr->type_info.matrix_cols;
            if (r < 0 || static_cast<uint32_t>(r) >= rows) report_error(e.line);
            if (c < 0 || static_cast<uint32_t>(c) >= cols) report_error(e.line);
            if (!target_ptr->matrix_data) report_error(e.line);
            double re = val.real_val;
            double im = val.imag_val;
            if (val.type_info.type == JanusType::QUBIT ||
                val.type_info.type == JanusType::QNUM) {
                re = static_cast<double>(to_integer(val, e.line));
                im = 0.0;
            }
            (*target_ptr->matrix_data)[
                static_cast<uint32_t>(r) * cols + static_cast<uint32_t>(c)]
                = {re, im};
            return val;
        }

        report_error(e.line);
    }

    report_error(e.line);
}


// Binary operators


JanusValue BackendQuEST::eval_binary(const IRBinary& e) {
    JanusValue lhs = eval_expr(*e.left);
    JanusValue rhs = eval_expr(*e.right);
    JanusType rt = e.result_type.type;

    switch (e.op) {
        case IRBinaryOp::ADD:     return binary_add(lhs, rhs, rt, e.line);
        case IRBinaryOp::SUB:     return binary_sub(lhs, rhs, rt, e.line);
        case IRBinaryOp::MUL:     return binary_mul(lhs, rhs, rt, e.line);
        case IRBinaryOp::DIV:     return binary_div(lhs, rhs, rt, e.line);
        case IRBinaryOp::INT_DIV: return binary_int_div(lhs, rhs, rt, e.line);
        case IRBinaryOp::MOD:     return binary_mod(lhs, rhs, rt, e.line);
        case IRBinaryOp::EXP:     return binary_exp(lhs, rhs, rt, e.line);
        case IRBinaryOp::EQ:      return binary_eq(lhs, rhs, e.line);
        case IRBinaryOp::LT:      return binary_lt(lhs, rhs, e.line);
        case IRBinaryOp::GT:      return binary_gt(lhs, rhs, e.line);
        case IRBinaryOp::LE:      return binary_le(lhs, rhs, e.line);
        case IRBinaryOp::GE:      return binary_ge(lhs, rhs, e.line);
        case IRBinaryOp::AND:
        case IRBinaryOp::NAND:
        case IRBinaryOp::OR:
        case IRBinaryOp::NOR:
        case IRBinaryOp::XOR:
        case IRBinaryOp::XNOR:
            return binary_bitwise(lhs, rhs, e.op, e.line);
        case IRBinaryOp::TENSOR:  return binary_tensor(lhs, rhs, e.line);
    }
    report_error(e.line);
}


// Helper: extract classical numeric pair from two values.
// Handles quantum values by measuring/peeking as needed for classical
// result types, or by leaving them for quantum arithmetic.

static double val_to_real(const JanusValue& v, uint32_t line) {
    switch (v.type_info.type) {
        case JanusType::NULL_TYPE: return 0.0;
        case JanusType::CBIT:
        case JanusType::CNUM:     return v.real_val;
        case JanusType::QUBIT:
        case JanusType::QNUM:
            if (!v.quantum_val) return 0.0;
            return static_cast<double>(v.quantum_val->peek(line));
        case JanusType::CSTR: {
            // CSTR treated as 2's complement register value.
            uint64_t acc = 0;
            for (char ch : v.str_val) {
                acc = (acc << 7) | (static_cast<uint64_t>(ch) & 0x7F);
            }
            return static_cast<double>(acc);
        }
        default:
            report_error(line);
    }
}


// Arithmetic binary operations


JanusValue BackendQuEST::binary_add(const JanusValue& lhs,
                                    const JanusValue& rhs,
                                    JanusType result_type, uint32_t line) {
    // Concatenation: if either operand is a list, concatenate.
    if (lhs.type_info.type == JanusType::LIST ||
        rhs.type_info.type == JanusType::LIST) {
        std::vector<JanusValue> elems;
        if (lhs.type_info.type == JanusType::LIST && lhs.list_data) {
            elems.insert(elems.end(), lhs.list_data->begin(),
                         lhs.list_data->end());
        } else {
            elems.push_back(lhs);
        }
        if (rhs.type_info.type == JanusType::LIST && rhs.list_data) {
            elems.insert(elems.end(), rhs.list_data->begin(),
                         rhs.list_data->end());
        } else {
            elems.push_back(rhs);
        }
        return JanusValue::make_list(std::move(elems));
    }

    // Quantum state vector arithmetic.
    bool lq = is_quantum(lhs.type_info.type);
    bool rq = is_quantum(rhs.type_info.type);

    if (lq && rq) {
        if (!lhs.quantum_val || !rhs.quantum_val) report_error(line);
        QuantumState res = lhs.quantum_val->add(*rhs.quantum_val, line);
        apply_p32(res);
        if (is_quantum(result_type)) {
            return JanusValue::make_qnum(std::move(res));
        }
        // Classical result: measure the combined state, collapse both
        // independently.
        uint64_t outcome = res.measure(line);
        return JanusValue::make_cnum(static_cast<double>(outcome));
    }

    if (lq || rq) {
        const JanusValue& qval = lq ? lhs : rhs;
        const JanusValue& cval = lq ? rhs : lhs;
        if (!qval.quantum_val) report_error(line);
        double cv = val_to_real(cval, line);
        // Create a quantum state from the classical value.
        uint64_t cint = (cv >= 0.0) ? static_cast<uint64_t>(cv) : 0;
        uint32_t nq = qubits_for_value(cint);
        QuantumState cqs(nq, cint, line);
        QuantumState res = lq
            ? lhs.quantum_val->add(cqs, line)
            : cqs.add(*rhs.quantum_val, line);
        apply_p32(res);
        if (is_quantum(result_type))
            return JanusValue::make_qnum(std::move(res));
        uint64_t outcome = res.measure(line);
        return JanusValue::make_cnum(static_cast<double>(outcome));
    }

    // String concatenation.
    if (lhs.type_info.type == JanusType::CSTR ||
        rhs.type_info.type == JanusType::CSTR) {
        return JanusValue::make_cstr(lhs.to_string() + rhs.to_string());
    }

    // Classical arithmetic.
    double result = val_to_real(lhs, line) + val_to_real(rhs, line);
    if (result_type == JanusType::CBIT)
        return JanusValue::make_cbit(result != 0.0 ? 1 : 0);
    return JanusValue::make_cnum(result);
}


JanusValue BackendQuEST::binary_sub(const JanusValue& lhs,
                                    const JanusValue& rhs,
                                    JanusType result_type, uint32_t line) {
    bool lq = is_quantum(lhs.type_info.type);
    bool rq = is_quantum(rhs.type_info.type);

    if (lq && rq) {
        if (!lhs.quantum_val || !rhs.quantum_val) report_error(line);
        QuantumState res = lhs.quantum_val->subtract(*rhs.quantum_val, line);
        apply_p32(res);
        if (is_quantum(result_type))
            return JanusValue::make_qnum(std::move(res));
        uint64_t outcome = res.measure(line);
        return JanusValue::make_cnum(static_cast<double>(outcome));
    }
    if (lq || rq) {
        const JanusValue& qval = lq ? lhs : rhs;
        const JanusValue& cval = lq ? rhs : lhs;
        if (!qval.quantum_val) report_error(line);
        double cv = val_to_real(cval, line);
        uint64_t cint = (cv >= 0.0) ? static_cast<uint64_t>(cv) : 0;
        uint32_t nq = qubits_for_value(cint);
        QuantumState cqs(nq, cint, line);
        QuantumState res = lq
            ? lhs.quantum_val->subtract(cqs, line)
            : cqs.subtract(*rhs.quantum_val, line);
        apply_p32(res);
        if (is_quantum(result_type))
            return JanusValue::make_qnum(std::move(res));
        uint64_t outcome = res.measure(line);
        return JanusValue::make_cnum(static_cast<double>(outcome));
    }
    double result = val_to_real(lhs, line) - val_to_real(rhs, line);
    if (result_type == JanusType::CBIT)
        return JanusValue::make_cbit(result != 0.0 ? 1 : 0);
    return JanusValue::make_cnum(result);
}


JanusValue BackendQuEST::binary_mul(const JanusValue& lhs,
                                    const JanusValue& rhs,
                                    JanusType result_type, uint32_t line) {
    // Matrix multiplication.
    if ((lhs.type_info.type == JanusType::MATRIX ||
         lhs.type_info.type == JanusType::GATE) &&
        (rhs.type_info.type == JanusType::MATRIX ||
         rhs.type_info.type == JanusType::GATE)) {
        if (!lhs.matrix_data || !rhs.matrix_data) report_error(line);
        uint32_t lr = lhs.type_info.matrix_rows;
        uint32_t lc = lhs.type_info.matrix_cols;
        uint32_t rr = rhs.type_info.matrix_rows;
        uint32_t rc = rhs.type_info.matrix_cols;
        if (lc != rr) report_error(line);
        std::vector<std::complex<double>> data(lr * rc);
        for (uint32_t i = 0; i < lr; ++i)
            for (uint32_t j = 0; j < rc; ++j) {
                std::complex<double> sum{0.0, 0.0};
                for (uint32_t k = 0; k < lc; ++k)
                    sum += (*lhs.matrix_data)[i * lc + k] *
                           (*rhs.matrix_data)[k * rc + j];
                data[i * rc + j] = sum;
            }
        if (result_type == JanusType::GATE)
            return JanusValue::make_gate(
                lhs.type_info.width, std::move(data));
        return JanusValue::make_matrix(lr, rc, std::move(data));
    }

    bool lq = is_quantum(lhs.type_info.type);
    bool rq = is_quantum(rhs.type_info.type);

    if (lq && rq) {
        if (!lhs.quantum_val || !rhs.quantum_val) report_error(line);
        QuantumState res = lhs.quantum_val->multiply(*rhs.quantum_val, line);
        apply_p32(res);
        if (is_quantum(result_type))
            return JanusValue::make_qnum(std::move(res));
        uint64_t outcome = res.measure(line);
        return JanusValue::make_cnum(static_cast<double>(outcome));
    }
    if (lq || rq) {
        const JanusValue& qval = lq ? lhs : rhs;
        const JanusValue& cval = lq ? rhs : lhs;
        if (!qval.quantum_val) report_error(line);
        double cv = val_to_real(cval, line);
        uint64_t cint = (cv >= 0.0) ? static_cast<uint64_t>(cv) : 0;
        uint32_t nq = qubits_for_value(cint);
        QuantumState cqs(nq, cint, line);
        QuantumState res = lq
            ? lhs.quantum_val->multiply(cqs, line)
            : cqs.multiply(*rhs.quantum_val, line);
        apply_p32(res);
        if (is_quantum(result_type))
            return JanusValue::make_qnum(std::move(res));
        uint64_t outcome = res.measure(line);
        return JanusValue::make_cnum(static_cast<double>(outcome));
    }
    double result = val_to_real(lhs, line) * val_to_real(rhs, line);
    if (result_type == JanusType::CBIT)
        return JanusValue::make_cbit(result != 0.0 ? 1 : 0);
    return JanusValue::make_cnum(result);
}


JanusValue BackendQuEST::binary_div(const JanusValue& lhs,
                                    const JanusValue& rhs,
                                    JanusType result_type, uint32_t line) {
    bool lq = is_quantum(lhs.type_info.type);
    bool rq = is_quantum(rhs.type_info.type);

    if (lq && rq) {
        if (!lhs.quantum_val || !rhs.quantum_val) report_error(line);
        QuantumState res = lhs.quantum_val->divide(*rhs.quantum_val, line);
        apply_p32(res);
        if (is_quantum(result_type))
            return JanusValue::make_qnum(std::move(res));
        uint64_t outcome = res.measure(line);
        return JanusValue::make_cnum(static_cast<double>(outcome));
    }
    if (lq || rq) {
        const JanusValue& qval = lq ? lhs : rhs;
        const JanusValue& cval = lq ? rhs : lhs;
        if (!qval.quantum_val) report_error(line);
        double cv = val_to_real(cval, line);
        uint64_t cint = (cv >= 0.0) ? static_cast<uint64_t>(cv) : 0;
        uint32_t nq = qubits_for_value(cint);
        QuantumState cqs(nq, cint, line);
        QuantumState res = lq
            ? lhs.quantum_val->divide(cqs, line)
            : cqs.divide(*rhs.quantum_val, line);
        apply_p32(res);
        if (is_quantum(result_type))
            return JanusValue::make_qnum(std::move(res));
        uint64_t outcome = res.measure(line);
        return JanusValue::make_cnum(static_cast<double>(outcome));
    }
    double rv = val_to_real(rhs, line);
    if (std::abs(rv) < ZERO_TOL) report_error(line);
    double result = val_to_real(lhs, line) / rv;
    if (result_type == JanusType::CBIT)
        return JanusValue::make_cbit(result != 0.0 ? 1 : 0);
    return JanusValue::make_cnum(result);
}


JanusValue BackendQuEST::binary_int_div(const JanusValue& lhs,
                                        const JanusValue& rhs,
                                        JanusType result_type,
                                        uint32_t line) {
    bool lq = is_quantum(lhs.type_info.type);
    bool rq = is_quantum(rhs.type_info.type);

    if (lq || rq) {
        // Integer division on quantum types: perform float division
        // then floor the measured result.
        JanusValue div_result = binary_div(lhs, rhs, result_type, line);
        if (is_quantum(div_result.type_info.type)) {
            // Measure, floor, return as classical.
            if (!div_result.quantum_val) report_error(line);
            uint64_t outcome = div_result.quantum_val->measure(line);
            return JanusValue::make_cnum(static_cast<double>(outcome));
        }
        div_result.real_val = std::floor(div_result.real_val);
        return div_result;
    }

    double rv = val_to_real(rhs, line);
    if (std::abs(rv) < ZERO_TOL) report_error(line);
    double result = std::floor(val_to_real(lhs, line) / rv);
    if (result_type == JanusType::CBIT)
        return JanusValue::make_cbit(result != 0.0 ? 1 : 0);
    return JanusValue::make_cnum(result);
}


JanusValue BackendQuEST::binary_mod(const JanusValue& lhs,
                                    const JanusValue& rhs,
                                    JanusType result_type, uint32_t line) {
    bool lq = is_quantum(lhs.type_info.type);
    bool rq = is_quantum(rhs.type_info.type);

    if (lq && rq) {
        if (!lhs.quantum_val || !rhs.quantum_val) report_error(line);
        QuantumState res = lhs.quantum_val->modulus(*rhs.quantum_val, line);
        apply_p32(res);
        if (is_quantum(result_type))
            return JanusValue::make_qnum(std::move(res));
        uint64_t outcome = res.measure(line);
        return JanusValue::make_cnum(static_cast<double>(outcome));
    }
    if (lq || rq) {
        const JanusValue& qval = lq ? lhs : rhs;
        const JanusValue& cval = lq ? rhs : lhs;
        if (!qval.quantum_val) report_error(line);
        double cv = val_to_real(cval, line);
        uint64_t cint = (cv >= 0.0) ? static_cast<uint64_t>(cv) : 0;
        uint32_t nq = qubits_for_value(cint);
        QuantumState cqs(nq, cint, line);
        QuantumState res = lq
            ? lhs.quantum_val->modulus(cqs, line)
            : cqs.modulus(*rhs.quantum_val, line);
        apply_p32(res);
        if (is_quantum(result_type))
            return JanusValue::make_qnum(std::move(res));
        uint64_t outcome = res.measure(line);
        return JanusValue::make_cnum(static_cast<double>(outcome));
    }
    double rv = val_to_real(rhs, line);
    if (std::abs(rv) < ZERO_TOL) report_error(line);
    double result = std::fmod(val_to_real(lhs, line), rv);
    if (result_type == JanusType::CBIT)
        return JanusValue::make_cbit(result != 0.0 ? 1 : 0);
    return JanusValue::make_cnum(result);
}


JanusValue BackendQuEST::binary_exp(const JanusValue& lhs,
                                    const JanusValue& rhs,
                                    JanusType result_type, uint32_t line) {
    bool lq = is_quantum(lhs.type_info.type);
    bool rq = is_quantum(rhs.type_info.type);

    if (lq && rq) {
        if (!lhs.quantum_val || !rhs.quantum_val) report_error(line);
        QuantumState res = lhs.quantum_val->power(*rhs.quantum_val, line);
        apply_p32(res);
        if (is_quantum(result_type))
            return JanusValue::make_qnum(std::move(res));
        uint64_t outcome = res.measure(line);
        return JanusValue::make_cnum(static_cast<double>(outcome));
    }
    if (lq || rq) {
        const JanusValue& qval = lq ? lhs : rhs;
        const JanusValue& cval = lq ? rhs : lhs;
        if (!qval.quantum_val) report_error(line);
        double cv = val_to_real(cval, line);
        uint64_t cint = (cv >= 0.0) ? static_cast<uint64_t>(cv) : 0;
        uint32_t nq = qubits_for_value(cint);
        QuantumState cqs(nq, cint, line);
        QuantumState res = lq
            ? lhs.quantum_val->power(cqs, line)
            : cqs.power(*rhs.quantum_val, line);
        apply_p32(res);
        if (is_quantum(result_type))
            return JanusValue::make_qnum(std::move(res));
        uint64_t outcome = res.measure(line);
        return JanusValue::make_cnum(static_cast<double>(outcome));
    }
    double result = std::pow(val_to_real(lhs, line), val_to_real(rhs, line));
    if (result_type == JanusType::CBIT)
        return JanusValue::make_cbit(result != 0.0 ? 1 : 0);
    return JanusValue::make_cnum(result);
}


// Comparison operators


JanusValue BackendQuEST::binary_eq(const JanusValue& lhs,
                                   const JanusValue& rhs, uint32_t line) {
    // Same type comparison.
    if (lhs.type_info.type == rhs.type_info.type) {
        switch (lhs.type_info.type) {
            case JanusType::NULL_TYPE:
                return JanusValue::make_cbit(1);
            case JanusType::CBIT:
            case JanusType::CNUM:
                return JanusValue::make_cbit(
                    (std::abs(lhs.real_val - rhs.real_val) < ZERO_TOL &&
                     std::abs(lhs.imag_val - rhs.imag_val) < ZERO_TOL)
                    ? 1 : 0);
            case JanusType::QUBIT:
            case JanusType::QNUM: {
                double lv = val_to_real(lhs, line);
                double rv = val_to_real(rhs, line);
                return JanusValue::make_cbit(
                    std::abs(lv - rv) < ZERO_TOL ? 1 : 0);
            }
            case JanusType::CSTR:
                return JanusValue::make_cbit(
                    lhs.str_val == rhs.str_val ? 1 : 0);
            case JanusType::LIST: {
                if (!lhs.list_data && !rhs.list_data)
                    return JanusValue::make_cbit(1);
                if (!lhs.list_data || !rhs.list_data)
                    return JanusValue::make_cbit(0);
                if (lhs.list_data->size() != rhs.list_data->size())
                    return JanusValue::make_cbit(0);
                return JanusValue::make_cbit(
                    lhs.to_string() == rhs.to_string() ? 1 : 0);
            }
            default:
                return JanusValue::make_cbit(
                    lhs.to_string() == rhs.to_string() ? 1 : 0);
        }
    }
    // Cross-type numeric comparison.
    if (is_numeric(lhs.type_info.type) && is_numeric(rhs.type_info.type)) {
        double lv = val_to_real(lhs, line);
        double rv = val_to_real(rhs, line);
        return JanusValue::make_cbit(std::abs(lv - rv) < ZERO_TOL ? 1 : 0);
    }
    // null == anything where anything is null: true.
    if (lhs.type_info.type == JanusType::NULL_TYPE)
        return JanusValue::make_cbit(rhs.is_null() ? 1 : 0);
    if (rhs.type_info.type == JanusType::NULL_TYPE)
        return JanusValue::make_cbit(lhs.is_null() ? 1 : 0);
    return JanusValue::make_cbit(0);
}


JanusValue BackendQuEST::binary_lt(const JanusValue& lhs,
                                   const JanusValue& rhs, uint32_t line) {
    double lv = val_to_real(lhs, line);
    double rv = val_to_real(rhs, line);
    return JanusValue::make_cbit(lv < rv ? 1 : 0);
}

JanusValue BackendQuEST::binary_gt(const JanusValue& lhs,
                                   const JanusValue& rhs, uint32_t line) {
    double lv = val_to_real(lhs, line);
    double rv = val_to_real(rhs, line);
    return JanusValue::make_cbit(lv > rv ? 1 : 0);
}

JanusValue BackendQuEST::binary_le(const JanusValue& lhs,
                                   const JanusValue& rhs, uint32_t line) {
    double lv = val_to_real(lhs, line);
    double rv = val_to_real(rhs, line);
    return JanusValue::make_cbit(lv <= rv ? 1 : 0);
}

JanusValue BackendQuEST::binary_ge(const JanusValue& lhs,
                                   const JanusValue& rhs, uint32_t line) {
    double lv = val_to_real(lhs, line);
    double rv = val_to_real(rhs, line);
    return JanusValue::make_cbit(lv >= rv ? 1 : 0);
}


// Bitwise operators


JanusValue BackendQuEST::binary_bitwise(const JanusValue& lhs,
                                        const JanusValue& rhs,
                                        IRBinaryOp op, uint32_t line) {
    int64_t lv = to_integer(lhs, line);
    int64_t rv = to_integer(rhs, line);
    int64_t result = 0;
    switch (op) {
        case IRBinaryOp::AND:  result = lv & rv; break;
        case IRBinaryOp::NAND: result = ~(lv & rv); break;
        case IRBinaryOp::OR:   result = lv | rv; break;
        case IRBinaryOp::NOR:  result = ~(lv | rv); break;
        case IRBinaryOp::XOR:  result = lv ^ rv; break;
        case IRBinaryOp::XNOR: result = ~(lv ^ rv); break;
        default: report_error(line);
    }
    // If both operands are CBIT/QUBIT, produce CBIT.
    if ((lhs.type_info.type == JanusType::CBIT ||
         lhs.type_info.type == JanusType::QUBIT) &&
        (rhs.type_info.type == JanusType::CBIT ||
         rhs.type_info.type == JanusType::QUBIT))
        return JanusValue::make_cbit(result != 0 ? 1 : 0);
    return JanusValue::make_cnum(static_cast<double>(result));
}


JanusValue BackendQuEST::binary_tensor(const JanusValue& lhs,
                                       const JanusValue& rhs,
                                       uint32_t line) {
    // Quantum tensor product.
    if (is_quantum(lhs.type_info.type) && is_quantum(rhs.type_info.type)) {
        if (!lhs.quantum_val || !rhs.quantum_val) report_error(line);
        QuantumState tp = tensor_product(*lhs.quantum_val, *rhs.quantum_val);
        apply_p32(tp);
        return JanusValue::make_qnum(std::move(tp));
    }

    // List-based tensor product.
    if (lhs.type_info.type == JanusType::LIST ||
        rhs.type_info.type == JanusType::LIST) {
        // Conservative: return a matrix representation of outer product.
        return JanusValue::make_matrix(0, 0, {});
    }

    // Gate tensor product.
    if (lhs.type_info.type == JanusType::GATE &&
        rhs.type_info.type == JanusType::GATE) {
        if (!lhs.matrix_data || !rhs.matrix_data) report_error(line);
        uint32_t ld = lhs.type_info.matrix_rows;
        uint32_t rd = rhs.type_info.matrix_rows;
        uint32_t nd = ld * rd;
        std::vector<std::complex<double>> data(nd * nd);
        for (uint32_t i = 0; i < ld; ++i)
            for (uint32_t j = 0; j < ld; ++j)
                for (uint32_t k = 0; k < rd; ++k)
                    for (uint32_t l = 0; l < rd; ++l)
                        data[(i * rd + k) * nd + (j * rd + l)] =
                            (*lhs.matrix_data)[i * ld + j] *
                            (*rhs.matrix_data)[k * rd + l];
        uint32_t w = lhs.type_info.width + rhs.type_info.width;
        return JanusValue::make_gate(w, std::move(data));
    }

    // Scalar tensor = multiplication.
    double lv = val_to_real(lhs, line);
    double rv = val_to_real(rhs, line);
    return JanusValue::make_cnum(lv * rv);
}


// Unary operators


JanusValue BackendQuEST::eval_unary(const IRUnary& e) {
    JanusValue operand = eval_expr(*e.operand);

    switch (e.op) {
        case IRUnaryOp::NEG: {
            if (is_quantum(operand.type_info.type)) {
                // Negate quantum state: multiply all amplitudes by -1.
                if (!operand.quantum_val) report_error(e.line);
                auto& amps = operand.quantum_val->amplitudes();
                for (auto& a : amps) a = -a;
                return operand;
            }
            double v = val_to_real(operand, e.line);
            return JanusValue::make_cnum(-v);
        }

        case IRUnaryOp::BITWISE_NOT: {
            // Quantum path: bitwise not on a quantum register permutes
            // the state vector by flipping every qubit, equivalent to
            // applying X to every qubit.  The permutation sends basis
            // state |i> to |~i & mask>, which is unitary and preserves
            // normalisation.
            if (is_quantum(operand.type_info.type)) {
                if (!operand.quantum_val) report_error(e.line);
                uint32_t nq = operand.quantum_val->num_qubits();
                uint64_t dim = uint64_t{1} << nq;
                uint64_t mask = dim - 1;
                const auto& src = operand.quantum_val->amplitudes();
                std::vector<std::complex<double>> dst(dim);
                for (uint64_t i = 0; i < dim; ++i) {
                    dst[(~i) & mask] = src[i];
                }
                QuantumState result(nq);
                auto& out = result.amplitudes();
                for (uint64_t i = 0; i < dim; ++i) out[i] = dst[i];
                apply_p32(result);
                if (operand.type_info.type == JanusType::QUBIT)
                    return JanusValue::make_qubit(std::move(result));
                return JanusValue::make_qnum(std::move(result));
            }
            // Classical path.
            int64_t v = to_integer(operand, e.line);
            int64_t result = ~v;
            if (operand.type_info.type == JanusType::CBIT)
                return JanusValue::make_cbit(result != 0 ? 1 : 0);
            return JanusValue::make_cnum(static_cast<double>(result));
        }

        case IRUnaryOp::BOOL_NOT: {
            bool truthy = operand.is_truthy();
            if (is_quantum(operand.type_info.type)) {
                QuantumState qs(1, truthy ? 0u : 1u, e.line);
                return JanusValue::make_qubit(std::move(qs));
            }
            return JanusValue::make_cbit(truthy ? 0 : 1);
        }

        case IRUnaryOp::SHIFT_LEFT: {
            // Quantum path: left shift by 1 permutes basis states by
            // (i << 1) & mask, dropping the top bit.  This is not
            // unitary when amplitude exists in states with the top
            // bit set, so the result is renormalised.  When the full
            // norm is zero (all amplitude was in high states), report
            // an error because the result is undefined.
            if (is_quantum(operand.type_info.type)) {
                if (!operand.quantum_val) report_error(e.line);
                uint32_t nq = operand.quantum_val->num_qubits();
                uint64_t dim = uint64_t{1} << nq;
                uint64_t mask = dim - 1;
                const auto& src = operand.quantum_val->amplitudes();
                std::vector<std::complex<double>> dst(dim, {0.0, 0.0});
                for (uint64_t i = 0; i < dim; ++i) {
                    uint64_t j = (i << 1) & mask;
                    dst[j] += src[i];
                }
                QuantumState result(nq);
                auto& out = result.amplitudes();
                for (uint64_t i = 0; i < dim; ++i) out[i] = dst[i];
                result.normalise(e.line);
                apply_p32(result);
                if (operand.type_info.type == JanusType::QUBIT)
                    return JanusValue::make_qubit(std::move(result));
                return JanusValue::make_qnum(std::move(result));
            }
            // Classical path.
            int64_t v = to_integer(operand, e.line);
            int64_t result = v << 1;
            if (operand.type_info.type == JanusType::CBIT)
                return JanusValue::make_cbit(result != 0 ? 1 : 0);
            return JanusValue::make_cnum(static_cast<double>(result));
        }

        case IRUnaryOp::SHIFT_RIGHT: {
            // Quantum path: right shift by 1 permutes basis states by
            // (i >> 1), dropping the low bit.  As with shift left the
            // result may be subnormalised and is renormalised here.
            if (is_quantum(operand.type_info.type)) {
                if (!operand.quantum_val) report_error(e.line);
                uint32_t nq = operand.quantum_val->num_qubits();
                uint64_t dim = uint64_t{1} << nq;
                const auto& src = operand.quantum_val->amplitudes();
                std::vector<std::complex<double>> dst(dim, {0.0, 0.0});
                for (uint64_t i = 0; i < dim; ++i) {
                    uint64_t j = i >> 1;
                    dst[j] += src[i];
                }
                QuantumState result(nq);
                auto& out = result.amplitudes();
                for (uint64_t i = 0; i < dim; ++i) out[i] = dst[i];
                result.normalise(e.line);
                apply_p32(result);
                if (operand.type_info.type == JanusType::QUBIT)
                    return JanusValue::make_qubit(std::move(result));
                return JanusValue::make_qnum(std::move(result));
            }
            // Classical path.
            int64_t v = to_integer(operand, e.line);
            int64_t result = v >> 1;
            if (operand.type_info.type == JanusType::CBIT)
                return JanusValue::make_cbit(result != 0 ? 1 : 0);
            return JanusValue::make_cnum(static_cast<double>(result));
        }
    }
    report_error(e.line);
}


JanusValue BackendQuEST::eval_postfix_bang(const IRPostfixBang& e) {
    JanusValue operand = eval_expr(*e.operand);
    bool truthy = operand.is_truthy();
    if (is_quantum(operand.type_info.type)) {
        QuantumState qs(1, truthy ? 0u : 1u, e.line);
        return JanusValue::make_qubit(std::move(qs));
    }
    return JanusValue::make_cbit(truthy ? 0 : 1);
}


// Type cast


JanusValue BackendQuEST::eval_type_cast(const IRTypeCast& e) {
    JanusValue operand = eval_expr(*e.operand);
    return perform_cast(operand, e.target_type, e.line);
}


JanusValue BackendQuEST::perform_cast(const JanusValue& val,
                                      JanusType target, uint32_t line) {
    JanusType source = val.type_info.type;
    if (source == target) return val;

    switch (target) {
        case JanusType::QUBIT: {
            uint64_t basis = (val.is_truthy()) ? 1u : 0u;
            QuantumState qs(1, basis, line);
            return JanusValue::make_qubit(std::move(qs));
        }
        case JanusType::CBIT:
            return JanusValue::make_cbit(val.is_truthy() ? 1 : 0);
        case JanusType::QNUM: {
            uint64_t v = static_cast<uint64_t>(val_to_real(val, line));
            return JanusValue::make_qnum(v, line);
        }
        case JanusType::CNUM:
            return JanusValue::make_cnum(val_to_real(val, line));
        case JanusType::CSTR:
            return JanusValue::make_cstr(val.to_string());
        case JanusType::LIST: {
            std::vector<JanusValue> elems;
            elems.push_back(val);
            return JanusValue::make_list(std::move(elems));
        }
        default:
            report_error(line);
    }
}


// Matrix literal


JanusValue BackendQuEST::eval_matrix_literal(const IRMatrixLiteral& e) {
    uint32_t rows = static_cast<uint32_t>(e.rows.size());
    if (rows == 0)
        return JanusValue::make_list({});

    uint32_t cols = static_cast<uint32_t>(e.rows[0].size());

    // Evaluate all elements.
    std::vector<std::vector<JanusValue>> evaluated_rows(rows);
    bool all_numeric = true;
    for (uint32_t r = 0; r < rows; ++r) {
        evaluated_rows[r].reserve(e.rows[r].size());
        for (const auto& elem : e.rows[r]) {
            JanusValue v = eval_expr(*elem);
            if (!is_numeric(v.type_info.type) &&
                !is_quantum(v.type_info.type) &&
                v.type_info.type != JanusType::NULL_TYPE)
                all_numeric = false;
            evaluated_rows[r].push_back(std::move(v));
        }
    }

    // Single row: might be a list literal.
    if (rows == 1 && !all_numeric) {
        return JanusValue::make_list(std::move(evaluated_rows[0]));
    }
    if (rows == 1 && cols == 1) {
        return evaluated_rows[0][0];
    }
    if (!all_numeric) {
        // Non-numeric multi-row: build a list of lists.
        std::vector<JanusValue> outer;
        for (auto& row : evaluated_rows) {
            outer.push_back(JanusValue::make_list(std::move(row)));
        }
        return JanusValue::make_list(std::move(outer));
    }

    // Build a numeric matrix.
    std::vector<std::complex<double>> data(rows * cols);
    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < cols; ++c) {
            auto& val = evaluated_rows[r][c];
            double re = 0.0, im = 0.0;
            if (val.type_info.type != JanusType::NULL_TYPE) {
                re = val.real_val;
                im = val.imag_val;
                if (is_quantum(val.type_info.type) && val.quantum_val) {
                    re = static_cast<double>(val.quantum_val->peek(e.line));
                    im = 0.0;
                }
            }
            data[r * cols + c] = {re, im};
        }
    }
    return JanusValue::make_matrix(rows, cols, std::move(data));
}


// Function call


JanusValue BackendQuEST::eval_call(const IRCall& e) {
    JanusValue callee = eval_expr(*e.callee);
    if (callee.type_info.type != JanusType::FUNCTION || !callee.func_data)
        report_error(e.line);

    const auto& params = callee.func_data->params;
    if (params.size() != e.args.size()) report_error(e.line);

    // Evaluate all arguments before pushing the function scope.
    std::vector<JanusValue> arg_vals;
    arg_vals.reserve(e.args.size());
    for (const auto& arg : e.args) {
        arg_vals.push_back(eval_expr(*arg));
    }

    // Push function scope (isolated from all enclosing scopes).
    scope_.push_function();

    // Bind parameters.
    for (std::size_t i = 0; i < params.size(); ++i) {
        scope_.assign(params[i], std::move(arg_vals[i]), e.line);
    }

    // Execute the function body.  The body pointer stored in FunctionData
    // is actually a const vector<IRStmtPtr>* stored via reinterpret_cast;
    // cast it back to its true type for IR statement execution.
    auto* ir_body = reinterpret_cast<const std::vector<IRStmtPtr>*>(
        callee.func_data->body);
    if (!ir_body) report_error(e.line);

    JanusValue return_val = JanusValue::make_null();
    try {
        exec_stmts(*ir_body);
    } catch (ReturnSignal& rs) {
        if (rs.has_value) {
            return_val = std::move(rs.value);
        }
    }

    scope_.pop_function();
    return return_val;
}


// Builtin call dispatch


JanusValue BackendQuEST::eval_builtin_call(const IRBuiltinCall& e) {
    // Evaluate arguments.
    std::vector<JanusValue> args;
    args.reserve(e.args.size());
    for (const auto& arg : e.args) {
        args.push_back(eval_expr(*arg));
    }

    switch (e.op) {
        case IRBuiltinOp::MEASURE:       return builtin_measure(args, e.line);
        case IRBuiltinOp::PEEK:          return builtin_peek(args, e.line);
        case IRBuiltinOp::STATE:         return builtin_state(args, e.line);
        case IRBuiltinOp::EXPECT:        return builtin_expect(args, e.line);
        case IRBuiltinOp::CTRLE:         return builtin_ctrle(args, e.line);
        case IRBuiltinOp::RUN:           return builtin_run(args, e.line);
        case IRBuiltinOp::RUNH:          return builtin_runh(args, e.line);
        case IRBuiltinOp::ISUNITARY:     return builtin_isunitary(args, e.line);
        case IRBuiltinOp::SAMEOUTPUT:    return builtin_sameoutput(args, e.line);
        case IRBuiltinOp::PRINT:         return builtin_print(args, e.line);
        case IRBuiltinOp::DELETE:        return builtin_delete(args, e.line);
        case IRBuiltinOp::SIN:           return builtin_sin(args, e.line);
        case IRBuiltinOp::COS:           return builtin_cos(args, e.line);
        case IRBuiltinOp::NUMBEROFGATES: return builtin_numberofgates(args, e.line);
        case IRBuiltinOp::DET:           return builtin_det(args, e.line);
        case IRBuiltinOp::TRANSPOSE:     return builtin_transpose(args, e.line);
        case IRBuiltinOp::TRANSPOSEC:    return builtin_transposec(args, e.line);
        case IRBuiltinOp::EVALS:         return builtin_evals(args, e.line);
        case IRBuiltinOp::EVECS:         return builtin_evecs(args, e.line);
        case IRBuiltinOp::GATES:         return builtin_gates(args, e.line);
        case IRBuiltinOp::QUBITS:        return builtin_qubits(args, e.line);
        case IRBuiltinOp::DEPTH:         return builtin_depth(args, e.line);
        case IRBuiltinOp::BITLENGTH: {
            if (args.empty()) report_error(e.line);
            const auto& arg = args[0];
            if (!arg.quantum_val) report_error(e.line);
            return JanusValue::make_cnum(
                static_cast<double>(arg.quantum_val->num_qubits()));
        }
    }
    report_error(e.line);
}


// Gate library


JanusValue BackendQuEST::eval_gate_library(const IRGateLibrary& e) {
    std::vector<JanusValue> args;
    args.reserve(e.args.size());
    for (const auto& arg : e.args) {
        args.push_back(eval_expr(*arg));
    }
    return gate_library::resolve_gate(e.gate_name, args, e.line);
}


// Type constructors


JanusValue BackendQuEST::eval_type_construct(const IRTypeConstruct& e) {
    std::vector<JanusValue> args;
    args.reserve(e.args.size());
    for (const auto& arg : e.args) {
        args.push_back(eval_expr(*arg));
    }

    switch (e.constructed_type) {
        case JanusType::QUBIT: {
            if (args.empty()) return JanusValue::make_qubit();
            // qubit(value): create qubit in |0> or |1>.
            uint64_t basis = args[0].is_truthy() ? 1u : 0u;
            QuantumState qs(1, basis, e.line);
            return JanusValue::make_qubit(std::move(qs));
        }
        case JanusType::CBIT: {
            if (args.empty()) return JanusValue::make_cbit(0);
            return JanusValue::make_cbit(args[0].is_truthy() ? 1 : 0);
        }
        case JanusType::QNUM: {
            if (args.empty()) return JanusValue::make_qnum(0u, e.line);
            uint64_t val = static_cast<uint64_t>(val_to_real(args[0], e.line));
            if (args.size() >= 2) {
                uint32_t max_w = static_cast<uint32_t>(
                    val_to_real(args[1], e.line));
                return JanusValue::make_qnum(val, max_w, e.line);
            }
            return JanusValue::make_qnum(val, e.line);
        }
        case JanusType::CNUM: {
            if (args.empty()) return JanusValue::make_cnum(0.0);
            if (args.size() == 1) {
                return JanusValue::make_cnum(val_to_real(args[0], e.line));
            }
            // cnum("i", real, imag) for complex.
            if (args.size() == 3 &&
                args[0].type_info.type == JanusType::CSTR &&
                args[0].str_val == "i") {
                float re = static_cast<float>(val_to_real(args[1], e.line));
                float im = static_cast<float>(val_to_real(args[2], e.line));
                return JanusValue::make_cnum_complex(re, im);
            }
            return JanusValue::make_cnum(val_to_real(args[0], e.line));
        }
        case JanusType::CSTR: {
            if (args.empty()) return JanusValue::make_cstr("");
            return JanusValue::make_cstr(args[0].to_string());
        }
        case JanusType::LIST: {
            if (args.empty()) return JanusValue::make_list({});
            // If the single argument is a list-of-lists from a matrix
            // literal, unwrap it.
            if (args.size() == 1 &&
                args[0].type_info.type == JanusType::LIST) {
                return args[0];
            }
            return JanusValue::make_list(std::move(args));
        }
        case JanusType::MATRIX: {
            if (args.empty()) return JanusValue::make_matrix(0, 0, {});
            // matrix([...]) from a matrix literal (already evaluated).
            if (args.size() == 1 &&
                args[0].type_info.type == JanusType::MATRIX) {
                return args[0];
            }
            if (args.size() == 1 &&
                args[0].type_info.type == JanusType::LIST) {
                // Try to build matrix from list of lists.
                if (!args[0].list_data || args[0].list_data->empty())
                    return JanusValue::make_matrix(0, 0, {});
                auto& outer = *args[0].list_data;
                uint32_t rows = static_cast<uint32_t>(outer.size());
                uint32_t cols = 0;
                std::vector<std::complex<double>> data;
                for (uint32_t r = 0; r < rows; ++r) {
                    if (outer[r].type_info.type == JanusType::LIST &&
                        outer[r].list_data) {
                        if (cols == 0)
                            cols = static_cast<uint32_t>(
                                outer[r].list_data->size());
                        for (auto& elem : *outer[r].list_data) {
                            data.push_back({val_to_real(elem, e.line), 0.0});
                        }
                    } else {
                        if (cols == 0) cols = 1;
                        data.push_back({val_to_real(outer[r], e.line), 0.0});
                    }
                }
                return JanusValue::make_matrix(rows, cols, std::move(data));
            }
            report_error(e.line);
        }
        case JanusType::GATE: {
            if (args.empty()) report_error(e.line);
            if (args[0].type_info.type == JanusType::MATRIX) {
                if (!args[0].matrix_data) report_error(e.line);
                uint32_t dim = args[0].type_info.matrix_rows;
                return gate_library::make_validated_gate(
                    *args[0].matrix_data, dim, e.line);
            }
            report_error(e.line);
        }
        case JanusType::CIRC: {
            if (args.size() < 2) report_error(e.line);
            auto qubit_states = circuit_synth::parse_qubit_arg(
                args[0], e.line);
            auto gate_grid = circuit_synth::parse_gate_grid(
                args[1], e.line);
            return circuit_synth::build_circ(
                std::move(qubit_states), std::move(gate_grid), e.line);
        }
        case JanusType::BLOCK: {
            if (args.empty()) report_error(e.line);
            auto gate_grid = circuit_synth::parse_gate_grid(
                args[0], e.line);
            return circuit_synth::build_block(std::move(gate_grid), e.line);
        }
        case JanusType::FUNCTION:
            report_error(e.line);
        case JanusType::NULL_TYPE:
            return JanusValue::make_null();
    }
    report_error(e.line);
}


// Function definition


JanusValue BackendQuEST::eval_function_def(const IRFunctionDef& e) {
    // Store a pointer to the IR body through FunctionData::body via
    // reinterpret_cast; eval_call casts it back to the correct IR type.
    return JanusValue::make_function(
        e.params,
        reinterpret_cast<const std::vector<std::unique_ptr<Stmt>>*>(
            &e.body));
}


// Control flow: if


JanusValue BackendQuEST::eval_if(const IRIf& e) {
    JanusValue cond = eval_expr(*e.condition);
    if (cond.is_truthy()) {
        scope_.push_block();
        try { exec_stmts(e.then_body); }
        catch (...) { scope_.pop_block(); throw; }
        scope_.pop_block();
        return JanusValue::make_cbit(1);
    }

    for (const auto& eif : e.else_ifs) {
        JanusValue eif_cond = eval_expr(*eif.condition);
        if (eif_cond.is_truthy()) {
            scope_.push_block();
            try { exec_stmts(eif.body); }
            catch (...) { scope_.pop_block(); throw; }
            scope_.pop_block();
            return JanusValue::make_cbit(1);
        }
    }

    if (!e.else_body.empty()) {
        scope_.push_block();
        try { exec_stmts(e.else_body); }
        catch (...) { scope_.pop_block(); throw; }
        scope_.pop_block();
        return JanusValue::make_cbit(1);
    }

    return JanusValue::make_cbit(0);
}


// Control flow: for


JanusValue BackendQuEST::eval_for(const IRFor& e) {
    // The init expression must be an assignment whose target is the
    // loop variable.
    auto* init_assign = dynamic_cast<const IRAssign*>(e.init.get());
    if (!init_assign) report_error(e.line);
    auto* init_var = dynamic_cast<const IRVariable*>(
        init_assign->target.get());
    if (!init_var) report_error(e.line);

    // Evaluate the init value and declare the loop variable in the
    // enclosing scope.
    JanusValue init_val = eval_expr(*init_assign->value);
    scope_.declare_loop_variable(init_var->name, std::move(init_val), e.line);

    int64_t iterations = 0;

    // Each iteration gets its own body block scope so that variables
    // declared inside the body do not leak across iterations.  The
    // condition and update are evaluated in the enclosing scope where
    // the loop variable lives, ensuring the update assignment modifies
    // the persisted loop variable rather than shadowing it.
    for (;;) {
        JanusValue cond = eval_expr(*e.condition);
        if (!cond.is_truthy()) break;

        ++iterations;
        bool did_break = false;
        scope_.push_block();
        try {
            exec_stmts(e.body);
        } catch (ContinueSignal&) {
            // Continue to update.
        } catch (BreakSignal&) {
            did_break = true;
        } catch (...) {
            scope_.pop_block();
            throw;
        }
        scope_.pop_block();

        if (did_break) break;

        // Execute update in the enclosing scope so that it finds and
        // modifies the loop variable declared via declare_loop_variable.
        eval_expr(*e.update);
    }

    // For returns iterations + 1, or 0 if body never executed.
    double result = (iterations > 0)
                    ? static_cast<double>(iterations + 1)
                    : 0.0;
    return JanusValue::make_cnum(result);
}


// Control flow: while


JanusValue BackendQuEST::eval_while(const IRWhile& e) {
    int64_t iterations = 0;

    // Each iteration gets its own body block scope so that variables
    // declared inside the body do not leak across iterations.
    for (;;) {
        JanusValue cond = eval_expr(*e.condition);
        if (!cond.is_truthy()) break;

        ++iterations;
        bool did_break = false;
        scope_.push_block();
        try {
            exec_stmts(e.body);
        } catch (ContinueSignal&) {
            // Continue to next iteration.
        } catch (BreakSignal&) {
            did_break = true;
        } catch (...) {
            scope_.pop_block();
            throw;
        }
        scope_.pop_block();

        if (did_break) break;
    }

    double result = (iterations > 0)
                    ? static_cast<double>(iterations + 1)
                    : 0.0;
    return JanusValue::make_cnum(result);
}


// Control flow: foreach


JanusValue BackendQuEST::eval_foreach(const IRForeach& e) {
    JanusValue collection = eval_expr(*e.collection);

    // Build the iteration list.
    std::vector<JanusValue> items;
    switch (collection.type_info.type) {
        case JanusType::LIST:
            if (collection.list_data) items = *collection.list_data;
            break;
        case JanusType::CSTR:
            for (char ch : collection.str_val)
                items.push_back(JanusValue::make_cstr(std::string(1, ch)));
            break;
        case JanusType::MATRIX:
        case JanusType::GATE: {
            if (collection.matrix_data) {
                for (const auto& elem : *collection.matrix_data) {
                    items.push_back(JanusValue::make_cnum_complex(
                        static_cast<float>(elem.real()),
                        static_cast<float>(elem.imag())));
                }
            }
            break;
        }
        case JanusType::CIRC:
        case JanusType::BLOCK: {
            const auto* grid = (collection.type_info.type == JanusType::CIRC)
                ? (collection.circ_data
                    ? &collection.circ_data->gate_grid : nullptr)
                : (collection.block_data
                    ? &collection.block_data->gate_grid : nullptr);
            if (grid) {
                auto gates = circuit_synth::extract_gates(*grid);
                items = std::move(gates);
            }
            break;
        }
        default:
            break;
    }

    // Evaluate from/to bounds.
    int64_t from_idx = 0;
    int64_t to_idx = static_cast<int64_t>(items.size());
    if (e.from_bound) {
        JanusValue fb = eval_expr(*e.from_bound);
        from_idx = to_integer(fb, e.line);
        if (from_idx < 0) from_idx = 0;
    }
    if (e.to_bound) {
        JanusValue tb = eval_expr(*e.to_bound);
        to_idx = to_integer(tb, e.line);
        if (to_idx > static_cast<int64_t>(items.size()))
            to_idx = static_cast<int64_t>(items.size());
    }

    // Declare the element variable.
    scope_.declare_loop_variable(e.element, JanusValue::make_null(), e.line);

    int64_t iterations = 0;

    // Each iteration gets its own body block scope so that variables
    // declared inside the body do not leak across iterations.  The
    // element variable update and where-clause evaluation happen in
    // the enclosing scope.
    for (int64_t i = from_idx; i < to_idx; ++i) {
        JanusValue& item = items[static_cast<std::size_t>(i)];

        // Update element variable in enclosing scope.
        scope_.update_loop_variable(e.element, item, e.line);

        // Check where clause in enclosing scope.
        if (e.where_cond) {
            JanusValue wc = eval_expr(*e.where_cond);
            if (!wc.is_truthy()) continue;
        }

        ++iterations;
        bool did_break = false;
        scope_.push_block();
        try {
            exec_stmts(e.body);
        } catch (ContinueSignal&) {
            // Continue.
        } catch (BreakSignal&) {
            did_break = true;
        } catch (...) {
            scope_.pop_block();
            throw;
        }
        scope_.pop_block();

        if (did_break) break;
    }

    double result = (iterations > 0)
                    ? static_cast<double>(iterations + 1)
                    : 0.0;
    return JanusValue::make_cnum(result);
}


// Builtin implementations


JanusValue BackendQuEST::builtin_measure(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    const auto& reg = args[0];
    if (reg.type_info.type == JanusType::QUBIT) {
        if (!reg.quantum_val) report_error(line);
        // Copy the state to avoid modifying the argument (value semantics).
        QuantumState qs = *reg.quantum_val;
        uint64_t outcome = qs.measure(line);
        return JanusValue::make_cbit(static_cast<int64_t>(outcome));
    }
    if (reg.type_info.type == JanusType::QNUM) {
        if (!reg.quantum_val) report_error(line);
        QuantumState qs = *reg.quantum_val;
        uint64_t outcome = qs.measure(line);
        return JanusValue::make_cnum(static_cast<double>(outcome));
    }
    // Classical types: return their value directly.
    if (reg.type_info.type == JanusType::CBIT)
        return JanusValue::make_cbit(reg.real_val != 0.0 ? 1 : 0);
    if (reg.type_info.type == JanusType::CNUM)
        return JanusValue::make_cnum(reg.real_val);
    report_error(line);
}


JanusValue BackendQuEST::builtin_peek(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    const auto& reg = args[0];
    if (reg.type_info.type == JanusType::QUBIT) {
        if (!reg.quantum_val) report_error(line);
        uint64_t outcome = reg.quantum_val->peek(line);
        return JanusValue::make_cbit(static_cast<int64_t>(outcome));
    }
    if (reg.type_info.type == JanusType::QNUM) {
        if (!reg.quantum_val) report_error(line);
        uint64_t outcome = reg.quantum_val->peek(line);
        return JanusValue::make_cnum(static_cast<double>(outcome));
    }
    if (reg.type_info.type == JanusType::CBIT)
        return JanusValue::make_cbit(reg.real_val != 0.0 ? 1 : 0);
    if (reg.type_info.type == JanusType::CNUM)
        return JanusValue::make_cnum(reg.real_val);
    report_error(line);
}


JanusValue BackendQuEST::builtin_state(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    const auto& reg = args[0];
    if (!is_quantum(reg.type_info.type) || !reg.quantum_val)
        report_error(line);
    return JanusValue::make_cstr(reg.quantum_val->to_dirac_string());
}


JanusValue BackendQuEST::builtin_expect(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.size() < 2) report_error(line);
    const auto& observable = args[0];
    const auto& reg = args[1];
    if ((observable.type_info.type != JanusType::MATRIX &&
         observable.type_info.type != JanusType::GATE) ||
        !observable.matrix_data)
        report_error(line);
    if (!is_quantum(reg.type_info.type) || !reg.quantum_val)
        report_error(line);

    uint32_t dim = observable.type_info.matrix_rows;
    uint64_t state_dim = reg.quantum_val->num_amplitudes();
    if (static_cast<uint64_t>(dim) != state_dim) report_error(line);

    // <psi|O|psi>
    const auto& amps = reg.quantum_val->amplitudes();
    const auto& mat = *observable.matrix_data;
    std::complex<double> expectation{0.0, 0.0};
    for (uint32_t i = 0; i < dim; ++i) {
        std::complex<double> Opsi_i{0.0, 0.0};
        for (uint32_t j = 0; j < dim; ++j) {
            Opsi_i += mat[i * dim + j] * amps[j];
        }
        expectation += std::conj(amps[i]) * Opsi_i;
    }
    return JanusValue::make_cnum(expectation.real());
}


JanusValue BackendQuEST::builtin_ctrle(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.size() < 2) report_error(line);
    const auto& gate_val = args[0];
    const auto& ctrl_val = args[1];

    if (gate_val.type_info.type != JanusType::GATE || !gate_val.matrix_data)
        report_error(line);
    uint32_t gw = gate_val.type_info.width;
    uint32_t gdim = gate_val.type_info.matrix_rows;

    uint32_t num_ctrls = 1;
    if (ctrl_val.type_info.type == JanusType::LIST && ctrl_val.list_data) {
        num_ctrls = static_cast<uint32_t>(ctrl_val.list_data->size());
    } else {
        num_ctrls = static_cast<uint32_t>(val_to_real(ctrl_val, line));
    }
    if (num_ctrls == 0) num_ctrls = 1;

    uint32_t total_w = gw + num_ctrls;
    uint32_t total_dim = 1u << total_w;
    std::vector<std::complex<double>> data(
        static_cast<std::size_t>(total_dim) * total_dim, {0.0, 0.0});

    // Identity for all basis states where any control qubit is 0.
    uint64_t ctrl_mask = ((uint64_t{1} << num_ctrls) - 1) << gw;
    for (uint32_t i = 0; i < total_dim; ++i) {
        if ((static_cast<uint64_t>(i) & ctrl_mask) != ctrl_mask) {
            data[static_cast<std::size_t>(i) * total_dim + i] = {1.0, 0.0};
        } else {
            uint32_t target_bits = i & ((1u << gw) - 1);
            for (uint32_t j = 0; j < gdim; ++j) {
                uint32_t col = (i & ~((1u << gw) - 1)) | j;
                data[static_cast<std::size_t>(i) * total_dim + col] =
                    (*gate_val.matrix_data)[
                        static_cast<std::size_t>(target_bits) * gdim + j];
            }
        }
    }

    return JanusValue::make_gate(total_w, std::move(data));
}


JanusValue BackendQuEST::builtin_numberofgates(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    const auto& val = args[0];
    const std::vector<std::vector<JanusValue>>* grid = nullptr;
    if (val.type_info.type == JanusType::CIRC && val.circ_data)
        grid = &val.circ_data->gate_grid;
    else if (val.type_info.type == JanusType::BLOCK && val.block_data)
        grid = &val.block_data->gate_grid;
    else
        report_error(line);

    uint64_t count = 0;
    for (const auto& row : *grid)
        for (const auto& entry : row)
            if (entry.type_info.type != JanusType::NULL_TYPE)
                ++count;
    return JanusValue::make_cnum(static_cast<double>(count));
}


JanusValue BackendQuEST::builtin_det(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    const auto& val = args[0];
    if ((val.type_info.type != JanusType::MATRIX &&
         val.type_info.type != JanusType::GATE) ||
        !val.matrix_data)
        report_error(line);
    uint32_t dim = val.type_info.matrix_rows;
    if (dim != val.type_info.matrix_cols) report_error(line);
    auto det = complex_determinant(*val.matrix_data, dim, line);
    return JanusValue::make_cnum_complex(
        static_cast<float>(det.real()),
        static_cast<float>(det.imag()));
}


JanusValue BackendQuEST::builtin_transpose(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    const auto& val = args[0];
    if ((val.type_info.type != JanusType::MATRIX &&
         val.type_info.type != JanusType::GATE) ||
        !val.matrix_data)
        report_error(line);
    uint32_t rows = val.type_info.matrix_rows;
    uint32_t cols = val.type_info.matrix_cols;
    std::vector<std::complex<double>> data(rows * cols);
    for (uint32_t r = 0; r < rows; ++r)
        for (uint32_t c = 0; c < cols; ++c)
            data[c * rows + r] = (*val.matrix_data)[r * cols + c];
    if (val.type_info.type == JanusType::GATE)
        return JanusValue::make_gate(val.type_info.width, std::move(data));
    return JanusValue::make_matrix(cols, rows, std::move(data));
}


JanusValue BackendQuEST::builtin_transposec(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    const auto& val = args[0];
    if ((val.type_info.type != JanusType::MATRIX &&
         val.type_info.type != JanusType::GATE) ||
        !val.matrix_data)
        report_error(line);
    uint32_t rows = val.type_info.matrix_rows;
    uint32_t cols = val.type_info.matrix_cols;
    std::vector<std::complex<double>> data(rows * cols);
    for (uint32_t r = 0; r < rows; ++r)
        for (uint32_t c = 0; c < cols; ++c)
            data[c * rows + r] = std::conj((*val.matrix_data)[r * cols + c]);
    if (val.type_info.type == JanusType::GATE)
        return JanusValue::make_gate(val.type_info.width, std::move(data));
    return JanusValue::make_matrix(cols, rows, std::move(data));
}


JanusValue BackendQuEST::builtin_evals(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    const auto& val = args[0];
    if ((val.type_info.type != JanusType::MATRIX &&
         val.type_info.type != JanusType::GATE) ||
        !val.matrix_data)
        report_error(line);
    uint32_t dim = val.type_info.matrix_rows;
    if (dim != val.type_info.matrix_cols) report_error(line);
    auto eigenvalues = complex_eigenvalues(*val.matrix_data, dim, line);
    std::vector<JanusValue> result_list;
    result_list.reserve(eigenvalues.size());
    for (const auto& ev : eigenvalues) {
        result_list.push_back(JanusValue::make_cnum_complex(
            static_cast<float>(ev.real()),
            static_cast<float>(ev.imag())));
    }
    return JanusValue::make_list(std::move(result_list));
}


JanusValue BackendQuEST::builtin_evecs(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    const auto& val = args[0];
    if ((val.type_info.type != JanusType::MATRIX &&
         val.type_info.type != JanusType::GATE) ||
        !val.matrix_data)
        report_error(line);
    uint32_t dim = val.type_info.matrix_rows;
    if (dim != val.type_info.matrix_cols) report_error(line);
    auto eigenvalues = complex_eigenvalues(*val.matrix_data, dim, line);
    auto evecs = complex_eigenvectors(
        *val.matrix_data, eigenvalues, dim, line);
    std::vector<JanusValue> outer_list;
    outer_list.reserve(evecs.size());
    for (const auto& vec : evecs) {
        std::vector<JanusValue> inner;
        inner.reserve(vec.size());
        for (const auto& c : vec) {
            inner.push_back(JanusValue::make_cnum_complex(
                static_cast<float>(c.real()),
                static_cast<float>(c.imag())));
        }
        outer_list.push_back(JanusValue::make_list(std::move(inner)));
    }
    return JanusValue::make_list(std::move(outer_list));
}


JanusValue BackendQuEST::builtin_gates(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    const auto& val = args[0];
    const std::vector<std::vector<JanusValue>>* grid = nullptr;
    if (val.type_info.type == JanusType::CIRC && val.circ_data)
        grid = &val.circ_data->gate_grid;
    else if (val.type_info.type == JanusType::BLOCK && val.block_data)
        grid = &val.block_data->gate_grid;
    else
        report_error(line);

    auto gate_list = circuit_synth::extract_gates(*grid);
    return JanusValue::make_list(std::move(gate_list));
}


JanusValue BackendQuEST::builtin_qubits(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    const auto& val = args[0];
    const std::vector<std::vector<JanusValue>>* grid = nullptr;
    if (val.type_info.type == JanusType::CIRC && val.circ_data)
        grid = &val.circ_data->gate_grid;
    else if (val.type_info.type == JanusType::BLOCK && val.block_data)
        grid = &val.block_data->gate_grid;
    else if (is_quantum(val.type_info.type)) {
        if (val.quantum_val)
            return JanusValue::make_cnum(
                static_cast<double>(val.quantum_val->num_qubits()));
        return JanusValue::make_cnum(0.0);
    } else
        report_error(line);

    return JanusValue::make_cnum(
        static_cast<double>(circuit_synth::qubit_count(*grid)));
}


JanusValue BackendQuEST::builtin_depth(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    const auto& val = args[0];
    const std::vector<std::vector<JanusValue>>* grid = nullptr;
    if (val.type_info.type == JanusType::CIRC && val.circ_data)
        grid = &val.circ_data->gate_grid;
    else if (val.type_info.type == JanusType::BLOCK && val.block_data)
        grid = &val.block_data->gate_grid;
    else
        report_error(line);
    return JanusValue::make_cnum(
        static_cast<double>(circuit_synth::circuit_depth(*grid)));
}


// Helper methods


double BackendQuEST::to_real(const JanusValue& val, uint32_t line) const {
    return val_to_real(val, line);
}


int64_t BackendQuEST::to_integer(const JanusValue& val, uint32_t line) const {
    return static_cast<int64_t>(val_to_real(val, line));
}


void BackendQuEST::apply_p32(QuantumState& qs) const {
    if (!p32_) return;
    auto& amps = qs.amplitudes();
    for (auto& a : amps) {
        float re = static_cast<float>(a.real());
        float im = static_cast<float>(a.imag());
        a = {static_cast<double>(re), static_cast<double>(im)};
    }
}


QuantumState BackendQuEST::tensor_product(const QuantumState& a,
                                           const QuantumState& b) {
    uint32_t na = a.num_qubits();
    uint32_t nb = b.num_qubits();
    uint32_t total = na + nb;
    uint64_t dim_a = a.num_amplitudes();
    uint64_t dim_b = b.num_amplitudes();
    uint64_t dim_total = dim_a * dim_b;

    QuantumState result(total);
    auto& res_amps = result.amplitudes();
    const auto& amps_a = a.amplitudes();
    const auto& amps_b = b.amplitudes();

    for (uint64_t i = 0; i < dim_a; ++i)
        for (uint64_t j = 0; j < dim_b; ++j)
            res_amps[i * dim_b + j] = amps_a[i] * amps_b[j];

    (void)dim_total;
    return result;
}


QuantumState BackendQuEST::execute_circuit(const JanusValue& circ,
                                            uint32_t line) {
    if (circ.type_info.type != JanusType::CIRC || !circ.circ_data)
        report_error(line);

    const auto& qubit_states = circ.circ_data->qubit_states;
    const auto& gate_grid = circ.circ_data->gate_grid;

    if (qubit_states.empty())
        report_error(line);

    // Build combined initial state via tensor product.
    QuantumState combined = qubit_states[0];
    for (std::size_t i = 1; i < qubit_states.size(); ++i) {
        combined = tensor_product(combined, qubit_states[i]);
    }

    apply_p32(combined);

    uint32_t num_qubits = combined.num_qubits();
    uint32_t num_lines = static_cast<uint32_t>(gate_grid.size());

    if (num_lines == 0) return combined;

    uint32_t time_steps = static_cast<uint32_t>(gate_grid[0].size());

    // Apply gates for each time step.
    for (uint32_t t = 0; t < time_steps; ++t) {
        uint32_t row = 0;
        while (row < num_lines) {
            const auto& entry = gate_grid[row][t];
            if (entry.type_info.type == JanusType::NULL_TYPE) {
                ++row;
                continue;
            }
            uint32_t gw = entry.type_info.width;
            if (gw == 0) gw = 1;

            // Build target qubit list for consecutive qubits.
            std::vector<uint32_t> targets(gw);
            for (uint32_t k = 0; k < gw; ++k) {
                targets[k] = row + k;
            }

            circuit_synth::apply_gate_to_state(
                combined, entry, targets, line);
            apply_p32(combined);

            row += gw;
        }
    }

    return combined;
}


// Matrix math: determinant via Gaussian elimination with partial pivoting.

std::complex<double> BackendQuEST::complex_determinant(
        const std::vector<std::complex<double>>& data,
        uint32_t dim, uint32_t line) {
    if (dim == 0) report_error(line);
    if (dim == 1) return data[0];

    // Work on a mutable copy.
    std::vector<std::complex<double>> M = data;
    std::complex<double> det{1.0, 0.0};

    for (uint32_t col = 0; col < dim; ++col) {
        // Partial pivoting: find the row with largest magnitude in this
        // column below the diagonal.
        uint32_t pivot_row = col;
        double best = std::abs(M[col * dim + col]);
        for (uint32_t r = col + 1; r < dim; ++r) {
            double mag = std::abs(M[r * dim + col]);
            if (mag > best) {
                best = mag;
                pivot_row = r;
            }
        }

        if (best < ZERO_TOL) return {0.0, 0.0};

        if (pivot_row != col) {
            for (uint32_t c = 0; c < dim; ++c) {
                std::swap(M[col * dim + c], M[pivot_row * dim + c]);
            }
            det = -det;
        }

        det *= M[col * dim + col];

        std::complex<double> pivot = M[col * dim + col];
        for (uint32_t r = col + 1; r < dim; ++r) {
            std::complex<double> factor = M[r * dim + col] / pivot;
            for (uint32_t c = col; c < dim; ++c) {
                M[r * dim + c] -= factor * M[col * dim + c];
            }
        }
    }

    return det;
}


// Eigenvalues via QR iteration with implicit shifts.
// Uses upper Hessenberg reduction followed by QR steps.

std::vector<std::complex<double>> BackendQuEST::complex_eigenvalues(
        const std::vector<std::complex<double>>& data,
        uint32_t dim, uint32_t line) {
    if (dim == 0) report_error(line);
    if (dim == 1) return {data[0]};

    const uint32_t n = dim;
    const uint32_t MAX_ITER = 1000;

    // Copy into working matrix H (will be reduced to upper Hessenberg).
    std::vector<std::complex<double>> H = data;

    // Reduce to upper Hessenberg form via Householder reflections.
    for (uint32_t k = 0; k + 2 < n; ++k) {
        // Build Householder vector for column k below the subdiagonal.
        std::vector<std::complex<double>> v(n - k - 1);
        for (uint32_t i = 0; i < n - k - 1; ++i)
            v[i] = H[(k + 1 + i) * n + k];

        double alpha = 0.0;
        for (const auto& vi : v) alpha += std::norm(vi);
        alpha = std::sqrt(alpha);
        if (alpha < ZERO_TOL) continue;

        if (std::abs(v[0]) > ZERO_TOL)
            alpha *= (v[0].real() > 0 ? 1.0 : -1.0);
        v[0] += std::complex<double>(alpha, 0.0);

        // Normalise v.
        double vnorm = 0.0;
        for (const auto& vi : v) vnorm += std::norm(vi);
        vnorm = std::sqrt(vnorm);
        if (vnorm < ZERO_TOL) continue;
        for (auto& vi : v) vi /= vnorm;

        // Apply H = (I - 2vv*) H (I - 2vv*) in two steps.
        // Left multiply: H <- (I - 2vv*) H for rows k+1..n-1.
        for (uint32_t j = 0; j < n; ++j) {
            std::complex<double> dot{0.0, 0.0};
            for (uint32_t i = 0; i < n - k - 1; ++i)
                dot += std::conj(v[i]) * H[(k + 1 + i) * n + j];
            for (uint32_t i = 0; i < n - k - 1; ++i)
                H[(k + 1 + i) * n + j] -= 2.0 * v[i] * dot;
        }
        // Right multiply: H <- H (I - 2vv*) for cols k+1..n-1.
        for (uint32_t i = 0; i < n; ++i) {
            std::complex<double> dot{0.0, 0.0};
            for (uint32_t j = 0; j < n - k - 1; ++j)
                dot += H[i * n + (k + 1 + j)] * v[j];
            for (uint32_t j = 0; j < n - k - 1; ++j)
                H[i * n + (k + 1 + j)] -= 2.0 * dot * std::conj(v[j]);
        }
    }

    // QR iteration on the Hessenberg matrix.
    std::vector<std::complex<double>> eigenvalues;
    eigenvalues.reserve(n);
    uint32_t p = n;

    while (p > 0) {
        if (p == 1) {
            eigenvalues.push_back(H[0]);
            break;
        }

        // Check for 1x1 or 2x2 deflation.
        if (std::abs(H[(p - 1) * n + (p - 2)]) < ZERO_TOL) {
            eigenvalues.push_back(H[(p - 1) * n + (p - 1)]);
            --p;
            continue;
        }

        if (p == 2) {
            // Solve 2x2 eigenvalue problem directly.
            auto a = H[0], b = H[1], c = H[n], d = H[n + 1];
            auto trace = a + d;
            auto det2 = a * d - b * c;
            auto disc = trace * trace - 4.0 * det2;
            auto sq = std::sqrt(disc);
            eigenvalues.push_back((trace + sq) / 2.0);
            eigenvalues.push_back((trace - sq) / 2.0);
            break;
        }

        // Implicit single-shift QR step with Wilkinson shift.
        std::complex<double> shift = H[(p - 1) * n + (p - 1)];

        uint32_t iter = 0;
        while (iter < MAX_ITER &&
               std::abs(H[(p - 1) * n + (p - 2)]) > ZERO_TOL) {
            // Shift.
            for (uint32_t i = 0; i < p; ++i)
                H[i * n + i] -= shift;

            // QR decomposition via Givens rotations.
            std::vector<double> cs(p - 1), sn_re(p - 1), sn_im(p - 1);
            for (uint32_t i = 0; i + 1 < p; ++i) {
                auto a = H[i * n + i];
                auto b = H[(i + 1) * n + i];
                double r = std::sqrt(std::norm(a) + std::norm(b));
                if (r < ZERO_TOL) {
                    cs[i] = 1.0;
                    sn_re[i] = 0.0;
                    sn_im[i] = 0.0;
                    continue;
                }
                cs[i] = std::abs(a) / r;
                auto phase = (std::abs(a) > ZERO_TOL)
                    ? a / std::abs(a) : std::complex<double>{1.0, 0.0};
                auto s = std::conj(phase) * b / r;
                sn_re[i] = s.real();
                sn_im[i] = s.imag();

                // Apply Givens rotation to rows i, i+1.
                for (uint32_t j = 0; j < p; ++j) {
                    auto t1 = H[i * n + j];
                    auto t2 = H[(i + 1) * n + j];
                    std::complex<double> sv{sn_re[i], sn_im[i]};
                    H[i * n + j] = cs[i] * t1 + sv * t2;
                    H[(i + 1) * n + j] = -std::conj(sv) * t1 + cs[i] * t2;
                }
            }

            // Multiply R * Q.
            for (uint32_t i = 0; i + 1 < p; ++i) {
                for (uint32_t j = 0; j < p; ++j) {
                    auto t1 = H[j * n + i];
                    auto t2 = H[j * n + (i + 1)];
                    std::complex<double> sv{sn_re[i], sn_im[i]};
                    H[j * n + i] = cs[i] * t1 + std::conj(sv) * t2;
                    H[j * n + (i + 1)] = -sv * t1 + cs[i] * t2;
                }
            }

            // Undo shift.
            for (uint32_t i = 0; i < p; ++i)
                H[i * n + i] += shift;

            ++iter;
        }

        // If QR did not converge, extract diagonal as approximate
        // eigenvalue. Conservative fallback.
        if (iter >= MAX_ITER) {
            eigenvalues.push_back(H[(p - 1) * n + (p - 1)]);
            --p;
        }
    }

    return eigenvalues;
}


// Eigenvectors: for each eigenvalue, find null space of (A - lambda I).

std::vector<std::vector<std::complex<double>>>
BackendQuEST::complex_eigenvectors(
        const std::vector<std::complex<double>>& data,
        const std::vector<std::complex<double>>& eigenvalues,
        uint32_t dim, uint32_t line) {
    std::vector<std::vector<std::complex<double>>> result;
    result.reserve(eigenvalues.size());

    for (const auto& lambda : eigenvalues) {
        // Build A - lambda * I.
        std::vector<std::complex<double>> M = data;
        for (uint32_t i = 0; i < dim; ++i)
            M[i * dim + i] -= lambda;

        // Gaussian elimination to find null space.
        uint32_t pivot_col = 0;
        std::vector<int> pivot_row_for_col(dim, -1);

        for (uint32_t r = 0; r < dim && pivot_col < dim; ++pivot_col) {
            // Find pivot in this column.
            uint32_t pr = r;
            double best = std::abs(M[r * dim + pivot_col]);
            for (uint32_t i = r + 1; i < dim; ++i) {
                double mag = std::abs(M[i * dim + pivot_col]);
                if (mag > best) { best = mag; pr = i; }
            }
            if (best < 1.0e-10) continue;

            if (pr != r) {
                for (uint32_t c = 0; c < dim; ++c)
                    std::swap(M[r * dim + c], M[pr * dim + c]);
            }

            pivot_row_for_col[pivot_col] = static_cast<int>(r);
            std::complex<double> pivot = M[r * dim + pivot_col];
            for (uint32_t c = 0; c < dim; ++c)
                M[r * dim + c] /= pivot;
            for (uint32_t i = 0; i < dim; ++i) {
                if (i == r) continue;
                std::complex<double> factor = M[i * dim + pivot_col];
                for (uint32_t c = 0; c < dim; ++c)
                    M[i * dim + c] -= factor * M[r * dim + c];
            }
            ++r;
        }

        // Find free variables and construct eigenvector.
        std::vector<std::complex<double>> vec(dim, {0.0, 0.0});
        bool found_free = false;
        for (uint32_t c = 0; c < dim; ++c) {
            if (pivot_row_for_col[c] < 0) {
                // Free variable: set to 1.
                vec[c] = {1.0, 0.0};
                found_free = true;
                // Backsubstitute.
                for (uint32_t cc = 0; cc < dim; ++cc) {
                    if (pivot_row_for_col[cc] >= 0) {
                        uint32_t pr2 =
                            static_cast<uint32_t>(pivot_row_for_col[cc]);
                        vec[cc] -= M[pr2 * dim + c];
                    }
                }
                break;
            }
        }
        if (!found_free) {
            // All columns are pivots: near-zero eigenvalue rounding.
            // Use last column as approximate eigenvector.
            vec[dim - 1] = {1.0, 0.0};
        }

        // Normalise.
        double norm = 0.0;
        for (const auto& vi : vec) norm += std::norm(vi);
        norm = std::sqrt(norm);
        if (norm > ZERO_TOL) {
            for (auto& vi : vec) vi /= norm;
        }

        result.push_back(std::move(vec));
    }

    return result;
}


JanusValue BackendQuEST::builtin_run(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    QuantumState final_state = execute_circuit(args[0], line);
    uint64_t outcome = final_state.measure(line);
    return JanusValue::make_cnum(static_cast<double>(outcome));
}


JanusValue BackendQuEST::builtin_runh(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.size() < 2) report_error(line);
    uint32_t shots = static_cast<uint32_t>(val_to_real(args[1], line));
    if (shots == 0) shots = 1;

    std::vector<JanusValue> results;
    results.reserve(shots);
    for (uint32_t s = 0; s < shots; ++s) {
        QuantumState final_state = execute_circuit(args[0], line);
        uint64_t outcome = final_state.measure(line);
        results.push_back(
            JanusValue::make_cnum(static_cast<double>(outcome)));
    }
    return JanusValue::make_list(std::move(results));
}


JanusValue BackendQuEST::builtin_isunitary(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    const auto& val = args[0];
    if ((val.type_info.type != JanusType::MATRIX &&
         val.type_info.type != JanusType::GATE) ||
        !val.matrix_data)
        return JanusValue::make_cbit(0);
    uint32_t dim = val.type_info.matrix_rows;
    if (dim == 0 || dim != val.type_info.matrix_cols)
        return JanusValue::make_cbit(0);
    if ((dim & (dim - 1)) != 0)
        return JanusValue::make_cbit(0);
    bool result = gate_library::is_unitary(*val.matrix_data, dim);
    return JanusValue::make_cbit(result ? 1 : 0);
}


JanusValue BackendQuEST::builtin_sameoutput(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.size() < 2) report_error(line);
    QuantumState s1 = execute_circuit(args[0], line);
    QuantumState s2 = execute_circuit(args[1], line);

    if (s1.num_qubits() != s2.num_qubits())
        return JanusValue::make_cbit(0);

    // Compare probability distributions.
    const auto& a1 = s1.amplitudes();
    const auto& a2 = s2.amplitudes();
    for (uint64_t i = 0; i < a1.size(); ++i) {
        double p1 = std::norm(a1[i]);
        double p2 = std::norm(a2[i]);
        if (std::abs(p1 - p2) > 1.0e-10)
            return JanusValue::make_cbit(0);
    }
    return JanusValue::make_cbit(1);
}


JanusValue BackendQuEST::builtin_print(
        const std::vector<JanusValue>& args, uint32_t line) {
    (void)line;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) std::cout << " ";
        std::cout << args[i].to_string();
    }
    std::cout << std::endl;
    return JanusValue::make_null();
}


JanusValue BackendQuEST::builtin_delete(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.size() < 2) report_error(line);
    JanusValue collection = args[0];
    if (collection.type_info.type == JanusType::LIST) {
        if (!collection.list_data) report_error(line);
        int64_t idx = to_integer(args[1], line);
        auto& list = *collection.list_data;
        if (idx < 0) idx += static_cast<int64_t>(list.size());
        if (idx < 0 || static_cast<uint64_t>(idx) >= list.size())
            report_error(line);
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(idx));
        return collection;
    }
    if (collection.type_info.type == JanusType::CSTR) {
        int64_t idx = to_integer(args[1], line);
        auto& s = collection.str_val;
        if (idx < 0) idx += static_cast<int64_t>(s.size());
        if (idx < 0 || static_cast<uint64_t>(idx) >= s.size())
            report_error(line);
        s.erase(static_cast<std::size_t>(idx), 1);
        collection.type_info = make_cstr_type(
            static_cast<uint32_t>(s.size()));
        return collection;
    }
    report_error(line);
}


JanusValue BackendQuEST::builtin_sin(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    double v = val_to_real(args[0], line);
    return JanusValue::make_cnum(std::sin(v));
}


JanusValue BackendQuEST::builtin_cos(
        const std::vector<JanusValue>& args, uint32_t line) {
    if (args.empty()) report_error(line);
    double v = val_to_real(args[0], line);
    return JanusValue::make_cnum(std::cos(v));
}


} // namespace janus
