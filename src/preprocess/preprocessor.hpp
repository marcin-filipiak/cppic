#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace cppic {

struct PreprocessError {
    std::string message;
    std::string file;
    int line = 0;
};

struct PreprocessResult {
    std::string source;
    // `#include <name>` directives that were not resolved on disk; the caller
    // forwards them into the generated C (sdcc resolves them as system headers).
    std::vector<std::string> systemIncludes;
};

// Line-based C preprocessor subset + file linker for cppic.
//
//  - #include "file" / <file>: resolved on disk (relative to the including
//    file, then any added include paths) and inlined recursively.  Unresolved
//    includes are collected into PreprocessResult::systemIncludes and forwarded
//    verbatim into the generated C (PIC/system headers).
//  - #define NAME [value] / #undef: object-like macros, substituted in code.
//  - #ifdef / #ifndef / #if / #elif / #else / #endif with a small boolean
//    expression language: defined(NAME), object-like macros, integers and
//    ! && || == != plus parentheses.
//  - #pragma / #line / #warning / other directives are ignored silently,
//    #error throws PreprocessError.
//  - Multiple entry files are processed in order into a single merged source,
//    which gives simple "linker" behaviour for multi-file sketches.
class Preprocessor {
public:
    void addIncludePath(const std::string& p);

    // Pre-processes `files` in order (merging them into one translation unit).
    // Throws PreprocessError on directives it cannot honour.
    PreprocessResult run(const std::vector<std::string>& files);

private:
    struct Cond {
        bool parent;  // whether enumeration was active before the block
        bool taken;   // a branch of this block has already been emitted
    };

    void processFile(const std::string& path);
    std::string slurpFile(const std::string& path) const;
    std::string findInclude(const std::string& name, const std::string& fromDir) const;
    void processLine(std::string& line);
    void processDirective(std::string body);
    std::string stripComments(const std::string& in) const;

    void defineMacro(const std::string& name, const std::string& value);
    std::string substituteMacros(const std::string& line) const;

    bool evalCond(const std::string& expr);
    bool isDefined(const std::string& name) const;
    std::string normalizeCondExpr(const std::string& expr) const;

    [[noreturn]] void err(const std::string& msg) const;

    std::vector<std::string> includePaths_;
    std::unordered_map<std::string, std::string> macros_;
    std::vector<std::string> openFiles_;  // cycle detection
    std::vector<Cond> condStack_;
    bool active_ = true;
    int lineNo_ = 0;
    std::string currentFile_;
    PreprocessResult result_;
};

}  // namespace cppic