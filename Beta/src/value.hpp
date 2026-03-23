#ifndef JANUS_VALUE_HPP
#define JANUS_VALUE_HPP

#include "types.hpp"
#include "quantum_state.hpp"

#include <complex>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace janus {

// Forward declarations.  Stmt is defined in ast.hpp. not including
// it here to prevent circular header dependencies.  FunctionData stores
// a non-owning pointer to the body vector whose lifetime is guaranteed
// by the AST outliving all runtime values.
struct Stmt;

// Forward declarations for compound data types that reference JanusValue.
// Defined after JanusValue is complete.
struct CircuitData;
struct BlockData;
struct FunctionData;


// JanusValue :: runtime value representation for every Janus type
//
// A JanusValue can hold any Beta type: qubit, cbit, qnum, cnum, cstr,
// list, matrix, gate, circ, block, function, and null.
//
// Assignment is always by value (deep copy).  Null is bitwise zero: a
// default-constructed JanusValue has type NULL_TYPE and all numeric fields
// set to 0.0.
//
// All purely numeric classical types are 64 bits.  When a cnum is complex,
// the real and imaginary parts each use 32 bits of precision (stored as
// doubles in the runtime but with float-precision semantics).
//
// Quantum types store their state via QuantumState on the heap.
// Collection and compound types store their data via unique_ptr to
// heap-allocated structures, deep-copied on value assignment.

struct JanusValue {

    // Type tag and compile-time metadata.
    TypeInfo type_info;

    // Classical numeric storage (CBIT, CNUM, NULL_TYPE).
    // CBIT:   real_val is 0.0 or 1.0.
    // CNUM (real):    real_val holds the 64-bit double value.
    // CNUM (complex): real_val and imag_val hold float-precision values
    //                 stored as doubles (32+32 = 64 bits semantic precision).
    // NULL_TYPE:      both are 0.0.
    double real_val = 0.0;
    double imag_val = 0.0;

    // String storage (CSTR).
    std::string str_val;

    // Quantum state (QUBIT, QNUM).
    // nullptr for non-quantum types.
    std::unique_ptr<QuantumState> quantum_val;

    // List elements (LIST).
    // nullptr for non-list types.
    std::unique_ptr<std::vector<JanusValue>> list_data;

    // Flat row-major complex matrix data (MATRIX, GATE).
    // Dimensions are stored in type_info.matrix_rows and matrix_cols.
    // nullptr for non-matrix and non-gate types.
    std::unique_ptr<std::vector<std::complex<double>>> matrix_data;

    // Circuit data (CIRC).
    // nullptr for non-circuit types.
    std::unique_ptr<CircuitData> circ_data;

    // Block data (BLOCK).
    // nullptr for non-block types.
    std::unique_ptr<BlockData> block_data;

    // Function data (FUNCTION).
    // nullptr for non-function types.
    std::unique_ptr<FunctionData> func_data;


    // Default constructor: produces a null value (bitwise zero).
    JanusValue();

    // Deep-copy constructor.  Every heap-allocated member is fully
    // duplicated so that the copy is independent of the original.
    JanusValue(const JanusValue& other);

    // Deep-copy assignment.
    JanusValue& operator=(const JanusValue& other);

    // Move constructor.
    JanusValue(JanusValue&& other) noexcept;

    // Move assignment.
    JanusValue& operator=(JanusValue&& other) noexcept;

    // Destructor.  Defined in the .cpp file because CircuitData,
    // BlockData, and FunctionData are incomplete here.
    ~JanusValue();


    // Factory methods


    // Null value (bitwise zero everywhere).
    static JanusValue make_null();

    // Classical bit.  val is clamped to 0 or 1.
    static JanusValue make_cbit(int64_t val);

    // Real classical number (64-bit double).
    static JanusValue make_cnum(double val);

    // Complex classical number (32+32 float precision stored as doubles).
    static JanusValue make_cnum_complex(float real, float imag);

    // Default qubit in the |0> state.
    static JanusValue make_qubit();

    // Qubit from an existing QuantumState (must be 1 qubit).
    static JanusValue make_qubit(QuantumState state);

    // Qnum from a classical non-negative integer value.
    // Allocates ceil(log2(val+1)) qubits (minimum 1).
    static JanusValue make_qnum(uint64_t val, uint32_t line);

    // Qnum from a classical value with a user-specified maximum width cap.
    static JanusValue make_qnum(uint64_t val, uint32_t max_width,
                                uint32_t line);

    // Qnum from an existing QuantumState.
    static JanusValue make_qnum(QuantumState state);

    // Qnum from an existing QuantumState with a max width cap.
    static JanusValue make_qnum(QuantumState state, uint32_t max_width);

    // Classical string.
    static JanusValue make_cstr(std::string val);

