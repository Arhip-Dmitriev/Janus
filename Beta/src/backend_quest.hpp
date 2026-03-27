#ifndef JANUS_BACKEND_QUEST_HPP
#define JANUS_BACKEND_QUEST_HPP

#include "ir.hpp"
#include "value.hpp"
#include "scope.hpp"
#include "error.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace janus {

// Control flow signals used internally by the executor.
// These are thrown as lightweight exceptions and caught by the
// corresponding loop or function handlers.  They never escape the
// BackendQuEST public interface.

struct BreakSignal {};
struct ContinueSignal {};
struct ReturnSignal {
    JanusValue value;
    bool has_value = false;
};


// BackendQuEST :: executes a Janus IR program using the internal
// amplitude-level quantum simulation provided by QuantumState.
//
// The backend walks the IR tree, evaluates expressions to JanusValues,
// manages the runtime Scope, and handles all quantum operations
// (gate application, measurement, state vector arithmetic) through the
// QuantumState and circuit_synth APIs.
//
// The -p32 precision flag causes all quantum amplitude computations to
// be truncated to float (32-bit) precision after each operation.
// Classical types remain at 64-bit precision regardless of this flag.

class BackendQuEST {
public:
    // Constructs the backend.
    // precision_32: when true, quantum amplitudes are truncated to
    //               float precision after each quantum operation.
    explicit BackendQuEST(bool precision_32 = false);

    // Executes the given IR program and returns the process exit code.
    // A top-level return with an integer sets the exit code (mod 256).
    // Absence of a top-level return yields exit code 0.
    int execute(const IRProgram& program);

private:
    bool p32_;
    Scope scope_;

    // Pre-scan top-level statements for function assignments so that
    // functions may be called before their source-order declaration.
    void forward_declare_functions(const std::vector<IRStmtPtr>& stmts);

    // Statement execution.
    void exec_stmts(const std::vector<IRStmtPtr>& stmts);
    void exec_stmt(const IRStmt& stmt);

    // Expression evaluation.  Returns the resulting JanusValue.
    JanusValue eval_expr(const IRExpr& expr);

    // Specific expression evaluators.
    JanusValue eval_integer_literal(const IRIntegerLiteral& e);
    JanusValue eval_float_literal(const IRFloatLiteral& e);
    JanusValue eval_string_literal(const IRStringLiteral& e);
    JanusValue eval_bool_literal(const IRBoolLiteral& e);
    JanusValue eval_null_literal(const IRNullLiteral& e);
    JanusValue eval_pi_literal(const IRPiLiteral& e);
    JanusValue eval_e_literal(const IRELiteral& e);
    JanusValue eval_interpolated_string(const IRInterpolatedString& e);
    JanusValue eval_variable(const IRVariable& e);
    JanusValue eval_index(const IRIndex& e);
    JanusValue eval_assign(const IRAssign& e);
    JanusValue eval_binary(const IRBinary& e);
    JanusValue eval_unary(const IRUnary& e);
    JanusValue eval_postfix_bang(const IRPostfixBang& e);
    JanusValue eval_type_cast(const IRTypeCast& e);
    JanusValue eval_matrix_literal(const IRMatrixLiteral& e);
    JanusValue eval_call(const IRCall& e);
    JanusValue eval_builtin_call(const IRBuiltinCall& e);
    JanusValue eval_gate_library(const IRGateLibrary& e);
    JanusValue eval_type_construct(const IRTypeConstruct& e);
    JanusValue eval_function_def(const IRFunctionDef& e);
    JanusValue eval_if(const IRIf& e);
    JanusValue eval_for(const IRFor& e);
    JanusValue eval_while(const IRWhile& e);
    JanusValue eval_foreach(const IRForeach& e);

    // Binary operation helpers.
    JanusValue binary_add(const JanusValue& lhs, const JanusValue& rhs,
                          JanusType result_type, uint32_t line);
    JanusValue binary_sub(const JanusValue& lhs, const JanusValue& rhs,
                          JanusType result_type, uint32_t line);
    JanusValue binary_mul(const JanusValue& lhs, const JanusValue& rhs,
                          JanusType result_type, uint32_t line);
    JanusValue binary_div(const JanusValue& lhs, const JanusValue& rhs,
                          JanusType result_type, uint32_t line);
    JanusValue binary_int_div(const JanusValue& lhs, const JanusValue& rhs,
                              JanusType result_type, uint32_t line);
    JanusValue binary_mod(const JanusValue& lhs, const JanusValue& rhs,
                          JanusType result_type, uint32_t line);
    JanusValue binary_exp(const JanusValue& lhs, const JanusValue& rhs,
                          JanusType result_type, uint32_t line);
    JanusValue binary_eq(const JanusValue& lhs, const JanusValue& rhs,
                         uint32_t line);
    JanusValue binary_lt(const JanusValue& lhs, const JanusValue& rhs,
                         uint32_t line);
    JanusValue binary_gt(const JanusValue& lhs, const JanusValue& rhs,
                         uint32_t line);
    JanusValue binary_le(const JanusValue& lhs, const JanusValue& rhs,
                         uint32_t line);
    JanusValue binary_ge(const JanusValue& lhs, const JanusValue& rhs,
                         uint32_t line);
    JanusValue binary_bitwise(const JanusValue& lhs, const JanusValue& rhs,
                              IRBinaryOp op, uint32_t line);
    JanusValue binary_tensor(const JanusValue& lhs, const JanusValue& rhs,
                             uint32_t line);

