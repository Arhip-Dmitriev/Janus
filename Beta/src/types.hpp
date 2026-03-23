#ifndef JANUS_TYPES_HPP
#define JANUS_TYPES_HPP

#include "token.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace janus {


// JanusType :: every first-class type available in the Beta

// Beta types: qubit, cbit, qnum, cnum, cstr, list, matrix, gate, circ,
//             block, function.
// NULL_TYPE is the internal representation of the null literal whose
// bitwise value is 0 everywhere.  It acts as a "type not yet determined"
// sentinel for uninitialised collection slots and as the concrete type of
// the null keyword.
// No dedicated boolean type.  The keywords true and false produce
// a CBIT value of 1 or 0 respectively.


enum class JanusType : uint8_t {
    QUBIT,          // 1-qubit quantum register
    CBIT,           // 1-bit classical register (also represents booleans)
    QNUM,           // multi-qubit quantum numeric register, dynamic width
    CNUM,           // 64-bit classical numeric (real or complex)
    CSTR,           // classical string, 7 bits per character (ASCII)
    LIST,           // ordered, dynamically sized collection
    MATRIX,         // fixed-dimension 2D numeric array
    GATE,           // unitary quantum gate (width inferred from matrix)
    CIRC,           // quantum circuit: qubits + gate schedule
    BLOCK,          // gate schedule without bound qubits
    FUNCTION,       // first-class function value
    NULL_TYPE       // null literal / uninitialised slot
};


// TypeInfo :: compile-time metadata attached to every typed entity

// The type checker annotates AST nodes and symbol table entries with a
// TypeInfo to carry enough information for code generation and further
// validation.  Many fields are only meaningful for certain JanusType
// values; irrelevant fields are left at their zero-initialised defaults.


struct TypeInfo {
    JanusType type = JanusType::NULL_TYPE;

    // Width information.
    // For QUBIT:  always 1 (one qubit).
    // For CBIT:   always 1 (one classical bit).
    // For QNUM:   number of qubits currently allocated (0 = not yet known).
    // For CNUM:   always 64 (bits).
    // For CSTR:   total bits = 7 * character count (0 = not yet known).
    // For GATE:   qubit width inferred from matrix dimensions (0 = unknown).
    // For others: 0 (not applicable).
    uint32_t width = 0;

    // Maximum qubit width cap set by the user for QNUM via the optional
    // second constructor argument.  0 means no user-imposed limit.
    uint32_t max_width = 0;

    // True when a CNUM stores a complex value (32 + 32 = 64 bits total).
    // Irrelevant for non-CNUM types.
    bool is_complex = false;

    // Matrix / block / gate dimensions.
    // For MATRIX: row and column counts fixed at declaration (0 = unknown).
    // For GATE:   matrix_rows == matrix_cols == 2^(qubit width).
    // For BLOCK:  matrix_rows = qubit lines, matrix_cols = time steps.
    // For CIRC:   same semantics as BLOCK.
    uint32_t matrix_rows = 0;
    uint32_t matrix_cols = 0;

    // Arity for FUNCTION types (number of declared parameters).
    // 0 for non-FUNCTION types or when not yet resolved.
    uint32_t arity = 0;
};


// Classification helpers


// Returns true for types backed by a quantum register.
constexpr bool is_quantum(JanusType t) noexcept {
    return t == JanusType::QUBIT || t == JanusType::QNUM;
}

// Returns true for types backed by classical storage.
// NULL_TYPE is treated as classical (bitwise 0 is classical).
constexpr bool is_classical(JanusType t) noexcept {
    return !is_quantum(t);
}

// Returns true for types that represent a single numeric value (scalar).
// Both quantum and classical numeric scalars are included.
constexpr bool is_numeric(JanusType t) noexcept {
    return t == JanusType::QUBIT
        || t == JanusType::CBIT
        || t == JanusType::QNUM
        || t == JanusType::CNUM;
}

// Returns true for ordered collection types.
constexpr bool is_collection(JanusType t) noexcept {
    return t == JanusType::LIST || t == JanusType::MATRIX;
}

// Returns true for types that describe quantum circuit structure.
constexpr bool is_circuit_structure(JanusType t) noexcept {
    return t == JanusType::GATE
        || t == JanusType::CIRC
        || t == JanusType::BLOCK;
}

// Returns true for types that represent a string.
// In the full language there are multiple string variants; in Beta only
// CSTR is available.
constexpr bool is_string(JanusType t) noexcept {
    return t == JanusType::CSTR;
}


// Naming


