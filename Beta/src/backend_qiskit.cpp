#include "backend_qiskit.hpp"
#include "ir.hpp"
#include "types.hpp"
#include "gate_library.hpp"
#include "error.hpp"

#include <cstdint>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace janus {

namespace {

// Emitter :: internal state machine that walks the IR and produces Python.
//
// emit_expr() may emit Python statements as a side effect (for expressions
// that require statement-level constructs in Python, such as if/for/while)
// and returns a Python expression string that evaluates to the result.
//
// emit_stmt() emits complete Python statements.

class Emitter {
public:
    std::string run(const IRProgram& program);

private:
    std::ostringstream out_;
    uint32_t indent_ = 0;
    uint32_t temp_count_ = 0;

    // Tracks whether we are currently inside a function body.
    // Determines whether return emits "return x" (function) or
    // "sys.exit(int(x) % 256)" (top-level).
    uint32_t function_depth_ = 0;

    std::string fresh_temp();
    void line(const std::string& text);
    void blank_line();
    void emit_preamble();
    void emit_stmts(const std::vector<IRStmtPtr>& stmts);
    void emit_stmt(const IRStmt& stmt);
    std::string emit_expr(const IRExpr& expr);

    // Specialised emitters.
    std::string emit_integer_literal(const IRIntegerLiteral& e);
    std::string emit_float_literal(const IRFloatLiteral& e);
    std::string emit_string_literal(const IRStringLiteral& e);
    std::string emit_bool_literal(const IRBoolLiteral& e);
    std::string emit_null_literal();
    std::string emit_pi_literal();
    std::string emit_e_literal();
    std::string emit_interpolated_string(const IRInterpolatedString& e);
    std::string emit_variable(const IRVariable& e);
    std::string emit_index(const IRIndex& e);
    std::string emit_qnum_index(const IRQnumIndex& e);
    std::string emit_qnum_index_assign(const IRQnumIndexAssign& e);
    std::string emit_assign(const IRAssign& e);
    std::string emit_binary(const IRBinary& e);
    std::string emit_unary(const IRUnary& e);
    std::string emit_postfix_bang(const IRPostfixBang& e);
    std::string emit_type_cast(const IRTypeCast& e);
    std::string emit_matrix_literal(const IRMatrixLiteral& e);
    std::string emit_call(const IRCall& e);
    std::string emit_builtin_call(const IRBuiltinCall& e);
    std::string emit_gate_library(const IRGateLibrary& e);
    std::string emit_type_construct(const IRTypeConstruct& e);
    std::string emit_function_def(const IRFunctionDef& e,
                                  const std::string& assigned_name);
    std::string emit_if(const IRIf& e);
    std::string emit_for(const IRFor& e);
    std::string emit_while(const IRWhile& e);
    std::string emit_foreach(const IRForeach& e);

    static std::string py_escape(const std::string& s);
    static std::string py_double(double v);

    // Returns true when the expression already emits full Python
    // statements and the returned string should not be emitted again
    // as a bare line by emit_stmt.
    static bool is_stmt_expression(const IRExpr& expr);
};


std::string Emitter::fresh_temp() {
    return "_jt" + std::to_string(temp_count_++);
}

void Emitter::line(const std::string& text) {
    for (uint32_t i = 0; i < indent_; ++i) {
        out_ << "    ";
    }
    out_ << text << "\n";
}

void Emitter::blank_line() {
    out_ << "\n";
}

std::string Emitter::py_escape(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 2);
    result += '"';
    for (char c : s) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"':  result += "\\\""; break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:   result += c;      break;
        }
    }
    result += '"';
    return result;
}

std::string Emitter::py_double(double v) {
    if (std::isinf(v)) {
        return v > 0 ? "float('inf')" : "float('-inf')";
    }
    if (std::isnan(v)) {
        return "float('nan')";
    }
    std::ostringstream oss;
    oss << std::setprecision(std::numeric_limits<double>::max_digits10) << v;
    std::string s = oss.str();
    if (s.find('.') == std::string::npos &&
        s.find('e') == std::string::npos &&
        s.find('E') == std::string::npos) {
        s += ".0";
    }
    return s;
}

bool Emitter::is_stmt_expression(const IRExpr& expr) {
    return dynamic_cast<const IRAssign*>(&expr) ||
           dynamic_cast<const IRQnumIndexAssign*>(&expr) ||
           dynamic_cast<const IRIf*>(&expr) ||
           dynamic_cast<const IRFor*>(&expr) ||
           dynamic_cast<const IRWhile*>(&expr) ||
           dynamic_cast<const IRForeach*>(&expr) ||
           dynamic_cast<const IRFunctionDef*>(&expr);
}


// Preamble: all imports and runtime helper functions emitted at the top
// of the generated Python file.

