#include "ast/dump.hpp"
#include "ast/ast.hpp"
#include "lexer/lexer.hpp"
#include "lexer/token.hpp"
#include "parser/parser.hpp"
#include "transpile/transpiler.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string readFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "cppic: cannot open '%s'\n", path);
        std::exit(1);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

enum class Mode { Lex, Dump, Emit }; // Emit = transpile to C

int usage() {
    std::fprintf(stderr,
                 "cppic - C++ -> C transpiler for PIC18 (work in progress)\n\n"
                 "usage: cppic [--lex|--dump|--emit] <file.cpp>\n"
                 "  --lex   print tokens\n"
                 "  --dump  print parsed AST\n"
                 "  --emit  transpile to C (default for .cpp with setup/loop)\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    Mode mode = Mode::Dump;
    const char* file = nullptr;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--lex") mode = Mode::Lex;
        else if (a == "--dump") mode = Mode::Dump;
        else if (a == "--emit") mode = Mode::Emit;
        else if (a == "-h" || a == "--help") return usage();
        else file = argv[i];
    }
    if (!file) return usage();

    std::string src = readFile(file);

    try {
        cppic::Lexer lexer(std::move(src));
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
            std::printf("// %s\n", file);
            for (const auto& d : tu.decls) std::printf("%s", cppic::dumpDecl(*d).c_str());
            return 0;
        }

        cppic::Transpiler transpiler;
        std::string c = transpiler.run(tu);
        std::fputs(c.c_str(), stdout);
        return 0;
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