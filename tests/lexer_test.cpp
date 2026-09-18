#include "lexer/lexer.hpp"
#include "lexer/token.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

std::vector<cppic::TT> kinds(const std::vector<cppic::Token>& toks) {
    std::vector<cppic::TT> k;
    for (const auto& t : toks) k.push_back(t.kind);
    return k;
}

void run(const std::string& name, const std::string& src, std::vector<cppic::TT> want) {
    try {
        cppic::Lexer lexer(src);
        auto toks = lexer.tokenize();
        CHECK(kinds(toks) == want);
        if (kinds(toks) != want) {
            std::fprintf(stderr, "  case '%s' failed:\n", name.c_str());
            for (const auto& t : toks)
                std::fprintf(stderr, "    got %s\n", cppic::tokenName(t.kind));
        }
    } catch (const cppic::LexError& e) {
        std::fprintf(stderr, "  case '%s' threw: %s\n", name.c_str(), e.message.c_str());
        ++failures;
    }
}

}  // namespace

int main() {
    using TT = cppic::TT;

    run("empty", "", {TT::End});
    run("hello", "int main() { return 0; }",
        {TT::KwInt, TT::Identifier, TT::Lparen, TT::Rparen, TT::Lbrace,
         TT::KwReturn, TT::IntLit, TT::Semicolon, TT::Rbrace, TT::End});
    run("keywords", "class struct public private bool void const static virtual",
        {TT::KwClass, TT::KwStruct, TT::KwPublic, TT::KwPrivate, TT::KwBool,
         TT::KwVoid, TT::KwConst, TT::KwStatic, TT::KwVirtual, TT::End});
    run("operators", "a == b; c != d; e && f; g || h; i <= j; k >= l; x++; --y; z <<= 2;",
        {TT::Identifier, TT::EqEq, TT::Identifier, TT::Semicolon,
         TT::Identifier, TT::NotEq, TT::Identifier, TT::Semicolon,
         TT::Identifier, TT::AmpAmp, TT::Identifier, TT::Semicolon,
         TT::Identifier, TT::PipePipe, TT::Identifier, TT::Semicolon,
         TT::Identifier, TT::LessEq, TT::Identifier, TT::Semicolon,
         TT::Identifier, TT::GreaterEq, TT::Identifier, TT::Semicolon,
         TT::Identifier, TT::PlusPlus, TT::Semicolon,
         TT::MinusMinus, TT::Identifier, TT::Semicolon,
         TT::Identifier, TT::ShiftLeftEq, TT::IntLit, TT::Semicolon,
         TT::End});
    run("scopes", "ns::detail::foo<int> x; a->b.c; a::*p;",
        {TT::Identifier, TT::ColonColon, TT::Identifier, TT::ColonColon,
         TT::Identifier, TT::Less, TT::KwInt, TT::Greater, TT::Identifier,
         TT::Semicolon,
         TT::Identifier, TT::Arrow, TT::Identifier, TT::Dot, TT::Identifier,
         TT::Semicolon,
         TT::Identifier, TT::ColonColon, TT::Star, TT::Identifier, TT::Semicolon,
         TT::End});
    run("numbers", "0 42 0xff 0b1010 3.14 1.5e-3 100u 0x1FUL 0 077",
        {TT::IntLit, TT::IntLit, TT::IntLit, TT::IntLit, TT::FloatLit,
         TT::FloatLit, TT::IntLit, TT::IntLit, TT::IntLit, TT::IntLit, TT::End});
    run("strings", "const char* s = \"he\\nllo\"; char c = 'x';",
        {TT::KwConst, TT::KwChar, TT::Star, TT::Identifier, TT::Assign,
         TT::StringLit, TT::Semicolon, TT::KwChar, TT::Identifier, TT::Assign,
         TT::CharLit, TT::Semicolon, TT::End});
    run("comments", "// line\nint a; /* block\nmulti */ int b;",
        {TT::KwInt, TT::Identifier, TT::Semicolon, TT::KwInt, TT::Identifier,
         TT::Semicolon, TT::End});
    run("ellipsis", "int f(...);",
        {TT::KwInt, TT::Identifier, TT::Lparen, TT::Ellipsis, TT::Rparen,
         TT::Semicolon, TT::End});
    run("new delete this", "Foo* p = new Foo(1); delete p; this->x = 1;",
        {TT::Identifier, TT::Star, TT::Identifier, TT::Assign, TT::KwNew,
         TT::Identifier, TT::Lparen, TT::IntLit, TT::Rparen, TT::Semicolon,
         TT::KwDelete, TT::Identifier, TT::Semicolon, TT::KwThis, TT::Arrow,
         TT::Identifier, TT::Assign, TT::IntLit, TT::Semicolon, TT::End});

    // values of string literal should be decoded
    {
        cppic::Lexer lexer("\"a\\nb\"");
        auto toks = lexer.tokenize();
        CHECK(toks[0].value == "a\nb");
        if (toks[0].value != "a\nb") std::fprintf(stderr, "  escaped string value wrong\n");
        CHECK(toks[0].loc.line == 1);
    }

    // lex error
    {
        cppic::Lexer lexer("int x = @;");
        bool threw = false;
        try {
            lexer.tokenize();
        } catch (const cppic::LexError&) {
            threw = true;
        }
        CHECK(threw);
    }
    {
        cppic::Lexer lexer("/* never closed");
        bool threw = false;
        try {
            lexer.tokenize();
        } catch (const cppic::LexError&) {
            threw = true;
        }
        CHECK(threw);
    }

    if (failures == 0) {
        std::puts("lexer: all tests passed");
        return 0;
    }
    std::fprintf(stderr, "lexer: %d failures\n", failures);
    return 1;
}