void Emitter::emit_preamble() {
    line("#!/usr/bin/env python3");
    line("# Generated by the Janus compiler (Qiskit backend)");
    blank_line();
    line("import math");
    line("import sys");
    line("import copy");
    line("import numpy as np");
    line("from qiskit import QuantumCircuit");
    line("from qiskit.quantum_info import Statevector, Operator");
    line("from qiskit.circuit.library import (");
    line("    IGate, XGate, YGate, ZGate, HGate, SGate, SdgGate,");
    line("    TGate, TdgGate, SXGate, SXdgGate,");
    line("    CXGate, CYGate, CZGate, CHGate, SwapGate, iSwapGate,");
    line("    CCXGate, CSwapGate,");
    line("    RXGate, RYGate, RZGate, PhaseGate, UGate,");
    line("    CRXGate, CRYGate, CRZGate, CPhaseGate,");
    line("    RXXGate, RYYGate, RZZGate,");
    line(")");
    blank_line();
    line("# Janus runtime helpers");
    blank_line();

    // Truthiness.
    line("def _janus_truthy(v):");
    line("    if v is None:");
    line("        return False");
    line("    if isinstance(v, bool):");
    line("        return v");
    line("    if isinstance(v, (int, float)):");
    line("        return v != 0");
    line("    if isinstance(v, complex):");
    line("        return v != 0");
    line("    if isinstance(v, str):");
    line("        return len(v) > 0");
    line("    if isinstance(v, Statevector):");
    line("        p = v.probabilities()");
    line("        return len(p) == 0 or abs(p[0] - 1.0) > 1e-15");
    line("    if isinstance(v, list):");
    line("        return len(v) > 0");
    line("    if isinstance(v, np.ndarray):");
    line("        return bool(np.any(v != 0))");
    line("    if isinstance(v, Operator):");
    line("        return bool(np.any(v.data != 0))");
    line("    if isinstance(v, dict):");
    line("        g = v.get('grid', [])");
    line("        return bool(g and g[0])");
    line("    if callable(v):");
    line("        return True");
    line("    return bool(v)");
    blank_line();

    line("def _janus_qubit():");
    line("    return Statevector.from_label('0')");
    blank_line();

    line("def _janus_qnum(val, max_width=0):");
    line("    val = int(val)");
    line("    if val < 0:");
    line("        val = 0");
    line("    n = max(1, val.bit_length()) if val > 0 else 1");
    line("    if 0 < max_width < n:");
    line("        print('Error (line 0): unknown error', file=sys.stderr)");
    line("        sys.exit(1)");
    line("    return Statevector.from_int(val, 2 ** n)");
    blank_line();

    // Quantum amplitude basis index resolver.  Accepts numeric indices
    // and string indices (binary literals or decimal strings), validates
    // the result against the register dimension, and returns a Python
    // integer.  _janus_error is defined later in the preamble; Python
    // resolves it at call time so forward reference is fine.
    line("def _janus_qbasis(idx, nq):");
    line("    dim = 1 << int(nq)");
    line("    if isinstance(idx, str):");
    line("        if len(idx) == 0:");
    line("            _janus_error(0)");
    line("        if all(c == '0' or c == '1' for c in idx):");
    line("            b = int(idx, 2)");
    line("        else:");
    line("            try:");
    line("                b = int(idx, 10)");
    line("            except Exception:");
    line("                _janus_error(0)");
    line("    else:");
    line("        b = int(idx)");
    line("    if b < 0 or b >= dim:");
    line("        _janus_error(0)");
    line("    return b");
    blank_line();

    line("def _janus_measure(sv):");
    line("    outcome, new_sv = sv.measure()");
    line("    return int(outcome, 2), new_sv");
    blank_line();

    line("def _janus_peek(sv):");
    line("    return int(np.argmax(sv.probabilities()))");
    blank_line();

    line("def _janus_state(sv):");
    line("    n = sv.num_qubits");
    line("    parts = []");
    line("    for i, amp in enumerate(sv.data):");
    line("        if abs(amp) < 1e-15:");
    line("            continue");
    line("        label = format(i, '0' + str(n) + 'b')");
    line("        re, im = amp.real, amp.imag");
    line("        if abs(im) < 1e-15:");
    line("            coeff = _janus_fmt_num(re)");
    line("        elif abs(re) < 1e-15:");
    line("            coeff = _janus_fmt_num(im) + 'i'");
    line("        else:");
    line("            sign = '+' if im >= 0 else ''");
    line("            coeff = '(' + _janus_fmt_num(re) + sign + _janus_fmt_num(im) + 'i)'");
    line("        parts.append(coeff + '|' + label + '>')");
    line("    return ' + '.join(parts) if parts else '0'");
    blank_line();

    line("def _janus_expect(matrix, sv):");
    line("    m = matrix.data if isinstance(matrix, Operator) else np.array(matrix)");
    line("    v = sv.data");
    line("    return complex(np.conj(v) @ m @ v).real");
    blank_line();

    line("def _janus_ctrle(gate_op, num_controls):");
    line("    base = gate_op if isinstance(gate_op, Operator) else Operator(np.array(gate_op))");
    line("    n = base.num_qubits");
    line("    total = n + int(num_controls)");
    line("    dim = 2 ** total");
    line("    gate_dim = 2 ** n");
    line("    ctrl_dim = 2 ** int(num_controls)");
    line("    u = np.eye(dim, dtype=complex)");
    line("    s = (ctrl_dim - 1) * gate_dim");
    line("    u[s:s+gate_dim, s:s+gate_dim] = base.data");
    line("    return Operator(u)");
    blank_line();

    line("def _janus_apply_gate(sv, gate_op, targets):");
    line("    if gate_op is None:");
    line("        return sv");
    line("    op = gate_op if isinstance(gate_op, Operator) else Operator(np.array(gate_op))");
    line("    n = sv.num_qubits");
    line("    qargs = [n - 1 - t for t in targets]");
    line("    return sv.evolve(op, qargs=qargs)");
    blank_line();

    line("def _janus_run(circ_data):");
    line("    states = circ_data.get('states', [])");
    line("    grid = circ_data.get('grid', [])");
    line("    if not grid or not states:");
    line("        return list(states)");
    line("    num_lines = len(grid)");
    line("    if len(states) == 1 and states[0].num_qubits == num_lines:");
    line("        combined = states[0].copy()");
    line("    elif len(states) == 1:");
    line("        combined = states[0].copy()");
    line("    else:");
    line("        combined = states[0]");
    line("        for s in states[1:]:");
    line("            combined = combined.tensor(s)");
    line("    time_steps = len(grid[0]) if grid else 0");
    line("    for t in range(time_steps):");
    line("        row = 0");
    line("        while row < num_lines:");
    line("            g = grid[row][t]");
    line("            if g is None:");
    line("                row += 1");
    line("                continue");
    line("            if not isinstance(g, Operator):");
    line("                row += 1");
    line("                continue");
    line("            w = g.num_qubits");
    line("            targets = list(range(row, row + w))");
    line("            combined = _janus_apply_gate(combined, g, targets)");
    line("            row += w");
    line("    return [combined]");
    blank_line();

    line("def _janus_runh(circ_data, shots):");
    line("    result = _janus_run(circ_data)");
    line("    if not result:");
    line("        return {}");
    line("    return dict(result[0].sample_counts(int(shots)))");
    blank_line();

    line("def _janus_isunitary(m):");
    line("    d = m.data if isinstance(m, Operator) else np.array(m)");
    line("    if not isinstance(d, np.ndarray) or d.ndim != 2:");
    line("        return False");
    line("    return bool(np.allclose(d.conj().T @ d, np.eye(d.shape[0])))");
    blank_line();

    line("def _janus_sameoutput(c1, c2):");
    line("    s1, s2 = _janus_run(c1), _janus_run(c2)");
    line("    if not s1 or not s2:");
    line("        return not s1 and not s2");
    line("    return bool(np.allclose(s1[0].data, s2[0].data))");
    blank_line();

    line("def _janus_numberofgates(cd):");
    line("    return sum(1 for r in cd.get('grid', []) for e in r if e is not None)");
    blank_line();

    line("def _janus_depth(cd):");
    line("    g = cd.get('grid', [])");
    line("    return len(g[0]) if g else 0");
    blank_line();

    line("def _janus_qubits(cd):");
    line("    return len(cd.get('grid', []))");
    blank_line();

    line("def _janus_gates_list(cd):");
    line("    return [e for r in cd.get('grid', []) for e in r if e is not None]");
    blank_line();

    line("def _janus_delete(coll, idx):");
    line("    c = list(coll)");
    line("    if isinstance(idx, int) and 0 <= idx < len(c):");
    line("        del c[idx]");
    line("    return c");
    blank_line();

    line("def _janus_det(m):");
    line("    d = m.data if isinstance(m, Operator) else np.array(m)");
    line("    return complex(np.linalg.det(d))");
    blank_line();

    line("def _janus_transpose(m):");
    line("    if isinstance(m, Operator):");
    line("        return Operator(m.data.T)");
    line("    return np.array(m).T");
    blank_line();

    line("def _janus_transposec(m):");
    line("    if isinstance(m, Operator):");
    line("        return Operator(m.data.conj().T)");
    line("    return np.array(m).conj().T");
    blank_line();

    line("def _janus_evals(m):");
    line("    d = m.data if isinstance(m, Operator) else np.array(m)");
    line("    return list(np.linalg.eigvals(d))");
    blank_line();

    line("def _janus_evecs(m):");
    line("    d = m.data if isinstance(m, Operator) else np.array(m)");
    line("    _, vecs = np.linalg.eig(d)");
    line("    return [vecs[:, i] for i in range(vecs.shape[1])]");
    blank_line();

    line("def _janus_tensor(a, b):");
    line("    if isinstance(a, Operator) and isinstance(b, Operator):");
    line("        return a.tensor(b)");
    line("    if isinstance(a, Statevector) and isinstance(b, Statevector):");
    line("        return a.tensor(b)");
    line("    aa = a.data if isinstance(a, (Operator, Statevector)) else np.array(a)");
    line("    bb = b.data if isinstance(b, (Operator, Statevector)) else np.array(b)");
    line("    return np.kron(aa, bb)");
    blank_line();

    line("def _janus_to_str(v):");
    line("    if v is None:");
    line("        return 'null'");
    line("    if isinstance(v, bool):");
    line("        return '1' if v else '0'");
    line("    if isinstance(v, Statevector):");
    line("        return _janus_state(v)");
    line("    if isinstance(v, Operator):");
    line("        return 'gate(' + str(v.data.tolist()) + ')'");
    line("    if isinstance(v, np.ndarray):");
    line("        return str(v.tolist())");
    line("    if isinstance(v, complex) and not isinstance(v, bool):");
    line("        if abs(v.imag) < 1e-15:");
    line("            return _janus_fmt_num(v.real)");
    line("        if abs(v.real) < 1e-15:");
    line("            return _janus_fmt_num(v.imag) + 'i'");
    line("        sign = '+' if v.imag >= 0 else ''");
    line("        return '(' + _janus_fmt_num(v.real) + sign + _janus_fmt_num(v.imag) + 'i)'");
    line("    if isinstance(v, float):");
    line("        return _janus_fmt_num(v)");
    line("    return str(v)");
    blank_line();

    line("def _janus_fmt_num(v):");
    line("    if v == 0.0:");
    line("        return '0'");
    line("    if v == int(v) and abs(v) < 2**53:");
    line("        return str(int(v))");
    line("    return repr(v)");
    blank_line();

    line("def _janus_error(ln):");
    line("    print(f'Error (line {ln}): unknown error', file=sys.stderr)");
    line("    sys.exit(1)");
    blank_line();

    line("def _janus_build_circ(states, grid):");
    line("    return {'states': list(states), 'grid': [list(r) for r in grid]}");
    blank_line();

    line("def _janus_build_block(grid):");
    line("    return {'grid': [list(r) for r in grid]}");
    blank_line();

    line("# End of Janus runtime helpers");
    blank_line();
    line("# Program begins");
    blank_line();
}