    // List from a vector of elements.
    static JanusValue make_list(std::vector<JanusValue> elements);

    // Matrix from dimensions and a flat row-major data vector.
    // data.size() must equal rows * cols.
    static JanusValue make_matrix(uint32_t rows, uint32_t cols,
                                  std::vector<std::complex<double>> data);

    // Gate from qubit width and a flat row-major unitary matrix.
    // data.size() must equal (2^width)^2.
    static JanusValue make_gate(uint32_t qubit_width,
                                std::vector<std::complex<double>> data);

    // Circuit from qubit initial states and a gate grid.
    // gate_grid[qubit_line][time_step] is a JanusValue of type GATE or
    // NULL_TYPE.
    static JanusValue make_circ(std::vector<QuantumState> qubit_states,
                                std::vector<std::vector<JanusValue>> gate_grid);

    // Block from a gate grid (no bound qubits).
    // gate_grid[qubit_line][time_step] is a JanusValue of type GATE or
    // NULL_TYPE.
    static JanusValue make_block(
        std::vector<std::vector<JanusValue>> gate_grid);

    // Function from parameter names and a non-owning pointer to the body
    // statements in the AST.  The AST must outlive all runtime values.
    static JanusValue make_function(
        std::vector<std::string> params,
        const std::vector<std::unique_ptr<Stmt>>* body);


    // Query helpers


    // Returns true if this value is of type NULL_TYPE.
    bool is_null() const noexcept;

    // Returns true if this value is truthy.
    // Null and bitwise-zero values are false; everything else is true.
    //   NULL_TYPE:  always false.
    //   CBIT:       false when real_val == 0.0.
    //   CNUM:       false when both real_val and imag_val are 0.0.
    //   QUBIT/QNUM: false when the state is purely |0...0>.
    //   CSTR:       false when the string is empty.
    //   LIST:       false when the list is empty.
    //   MATRIX/GATE: false when every matrix element is zero.
    //   CIRC:       false when the gate grid has zero time steps.
    //   BLOCK:      false when the gate grid has zero time steps.
    //   FUNCTION:   always true (a valid function is never bitwise zero).
    bool is_truthy() const noexcept;


    // Conversion helpers


    // Returns the real numeric value.
    // For CBIT and CNUM returns real_val.
    // For QUBIT and QNUM measures the state and returns the collapsed
    //   basis-state index as a double.  This is NOT a const operation
    //   on the underlying quantum state; use with care.
    // For NULL_TYPE returns 0.0.
    // For other types calls report_error.
    double as_real(uint32_t line) const;

    // Returns the integer numeric value.
    // For CBIT returns 0 or 1.
    // For CNUM returns static_cast<int64_t>(real_val).
    // For NULL_TYPE returns 0.
    // For other types calls report_error.
    int64_t as_integer(uint32_t line) const;

    // Converts this value to its string representation suitable for
    // print() and string interpolation.
    //   NULL_TYPE:  "null"
    //   CBIT:       "0" or "1"
    //   CNUM (real): decimal representation at max double precision
    //   CNUM (complex): "(real+imagi)" or "(real-imagi)"
    //   QUBIT/QNUM: Dirac-notation string via QuantumState::to_dirac_string
    //   CSTR:       the string value itself
    //   LIST:       "[elem, elem, ...]"
    //   MATRIX:     "[row; row; ...]"
    //   GATE:       "gate(matrix)"
    //   CIRC:       "circ(...)"
    //   BLOCK:      "block(...)"
    //   FUNCTION:   "function(param_count)"
    std::string to_string() const;
};


// Compound data types defined after JanusValue is complete.


// Circuit: a register of qubits with initial states and a gate schedule.
struct CircuitData {
    // Initial state of each qubit line.
    std::vector<QuantumState> qubit_states;

    // Gate schedule: gate_grid[qubit_line][time_step].
    // Each entry is a JanusValue of type GATE or NULL_TYPE.
    // All inner vectors have the same length (number of time steps).
    std::vector<std::vector<JanusValue>> gate_grid;
};

// Block: a gate schedule without bound qubits.
struct BlockData {
    // Gate schedule: gate_grid[qubit_line][time_step].
    // Each entry is a JanusValue of type GATE or NULL_TYPE.
    // All inner vectors have the same length (number of time steps).
    std::vector<std::vector<JanusValue>> gate_grid;
};

// Function: parameter names and a non-owning reference to the body.
struct FunctionData {
    // Parameter names in declaration order.
    std::vector<std::string> params;

    // Non-owning pointer to the body statements in the AST.
    // The AST is guaranteed to outlive all runtime values because the
    // compiler holds the AST on the stack of main() for the entire
    // duration of execution.
    const std::vector<std::unique_ptr<Stmt>>* body = nullptr;
};

} // namespace janus

#endif // JANUS_VALUE_HPP