// Returns the short-form name of a type as it appears in Janus source and
// in the "allowed types" lists of the documentation.
constexpr std::string_view type_name(JanusType t) noexcept {
    switch (t) {
        case JanusType::QUBIT:     return "qubit";
        case JanusType::CBIT:      return "cbit";
        case JanusType::QNUM:      return "qnum";
        case JanusType::CNUM:      return "cnum";
        case JanusType::CSTR:      return "cstr";
        case JanusType::LIST:      return "list";
        case JanusType::MATRIX:    return "matrix";
        case JanusType::GATE:      return "gate";
        case JanusType::CIRC:      return "circ";
        case JanusType::BLOCK:     return "block";
        case JanusType::FUNCTION:  return "function";
        case JanusType::NULL_TYPE: return "null";
    }
    return "unknown";
}


// Conversion from TokenType to JanusType


// Maps a type-keyword TokenType (KW_QUBIT, KW_CBIT, etc.) to its
// corresponding JanusType.  Returns std::nullopt for any TokenType that
// does not name a Beta type.
constexpr std::optional<JanusType> janus_type_from_keyword(TokenType tok) noexcept {
    switch (tok) {
        case TokenType::KW_QUBIT:    return JanusType::QUBIT;
        case TokenType::KW_CBIT:     return JanusType::CBIT;
        case TokenType::KW_QNUM:     return JanusType::QNUM;
        case TokenType::KW_CNUM:     return JanusType::CNUM;
        case TokenType::KW_CSTR:     return JanusType::CSTR;
        case TokenType::KW_LIST:     return JanusType::LIST;
        case TokenType::KW_MATRIX:   return JanusType::MATRIX;
        case TokenType::KW_GATE:     return JanusType::GATE;
        case TokenType::KW_CIRC:     return JanusType::CIRC;
        case TokenType::KW_BLOCK:    return JanusType::BLOCK;
        case TokenType::KW_FUNCTION: return JanusType::FUNCTION;
        default:                     return std::nullopt;
    }
}


// Type compatibility and comparison


// Two TypeInfo values describe the same type when their JanusType tags
// match.  Width and dimension metadata are not considered because Janus
// checks type identity at the tag level. width mismatches are runtime
// errors rather than type errors.
constexpr bool types_equal(const TypeInfo& a, const TypeInfo& b) noexcept {
    return a.type == b.type;
}

// Returns true when a value of type `source` may be assigned to a variable
// whose current type is `target` WITHOUT an explicit type cast.
//
// Rules (Beta):
//   - A value may always be assigned to a variable of the same JanusType.
//   - NULL_TYPE may be assigned to any variable (null is valid everywhere).
//   - CBIT may be assigned to CNUM (widening: 1 bit to 64 bits).
//   - QUBIT may be assigned to QNUM (widening: 1 qubit into a qnum register).
//   - No other implicit conversions are permitted.
constexpr bool is_assignable_without_cast(JanusType target, JanusType source) noexcept {
    if (source == target)           return true;
    if (source == JanusType::NULL_TYPE) return true;
    if (target == JanusType::CNUM  && source == JanusType::CBIT)  return true;
    if (target == JanusType::QNUM  && source == JanusType::QUBIT) return true;
    return false;
}

// Returns true when an explicit cast from `source` to `target` is
// permitted by the type checker.  The cast may still fail at runtime, but the type checker
// allows the attempt.
//
// In Beta, an explicit cast is allowed between any two Beta types.
// The documentation states: "all others will simply cast bitwise."
// The type checker therefore permits every (target, source) pair where
// both are valid Beta types.  NULL_TYPE as a target is disallowed
// because null is not a user-facing type keyword and cannot appear in
// a cast expression.
constexpr bool is_cast_allowed(JanusType target, JanusType source) noexcept {
    // Cannot cast TO null; null is not a type keyword.
    if (target == JanusType::NULL_TYPE) return false;
    // Every other combination is permitted in Beta.
    return true;
}

// Returns the classical counterpart of a quantum type, or the type
// itself if it is already classical.  Useful for determining the result
// type of a measurement.
//   QUBIT -> CBIT
//   QNUM  -> CNUM
//   all others -> unchanged
constexpr JanusType classical_counterpart(JanusType t) noexcept {
    switch (t) {
        case JanusType::QUBIT: return JanusType::CBIT;
        case JanusType::QNUM:  return JanusType::CNUM;
        default:               return t;
    }
}

// Returns the quantum counterpart of a classical type, or the type
// itself if it is already quantum.
//   CBIT -> QUBIT
//   CNUM -> QNUM
//   all others -> unchanged
constexpr JanusType quantum_counterpart(JanusType t) noexcept {
    switch (t) {
        case JanusType::CBIT: return JanusType::QUBIT;
        case JanusType::CNUM: return JanusType::QNUM;
        default:              return t;
    }
}