// Top-level entry.

std::string Emitter::run(const IRProgram& program) {
    emit_preamble();
    emit_stmts(program.statements);
    return out_.str();
}


// Statement emission.

void Emitter::emit_stmts(const std::vector<IRStmtPtr>& stmts) {
    for (const auto& stmt : stmts) {
        if (stmt) {
            emit_stmt(*stmt);
        }
    }
}

void Emitter::emit_stmt(const IRStmt& stmt) {
    if (const auto* es = dynamic_cast<const IRExprStmt*>(&stmt)) {
        // Special case: function definition assignment becomes def.
        if (const auto* assign = dynamic_cast<const IRAssign*>(es->expr.get())) {
            if (const auto* fdef = dynamic_cast<const IRFunctionDef*>(assign->value.get())) {
                if (const auto* var = dynamic_cast<const IRVariable*>(assign->target.get())) {
                    emit_function_def(*fdef, var->name);
                    return;
                }
            }
        }
        std::string val = emit_expr(*es->expr);
        // Expressions that emit their own statements should not be
        // re-emitted as a bare line.
        if (!is_stmt_expression(*es->expr) && !val.empty()) {
            line(val);
        }
        return;
    }

    if (dynamic_cast<const IRBreakStmt*>(&stmt)) {
        line("break");
        return;
    }

    if (dynamic_cast<const IRContinueStmt*>(&stmt)) {
        line("continue");
        return;
    }

    if (const auto* rs = dynamic_cast<const IRReturnStmt*>(&stmt)) {
        if (function_depth_ > 0) {
            if (rs->value) {
                std::string val = emit_expr(*rs->value);
                line("return " + val);
            } else {
                line("return None");
            }
        } else {
            if (rs->value) {
                std::string val = emit_expr(*rs->value);
                line("sys.exit(int(" + val + ") % 256)");
            } else {
                line("sys.exit(0)");
            }
        }
        return;
    }

    report_error(stmt.line);
}


