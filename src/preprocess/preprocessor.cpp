#include "preprocess/preprocessor.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace cppic {
namespace {

std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool isIdentStart(unsigned char c) {
    return std::isalpha(c) || c == '_';
}

bool isIdentChar(unsigned char c) {
    return std::isalnum(c) || c == '_';
}

}  // namespace

void Preprocessor::addIncludePath(const std::string& p) {
    includePaths_.push_back(p);
}

std::string Preprocessor::slurpFile(const std::string& path) const {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw PreprocessError{"cannot open '" + path + "'", currentFile_, lineNo_};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string Preprocessor::findInclude(const std::string& name,
                                      const std::string& fromDir) const {
    std::vector<std::string> dirs;
    if (!fromDir.empty()) dirs.push_back(fromDir);
    dirs.insert(dirs.end(), includePaths_.begin(), includePaths_.end());
    for (const auto& d : dirs) {
        fs::path cand = fs::path(d) / name;
        if (fs::exists(cand)) return cand.string();
    }
    return {};
}

void Preprocessor::defineMacro(const std::string& name, const std::string& value) {
    macros_[name] = value;
}

bool Preprocessor::isDefined(const std::string& name) const {
    return macros_.find(name) != macros_.end();
}

[[noreturn]] void Preprocessor::err(const std::string& msg) const {
    throw PreprocessError{msg, currentFile_, lineNo_};
}

void Preprocessor::processFile(const std::string& path) {
    std::string canon = fs::exists(path) ? fs::weakly_canonical(path).string() : path;
    for (const auto& o : openFiles_)
        if (o == canon) return;  // already open: guards against include cycles
    openFiles_.push_back(canon);

    std::string text = slurpFile(canon);
    std::string prevFile = currentFile_;
    currentFile_ = canon;
    lineNo_ = 0;

    // Split into lines, joining continuations (trailing '\').
    std::istringstream in(text);
    std::string line;
    std::string acc;
    while (std::getline(in, line)) {
        ++lineNo_;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!acc.empty()) {
            lineNo_ -= 1;  // spliced line: keep the directive's start line
        }
        acc += line;
        std::string t = trim(acc);
        if (!t.empty() && t.back() == '\\') {
            acc = t.substr(0, t.size() - 1);
            continue;
        }
        processLine(acc);
        acc.clear();
    }
    if (!acc.empty()) processLine(acc);

    openFiles_.pop_back();
    currentFile_ = prevFile;
}

void Preprocessor::processLine(std::string& line) {
    std::string t = trim(line);
    if (t.empty()) return;
    if (t[0] == '#') {
        processDirective(trim(t.substr(1)));
        return;
    }
    if (active_) {
        result_.source += substituteMacros(line);
        result_.source += '\n';
    }
}

