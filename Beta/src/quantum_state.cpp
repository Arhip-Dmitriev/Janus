#include "quantum_state.hpp"
#include "error.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numbers>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace janus {


//  Internal random engine (thread-local, seeded from random_device)


static std::mt19937_64& rng_engine() {
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    return engine;
}


//  Numeric thresholds


// Two doubles whose absolute value is below this threshold are treated
// as zero for the purposes of Dirac-notation output suppression and
// zero-divisor detection.
static constexpr double ZERO_THRESHOLD = 1.0e-15; //important


//  Expression evaluator for Dirac-notation amplitude coefficients

//
// Grammar Guide (recursive descent):
//   expr      -> term (('+' | '-') term)*
//   term      -> power (('*' | '/') power)*
//   power     -> unary ('^' power)?          (right-associative)
//   unary     -> '-' unary | '+' unary | primary
//   primary   -> NUMBER ['i']
//              | 'i'
//              | IDENT '(' expr ')'           (sqrt, sin, cos)
//              | IDENT                         (pi, e)
//              | '(' expr ')' ['i']
//
// NUMBER accepts integer and floating-point literals with optional
// scientific notation.  'i' produces the imaginary unit (0+1i).

namespace {

enum class DiracTokKind : uint8_t {
    NUMBER,
    IDENT,
    PLUS,
    MINUS,
    STAR,
    SLASH,
    CARET,
    LPAREN,
    RPAREN,
    IMAG_I,
    END
};

struct DiracTok {
    DiracTokKind kind;
    double       number_val = 0.0;
    std::string  ident;
};

// Tokeniser state for the amplitude sub-expression evaluator.
struct DiracLexer {
    const std::string& src;
    std::size_t        pos;
    uint32_t           line;

    explicit DiracLexer(const std::string& s, std::size_t start, uint32_t ln)
        : src(s), pos(start), line(ln) {}

    void skip_spaces() {
        while (pos < src.size() && src[pos] == ' ')
            ++pos;
    }

    DiracTok next() {
        skip_spaces();
        if (pos >= src.size())
            return {DiracTokKind::END, 0.0, {}};

        char c = src[pos];

        if (c == '+') { ++pos; return {DiracTokKind::PLUS, 0.0, {}}; }
        if (c == '-') { ++pos; return {DiracTokKind::MINUS, 0.0, {}}; }
        if (c == '*') { ++pos; return {DiracTokKind::STAR, 0.0, {}}; }
        if (c == '/') { ++pos; return {DiracTokKind::SLASH, 0.0, {}}; }
        if (c == '^') { ++pos; return {DiracTokKind::CARET, 0.0, {}}; }
        if (c == '(') { ++pos; return {DiracTokKind::LPAREN, 0.0, {}}; }
        if (c == ')') { ++pos; return {DiracTokKind::RPAREN, 0.0, {}}; }

        // Number literal.
        if (c >= '0' && c <= '9') {
            std::size_t start = pos;
            while (pos < src.size() &&
                   ((src[pos] >= '0' && src[pos] <= '9') || src[pos] == '.'))
                ++pos;
            // Scientific notation (e.g. 1e-5, 3.14E+2).
            if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
                // Peek ahead: only consume if followed by digit or sign+digit
                // to avoid consuming the constant name 'e' when it stands
                // alone (e.g. "e^(i*pi)").  However, since we already matched
                // at least one digit, 'e' here is unambiguously scientific
                // notation for the number literal.
                ++pos;
                if (pos < src.size() && (src[pos] == '+' || src[pos] == '-'))
                    ++pos;
                while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9')
                    ++pos;
            }
            double val = 0.0;
            try {
                val = std::stod(src.substr(start, pos - start));
            } catch (...) {
                report_error(line);
            }
            return {DiracTokKind::NUMBER, val, {}};
        }

        // Decimal point without leading digit (e.g. .707).
        if (c == '.' && pos + 1 < src.size() &&
            src[pos + 1] >= '0' && src[pos + 1] <= '9') {
            std::size_t start = pos;
            ++pos;
            while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9')
                ++pos;
            if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
                ++pos;
                if (pos < src.size() && (src[pos] == '+' || src[pos] == '-'))
                    ++pos;
                while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9')
                    ++pos;
            }
            double val = 0.0;
            try {
                val = std::stod(src.substr(start, pos - start));
            } catch (...) {
                report_error(line);
            }
            return {DiracTokKind::NUMBER, val, {}};
        }