// Expression dispatch.

std::string Emitter::emit_expr(const IRExpr& expr) {
    if (const auto* e = dynamic_cast<const IRIntegerLiteral*>(&expr))
        return emit_integer_literal(*e);
    if (const auto* e = dynamic_cast<const IRFloatLiteral*>(&expr))
        return emit_float_literal(*e);
    if (const auto* e = dynamic_cast<const IRStringLiteral*>(&expr))
        return emit_string_literal(*e);
    if (const auto* e = dynamic_cast<const IRBoolLiteral*>(&expr))
        return emit_bool_literal(*e);
    if (dynamic_cast<const IRNullLiteral*>(&expr))
        return emit_null_literal();
    if (dynamic_cast<const IRPiLiteral*>(&expr))
        return emit_pi_literal();
    if (dynamic_cast<const IRELiteral*>(&expr))
        return emit_e_literal();
    if (const auto* e = dynamic_cast<const IRInterpolatedString*>(&expr))
        return emit_interpolated_string(*e);
    if (const auto* e = dynamic_cast<const IRVariable*>(&expr))
        return emit_variable(*e);
    if (const auto* e = dynamic_cast<const IRIndex*>(&expr))
        return emit_index(*e);
    if (const auto* e = dynamic_cast<const IRQnumIndex*>(&expr))
        return emit_qnum_index(*e);
    if (const auto* e = dynamic_cast<const IRQnumIndexAssign*>(&expr))
        return emit_qnum_index_assign(*e);
    if (const auto* e = dynamic_cast<const IRAssign*>(&expr))
        return emit_assign(*e);
    if (const auto* e = dynamic_cast<const IRBinary*>(&expr))
        return emit_binary(*e);
    if (const auto* e = dynamic_cast<const IRUnary*>(&expr))
        return emit_unary(*e);
    if (const auto* e = dynamic_cast<const IRPostfixBang*>(&expr))
        return emit_postfix_bang(*e);
    if (const auto* e = dynamic_cast<const IRTypeCast*>(&expr))
        return emit_type_cast(*e);
    if (const auto* e = dynamic_cast<const IRMatrixLiteral*>(&expr))
        return emit_matrix_literal(*e);
    if (const auto* e = dynamic_cast<const IRCall*>(&expr))
        return emit_call(*e);
    if (const auto* e = dynamic_cast<const IRBuiltinCall*>(&expr))
        return emit_builtin_call(*e);
    if (const auto* e = dynamic_cast<const IRGateLibrary*>(&expr))
        return emit_gate_library(*e);
    if (const auto* e = dynamic_cast<const IRTypeConstruct*>(&expr))
        return emit_type_construct(*e);
    if (const auto* e = dynamic_cast<const IRFunctionDef*>(&expr))
        return emit_function_def(*e, "");
    if (const auto* e = dynamic_cast<const IRIf*>(&expr))
        return emit_if(*e);
    if (const auto* e = dynamic_cast<const IRFor*>(&expr))
        return emit_for(*e);
    if (const auto* e = dynamic_cast<const IRWhile*>(&expr))
        return emit_while(*e);
    if (const auto* e = dynamic_cast<const IRForeach*>(&expr))
        return emit_foreach(*e);
    report_error(expr.line);
}


// Literals.

std::string Emitter::emit_integer_literal(const IRIntegerLiteral& e) {
    return std::to_string(e.value);
}

std::string Emitter::emit_float_literal(const IRFloatLiteral& e) {
    return py_double(e.value);
}

std::string Emitter::emit_string_literal(const IRStringLiteral& e) {
    return py_escape(e.value);
}

std::string Emitter::emit_bool_literal(const IRBoolLiteral& e) {
    return e.value ? "True" : "False";
}

std::string Emitter::emit_null_literal() { return "None"; }
std::string Emitter::emit_pi_literal()   { return "math.pi"; }
std::string Emitter::emit_e_literal()    { return "math.e"; }


// Interpolated string.

std::string Emitter::emit_interpolated_string(const IRInterpolatedString& e) {
    std::string result = "f\"";
    for (size_t i = 0; i < e.expressions.size(); ++i) {
        for (char c : e.segments[i]) {
            switch (c) {
                case '{':  result += "{{"; break;
                case '}':  result += "}}"; break;
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                default:   result += c; break;
            }
        }
        std::string sub = emit_expr(*e.expressions[i]);
        result += "{_janus_to_str(" + sub + ")}";
    }
    if (!e.segments.empty()) {
        for (char c : e.segments.back()) {
            switch (c) {
                case '{':  result += "{{"; break;
                case '}':  result += "}}"; break;
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                default:   result += c; break;
            }
        }
    }
    result += "\"";
    return result;
}


