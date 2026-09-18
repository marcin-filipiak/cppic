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
    int rc = readSample(argc > 1 ? argv[1] :
        "/home/kolgreen/playground/cppic/tests/samples/blink.cpp");
    if (rc) return rc;

    // new/delete lowering: malloc + ctor call, dtor + free.
    const char* src =
        "class Led {\n"
        "public:\n"
        "    Led(unsigned char p) { this->pin = p; }\n"
        "    ~Led() { this->pin = 0; }\n"
        "private:\n"
        "    unsigned char pin;\n"
        "};\n"
        "Led* led;\n"
        "int* arr;\n"
        "void setup() { led = new Led(1); arr = new int[4]; }\n"
        "void loop() { delete[] arr; delete led; }\n";

    cppic::Lexer lexer2(src);
    auto tokens2 = lexer2.tokenize();
    cppic::Parser parser2(std::move(tokens2));
    cppic::TranslationUnit tu2 = parser2.parseTranslationUnit();
    cppic::Transpiler t2;
    std::string c2 = t2.run(tu2);

    const char* nd_expects[] = {
        "cppic_malloc",
        "cppic_free",
        "Led__ctor(led, 1)",
        "Led__dtor(led)",
    };
    for (const char* e : nd_expects) {
        if (c2.find(e) == std::string::npos) {
            std::fprintf(stderr, "transpile_test(new/delete): missing: %s\n", e);
            return 1;
        }
    }
    std::fprintf(stderr, "transpile_test: new/delete lowering OK\n");
    return 0;
}
