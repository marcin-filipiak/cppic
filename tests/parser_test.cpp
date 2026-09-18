#include "ast/ast.hpp"
#include "ast/dump.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

using cppic::Decl;
using cppic::Expr;
using cppic::FunctionDecl;
using cppic::Lexer;
using cppic::Parser;
using cppic::ParseError;
using cppic::Stmt;
using cppic::TranslationUnit;
using cppic::TT;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

TranslationUnit parse(const std::string& src) {
    Lexer lexer(src);
    Parser parser(lexer.tokenize());
    try {
        return parser.parseTranslationUnit();
    } catch (const ParseError& e) {
        std::fprintf(stderr, "  UNEXPECTED parse error: %d:%d %s (source below)\n  %s\n",
                     e.loc.line, e.loc.col, e.message.c_str(), src.c_str());
        throw;
    }
}

bool parses(const std::string& src) {
    try {
        parse(src);
        return true;
    } catch (const ParseError& e) {
        std::fprintf(stderr, "  unexpected parse error: %d:%d %s\n", e.loc.line,
                     e.loc.col, e.message.c_str());
        return false;
    }
}

// Simple checks ---------------------------------------------------------------
void testSimpleFunctions() {
    auto tu = parse(R"(
        int add(int a, int b) { return a + b; }
        void nothing() {}
    )");
    CHECK(tu.decls.size() == 2);
    CHECK(tu.decls[0]->kind == Decl::Kind::Function);
    CHECK(tu.decls[0]->func->name == "add");
    CHECK(tu.decls[0]->func->params.size() == 2);
    CHECK(tu.decls[0]->func->params[0].name == "a");
    CHECK(tu.decls[1]->func->name == "nothing");
    CHECK(tu.decls[1]->func->body);
    CHECK(tu.decls[1]->func->body->items.empty());
}

void testOperatorsAndStatements() {
    auto tu = parse(R"(
        int f(int x) {
            int y = 0;
            if (x > 5) y = x * 2;
            else y = x / 2;
            for (int i = 0; i < 10; i++) y += i & 1;
            while (y < 100 && x != 0) { y <<= 2; }
            do { --y; } while (y > 0);
            switch (x) {
                case 1: y = 1; break;
                case 2: break;
                default: y = 0;
            }
            return y ? x : -x;
        }
    )");
    CHECK(tu.decls.size() == 1);
    auto& body = tu.decls[0]->func->body->items;
    CHECK(body.size() == 7);  // decl, if, for, while, do, switch, return
    CHECK(body[0]->kind == Stmt::Kind::Declaration);
    CHECK(body[1]->kind == Stmt::Kind::If);
    CHECK(body[1]->elseBody);
    CHECK(body[2]->kind == Stmt::Kind::For);
    CHECK(body[2]->cond);
    CHECK(body[2]->thenExpr);
    CHECK(body[2]->initStmt);
    CHECK(body[3]->kind == Stmt::Kind::While);
    CHECK(body[4]->kind == Stmt::Kind::DoWhile);
    CHECK(body[5]->kind == Stmt::Kind::Switch);
    CHECK(body[6]->kind == Stmt::Kind::Return);
    // return expr is conditional
    CHECK(body[6]->expr->kind == Expr::Kind::Conditional);
}

void testClasses() {
    auto tu = parse(R"(
        class Led {
        public:
            Led(int p) : pin(p) {}
            void on() { state = true; }
            bool off() const { return state; }
            Led& operator+=(int n) { pin += n; return *this; }
            virtual void tick() = 0;
        private:
            int pin;
            bool state = false;
        protected:
            static int count;
        };
    )");
    CHECK(tu.decls.size() == 1);
    CHECK(tu.decls[0]->kind == Decl::Kind::Class);
    auto& m = tu.decls[0]->klass->members;
    CHECK(m.size() == 8);
    CHECK(m[0].kind == cppic::ClassMember::Kind::Method);
    CHECK(m[0].method->isConstructor);
    CHECK(m[0].method->name == "Led");
    CHECK(m[0].method->params.size() == 1);
    CHECK(m[1].method->name == "on");
    CHECK(m[2].method->isConstMethod);
    CHECK(m[3].method->isOperator);
    CHECK(m[3].method->name.rfind("operator", 0) == 0);
    CHECK(m[4].method->isVirtual);
    CHECK(m[5].kind == cppic::ClassMember::Kind::Field);
    CHECK(m[5].isPrivate);
    CHECK(m[5].field->name == "pin");
    CHECK(m[6].field->init);
}