// Variable.

std::string Emitter::emit_variable(const IRVariable& e) {
    return e.name;
}


// Index access.

std::string Emitter::emit_index(const IRIndex& e) {
    std::string obj = emit_expr(*e.object);
    if (e.indices.size() == 1) {
        std::string idx = emit_expr(*e.indices[0]);
        return obj + "[int(" + idx + ")]";
    }
    if (e.indices.size() == 2) {
        std::string row = emit_expr(*e.indices[0]);
        std::string col = emit_expr(*e.indices[1]);
        return obj + "[int(" + row + "), int(" + col + ")]";
    }
    report_error(e.line);
}


// Quantum amplitude read: qnum_or_qubit[index].
// Always routed through _janus_qbasis so that string and numeric indices
// share a single validated resolution path.  Uses .data so that the
// resulting expression is a numpy complex scalar, matching the write
// path which mutates OBJ.data.

std::string Emitter::emit_qnum_index(const IRQnumIndex& e) {
    std::string obj = emit_expr(*e.object);
    std::string idx = emit_expr(*e.index);
    return obj + ".data[_janus_qbasis(" + idx + ", " + obj + ".num_qubits)]";
}


// Quantum amplitude write: qnum_or_qubit[index] = value.

std::string Emitter::emit_qnum_index_assign(const IRQnumIndexAssign& e) {
    // The object is always an IRVariable (enforced by the type checker).
    auto* target_var = dynamic_cast<const IRVariable*>(e.object.get());
    std::string var_name = target_var->name;

    // Compute index expression unconditionally; _janus_qbasis handles
    // literal strings, runtime string variables, and numeric expressions.
    std::string idx_src = emit_expr(*e.index);

    // Evaluate the value.  If the value is a quantum register, measure it
    // first to obtain a classical scalar, mirroring the runtime
    // measurement-on-classical-assignment rule.
    std::string scalar;
    JanusType val_type = e.value->result_type.type;
    if (val_type == JanusType::QNUM || val_type == JanusType::QUBIT) {
        std::string val_expr = emit_expr(*e.value);
        std::string tv = fresh_temp();
        std::string ts = fresh_temp();
        line(tv + ", " + ts + " = _janus_measure(" + val_expr + ")");
        // Update the source variable with the collapsed state.
        if (const auto* val_var = dynamic_cast<const IRVariable*>(e.value.get())) {
            line(val_var->name + " = " + ts);
        }
        scalar = tv;
    } else {
        scalar = emit_expr(*e.value);
    }

    // Resolve the basis index, mutate a copy of the underlying data
    // array, renormalise, and rebuild the Statevector.
    std::string tb = fresh_temp();
    std::string td = fresh_temp();
    std::string tn = fresh_temp();
    line(tb + " = _janus_qbasis(" + idx_src + ", " + var_name + ".num_qubits)");
    line(td + " = " + var_name + ".data.copy()");
    line(td + "[" + tb + "] = " + scalar);
    line(tn + " = np.linalg.norm(" + td + ")");
    line(var_name + " = Statevector(" + td + " / " + tn + ")");

    // The expression value is the scalar that was written, consistent
    // with how other assignment expressions are emitted.
    return scalar;
}


// Assignment.

std::string Emitter::emit_assign(const IRAssign& e) {
    // Function definition on the RHS.
    if (const auto* fdef = dynamic_cast<const IRFunctionDef*>(e.value.get())) {
        if (const auto* var = dynamic_cast<const IRVariable*>(e.target.get())) {
            return emit_function_def(*fdef, var->name);
        }
    }

    std::string val = emit_expr(*e.value);

    // Deep copy for mutable Python types to enforce Janus value semantics.
    JanusType vt = e.value->result_type.type;
    bool needs_copy = (vt == JanusType::QUBIT || vt == JanusType::QNUM ||
                       vt == JanusType::LIST  || vt == JanusType::CIRC ||
                       vt == JanusType::BLOCK);
    std::string rhs = needs_copy ? "copy.deepcopy(" + val + ")" : val;

    if (const auto* var = dynamic_cast<const IRVariable*>(e.target.get())) {
        line(var->name + " = " + rhs);
        return var->name;
    }
    if (const auto* idx = dynamic_cast<const IRIndex*>(e.target.get())) {
        std::string obj = emit_expr(*idx->object);
        if (idx->indices.size() == 1) {
            std::string i = emit_expr(*idx->indices[0]);
            line(obj + "[int(" + i + ")] = " + rhs);
        } else if (idx->indices.size() == 2) {
            std::string r = emit_expr(*idx->indices[0]);
            std::string c = emit_expr(*idx->indices[1]);
            line(obj + "[int(" + r + "), int(" + c + ")] = " + rhs);
        }
        return rhs;
    }
    std::string target = emit_expr(*e.target);
    line(target + " = " + rhs);
    return target;
}


// Binary.

std::string Emitter::emit_binary(const IRBinary& e) {
    std::string l = emit_expr(*e.left);
    std::string r = emit_expr(*e.right);
    switch (e.op) {
        case IRBinaryOp::ADD:     return "(" + l + " + " + r + ")";
        case IRBinaryOp::SUB:     return "(" + l + " - " + r + ")";
        case IRBinaryOp::MUL:     return "(" + l + " * " + r + ")";
        case IRBinaryOp::DIV:     return "(" + l + " / " + r + ")";
        case IRBinaryOp::INT_DIV: return "(" + l + " // " + r + ")";
        case IRBinaryOp::MOD:     return "(" + l + " % " + r + ")";
        case IRBinaryOp::EXP:     return "(" + l + " ** " + r + ")";
        case IRBinaryOp::EQ:      return "(" + l + " == " + r + ")";
        case IRBinaryOp::LT:      return "(" + l + " < " + r + ")";
        case IRBinaryOp::GT:      return "(" + l + " > " + r + ")";
        case IRBinaryOp::LE:      return "(" + l + " <= " + r + ")";
        case IRBinaryOp::GE:      return "(" + l + " >= " + r + ")";
        case IRBinaryOp::AND:     return "(" + l + " & " + r + ")";
        case IRBinaryOp::NAND:    return "(~(" + l + " & " + r + "))";
        case IRBinaryOp::OR:      return "(" + l + " | " + r + ")";
        case IRBinaryOp::NOR:     return "(~(" + l + " | " + r + "))";
        case IRBinaryOp::XOR:     return "(" + l + " ^ " + r + ")";
        case IRBinaryOp::XNOR:    return "(~(" + l + " ^ " + r + "))";
        case IRBinaryOp::TENSOR:  return "_janus_tensor(" + l + ", " + r + ")";
    }
    report_error(e.line);
}