// Returns true when two types are "the same type ignoring the
// quantum/classical divide," note: that is the istypeofoperator.
// In Beta this is still useful internally: QUBIT/CBIT are companions,
// QNUM/CNUM are companions, and all others match only themselves.
constexpr bool types_match_ignoring_quantum(JanusType a, JanusType b) noexcept {
    if (a == b) return true;
    if ((a == JanusType::QUBIT && b == JanusType::CBIT) ||
        (a == JanusType::CBIT  && b == JanusType::QUBIT)) return true;
    if ((a == JanusType::QNUM && b == JanusType::CNUM) ||
        (a == JanusType::CNUM && b == JanusType::QNUM)) return true;
    return false;
}


// TypeInfo construction helpers


// Creates a TypeInfo for a qubit (always 1 qubit wide).
constexpr TypeInfo make_qubit_type() noexcept {
    TypeInfo ti;
    ti.type  = JanusType::QUBIT;
    ti.width = 1;
    return ti;
}

// Creates a TypeInfo for a cbit (always 1 bit wide).
constexpr TypeInfo make_cbit_type() noexcept {
    TypeInfo ti;
    ti.type  = JanusType::CBIT;
    ti.width = 1;
    return ti;
}

// Creates a TypeInfo for a qnum with a given qubit width and optional max.
constexpr TypeInfo make_qnum_type(uint32_t qubits = 0,
                                  uint32_t max_qubits = 0) noexcept {
    TypeInfo ti;
    ti.type      = JanusType::QNUM;
    ti.width     = qubits;
    ti.max_width = max_qubits;
    return ti;
}

// Creates a TypeInfo for a cnum (always 64 bits).
constexpr TypeInfo make_cnum_type(bool complex = false) noexcept {
    TypeInfo ti;
    ti.type       = JanusType::CNUM;
    ti.width      = 64;
    ti.is_complex = complex;
    return ti;
}

// Creates a TypeInfo for a cstr with a known character count.
// Width is 7 * char_count (ASCII encoding, 7 bits per character).
constexpr TypeInfo make_cstr_type(uint32_t char_count = 0) noexcept {
    TypeInfo ti;
    ti.type  = JanusType::CSTR;
    ti.width = char_count * 7;
    return ti;
}

// Creates a TypeInfo for a list.
constexpr TypeInfo make_list_type() noexcept {
    TypeInfo ti;
    ti.type = JanusType::LIST;
    return ti;
}

// Creates a TypeInfo for a matrix with known dimensions.
constexpr TypeInfo make_matrix_type(uint32_t rows = 0,
                                    uint32_t cols = 0) noexcept {
    TypeInfo ti;
    ti.type        = JanusType::MATRIX;
    ti.matrix_rows = rows;
    ti.matrix_cols = cols;
    return ti;
}

// Creates a TypeInfo for a gate whose qubit width is known.
// The matrix is 2^width x 2^width.
constexpr TypeInfo make_gate_type(uint32_t qubit_width = 0) noexcept {
    TypeInfo ti;
    ti.type  = JanusType::GATE;
    ti.width = qubit_width;
    if (qubit_width > 0) {
        uint32_t dim    = static_cast<uint32_t>(1) << qubit_width;
        ti.matrix_rows  = dim;
        ti.matrix_cols  = dim;
    }
    return ti;
}

// Creates a TypeInfo for a circ.
constexpr TypeInfo make_circ_type(uint32_t qubit_lines = 0,
                                  uint32_t time_steps = 0) noexcept {
    TypeInfo ti;
    ti.type        = JanusType::CIRC;
    ti.matrix_rows = qubit_lines;
    ti.matrix_cols = time_steps;
    return ti;
}

// Creates a TypeInfo for a block.
constexpr TypeInfo make_block_type(uint32_t qubit_lines = 0,
                                   uint32_t time_steps = 0) noexcept {
    TypeInfo ti;
    ti.type        = JanusType::BLOCK;
    ti.matrix_rows = qubit_lines;
    ti.matrix_cols = time_steps;
    return ti;
}

// Creates a TypeInfo for a function with a known parameter count.
constexpr TypeInfo make_function_type(uint32_t param_count = 0) noexcept {
    TypeInfo ti;
    ti.type  = JanusType::FUNCTION;
    ti.arity = param_count;
    return ti;
}

// Creates a TypeInfo for the null literal.
constexpr TypeInfo make_null_type() noexcept {
    TypeInfo ti;
    ti.type = JanusType::NULL_TYPE;
    return ti;
}


// Arithmetic result type resolution