void testEnumsTypedefs() {
    auto tu = parse(R"(
        enum Color { RED, GREEN = 5, BLUE };
        typedef unsigned long uint32_t;
        using MyInt = int;
        struct Point { int x; int y; };
    )");
    CHECK(tu.decls.size() == 4);
    CHECK(tu.decls[0]->kind == Decl::Kind::Enum);
    CHECK(tu.decls[0]->enumm->enumerators.size() == 3);
    CHECK(tu.decls[0]->enumm->enumerators[1].value->intValue == 5);
    CHECK(tu.decls[1]->kind == Decl::Kind::Typedef);
    CHECK(tu.decls[1]->typedefName == "uint32_t");
    CHECK(tu.decls[2]->kind == Decl::Kind::Typedef);
    CHECK(tu.decls[2]->typedefName == "MyInt");
    CHECK(tu.decls[3]->kind == Decl::Kind::Class);
    CHECK(tu.decls[3]->klass->isStruct);
    CHECK(tu.decls[3]->klass->members.size() == 2);
}

void testOutOfLineMethod() {
    auto tu = parse(R"(
        class Counter {
        public:
            int get() const;
        };
        int Counter::get() const { return n; }
    )");
    CHECK(tu.decls.size() == 2);
    CHECK(tu.decls[1]->kind == Decl::Kind::Function);
    CHECK(tu.decls[1]->func->name == "Counter::get");
    CHECK(tu.decls[1]->func->isConstMethod);
}

void testNewDelete() {
    auto tu = parse(R"(
        void f() {
            char* buf = new char[64];
            delete[] buf;
            Foo* foo = new Foo(1, 2);
            delete foo;
        }
    )");
    CHECK(tu.decls.size() == 1);
    CHECK(tu.decls[0]->func->body->items.size() == 4);
    CHECK(tu.decls[0]->func->body->items[0]->decl->init->kind == Expr::Kind::NewExpr);
    CHECK(tu.decls[0]->func->body->items[0]->decl->init->isArrayNew);
    CHECK(tu.decls[0]->func->body->items[1]->expr->kind == Expr::Kind::DeleteExpr);
    CHECK(tu.decls[0]->func->body->items[1]->expr->isArrayDelete);
    CHECK(tu.decls[0]->func->body->items[2]->decl->init->kind == Expr::Kind::NewExpr);
    CHECK(tu.decls[0]->func->body->items[2]->decl->init->args.size() == 2);
    CHECK(tu.decls[0]->func->body->items[3]->expr->kind == Expr::Kind::DeleteExpr);
}

void testNumbers() {
    auto tu = parse("int a = 0xFF; int b = 0b1010; int c = 010; unsigned long d = 4000000000UL;");
    CHECK(tu.decls.size() == 4);
    CHECK(tu.decls[0]->var->init->intValue == 255);
    CHECK(tu.decls[1]->var->init->intValue == 10);
    CHECK(tu.decls[2]->var->init->intValue == 8);
    CHECK(tu.decls[3]->var->init->intValue == 4000000000ULL);
}

void testInheritanceAndTemplatesSkip() {
    CHECK(parses("class Base { public: void f() {} }; class Derived : public Base {};"));
    CHECK(parses("template <class T> T max(T a, T b) { return a > b ? a : b; }"));
}

}  // namespace

int main() {
    testSimpleFunctions();
    testOperatorsAndStatements();
    testClasses();
    testEnumsTypedefs();
    testOutOfLineMethod();
    testNewDelete();
    testNumbers();
    testInheritanceAndTemplatesSkip();

    if (failures == 0) {
        std::puts("parser: all tests passed");
        return 0;
    }
    std::fprintf(stderr, "parser: %d failures\n", failures);
    return 1;
}