// Unary.

std::string Emitter::emit_unary(const IRUnary& e) {
    std::string o = emit_expr(*e.operand);
    switch (e.op) {
        case IRUnaryOp::NEG:         return "(-(" + o + "))";
        case IRUnaryOp::BITWISE_NOT: return "(~(" + o + "))";
        case IRUnaryOp::BOOL_NOT:    return "(not _janus_truthy(" + o + "))";
        case IRUnaryOp::SHIFT_LEFT:  return "(int(" + o + ") << 1)";
        case IRUnaryOp::SHIFT_RIGHT: return "(int(" + o + ") >> 1)";
    }
    report_error(e.line);
}


// Postfix bang.

std::string Emitter::emit_postfix_bang(const IRPostfixBang& e) {
    std::string o = emit_expr(*e.operand);
    return "(not _janus_truthy(" + o + "))";
}


// Type cast.

std::string Emitter::emit_type_cast(const IRTypeCast& e) {
    std::string o = emit_expr(*e.operand);
    switch (e.target_type) {
        case JanusType::CBIT:     return "(1 if _janus_truthy(" + o + ") else 0)";
        case JanusType::CNUM:     return "float(" + o + ")";
        case JanusType::CSTR:     return "_janus_to_str(" + o + ")";
        case JanusType::QUBIT:    return "_janus_qubit()";
        case JanusType::QNUM:     return "_janus_qnum(int(" + o + "))";
        case JanusType::LIST:     return "list([" + o + "])";
        case JanusType::MATRIX:   return "np.array(" + o + ", dtype=complex)";
        case JanusType::GATE:     return "Operator(np.array(" + o + ", dtype=complex))";
        case JanusType::CIRC:     return "_janus_build_circ([], [])";
        case JanusType::BLOCK:    return "_janus_build_block([])";
        case JanusType::FUNCTION: return "(lambda: " + o + ")";
        case JanusType::NULL_TYPE:return "None";
    }
    report_error(e.line);
}


// Matrix literal.

std::string Emitter::emit_matrix_literal(const IRMatrixLiteral& e) {
    bool all_gate_or_null = true;
    for (const auto& row : e.rows) {
        for (const auto& elem : row) {
            if (elem) {
                JanusType et = elem->result_type.type;
                if (et != JanusType::GATE && et != JanusType::NULL_TYPE) {
                    all_gate_or_null = false;
                }
            }
        }
    }
    if (all_gate_or_null && !e.rows.empty()) {
        std::string result = "[";
        for (size_t r = 0; r < e.rows.size(); ++r) {
            if (r > 0) result += ", ";
            result += "[";
            for (size_t c = 0; c < e.rows[r].size(); ++c) {
                if (c > 0) result += ", ";
                result += emit_expr(*e.rows[r][c]);
            }
            result += "]";
        }
        result += "]";
        return result;
    }
    std::string result = "np.array([";
    for (size_t r = 0; r < e.rows.size(); ++r) {
        if (r > 0) result += ", ";
        result += "[";
        for (size_t c = 0; c < e.rows[r].size(); ++c) {
            if (c > 0) result += ", ";
            result += emit_expr(*e.rows[r][c]);
        }
        result += "]";
    }
    result += "], dtype=complex)";
    return result;
}


// User function call.

std::string Emitter::emit_call(const IRCall& e) {
    std::string callee = emit_expr(*e.callee);
    std::string a;
    for (size_t i = 0; i < e.args.size(); ++i) {
        if (i > 0) a += ", ";
        a += emit_expr(*e.args[i]);
    }
    return callee + "(" + a + ")";
}


// Builtin call.

