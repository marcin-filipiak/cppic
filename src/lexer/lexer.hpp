#pragma once

#include "lexer/token.hpp"

#include <string>
#include <vector>

namespace cppic {

struct LexError {
    std::string message;
    SourceLoc loc;
};

// Tokenizes a single C++ source file.
// Throws LexError on an unrecognized character sequence.
class Lexer {
public:
    explicit Lexer(std::string source);

    std::vector<Token> tokenize();

private:
    char peek(std::size_t ahead = 0) const;
    char advance();
    void skipWhitespaceAndComments();
    bool matchAndSkip(char c);

    Token makeToken(TT kind);
    void add(TT kind);

    Token lexIdentifier();
    Token lexNumber();
    Token lexString(char quote);

    void error(const std::string& msg);

    std::string src_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    std::vector<Token> out_;
};

}  // namespace cppic