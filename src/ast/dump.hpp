#pragma once

#include "ast/ast.hpp"

#include <string>

namespace cppic {

std::string dumpType(const Type& t);
std::string dumpExpr(const Expr& e);
std::string dumpStmt(const Stmt& s, int indent = 0);
std::string dumpDecl(const Decl& d);

}  // namespace cppic