        // Identifier or 'i' (imaginary unit).
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            std::size_t start = pos;
            while (pos < src.size() &&
                   ((src[pos] >= 'a' && src[pos] <= 'z') ||
                    (src[pos] >= 'A' && src[pos] <= 'Z') ||
                    (src[pos] >= '0' && src[pos] <= '9') ||
                    src[pos] == '_'))
                ++pos;
            std::string id = src.substr(start, pos - start);
            if (id == "i")
                return {DiracTokKind::IMAG_I, 0.0, {}};
            return {DiracTokKind::IDENT, 0.0, std::move(id)};
        }

        // Any other character (including '|', '>') terminates the
        // coefficient expression.
        return {DiracTokKind::END, 0.0, {}};
    }
};

// Recursive-descent expression parser producing a std::complex<double>.
struct DiracExprParser {
    DiracLexer& lex;
    DiracTok    current;
    uint32_t    line;

    explicit DiracExprParser(DiracLexer& l, uint32_t ln)
        : lex(l), current(l.next()), line(ln) {}

    void advance() { current = lex.next(); }

    // expr -> term (('+' | '-') term)*
    std::complex<double> expr() {
        std::complex<double> result = term();
        while (current.kind == DiracTokKind::PLUS ||
               current.kind == DiracTokKind::MINUS) {
            bool is_add = (current.kind == DiracTokKind::PLUS);
            advance();
            std::complex<double> rhs = term();
            result = is_add ? (result + rhs) : (result - rhs);
        }
        return result;
    }

    // term -> power (('*' | '/') power)*
    std::complex<double> term() {
        std::complex<double> result = power_rule();
        while (current.kind == DiracTokKind::STAR ||
               current.kind == DiracTokKind::SLASH) {
            bool is_mul = (current.kind == DiracTokKind::STAR);
            advance();
            std::complex<double> rhs = power_rule();
            if (is_mul) {
                result *= rhs;
            } else {
                if (std::abs(rhs) < ZERO_THRESHOLD)
                    report_error(line);
                result /= rhs;
            }
        }
        return result;
    }

    // power -> unary ('^' power)?   (right-associative)
    std::complex<double> power_rule() {
        std::complex<double> base = unary();
        if (current.kind == DiracTokKind::CARET) {
            advance();
            std::complex<double> exp_val = power_rule();
            return std::pow(base, exp_val);
        }
        return base;
    }

    // unary -> '-' unary | '+' unary | primary
    std::complex<double> unary() {
        if (current.kind == DiracTokKind::MINUS) {
            advance();
            return -unary();
        }
        if (current.kind == DiracTokKind::PLUS) {
            advance();
            return unary();
        }
        return primary();
    }
    
    std::complex<double> primary() {
        if (current.kind == DiracTokKind::NUMBER) {
            double val = current.number_val;
            advance();
            // Trailing 'i' makes this an imaginary literal.
            if (current.kind == DiracTokKind::IMAG_I) {
                advance();
                return {0.0, val};
            }
            return {val, 0.0};
        }

        if (current.kind == DiracTokKind::IMAG_I) {
            advance();
            return {0.0, 1.0};
        }

        if (current.kind == DiracTokKind::IDENT) {
            std::string id = current.ident;
            advance();

            // Constants
            if (id == "pi")
                return {std::numbers::pi, 0.0};
            if (id == "e")
                return {std::numbers::e, 0.0};

            // Functions: sqrt, sin, cos.
            if (id == "sqrt" || id == "sin" || id == "cos") {
                if (current.kind != DiracTokKind::LPAREN)
                    report_error(line);
                advance();
                std::complex<double> arg = expr();
                if (current.kind != DiracTokKind::RPAREN)
                    report_error(line);
                advance();
                if (id == "sqrt") return std::sqrt(arg);
                if (id == "sin")  return std::sin(arg);
                return std::cos(arg);
            }

            // Unknown identifier in a Dirac coefficient is an error
            // (variable references are not permitted at compile time).
            report_error(line);
        }

        if (current.kind == DiracTokKind::LPAREN) {
            advance();
            std::complex<double> val = expr();
            if (current.kind != DiracTokKind::RPAREN)
                report_error(line);
            advance();
            // Trailing 'i' after parenthesised expression.
            if (current.kind == DiracTokKind::IMAG_I) {
                advance();
                return val * std::complex<double>(0.0, 1.0);
            }
            return val;
        }

        // Nothing matched: malformed expression.
        report_error(line);
    }
};

}