std::string Emitter::emit_builtin_call(const IRBuiltinCall& e) {
    std::vector<std::string> args;
    args.reserve(e.args.size());
    for (const auto& arg : e.args) {
        args.push_back(emit_expr(*arg));
    }
    switch (e.op) {
        case IRBuiltinOp::MEASURE: {
            std::string tv = fresh_temp();
            std::string ts = fresh_temp();
            line(tv + ", " + ts + " = _janus_measure(" + args[0] + ")");
            // Update the source variable with the collapsed state.
            if (const auto* var = dynamic_cast<const IRVariable*>(e.args[0].get())) {
                line(var->name + " = " + ts);
            }
            return tv;
        }
        case IRBuiltinOp::PEEK:
            return "_janus_peek(" + args[0] + ")";
        case IRBuiltinOp::STATE:
            return "_janus_state(" + args[0] + ")";
        case IRBuiltinOp::EXPECT:
            return "_janus_expect(" + args[0] + ", " + args[1] + ")";
        case IRBuiltinOp::CTRLE:
            return "_janus_ctrle(" + args[0] + ", " + args[1] + ")";
        case IRBuiltinOp::RUN: {
            std::string tmp = fresh_temp();
            line(tmp + " = _janus_run(" + args[0] + ")");
            return tmp;
        }
        case IRBuiltinOp::RUNH:
            return "_janus_runh(" + args[0] + ", " + args[1] + ")";
        case IRBuiltinOp::ISUNITARY:
            return "_janus_isunitary(" + args[0] + ")";
        case IRBuiltinOp::SAMEOUTPUT:
            return "_janus_sameoutput(" + args[0] + ", " + args[1] + ")";
        case IRBuiltinOp::PRINT: {
            std::string pa;
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0) pa += ", ";
                pa += "_janus_to_str(" + args[i] + ")";
            }
            line("print(" + pa + ")");
            return "None";
        }
        case IRBuiltinOp::DELETE:
            return "_janus_delete(" + args[0] + ", " + args[1] + ")";
        case IRBuiltinOp::SIN:
            return "math.sin(" + args[0] + ")";
        case IRBuiltinOp::COS:
            return "math.cos(" + args[0] + ")";
        case IRBuiltinOp::NUMBEROFGATES:
            return "_janus_numberofgates(" + args[0] + ")";
        case IRBuiltinOp::DET:
            return "_janus_det(" + args[0] + ")";
        case IRBuiltinOp::TRANSPOSE:
            return "_janus_transpose(" + args[0] + ")";
        case IRBuiltinOp::TRANSPOSEC:
            return "_janus_transposec(" + args[0] + ")";
        case IRBuiltinOp::EVALS:
            return "_janus_evals(" + args[0] + ")";
        case IRBuiltinOp::EVECS:
            return "_janus_evecs(" + args[0] + ")";
        case IRBuiltinOp::GATES:
            return "_janus_gates_list(" + args[0] + ")";
        case IRBuiltinOp::QUBITS:
            return "_janus_qubits(" + args[0] + ")";
        case IRBuiltinOp::DEPTH:
            return "_janus_depth(" + args[0] + ")";
        case IRBuiltinOp::BITLENGTH:
            // Statevector objects expose .num_qubits for the qubit count.
            return args[0] + ".num_qubits";
    }
    report_error(e.line);
}


// Gate library.

std::string Emitter::emit_gate_library(const IRGateLibrary& e) {
    std::vector<std::string> p;
    p.reserve(e.args.size());
    for (const auto& arg : e.args) {
        p.push_back(emit_expr(*arg));
    }
    const std::string& n = e.gate_name;

    if (n == "i")       return "Operator(IGate())";
    if (n == "x")       return "Operator(XGate())";
    if (n == "y")       return "Operator(YGate())";
    if (n == "z")       return "Operator(ZGate())";
    if (n == "h")       return "Operator(HGate())";
    if (n == "s")       return "Operator(SGate())";
    if (n == "sdg")     return "Operator(SdgGate())";
    if (n == "t")       return "Operator(TGate())";
    if (n == "tdg")     return "Operator(TdgGate())";
    if (n == "sx")      return "Operator(SXGate())";
    if (n == "sxdg")    return "Operator(SXdgGate())";
    if (n == "cnot")    return "Operator(CXGate())";
    if (n == "cy")      return "Operator(CYGate())";
    if (n == "cz")      return "Operator(CZGate())";
    if (n == "ch")      return "Operator(CHGate())";
    if (n == "swap")    return "Operator(SwapGate())";
    if (n == "iswap")   return "Operator(iSwapGate())";
    if (n == "toffoli") return "Operator(CCXGate())";
    if (n == "cswap")   return "Operator(CSwapGate())";
    if (n == "rx")      return "Operator(RXGate(" + p[0] + "))";
    if (n == "ry")      return "Operator(RYGate(" + p[0] + "))";
    if (n == "rz")      return "Operator(RZGate(" + p[0] + "))";
    if (n == "p")       return "Operator(PhaseGate(" + p[0] + "))";
    if (n == "u")       return "Operator(UGate(" + p[0] + ", " + p[1] + ", " + p[2] + "))";
    if (n == "crx")     return "Operator(CRXGate(" + p[0] + "))";
    if (n == "cry")     return "Operator(CRYGate(" + p[0] + "))";
    if (n == "crz")     return "Operator(CRZGate(" + p[0] + "))";
    if (n == "cp")      return "Operator(CPhaseGate(" + p[0] + "))";
    if (n == "xx")      return "Operator(RXXGate(" + p[0] + "))";
    if (n == "yy")      return "Operator(RYYGate(" + p[0] + "))";
    if (n == "zz")      return "Operator(RZZGate(" + p[0] + "))";

    report_error(e.line);
}


// Type constructor.

std::string Emitter::emit_type_construct(const IRTypeConstruct& e) {
    std::vector<std::string> a;
    a.reserve(e.args.size());
    for (const auto& arg : e.args) {
        a.push_back(emit_expr(*arg));
    }
    switch (e.constructed_type) {
        case JanusType::QUBIT:
            return "_janus_qubit()";
        case JanusType::CBIT:
            return a.empty() ? "0" : "(1 if _janus_truthy(" + a[0] + ") else 0)";
        case JanusType::QNUM:
            if (a.empty()) return "_janus_qnum(0)";
            if (a.size() == 1) return "_janus_qnum(" + a[0] + ")";
            return "_janus_qnum(" + a[0] + ", " + a[1] + ")";
        case JanusType::CNUM:
            if (a.empty()) return "0.0";
            if (a.size() == 1) return "float(" + a[0] + ")";
            if (a.size() == 3) return "complex(" + a[1] + ", " + a[2] + ")";
            return "float(" + a[0] + ")";
        case JanusType::CSTR:
            return a.empty() ? "''" : "_janus_to_str(" + a[0] + ")";
        case JanusType::LIST:
            return a.empty() ? "[]" : "list(" + a[0] + ")";
        case JanusType::MATRIX:
            return a.empty() ? "np.array([[]], dtype=complex)"
                             : "np.array(" + a[0] + ", dtype=complex)";
        case JanusType::GATE:
            return a.empty() ? "Operator(np.eye(2, dtype=complex))"
                             : "Operator(np.array(" + a[0] + ", dtype=complex))";
        case JanusType::CIRC: {
            if (a.size() < 2) {
                if (a.size() == 1) {
                    JanusType a0t = e.args[0]->result_type.type;
                    if (a0t == JanusType::CNUM || a0t == JanusType::NULL_TYPE ||
                        a0t == JanusType::CBIT) {
                        return "_janus_build_circ("
                               "[_janus_qubit() for _ in range(int(" + a[0] + "))], [])";
                    }
                    return "_janus_build_circ([" + a[0] + "], [])";
                }
                return "_janus_build_circ([], [])";
            }
            std::string states;
            JanusType a0t = e.args[0]->result_type.type;
            if (a0t == JanusType::CNUM || a0t == JanusType::NULL_TYPE ||
                a0t == JanusType::CBIT) {
                states = "[_janus_qubit() for _ in range(int(" + a[0] + "))]";
            } else if (a0t == JanusType::LIST) {
                states = a[0];
            } else {
                states = "[" + a[0] + "]";
            }
            return "_janus_build_circ(" + states + ", " + a[1] + ")";
        }
        case JanusType::BLOCK:
            return a.empty() ? "_janus_build_block([])"
                             : "_janus_build_block(" + a[0] + ")";
        case JanusType::FUNCTION:
            report_error(e.line);
        case JanusType::NULL_TYPE:
            return "None";
    }
    report_error(e.line);
}