// Given two operand types involved in a binary arithmetic operation
// (+, -, *, /, //, %, ^), returns the JanusType of the result.
//
// Rules from the documentation:
//   - If either operand is quantum (QUBIT or QNUM), the result is quantum.
//   - If at least one operand is a LIST, the result is a LIST
//     (concatenation trumps element-wise arithmetic in Beta).
//   - MATRIX operands produce a MATRIX result.
//   - Otherwise the result is the wider numeric type.
//   - NULL_TYPE operands are treated as CNUM (null == 0).
//
// Returns std::nullopt if the combination is invalid for arithmetic.
constexpr std::optional<JanusType> arithmetic_result_type(
        JanusType lhs, JanusType rhs) noexcept {

    // Concatenation rule: list + anything = list.
    if (lhs == JanusType::LIST || rhs == JanusType::LIST) {
        return JanusType::LIST;
    }

    // Promote NULL_TYPE to CNUM for arithmetic purposes.
    if (lhs == JanusType::NULL_TYPE) lhs = JanusType::CNUM;
    if (rhs == JanusType::NULL_TYPE) rhs = JanusType::CNUM;

    // Both must be numeric or matrix for arithmetic.
    bool lhs_numeric = is_numeric(lhs);
    bool rhs_numeric = is_numeric(rhs);
    bool lhs_matrix  = (lhs == JanusType::MATRIX);
    bool rhs_matrix  = (rhs == JanusType::MATRIX);

    if (!lhs_numeric && !lhs_matrix) return std::nullopt;
    if (!rhs_numeric && !rhs_matrix) return std::nullopt;

    // Matrix arithmetic produces a matrix.
    if (lhs_matrix || rhs_matrix) return JanusType::MATRIX;

    // Both are numeric scalars.  Quantum wins over classical,
    // multi-qubit wins over single-qubit.
    bool any_quantum = is_quantum(lhs) || is_quantum(rhs);
    bool any_multi   = (lhs == JanusType::QNUM || lhs == JanusType::CNUM ||
                        rhs == JanusType::QNUM || rhs == JanusType::CNUM);

    if (any_quantum && any_multi) return JanusType::QNUM;
    if (any_quantum)              return JanusType::QUBIT;
    if (any_multi)                return JanusType::CNUM;
    return JanusType::CBIT;
}


// Comparison result type


// The result of a comparison (==, <, >, <=, >=) is always a 1-bit value.
// If either operand is quantum the result is QUBIT; otherwise CBIT.
constexpr JanusType comparison_result_type(JanusType lhs,
                                           JanusType rhs) noexcept {
    if (is_quantum(lhs) || is_quantum(rhs)) return JanusType::QUBIT;
    return JanusType::CBIT;
}


// Bitwise operation result type


// For bitwise operations (and, or, xor, nand, nor, xnor, not, <<, >>)
// the result mirrors the wider / more quantum operand, similar to
// arithmetic but limited to numeric and matrix types.
// Returns std::nullopt if the operand combination is invalid.
constexpr std::optional<JanusType> bitwise_result_type(
        JanusType lhs, JanusType rhs) noexcept {
    // Same promotion logic as arithmetic, without the list/concat rule.
    if (lhs == JanusType::NULL_TYPE) lhs = JanusType::CNUM;
    if (rhs == JanusType::NULL_TYPE) rhs = JanusType::CNUM;

    bool lhs_ok = is_numeric(lhs) || lhs == JanusType::MATRIX || lhs == JanusType::LIST;
    bool rhs_ok = is_numeric(rhs) || rhs == JanusType::MATRIX || rhs == JanusType::LIST;
    if (!lhs_ok || !rhs_ok) return std::nullopt;

    if (lhs == JanusType::LIST   || rhs == JanusType::LIST)   return JanusType::LIST;
    if (lhs == JanusType::MATRIX || rhs == JanusType::MATRIX) return JanusType::MATRIX;

    bool any_quantum = is_quantum(lhs) || is_quantum(rhs);
    bool any_multi   = (lhs == JanusType::QNUM || lhs == JanusType::CNUM ||
                        rhs == JanusType::QNUM || rhs == JanusType::CNUM);

    if (any_quantum && any_multi) return JanusType::QNUM;
    if (any_quantum)              return JanusType::QUBIT;
    if (any_multi)                return JanusType::CNUM;
    return JanusType::CBIT;
}

// Unary bitwise result type (not, <<, >>).
constexpr std::optional<JanusType> unary_bitwise_result_type(
        JanusType operand) noexcept {
    if (operand == JanusType::NULL_TYPE) return JanusType::CNUM;
    if (is_numeric(operand) || operand == JanusType::MATRIX ||
        operand == JanusType::LIST) {
        return operand;
    }
    return std::nullopt;
}


// Qnum width calculation


// Returns the minimum number of qubits required to represent a non-negative
// integer value in a qnum register.  qnum(0) still needs 1 qubit.
constexpr uint32_t qubits_for_value(uint64_t value) noexcept {
    if (value == 0) return 1;
    uint32_t bits = 0;
    uint64_t v    = value;
    while (v > 0) {
        ++bits;
        v >>= 1;
    }
    return bits;
}

} // namespace janus

#endif // JANUS_TYPES_HPP