//  Dirac notation string parser

//
// Accepted format:
//   "coeff|ket> + coeff|ket> - coeff|ket> ..."
//
// A ket label is either a binary string (all '0'/'1') whose length
// determines the minimum qubit count, or a decimal non-negative integer
// whose magnitude determines the minimum qubit count.
//
// If no coefficient precedes a ket, the coefficient is +1 (or -1 if the
// preceding separator was '-').

QuantumState QuantumState::from_dirac_string(const std::string& dirac,
                                             uint32_t line) {
    if (dirac.empty())
        report_error(line);

    struct Term {
        std::complex<double> coeff;
        uint64_t             basis;
        uint32_t             ket_qubit_hint;
    };
    std::vector<Term> terms;

    std::size_t pos = 0;
    const std::size_t len = dirac.size();

    auto skip_ws = [&]() {
        while (pos < len && dirac[pos] == ' ')
            ++pos;
    };

    // Sign of the next term: +1.0 or -1.0.  The first term defaults to
    // +1 unless explicitly preceded by '-'.
    double leading_sign = 1.0;

    while (pos < len) {
        skip_ws();
        if (pos >= len) break;

        // Detect explicit leading sign for this term.  This handles the
        // '+' or '-' separators between terms as well as a leading sign
        // on the very first term.
        if (dirac[pos] == '+') {
            leading_sign = 1.0;
            ++pos;
            skip_ws();
        } else if (dirac[pos] == '-') {
            leading_sign = -1.0;
            ++pos;
            skip_ws();
        }

        // Parse coefficient (everything up to '|').
        // If the next character is already '|', coefficient is 1.0 * sign.
        std::complex<double> coeff(leading_sign, 0.0);

        if (pos < len && dirac[pos] != '|') {
            DiracLexer lexer(dirac, pos, line);
            DiracExprParser parser(lexer, line);
            std::complex<double> parsed = parser.expr();
            coeff = parsed * leading_sign;
            pos = lexer.pos;
            skip_ws();
        }

        // Expect '|'.
        if (pos >= len || dirac[pos] != '|')
            report_error(line);
        ++pos;  // consume '|'

        // Parse ket label (up to '>').
        std::size_t ket_start = pos;
        while (pos < len && dirac[pos] != '>')
            ++pos;
        if (pos >= len)
            report_error(line);
        std::string ket_label = dirac.substr(ket_start, pos - ket_start);
        ++pos;  // consume '>'

        if (ket_label.empty())
            report_error(line);

        // Determine whether the label is binary or decimal.
        bool is_binary = true;
        for (char ch : ket_label) {
            if (ch != '0' && ch != '1') {
                is_binary = false;
                break;
            }
        }

        uint64_t basis_index = 0;
        uint32_t ket_qubit_hint = 0;

        if (is_binary) {
            ket_qubit_hint = static_cast<uint32_t>(ket_label.size());
            for (char ch : ket_label) {
                basis_index = (basis_index << 1) |
                              static_cast<uint64_t>(ch - '0');
            }
        } else {
            // Decimal label: every character must be a digit.
            for (char ch : ket_label) {
                if (ch < '0' || ch > '9')
                    report_error(line);
            }
            try {
                basis_index = std::stoull(ket_label);
            } catch (...) {
                report_error(line);
            }
            ket_qubit_hint = qubits_for_value(basis_index);
        }

        terms.push_back({coeff, basis_index, ket_qubit_hint});

        // Reset sign for the next term (defaults to +1; the separator
        // will override if present).
        leading_sign = 1.0;

        skip_ws();
    }

    if (terms.empty())
        report_error(line);

    // Determine the number of qubits: the maximum qubit hint across all
    // terms, but at least 1.
    uint32_t num_qubits = 1;
    for (const auto& t : terms) {
        if (t.ket_qubit_hint > num_qubits)
            num_qubits = t.ket_qubit_hint;
        uint32_t val_bits = qubits_for_value(t.basis);
        if (val_bits > num_qubits)
            num_qubits = val_bits;
    }

    uint64_t dim = uint64_t{1} << num_qubits;

    // Build the state vector.
    std::vector<std::complex<double>> amps(dim, {0.0, 0.0});
    for (const auto& t : terms) {
        if (t.basis >= dim)
            report_error(line);
        amps[t.basis] += t.coeff;
    }

    QuantumState state;
    state.num_qubits_ = num_qubits;
    state.amplitudes_  = std::move(amps);

    // Normalise automatically with no warning.
    double nsq = state.norm_sq();
    if (nsq < ZERO_THRESHOLD * ZERO_THRESHOLD)
        report_error(line);
    double inv_norm = 1.0 / std::sqrt(nsq);
    for (auto& a : state.amplitudes_)
        a *= inv_norm;

    return state;
}