// Function definition.

std::string Emitter::emit_function_def(const IRFunctionDef& e,
                                       const std::string& assigned_name) {
    std::string name = assigned_name.empty() ? fresh_temp() : assigned_name;
    std::string ps;
    for (size_t i = 0; i < e.params.size(); ++i) {
        if (i > 0) ps += ", ";
        ps += e.params[i];
    }
    line("def " + name + "(" + ps + "):");
    ++indent_;
    ++function_depth_;
    if (e.body.empty()) {
        line("pass");
    } else {
        emit_stmts(e.body);
    }
    --function_depth_;
    --indent_;
    blank_line();
    return name;
}


// If.

std::string Emitter::emit_if(const IRIf& e) {
    std::string rv = fresh_temp();
    line(rv + " = False");
    std::string cond = emit_expr(*e.condition);
    line("if _janus_truthy(" + cond + "):");
    ++indent_;
    if (e.then_body.empty()) line("pass");
    else emit_stmts(e.then_body);
    line(rv + " = True");
    --indent_;
    for (const auto& eic : e.else_ifs) {
        std::string ec = emit_expr(*eic.condition);
        line("elif _janus_truthy(" + ec + "):");
        ++indent_;
        if (eic.body.empty()) line("pass");
        else emit_stmts(eic.body);
        line(rv + " = True");
        --indent_;
    }
    if (!e.else_body.empty()) {
        line("else:");
        ++indent_;
        emit_stmts(e.else_body);
        line(rv + " = True");
        --indent_;
    }
    return rv;
}


// For: emitted as while True with explicit condition check so that
// the condition expression is re-evaluated each iteration.

std::string Emitter::emit_for(const IRFor& e) {
    std::string cv = fresh_temp();
    line(cv + " = 0");
    emit_expr(*e.init);
    line("while True:");
    ++indent_;
    std::string cond = emit_expr(*e.condition);
    line("if not _janus_truthy(" + cond + "):");
    ++indent_;
    line("break");
    --indent_;
    line(cv + " += 1");
    emit_stmts(e.body);
    emit_expr(*e.update);
    --indent_;
    return "((" + cv + " + 1) if " + cv + " > 0 else 0)";
}


// While: same while True pattern.

std::string Emitter::emit_while(const IRWhile& e) {
    std::string cv = fresh_temp();
    line(cv + " = 0");
    line("while True:");
    ++indent_;
    std::string cond = emit_expr(*e.condition);
    line("if not _janus_truthy(" + cond + "):");
    ++indent_;
    line("break");
    --indent_;
    line(cv + " += 1");
    if (e.body.empty()) line("pass");
    else emit_stmts(e.body);
    --indent_;
    return "((" + cv + " + 1) if " + cv + " > 0 else 0)";
}


// Foreach.

std::string Emitter::emit_foreach(const IRForeach& e) {
    std::string cv = fresh_temp();
    line(cv + " = 0");
    std::string coll = emit_expr(*e.collection);
    std::string iter_src = coll;
    if (e.from_bound || e.to_bound) {
        std::string sl = fresh_temp();
        line(sl + " = list(" + coll + ")");
        if (e.from_bound && e.to_bound) {
            std::string fb = emit_expr(*e.from_bound);
            std::string tb = emit_expr(*e.to_bound);
            line(sl + " = " + sl + "[int(" + fb + "):int(" + tb + ")+1]");
        } else if (e.from_bound) {
            std::string fb = emit_expr(*e.from_bound);
            line(sl + " = " + sl + "[int(" + fb + "):]");
        } else {
            std::string tb = emit_expr(*e.to_bound);
            line(sl + " = " + sl + "[:int(" + tb + ")+1]");
        }
        iter_src = sl;
    }
    line("for " + e.element + " in " + iter_src + ":");
    ++indent_;
    if (e.where_cond) {
        std::string wc = emit_expr(*e.where_cond);
        line("if not _janus_truthy(" + wc + "):");
        ++indent_;
        line("continue");
        --indent_;
    }
    line(cv + " += 1");
    if (e.body.empty()) line("pass");
    else emit_stmts(e.body);
    --indent_;
    return "((" + cv + " + 1) if " + cv + " > 0 else 0)";
}


} // anonymous namespace


// Public interface.

void QiskitBackend::emit(const IRProgram& program,
                         const std::string& output_path) {
    Emitter emitter;
    std::string content = emitter.run(program);

    std::ofstream file(output_path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        report_error(0);
    }

    file << content;
    file.flush();

    if (!file.good()) {
        report_error(0);
    }
}


} // namespace janus
