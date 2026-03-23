#include "value.hpp"
#include "error.hpp"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>

namespace janus {


// Threshold below which a double is considered zero for truthiness checks.
static constexpr double VALUE_ZERO_THRESHOLD = 1.0e-15;


// Default constructor: null value (bitwise zero).

JanusValue::JanusValue() = default;


// Deep-copy constructor.

JanusValue::JanusValue(const JanusValue& other)
    : type_info(other.type_info),
      real_val(other.real_val),
      imag_val(other.imag_val),
      str_val(other.str_val) {

    if (other.quantum_val) {
        quantum_val = std::make_unique<QuantumState>(*other.quantum_val);
    }
    if (other.list_data) {
        list_data = std::make_unique<std::vector<JanusValue>>(*other.list_data);
    }
    if (other.matrix_data) {
        matrix_data = std::make_unique<std::vector<std::complex<double>>>(
            *other.matrix_data);
    }
    if (other.circ_data) {
        circ_data = std::make_unique<CircuitData>(*other.circ_data);
    }
    if (other.block_data) {
        block_data = std::make_unique<BlockData>(*other.block_data);
    }
    if (other.func_data) {
        func_data = std::make_unique<FunctionData>(*other.func_data);
    }
}


// Deep-copy assignment.

JanusValue& JanusValue::operator=(const JanusValue& other) {
    if (this == &other) return *this;

    type_info = other.type_info;
    real_val  = other.real_val;
    imag_val  = other.imag_val;
    str_val   = other.str_val;

    if (other.quantum_val) {
        quantum_val = std::make_unique<QuantumState>(*other.quantum_val);
    } else {
        quantum_val.reset();
    }

    if (other.list_data) {
        list_data = std::make_unique<std::vector<JanusValue>>(*other.list_data);
    } else {
        list_data.reset();
    }

    if (other.matrix_data) {
        matrix_data = std::make_unique<std::vector<std::complex<double>>>(
            *other.matrix_data);
    } else {
        matrix_data.reset();
    }

    if (other.circ_data) {
        circ_data = std::make_unique<CircuitData>(*other.circ_data);
    } else {
        circ_data.reset();
    }

    if (other.block_data) {
        block_data = std::make_unique<BlockData>(*other.block_data);
    } else {
        block_data.reset();
    }

    if (other.func_data) {
        func_data = std::make_unique<FunctionData>(*other.func_data);
    } else {
        func_data.reset();
    }

    return *this;
}


// Move constructor.

JanusValue::JanusValue(JanusValue&& other) noexcept
    : type_info(other.type_info),
      real_val(other.real_val),
      imag_val(other.imag_val),
      str_val(std::move(other.str_val)),
      quantum_val(std::move(other.quantum_val)),
      list_data(std::move(other.list_data)),
      matrix_data(std::move(other.matrix_data)),
      circ_data(std::move(other.circ_data)),
      block_data(std::move(other.block_data)),
      func_data(std::move(other.func_data)) {

    // Leave the source in a valid null state.
    other.type_info = TypeInfo{};
    other.real_val  = 0.0;
    other.imag_val  = 0.0;
}


// Move assignment.

JanusValue& JanusValue::operator=(JanusValue&& other) noexcept {
    if (this == &other) return *this;

    type_info   = other.type_info;
    real_val    = other.real_val;
    imag_val    = other.imag_val;
    str_val     = std::move(other.str_val);
    quantum_val = std::move(other.quantum_val);
    list_data   = std::move(other.list_data);
    matrix_data = std::move(other.matrix_data);
    circ_data   = std::move(other.circ_data);
    block_data  = std::move(other.block_data);
    func_data   = std::move(other.func_data);

    other.type_info = TypeInfo{};
    other.real_val  = 0.0;
    other.imag_val  = 0.0;

    return *this;
}


// Destructor.  Must be defined here because CircuitData, BlockData, and
// FunctionData are incomplete in the header.

JanusValue::~JanusValue() = default;


// Factory methods


JanusValue JanusValue::make_null() {
    return JanusValue{};
}

JanusValue JanusValue::make_cbit(int64_t val) {
    JanusValue v;
    v.type_info = make_cbit_type();
    v.real_val  = (val != 0) ? 1.0 : 0.0;
    return v;
}

JanusValue JanusValue::make_cnum(double val) {
    JanusValue v;
    v.type_info = make_cnum_type(false);
    v.real_val  = val;
    return v;
}

JanusValue JanusValue::make_cnum_complex(float real, float imag) {
    JanusValue v;
    v.type_info = make_cnum_type(true);
    // Store float-precision values in doubles.  The 32+32 precision
    // constraint is semantic; the runtime carries the values as doubles
    // but rounds to float precision on construction.
    v.real_val = static_cast<double>(real);
    v.imag_val = static_cast<double>(imag);
    return v;
}

JanusValue JanusValue::make_qubit() {
    JanusValue v;
    v.type_info   = make_qubit_type();
    v.quantum_val = std::make_unique<QuantumState>();
    return v;
}

JanusValue JanusValue::make_qubit(QuantumState state) {
    JanusValue v;
    v.type_info   = make_qubit_type();
    v.quantum_val = std::make_unique<QuantumState>(std::move(state));
    return v;
}

JanusValue JanusValue::make_qnum(uint64_t val, uint32_t line) {
    uint32_t qubits = qubits_for_value(val);
    JanusValue v;
    v.type_info   = make_qnum_type(qubits, 0);
    v.quantum_val = std::make_unique<QuantumState>(qubits, val, line);
    return v;
}

JanusValue JanusValue::make_qnum(uint64_t val, uint32_t max_width,
                                 uint32_t line) {
    uint32_t qubits = qubits_for_value(val);
    if (max_width > 0 && qubits > max_width) {
        // Value exceeds the user-specified maximum register width.
        report_error(line);
    }
    // max_width is stored as a cap for future growth checks but does not
    // force the initial allocation wider than needed.
    JanusValue v;
    v.type_info   = make_qnum_type(qubits, max_width);
    v.quantum_val = std::make_unique<QuantumState>(qubits, val, line);
    return v;
}

JanusValue JanusValue::make_qnum(QuantumState state) {
    JanusValue v;
    uint32_t qubits = state.num_qubits();
    v.type_info   = make_qnum_type(qubits, 0);
    v.quantum_val = std::make_unique<QuantumState>(std::move(state));
    return v;
}

JanusValue JanusValue::make_qnum(QuantumState state, uint32_t max_width) {
    JanusValue v;
    uint32_t qubits = state.num_qubits();
    v.type_info   = make_qnum_type(qubits, max_width);
    v.quantum_val = std::make_unique<QuantumState>(std::move(state));
    return v;
}

JanusValue JanusValue::make_cstr(std::string val) {
    JanusValue v;
    v.type_info = make_cstr_type(static_cast<uint32_t>(val.size()));
    v.str_val   = std::move(val);
    return v;
}

JanusValue JanusValue::make_list(std::vector<JanusValue> elements) {
    JanusValue v;
    v.type_info  = make_list_type();
    v.list_data  = std::make_unique<std::vector<JanusValue>>(
        std::move(elements));
    return v;
}

JanusValue JanusValue::make_matrix(uint32_t rows, uint32_t cols,
                                   std::vector<std::complex<double>> data) {
    JanusValue v;
    v.type_info    = make_matrix_type(rows, cols);
    v.matrix_data  = std::make_unique<std::vector<std::complex<double>>>(
        std::move(data));
    return v;
}

JanusValue JanusValue::make_gate(uint32_t qubit_width,
                                 std::vector<std::complex<double>> data) {
    JanusValue v;
    v.type_info    = make_gate_type(qubit_width);
    v.matrix_data  = std::make_unique<std::vector<std::complex<double>>>(
        std::move(data));
    return v;
}

JanusValue JanusValue::make_circ(
        std::vector<QuantumState> qubit_states,
        std::vector<std::vector<JanusValue>> gate_grid) {
    uint32_t qubit_lines = static_cast<uint32_t>(gate_grid.size());
    uint32_t time_steps  = qubit_lines > 0
        ? static_cast<uint32_t>(gate_grid[0].size())
        : 0;

    JanusValue v;
    v.type_info = make_circ_type(qubit_lines, time_steps);
    v.circ_data = std::make_unique<CircuitData>();
    v.circ_data->qubit_states = std::move(qubit_states);
    v.circ_data->gate_grid    = std::move(gate_grid);
    return v;
}

JanusValue JanusValue::make_block(
        std::vector<std::vector<JanusValue>> gate_grid) {
    uint32_t qubit_lines = static_cast<uint32_t>(gate_grid.size());
    uint32_t time_steps  = qubit_lines > 0
        ? static_cast<uint32_t>(gate_grid[0].size())
        : 0;

    JanusValue v;
    v.type_info  = make_block_type(qubit_lines, time_steps);
    v.block_data = std::make_unique<BlockData>();
    v.block_data->gate_grid = std::move(gate_grid);
    return v;
}

JanusValue JanusValue::make_function(
        std::vector<std::string> params,
        const std::vector<std::unique_ptr<Stmt>>* body) {
    JanusValue v;
    v.type_info = make_function_type(static_cast<uint32_t>(params.size()));
    v.func_data = std::make_unique<FunctionData>();
    v.func_data->params = std::move(params);
    v.func_data->body   = body;
    return v;
}


// Query helpers


bool JanusValue::is_null() const noexcept {
    return type_info.type == JanusType::NULL_TYPE;
}

bool JanusValue::is_truthy() const noexcept {
    switch (type_info.type) {

        case JanusType::NULL_TYPE:
            return false;

        case JanusType::CBIT:
            return real_val != 0.0;

        case JanusType::CNUM:
            return std::abs(real_val) > VALUE_ZERO_THRESHOLD ||
                   std::abs(imag_val) > VALUE_ZERO_THRESHOLD;

        case JanusType::QUBIT:
        case JanusType::QNUM: {
            // A quantum state is falsy when all amplitude is on |0...0>.
            if (!quantum_val) return false;
            const auto& amps = quantum_val->amplitudes();
            if (amps.empty()) return false;
            // Check if amplitude at index 0 has magnitude ~1 and all
            // others are ~0.  This means the state is |0...0>.
            if (std::abs(std::norm(amps[0]) - 1.0) > VALUE_ZERO_THRESHOLD)
                return true;
            for (uint64_t i = 1; i < amps.size(); ++i) {
                if (std::norm(amps[i]) > VALUE_ZERO_THRESHOLD)
                    return true;
            }
            return false;
        }

        case JanusType::CSTR:
            return !str_val.empty();

        case JanusType::LIST:
            return list_data && !list_data->empty();

        case JanusType::MATRIX:
        case JanusType::GATE: {
            if (!matrix_data || matrix_data->empty()) return false;
            for (const auto& elem : *matrix_data) {
                if (std::abs(elem.real()) > VALUE_ZERO_THRESHOLD ||
                    std::abs(elem.imag()) > VALUE_ZERO_THRESHOLD) {
                    return true;
                }
            }
            return false;
        }

        case JanusType::CIRC:
            return circ_data && type_info.matrix_cols > 0;

        case JanusType::BLOCK:
            return block_data && type_info.matrix_cols > 0;

        case JanusType::FUNCTION:
            // A valid function value is always truthy.
            return func_data != nullptr;
    }

    return false;
}


// Conversion helpers


double JanusValue::as_real(uint32_t line) const {
    switch (type_info.type) {
        case JanusType::NULL_TYPE:
            return 0.0;
        case JanusType::CBIT:
        case JanusType::CNUM:
            return real_val;
        case JanusType::QUBIT:
        case JanusType::QNUM:
            // Extracting a real from a quantum type requires measurement.
            // The caller should measure explicitly; this is a fallback that
            // peeks without collapsing to avoid accidental side effects
            // in a const-like context.
            if (!quantum_val) return 0.0;
            return static_cast<double>(quantum_val->peek(line));
        default:
            report_error(line);
    }
}

int64_t JanusValue::as_integer(uint32_t line) const {
    switch (type_info.type) {
        case JanusType::NULL_TYPE:
            return 0;
        case JanusType::CBIT:
            return (real_val != 0.0) ? 1 : 0;
        case JanusType::CNUM:
            return static_cast<int64_t>(real_val);
        case JanusType::QUBIT:
        case JanusType::QNUM:
            if (!quantum_val) return 0;
            return static_cast<int64_t>(quantum_val->peek(line));
        default:
            report_error(line);
    }
}


// Helper: format a double at maximum precision, stripping trailing zeros
// after the decimal point for readability.

static std::string format_double(double val) {
    if (val == 0.0) return "0";

    // Check if the value is an exact integer.
    double int_part = 0.0;
    if (std::modf(val, &int_part) == 0.0 &&
        std::abs(val) < static_cast<double>(INT64_MAX)) {
        std::ostringstream oss;
        oss << static_cast<int64_t>(val);
        return oss.str();
    }

    std::ostringstream oss;
    oss << std::setprecision(std::numeric_limits<double>::max_digits10) << val;
    std::string s = oss.str();

    // Strip trailing zeros after the decimal point.
    if (s.find('.') != std::string::npos) {
        std::size_t last_nonzero = s.find_last_not_of('0');
        if (last_nonzero != std::string::npos && s[last_nonzero] == '.') {
            s.erase(last_nonzero + 1);
        } else if (last_nonzero != std::string::npos) {
            s.erase(last_nonzero + 1);
        }
    }
    return s;
}

// Helper: format a complex<double> matrix element.

static std::string format_complex(std::complex<double> c) {
    double re = c.real();
    double im = c.imag();
    bool re_zero = std::abs(re) < VALUE_ZERO_THRESHOLD;
    bool im_zero = std::abs(im) < VALUE_ZERO_THRESHOLD;

    if (re_zero && im_zero) return "0";
    if (im_zero) return format_double(re);
    if (re_zero) return format_double(im) + "i";

    std::string result = "(";
    result += format_double(re);
    if (im >= 0.0) result += "+";
    result += format_double(im);
    result += "i)";
    return result;
}

std::string JanusValue::to_string() const {
    switch (type_info.type) {

        case JanusType::NULL_TYPE:
            return "null";

        case JanusType::CBIT:
            return (real_val != 0.0) ? "1" : "0";

        case JanusType::CNUM: {
            if (type_info.is_complex) {
                float re = static_cast<float>(real_val);
                float im = static_cast<float>(imag_val);
                bool im_zero = std::abs(im) < static_cast<float>(VALUE_ZERO_THRESHOLD);
                if (im_zero) {
                    return format_double(static_cast<double>(re));
                }
                bool re_zero = std::abs(re) < static_cast<float>(VALUE_ZERO_THRESHOLD);
                if (re_zero) {
                    return format_double(static_cast<double>(im)) + "i";
                }
                std::string result = "(";
                result += format_double(static_cast<double>(re));
                if (im >= 0.0f) result += "+";
                result += format_double(static_cast<double>(im));
                result += "i)";
                return result;
            }
            return format_double(real_val);
        }

        case JanusType::QUBIT:
        case JanusType::QNUM: {
            if (!quantum_val) return "|0>";
            return quantum_val->to_dirac_string();
        }

        case JanusType::CSTR:
            return str_val;

        case JanusType::LIST: {
            std::string result = "[";
            if (list_data) {
                for (std::size_t i = 0; i < list_data->size(); ++i) {
                    if (i > 0) result += ", ";
                    result += (*list_data)[i].to_string();
                }
            }
            result += "]";
            return result;
        }

        case JanusType::MATRIX: {
            uint32_t rows = type_info.matrix_rows;
            uint32_t cols = type_info.matrix_cols;
            std::string result = "[";
            if (matrix_data) {
                for (uint32_t r = 0; r < rows; ++r) {
                    if (r > 0) result += "; ";
                    for (uint32_t c = 0; c < cols; ++c) {
                        if (c > 0) result += ", ";
                        uint32_t idx = r * cols + c;
                        if (idx < matrix_data->size()) {
                            result += format_complex((*matrix_data)[idx]);
                        } else {
                            result += "0";
                        }
                    }
                }
            }
            result += "]";
            return result;
        }

        case JanusType::GATE: {
            uint32_t dim = type_info.matrix_rows;
            std::string result = "gate([";
            if (matrix_data) {
                for (uint32_t r = 0; r < dim; ++r) {
                    if (r > 0) result += "; ";
                    for (uint32_t c = 0; c < dim; ++c) {
                        if (c > 0) result += ", ";
                        uint32_t idx = r * dim + c;
                        if (idx < matrix_data->size()) {
                            result += format_complex((*matrix_data)[idx]);
                        } else {
                            result += "0";
                        }
                    }
                }
            }
            result += "])";
            return result;
        }

        case JanusType::CIRC: {
            std::string result = "circ(";
            result += std::to_string(type_info.matrix_rows);
            result += " qubits, ";
            result += std::to_string(type_info.matrix_cols);
            result += " steps)";
            return result;
        }

        case JanusType::BLOCK: {
            std::string result = "block(";
            result += std::to_string(type_info.matrix_rows);
            result += " lines, ";
            result += std::to_string(type_info.matrix_cols);
            result += " steps)";
            return result;
        }

        case JanusType::FUNCTION: {
            std::string result = "function(";
            result += std::to_string(type_info.arity);
            result += ")";
            return result;
        }
    }

    return "unknown";
}

} // namespace janus