//  Constructors


QuantumState::QuantumState()
    : amplitudes_(2, {0.0, 0.0}), num_qubits_(1) {
    amplitudes_[0] = {1.0, 0.0};
}

QuantumState::QuantumState(uint32_t num_qubits)
    : num_qubits_(num_qubits < 1 ? 1 : num_qubits) {
    amplitudes_.assign(uint64_t{1} << num_qubits_, {0.0, 0.0});
    amplitudes_[0] = {1.0, 0.0};
}

QuantumState::QuantumState(uint32_t num_qubits, uint64_t basis_state,
                           uint32_t line)
    : num_qubits_(num_qubits < 1 ? 1 : num_qubits) {
    uint64_t dim = uint64_t{1} << num_qubits_;
    if (basis_state >= dim)
        report_error(line);
    amplitudes_.assign(dim, {0.0, 0.0});
    amplitudes_[basis_state] = {1.0, 0.0};
}


//  Accessors


uint32_t QuantumState::num_qubits() const noexcept {
    return num_qubits_;
}

uint64_t QuantumState::num_amplitudes() const noexcept {
    return amplitudes_.size();
}

const std::complex<double>& QuantumState::amplitude(uint64_t index) const noexcept {
    return amplitudes_[index];
}

std::complex<double>& QuantumState::amplitude(uint64_t index) noexcept {
    return amplitudes_[index];
}

const std::vector<std::complex<double>>& QuantumState::amplitudes() const noexcept {
    return amplitudes_;
}

std::vector<std::complex<double>>& QuantumState::amplitudes() noexcept {
    return amplitudes_;
}


//  Norm and normalisation


double QuantumState::norm_sq() const noexcept {
    double total = 0.0;
    for (const auto& a : amplitudes_)
        total += std::norm(a);  // std::norm returns |z|^2
    return total;
}

void QuantumState::normalise(uint32_t line) {
    double nsq = norm_sq();
    if (nsq < ZERO_THRESHOLD * ZERO_THRESHOLD)
        report_error(line);
    double inv = 1.0 / std::sqrt(nsq);
    for (auto& a : amplitudes_)
        a *= inv;
}


//  Dimension unification helper


void QuantumState::unify_dimensions(
        const QuantumState& a, const QuantumState& b,
        std::vector<std::complex<double>>& out_a,
        std::vector<std::complex<double>>& out_b,
        uint32_t& out_qubits) {
    out_qubits = std::max(a.num_qubits_, b.num_qubits_);
    uint64_t dim = uint64_t{1} << out_qubits;
    out_a = a.amplitudes_;
    out_b = b.amplitudes_;
    out_a.resize(dim, {0.0, 0.0});
    out_b.resize(dim, {0.0, 0.0});
}


//  Element-wise arithmetic operations


QuantumState QuantumState::add(const QuantumState& rhs, uint32_t line) const {
    std::vector<std::complex<double>> va, vb;
    uint32_t qubits = 0;
    unify_dimensions(*this, rhs, va, vb, qubits);

    QuantumState result;
    result.num_qubits_ = qubits;
    result.amplitudes_.resize(va.size());
    for (std::size_t i = 0; i < va.size(); ++i)
        result.amplitudes_[i] = va[i] + vb[i];

    result.normalise(line);
    return result;
}

QuantumState QuantumState::subtract(const QuantumState& rhs, uint32_t line) const {
    std::vector<std::complex<double>> va, vb;
    uint32_t qubits = 0;
    unify_dimensions(*this, rhs, va, vb, qubits);

    QuantumState result;
    result.num_qubits_ = qubits;
    result.amplitudes_.resize(va.size());
    for (std::size_t i = 0; i < va.size(); ++i)
        result.amplitudes_[i] = va[i] - vb[i];

    result.normalise(line);
    return result;
}