void Preprocessor::processDirective(std::string body) {
    body = stripComments(trim(body));
    std::istringstream d(body);
    std::string kw;
    d >> kw;

    if (kw == "include") {
        if (!active_) return;
        std::string rest = trim(body.substr(kw.size()));
        if (rest.empty()) return;
        char endc = 0;
        std::size_t start = std::string::npos;
        if (rest[0] == '"') {
            endc = '"';
            start = 1;
        } else if (rest[0] == '<') {
            endc = '>';
            start = 1;
        } else {
            return;  // not a valid include form; ignore
        }
        std::size_t e = rest.find(endc, start);
        if (e == std::string::npos) return;
        std::string name = rest.substr(start, e - start);
        if (name.empty()) return;

        fs::path fromDir = fs::path(currentFile_).parent_path();
        std::string resolved = findInclude(name, fromDir.string());
        if (resolved.empty()) {
            // Forward unresolved includes to the generated C; the runtime and
            // standard/PIC headers are handled by sdcc at that point.
            if (name != "cppic_runtime.h" && name != "stdint.h")
                result_.systemIncludes.push_back(name);
            return;
        }
        processFile(resolved);
        return;
    }

    if (kw == "define") {
        if (!active_) return;
        std::string rest = trim(body.substr(6));
        std::size_t i = 0;
        while (i < rest.size() && isIdentStart(static_cast<unsigned char>(rest[i]))) ++i;
        std::string name = rest.substr(0, i);
        if (name.empty()) return;
        if (i < rest.size() && rest[i] == '(') return;  // function-like macro: unsupported
        defineMacro(name, trim(rest.substr(i)));
        return;
    }

    if (kw == "undef") {
        if (!active_) return;
        std::string rest = trim(body.substr(5));
        macros_.erase(rest);
        return;
    }

    if (kw == "ifdef" || kw == "ifndef") {
        std::string name = trim(body.substr(kw.size()));
        bool cond = isDefined(name);
        if (kw == "ifndef") cond = !cond;
        condStack_.push_back({active_, active_ && cond});
        active_ = active_ && cond;
        return;
    }

    if (kw == "if") {
        bool cond = evalCond(trim(body.substr(2)));
        condStack_.push_back({active_, active_ && cond});
        active_ = active_ && cond;
        return;
    }

    if (kw == "elif") {
        if (condStack_.empty()) err("#elif without matching #if");
        Cond& c = condStack_.back();
        if (!c.parent || c.taken) {
            active_ = false;
            return;
        }
        bool cond = evalCond(trim(body.substr(4)));
        c.taken = cond;
        active_ = cond;
        return;
    }

    if (kw == "else") {
        if (condStack_.empty()) err("#else without matching #if");
        Cond& c = condStack_.back();
        if (!c.parent || c.taken) {
            active_ = false;
            return;
        }
        c.taken = true;
        active_ = true;
        return;
    }

    if (kw == "endif") {
        if (condStack_.empty()) err("#endif without matching #if");
        active_ = condStack_.back().parent;
        condStack_.pop_back();
        return;
    }

    if (kw == "error") {
        err("preprocessor error: " + trim(body.substr(5)));
    }

    // #pragma / #line / #warning / ... : ignored
}

std::string Preprocessor::stripComments(const std::string& in) const {
    std::string out;
    out.reserve(in.size());
    std::size_t i = 0;
    while (i < in.size()) {
        if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '/') break;
        if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '*') {
            i += 2;
            while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/')) ++i;
            i += 2;
            continue;
        }
        out += in[i++];
    }
    return out;
}