    // Builtin operation helpers.
    JanusValue builtin_measure(const std::vector<JanusValue>& args,
                               uint32_t line);
    JanusValue builtin_peek(const std::vector<JanusValue>& args,
                            uint32_t line);
    JanusValue builtin_state(const std::vector<JanusValue>& args,
                             uint32_t line);
    JanusValue builtin_expect(const std::vector<JanusValue>& args,
                              uint32_t line);
    JanusValue builtin_ctrle(const std::vector<JanusValue>& args,
                             uint32_t line);
    JanusValue builtin_run(const std::vector<JanusValue>& args,
                           uint32_t line);
    JanusValue builtin_runh(const std::vector<JanusValue>& args,
                            uint32_t line);
    JanusValue builtin_isunitary(const std::vector<JanusValue>& args,
                                 uint32_t line);
    JanusValue builtin_sameoutput(const std::vector<JanusValue>& args,
                                  uint32_t line);
    JanusValue builtin_print(const std::vector<JanusValue>& args,
                             uint32_t line);
    JanusValue builtin_delete(const std::vector<JanusValue>& args,
                              uint32_t line);
    JanusValue builtin_sin(const std::vector<JanusValue>& args,
                           uint32_t line);
    JanusValue builtin_cos(const std::vector<JanusValue>& args,
                           uint32_t line);
    JanusValue builtin_numberofgates(const std::vector<JanusValue>& args,
                                     uint32_t line);
    JanusValue builtin_det(const std::vector<JanusValue>& args,
                           uint32_t line);
    JanusValue builtin_transpose(const std::vector<JanusValue>& args,
                                 uint32_t line);
    JanusValue builtin_transposec(const std::vector<JanusValue>& args,
                                  uint32_t line);
    JanusValue builtin_evals(const std::vector<JanusValue>& args,
                             uint32_t line);
    JanusValue builtin_evecs(const std::vector<JanusValue>& args,
                             uint32_t line);
    JanusValue builtin_gates(const std::vector<JanusValue>& args,
                             uint32_t line);
    JanusValue builtin_qubits(const std::vector<JanusValue>& args,
                              uint32_t line);
    JanusValue builtin_depth(const std::vector<JanusValue>& args,
                             uint32_t line);

    // Type cast helper.
    JanusValue perform_cast(const JanusValue& val, JanusType target,
                            uint32_t line);

    // Extracts a classical real value from any numeric or quantum value.
    // Quantum values are peeked (not collapsed).
    double to_real(const JanusValue& val, uint32_t line) const;

    // Extracts a classical integer from any numeric or quantum value.
    int64_t to_integer(const JanusValue& val, uint32_t line) const;

    // Applies -p32 precision truncation to a QuantumState if the flag
    // is set.  Each amplitude is cast to complex<float> and back.
    void apply_p32(QuantumState& qs) const;

    // Tensor product of two QuantumStates.
    static QuantumState tensor_product(const QuantumState& a,
                                       const QuantumState& b);

    // Execute a circuit and return the combined final QuantumState.
    QuantumState execute_circuit(const JanusValue& circ, uint32_t line);

    // Matrix math helpers.
    static std::complex<double> complex_determinant(
        const std::vector<std::complex<double>>& data,
        uint32_t dim, uint32_t line);

    static std::vector<std::complex<double>> complex_eigenvalues(
        const std::vector<std::complex<double>>& data,
        uint32_t dim, uint32_t line);

    static std::vector<std::vector<std::complex<double>>> complex_eigenvectors(
        const std::vector<std::complex<double>>& data,
        const std::vector<std::complex<double>>& eigenvalues,
        uint32_t dim, uint32_t line);
};

} // namespace janus

#endif // JANUS_BACKEND_QUEST_HPP