QuantumState QuantumState::multiply(const QuantumState& rhs, uint32_t line) const {
    std::vector<std::complex<double>> va, vb;
    uint32_t qubits = 0;
    unify_dimensions(*this, rhs, va, vb, qubits);

    QuantumState result;
    result.num_qubits_ = qubits;
    result.amplitudes_.resize(va.size());
    for (std::size_t i = 0; i < va.size(); ++i)
        result.amplitudes_[i] = va[i] * vb[i];

    result.normalise(line);
    return result;
}

QuantumState QuantumState::divide(const QuantumState& rhs, uint32_t line) const {
    std::vector<std::complex<double>> va, vb;
    uint32_t qubits = 0;
    unify_dimensions(*this, rhs, va, vb, qubits);

    QuantumState result;
    result.num_qubits_ = qubits;
    result.amplitudes_.resize(va.size());
    for (std::size_t i = 0; i < va.size(); ++i) {
        if (std::abs(vb[i]) < ZERO_THRESHOLD)
            report_error(line);
        result.amplitudes_[i] = va[i] / vb[i];
    }

    result.normalise(line);
    return result;
}

QuantumState QuantumState::modulus(const QuantumState& rhs, uint32_t line) const {
    std::vector<std::complex<double>> va, vb;
    uint32_t qubits = 0;
    unify_dimensions(*this, rhs, va, vb, qubits);

    QuantumState result;
    result.num_qubits_ = qubits;
    result.amplitudes_.resize(va.size());
    for (std::size_t i = 0; i < va.size(); ++i) {
        if (std::abs(vb[i]) < ZERO_THRESHOLD)
            report_error(line);
        // Complex modulus: apply fmod independently to real and imaginary
        // parts.  The documentation specifies element-wise modulus
        // analogous to division but does not define complex modulus
        // explicitly; this is the most conservative interpretation.
        double re_b = vb[i].real();
        double im_b = vb[i].imag();
        double re_r = (std::abs(re_b) < ZERO_THRESHOLD)
                          ? 0.0
                          : std::fmod(va[i].real(), re_b);
        double im_r = (std::abs(im_b) < ZERO_THRESHOLD)
                          ? 0.0
                          : std::fmod(va[i].imag(), im_b);
        result.amplitudes_[i] = {re_r, im_r};
    }

    result.normalise(line);
    return result;
}

QuantumState QuantumState::power(const QuantumState& rhs, uint32_t line) const {
    std::vector<std::complex<double>> va, vb;
    uint32_t qubits = 0;
    unify_dimensions(*this, rhs, va, vb, qubits);

    QuantumState result;
    result.num_qubits_ = qubits;
    result.amplitudes_.resize(va.size());
    for (std::size_t i = 0; i < va.size(); ++i)
        result.amplitudes_[i] = std::pow(va[i], vb[i]);

    result.normalise(line);
    return result;
}


//  Measurement helpers


uint64_t QuantumState::sample_outcome() const {
    // Build cumulative probability distribution and sample.
    double total = 0.0;
    for (const auto& a : amplitudes_)
        total += std::norm(a);

    std::uniform_real_distribution<double> dist(0.0, total);
    double r = dist(rng_engine());

    double cumulative = 0.0;
    for (uint64_t i = 0; i < amplitudes_.size(); ++i) {
        cumulative += std::norm(amplitudes_[i]);
        if (r <= cumulative)
            return i;
    }
    // Floating-point rounding edge case: return the last basis state.
    return amplitudes_.size() - 1;
}

uint64_t QuantumState::measure(uint32_t line) {
    double nsq = norm_sq();
    if (nsq < ZERO_THRESHOLD * ZERO_THRESHOLD)
        report_error(line);

    uint64_t outcome = sample_outcome();

    // Collapse: set all amplitudes to zero except the measured one.
    for (uint64_t i = 0; i < amplitudes_.size(); ++i)
        amplitudes_[i] = {0.0, 0.0};
    amplitudes_[outcome] = {1.0, 0.0};

    return outcome;
}

uint64_t QuantumState::peek(uint32_t line) const {
    double nsq = norm_sq();
    if (nsq < ZERO_THRESHOLD * ZERO_THRESHOLD)
        report_error(line);
    return sample_outcome();
}


//  Dirac-notation string output


