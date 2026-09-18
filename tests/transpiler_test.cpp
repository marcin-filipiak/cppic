#include "ast/ast.hpp"
#include "ast/dump.hpp"
#include "lexer/lexer.hpp"
#include "lexer/token.hpp"
#include "parser/parser.hpp"
#include "transpile/transpiler.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

int readSample(const char* kcollection) {
    const char* p = std::getenv("CPPIC_SAMPLE_BLINK");
    if (!p) p = kcollection;
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "transpile: cannot open sample '%s'\n", p);
        return 2;
    }
    std::ostringstream ss;
    ss << f.rdbuf();

    cppic::Lexer lexer(ss.str());
    auto tokens = lexer.tokenize();
    cppic::Parser parser(std::move(tokens));
    cppic::TranslationUnit tu = parser.parseTranslationUnit();

    cppic::Transpiler transpiler;
    std::string c = transpiler.run(tu);

    const char* expects[] = {
        "#include \"cppic_runtime.h\"",
        "typedef struct Led Led;",
        "Led__init(",
        "Led__on(",
        "Led led;",
    };
    for (const char* e : expects) {
        if (c.find(e) == std::string::npos) {
            std::fprintf(stderr, "transpile_test: missing: %s\n", e);
            return 1;
        }
    }
    std::fputs(c.c_str(), stdout);
    return 0;
}

int main(int argc, char** argv) {
    return readSample(argc > 1 ? argv[1] :
        "/home/kolgreen/playground/cppic/tests/samples/blink.cpp");
}
