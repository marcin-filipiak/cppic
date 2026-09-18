#include "ast/dump.hpp"
#include "ast/ast.hpp"
#include "lexer/lexer.hpp"
#include "lexer/token.hpp"
#include "parser/parser.hpp"
#include "preprocess/preprocessor.hpp"
#include "transpile/transpiler.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

enum class Mode { Lex, Dump, Emit }; // Emit = transpile to C

int usage() {
    std::fprintf(stderr,
                 "cppic - C++ -> C transpiler for PIC18 (work in progress)\n\n"
                 "usage: cppic [--lex|--dump|--emit] [-I dir] <file.cpp> [more.cpp ...]\n"
                 "  --lex    print tokens\n"
                 "  --dump   print parsed AST\n"
                 "  --emit   transpile to C (default for .cpp with setup/loop)\n"
                 "  -I dir   add an include search directory for #include\n"
                 "  multiple files are preprocessed and linked into one unit\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    Mode mode = Mode::Dump;
    std::vector<std::string> files;
    std::vector<std::string> includePaths;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--lex") mode = Mode::Lex;
        else if (a == "--dump") mode = Mode::Dump;
        else if (a == "--emit") mode = Mode::Emit;
        else if (a == "-h" || a == "--help") return usage();
        else if (a == "-I" && i + 1 < argc) includePaths.push_back(argv[++i]);
        else if (a.rfind("-I", 0) == 0 && a.size() > 2) includePaths.push_back(a.substr(2));
        else files.push_back(a);
    }
    if (files.empty()) return usage();

    try {
        cppic::Preprocessor preprocessor;
        for (const auto& p : includePaths) preprocessor.addIncludePath(p);
        cppic::PreprocessResult pr = preprocessor.run(files);

        cppic::Lexer lexer(std::move(pr.source));
        auto tokens = lexer.tokenize();

        if (mode == Mode::Lex) {
            for (const auto& t : tokens) {
                std::string kind(cppic::tokenName(t.kind));
                if (kind.rfind("Kw", 0) == 0)
                    kind += "{" + t.text + "}";
                else if (!t.value.empty())
                    kind += "{" + t.value + "}";
                std::printf("%-20s @%d:%d\n", kind.c_str(), t.loc.line, t.loc.col);
            }
            return 0;
        }

        cppic::Parser parser(std::move(tokens));
        cppic::TranslationUnit tu = parser.parseTranslationUnit();
        if (mode == Mode::Dump) {
            std::printf("// %s\n", files.front().c_str());
            for (const auto& d : tu.decls) std::printf("%s", cppic::dumpDecl(*d).c_str());
            return 0;
        }

        cppic::Transpiler transpiler;
        std::string c = transpiler.run(tu);

        // Forward `#include <...>` directives that stayed unresolved into the
        // generated C, right after cppic's own includes.
        if (!pr.systemIncludes.empty()) {
            const std::string anchor = "#include \"cppic_runtime.h\"\n";
            std::size_t pos = c.find(anchor);
            if (pos != std::string::npos) {
                std::string incs;
                for (const auto& n : pr.systemIncludes)
                    incs += "#include <" + n + ">\n";
                std::string prefix = c.substr(0, pos + anchor.size());
                c = prefix + incs + c.substr(pos + anchor.size());
            }
        }
        std::fputs(c.c_str(), stdout);
        return 0;
    } catch (const cppic::PreprocessError& e) {
        if (e.file.empty())
            std::fprintf(stderr, "preprocess error: %s\n", e.message.c_str());
        else
            std::fprintf(stderr, "preprocess error: %s:%d: %s\n",
                         e.file.c_str(), e.line, e.message.c_str());
        return 1;
    } catch (const cppic::LexError& e) {
        std::fprintf(stderr, "lex error: %d:%d: %s\n", e.loc.line, e.loc.col,
                     e.message.c_str());
        return 1;
    } catch (const cppic::ParseError& e) {
        std::fprintf(stderr, "parse error: %d:%d: %s\n", e.loc.line, e.loc.col,
                     e.message.c_str());
        return 1;
    } catch (const cppic::TranspileError& e) {
        std::fprintf(stderr, "transpile error: %d:%d: %s\n", e.loc.line,
                     e.loc.col, e.message.c_str());
        return 1;
    }
}