std::string QuantumState::to_dirac_string() const {
    // Work on a copy so we can normalise out global phase without
    // modifying the actual state.
    std::vector<std::complex<double>> amps = amplitudes_;
    uint64_t dim = amps.size();

    // Find the first non-zero amplitude to determine global phase.
    std::complex<double> phase_ref(0.0, 0.0);
    for (uint64_t i = 0; i < dim; ++i) {
        if (std::abs(amps[i]) > ZERO_THRESHOLD) {
            phase_ref = amps[i];
            break;
        }
    }

    // Normalise out global phase: multiply every amplitude by
    // e^{-i * arg(phase_ref)} so the first non-zero amplitude becomes
    // real and positive.
    if (std::abs(phase_ref) > ZERO_THRESHOLD) {
        double angle = std::arg(phase_ref);
        if (std::abs(angle) > ZERO_THRESHOLD) {
            std::complex<double> rotator = std::exp(
                std::complex<double>(0.0, -angle));
            for (auto& a : amps)
                a *= rotator;
        }
    }

    // Collect non-zero (basis, coefficient) pairs.
    struct OutputTerm {
        std::complex<double> coeff;
        uint64_t             basis;
    };
    std::vector<OutputTerm> terms;
    for (uint64_t i = 0; i < dim; ++i) {
        double re = amps[i].real();
        double im = amps[i].imag();
        if (std::abs(re) < ZERO_THRESHOLD && std::abs(im) < ZERO_THRESHOLD)
            continue;
        // Snap tiny residuals to exact zero.
        if (std::abs(re) < ZERO_THRESHOLD) re = 0.0;
        if (std::abs(im) < ZERO_THRESHOLD) im = 0.0;
        terms.push_back({{re, im}, i});
    }

    if (terms.empty()) {
        // All amplitudes zero (should not happen for a valid state).
        std::string ket(num_qubits_, '0');
        return "0|" + ket + ">";
    }

    constexpr int max_prec = std::numeric_limits<double>::max_digits10;
    std::ostringstream oss;
    oss << std::setprecision(max_prec);

    for (std::size_t ti = 0; ti < terms.size(); ++ti) {
        double re = terms[ti].coeff.real();
        double im = terms[ti].coeff.imag();
        uint64_t basis = terms[ti].basis;
        bool is_first = (ti == 0);

        bool re_zero = (re == 0.0);
        bool im_zero = (im == 0.0);

        // Build the ket label in binary.
        std::string ket_label;
        ket_label.reserve(num_qubits_);
        for (int b = static_cast<int>(num_qubits_) - 1; b >= 0; --b)
            ket_label += static_cast<char>('0' + ((basis >> b) & 1u));

        if (im_zero) {
            // Purely real coefficient.
            if (is_first) {
                if (std::abs(re - 1.0) < ZERO_THRESHOLD) {
                    // coefficient 1: omit
                } else if (std::abs(re + 1.0) < ZERO_THRESHOLD) {
                    oss << "-";
                } else {
                    oss << re;
                }
            } else {
                if (re >= 0.0) {
                    oss << " + ";
                    if (std::abs(re - 1.0) < ZERO_THRESHOLD) {
                        // omit
                    } else {
                        oss << re;
                    }
                } else {
                    oss << " - ";
                    double abs_re = -re;
                    if (std::abs(abs_re - 1.0) < ZERO_THRESHOLD) {
                        // omit
                    } else {
                        oss << abs_re;
                    }
                }
            }
        } else if (re_zero) {
            // Purely imaginary coefficient.
            if (is_first) {
                if (std::abs(im - 1.0) < ZERO_THRESHOLD) {
                    oss << "i";
                } else if (std::abs(im + 1.0) < ZERO_THRESHOLD) {
                    oss << "-i";
                } else if (im < 0.0) {
                    oss << im << "i";
                } else {
                    oss << im << "i";
                }
            } else {
                if (im >= 0.0) {
                    oss << " + ";
                    if (std::abs(im - 1.0) < ZERO_THRESHOLD) {
                        oss << "i";
                    } else {
                        oss << im << "i";
                    }
                } else {
                    oss << " - ";
                    double abs_im = -im;
                    if (std::abs(abs_im - 1.0) < ZERO_THRESHOLD) {
                        oss << "i";
                    } else {
                        oss << abs_im << "i";
                    }
                }
            }
        } else {
            // General complex coefficient: parenthesised form.
            if (!is_first)
                oss << " + ";
            oss << "(" << re;
            if (im >= 0.0)
                oss << "+" << im << "i)";
            else
                oss << im << "i)";
        }

        oss << "|" << ket_label << ">";
    }

    return oss.str();
}

} // namespace janus