std::string Preprocessor::substituteMacros(const std::string& line) const {
    std::string out;
    out.reserve(line.size());
    std::size_t i = 0;
    while (i < line.size()) {
        char c = line[i];
        if (c == '"' || c == '\'') {  // copy string / char literals verbatim
            char q = c;
            out += line[i++];
            while (i < line.size()) {
                out += line[i];
                if (line[i] == '\\' && i + 1 < line.size()) {
                    out += line[i + 1];
                    i += 2;
                    continue;
                }
                if (line[i] == q) { ++i; break; }
                ++i;
            }
            continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            out += line.substr(i);  // trailing comment: leave as-is
            break;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            out += "/*";  // cppic's lexer skips block comments
            i += 2;
            continue;
        }
        if (isIdentStart(static_cast<unsigned char>(c))) {
            std::size_t j = i;
            while (j < line.size() && isIdentChar(static_cast<unsigned char>(line[j]))) ++j;
            std::string name = line.substr(i, j - i);
            auto it = macros_.find(name);
            if (it != macros_.end()) out += it->second;
            else out += name;
            i = j;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

// Rewrites a #if expression into a boolean expression over plain integers:
// `defined(X)` / `defined X` become 1/0, object-like macros are substituted
// and undefined identifiers collapse to 0 (standard preprocessor behaviour).
std::string Preprocessor::normalizeCondExpr(const std::string& expr) const {
    std::string out;
    std::size_t i = 0;
    while (i < expr.size()) {
        if (std::isspace(static_cast<unsigned char>(expr[i]))) { ++i; continue; }
        if (expr[i] == '/' && i + 1 < expr.size() && expr[i + 1] == '*') {
            i += 2;
            while (i + 1 < expr.size() && !(expr[i] == '*' && expr[i + 1] == '/')) ++i;
            i += 2;
            continue;
        }
        if (isIdentStart(static_cast<unsigned char>(expr[i]))) {
            std::size_t j = i;
            while (j < expr.size() && isIdentChar(static_cast<unsigned char>(expr[j]))) ++j;
            std::string word = expr.substr(i, j - i);
            if (word == "defined") {
                std::size_t k = j;  // skip whitespace / optional '('
                while (k < expr.size() && std::isspace(static_cast<unsigned char>(expr[k]))) ++k;
                bool paren = false;
                if (k < expr.size() && expr[k] == '(') { paren = true; ++k; }
                while (k < expr.size() && std::isspace(static_cast<unsigned char>(expr[k]))) ++k;
                if (k >= expr.size() || !isIdentStart(static_cast<unsigned char>(expr[k])))
                    return "";
                std::size_t m = k;
                while (m < expr.size() && isIdentChar(static_cast<unsigned char>(expr[m]))) ++m;
                std::string mname = expr.substr(k, m - k);
                out += isDefined(mname) ? "1" : "0";
                i = m;
                if (paren) {
                    while (i < expr.size() && std::isspace(static_cast<unsigned char>(expr[i]))) ++i;
                    if (i < expr.size() && expr[i] == ')') ++i;
                }
                continue;
            }
            auto it = macros_.find(word);
            out += it == macros_.end() || it->second.empty()
                       ? "0"
                       : it->second;
            i = j;
            continue;
        }
        out += expr[i++];
    }
    return out;
}

bool Preprocessor::evalCond(const std::string& expr) {
    const std::string s = normalizeCondExpr(expr);
    struct P {
        const std::string& s;
        std::size_t i = 0;

        void skip() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }

        bool parse() {
            long long v = orExpr();
            skip();
            return v != 0 && i >= s.size();
        }
        long long orExpr() {
            long long v = andExpr();
            for (;;) {
                skip();
                if (i + 1 < s.size() && s[i] == '|' && s[i + 1] == '|') {
                    i += 2;
                    long long r = andExpr();
                    v = (v != 0 || r != 0) ? 1 : 0;
                } else return v;
            }
        }
        long long andExpr() {
            long long v = eqExpr();
            for (;;) {
                skip();
                if (i + 1 < s.size() && s[i] == '&' && s[i + 1] == '&') {
                    i += 2;
                    long long r = eqExpr();
                    v = (v != 0 && r != 0) ? 1 : 0;
                } else return v;
            }
        }
        long long eqExpr() {
            long long v = unary();
            skip();
            if (i + 1 < s.size() && s[i] == '=' && s[i + 1] == '=') {
                i += 2;
                long long r = unary();
                return v == r ? 1 : 0;
            }
            if (i + 1 < s.size() && s[i] == '!' && s[i + 1] == '=') {
                i += 2;
                long long r = unary();
                return v != r ? 1 : 0;
            }
            return v;
        }
        long long unary() {
            skip();
            if (i < s.size() && s[i] == '!') {
                ++i;
                return unary() == 0 ? 1 : 0;
            }
            return primary();
        }
        long long primary() {
            skip();
            if (i < s.size() && s[i] == '(') {
                ++i;
                long long v = orExpr();
                skip();
                if (i < s.size() && s[i] == ')') ++i;
                return v;
            }
            if (i >= s.size()) return 0;
            const char* p = s.c_str() + i;
            char* end = nullptr;
            long long n = std::strtoll(p, &end, 0);
            if (end == p) return 0;
            i += static_cast<std::size_t>(end - p);
            return n;
        }
    };
    P p{s};
    return p.parse();
}

PreprocessResult Preprocessor::run(const std::vector<std::string>& files) {
    openFiles_.clear();
    macros_.clear();
    condStack_.clear();
    active_ = true;
    result_ = PreprocessResult{};
    for (const auto& f : files) {
        if (f.empty()) continue;
        if (fs::exists(f)) {
            processFile(f);
        } else {
            throw PreprocessError{"cannot open '" + f + "'", f, 0};
        }
        result_.source += '\n';
    }
    if (!condStack_.empty())
        throw PreprocessError{"unterminated #if directive", currentFile_, lineNo_};
    return result_;
}

}  // namespace cppic