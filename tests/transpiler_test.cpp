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

    // String lowering: zero-init locals, deep copy, concat/append, compare,
    // methods and indexing.
    const char* ssrc =
        "String g = \"Hello\";\n"
        "String other;\n"
        "void setup() {\n"
        "    String s = \"world\";\n"
        "    other = g + \", world\";\n"
        "    other += \"!\";\n"
        "    s = other;\n"
        "    unsigned char ok = (s == other) && (s != g) && (g < s);\n"
        "    ok = ok && (s.length() == 13) && (s.charAt(0) == 'o');\n"
        "    PORTB = ok;\n"
        "}\n";

    cppic::Lexer lexer3(ssrc);
    auto tokens3 = lexer3.tokenize();
    cppic::Parser parser3(std::move(tokens3));
    cppic::TranslationUnit tu3 = parser3.parseTranslationUnit();
    cppic::Transpiler t3;
    std::string c3 = t3.run(tu3);

    const char* str_expects[] = {
        "CppicString g = {(char*)\"Hello\", 5, 5, 0};",
        "CppicString other = {0, 0, 0, 0};",
        "CppicString s = {0, 0, 0, 0};",
        "cppic_string_set(&s, \"world\")",
        "cppic_string_concat_lit(&other, &g, \", world\")",
        "cppic_string_append_lit(&other, \"!\")",
        "cppic_string_copy(&s, &other)",
        "cppic_string_compare(&s, &other)",
        "cppic_string_compare(&g, &s)",
        "cppic_string_length(&s)",
        "cppic_string_char_at(&s, 0)",
    };
    for (const char* e : str_expects) {
        if (c3.find(e) == std::string::npos) {
            std::fprintf(stderr, "transpile_test(String): missing: %s\n", e);
            return 1;
        }
    }

    // String returned by value must be a clean transpiler error.
    {
        const char* bad = "String f() { return \"x\"; }\n";
        cppic::Lexer lx(bad);
        auto tk = lx.tokenize();
        cppic::Parser p(std::move(tk));
        cppic::TranslationUnit tu = p.parseTranslationUnit();
        cppic::Transpiler t;
        bool threw = false;
        try {
            t.run(tu);
        } catch (const cppic::TranspileError&) {
            threw = true;
        }
        if (!threw) {
            std::fprintf(stderr, "transpile_test(String): return-by-value not rejected\n");
            return 1;
        }
    }

    // Writing through String::operator[] must be rejected (read-only).
    {
        const char* bad = "void setup() { String s = \"x\"; s[0] = 'y'; }\n";
        cppic::Lexer lx(bad);
        auto tk = lx.tokenize();
        cppic::Parser p(std::move(tk));
        cppic::TranslationUnit tu = p.parseTranslationUnit();
        cppic::Transpiler t;
        bool threw = false;
        try {
            t.run(tu);
        } catch (const cppic::TranspileError&) {
            threw = true;
        }
        if (!threw) {
            std::fprintf(stderr, "transpile_test(String): writable index not rejected\n");
            return 1;
        }
    }

    std::fprintf(stderr, "transpile_test: String lowering OK\n");
    return 0;
}
