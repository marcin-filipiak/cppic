#include "lexer/lexer.hpp"

#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>

namespace cppic {
namespace {

const std::unordered_map<std::string, TT>& keywordMap() {
    static const std::unordered_map<std::string, TT> map = {
        {"alignas", TT::KwAlignas},   {"alignof", TT::KwAlignof},
        {"asm", TT::KwAsm},           {"auto", TT::KwAuto},
        {"bool", TT::KwBool},         {"break", TT::KwBreak},
        {"case", TT::KwCase},         {"catch", TT::KwCatch},
        {"char", TT::KwChar},         {"class", TT::KwClass},
        {"const", TT::KwConst},       {"constexpr", TT::KwConstexpr},
        {"const_cast", TT::KwConstCast}, {"continue", TT::KwContinue},
        {"default", TT::KwDefault},   {"delete", TT::KwDelete},
        {"do", TT::KwDo},             {"double", TT::KwDouble},
        {"dynamic_cast", TT::KwDynamicCast}, {"else", TT::KwElse},
        {"enum", TT::KwEnum},         {"explicit", TT::KwExplicit},
        {"export", TT::KwExport},     {"extern", TT::KwExtern},
        {"false", TT::KwFalse},       {"float", TT::KwFloat},
        {"for", TT::KwFor},           {"friend", TT::KwFriend},
        {"goto", TT::KwGoto},         {"if", TT::KwIf},
        {"inline", TT::KwInline},     {"int", TT::KwInt},
        {"long", TT::KwLong},         {"mutable", TT::KwMutable},
        {"namespace", TT::KwNamespace}, {"new", TT::KwNew},
        {"noexcept", TT::KwNoexcept}, {"nullptr", TT::KwNullptr},
        {"operator", TT::KwOperator}, {"private", TT::KwPrivate},
        {"protected", TT::KwProtected}, {"public", TT::KwPublic},
        {"register", TT::KwRegister}, {"reinterpret_cast", TT::KwReinterpretCast},
        {"return", TT::KwReturn},     {"short", TT::KwShort},
        {"signed", TT::KwSigned},     {"sizeof", TT::KwSizeof},
        {"static", TT::KwStatic},     {"static_assert", TT::KwStaticAssert},
        {"static_cast", TT::KwStaticCast}, {"struct", TT::KwStruct},
        {"switch", TT::KwSwitch},     {"template", TT::KwTemplate},
        {"this", TT::KwThis},         {"throw", TT::KwThrow},
        {"true", TT::KwTrue},         {"try", TT::KwTry},
        {"typedef", TT::KwTypedef},   {"typeid", TT::KwTypeid},
        {"typename", TT::KwTypename}, {"union", TT::KwUnion},
        {"unsigned", TT::KwUnsigned}, {"using", TT::KwUsing},
        {"virtual", TT::KwVirtual},   {"void", TT::KwVoid},
        {"volatile", TT::KwVolatile}, {"while", TT::KwWhile},
    };
    return map;
}

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool isDigit(char c) {
    return std::isdigit(static_cast<unsigned char>(c));
}

bool isHexDigit(char c) {
    return std::isxdigit(static_cast<unsigned char>(c));
}

}  // namespace

Lexer::Lexer(std::string source) : src_(std::move(source)) {}

char Lexer::peek(std::size_t ahead) const {
    if (pos_ + ahead >= src_.size()) return '\0';
    return src_[pos_ + ahead];
}

char Lexer::advance() {
    char c = peek();
    if (c == '\0') return c;
    ++pos_;
    if (c == '\n') {
        ++line_;
        col_ = 1;
    } else {
        ++col_;
    }
    return c;
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        char c = peek();
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t' || c == '\f' || c == '\v') {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            while (peek() != '\n' && peek() != '\0') advance();
        } else if (c == '/' && peek(1) == '*') {
            advance();
            advance();
            bool closed = false;
            while (peek() != '\0') {
                if (peek() == '*' && peek(1) == '/') {
                    advance();
                    advance();
                    closed = true;
                    break;
                }
                advance();
            }
            if (!closed) error("unterminated block comment");
        } else {
            return;
        }
    }
}

bool Lexer::matchAndSkip(char c) {
    if (peek() != c) return false;
    advance();
    return true;
}

Token Lexer::makeToken(TT kind) {
    Token t;
    t.kind = kind;
    t.loc = {line_, col_, pos_};
    return t;
}

void Lexer::add(TT kind) {
    out_.push_back(makeToken(kind));
}

Token Lexer::lexIdentifier() {
    Token t = makeToken(TT::Identifier);
    std::size_t start = pos_;
    advance();  // first char already valid
    while (isIdentChar(peek())) advance();
    t.text = src_.substr(start, pos_ - start);

    auto it = keywordMap().find(t.text);
    if (it != keywordMap().end()) {
        t.kind = it->second;
        return t;
    }
    t.value = t.text;
    return t;
}

Token Lexer::lexNumber() {
    Token t = makeToken(TT::IntLit);
    std::size_t start = pos_;

    bool isFloat = false;
    bool isHex = false;
    bool isBinary = false;

    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
        isHex = true;
        advance();
        advance();
        while (isHexDigit(peek()) || peek() == '\'') advance();
    } else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
        isBinary = true;
        advance();
        advance();
        while (peek() == '0' || peek() == '1' || peek() == '\'') advance();
    } else if (peek() == '0' && isDigit(peek(1))) {
        // octal
        while (isDigit(peek()) || peek() == '\'') advance();
    } else {
        while (isDigit(peek()) || peek() == '\'') advance();
    }

    if (!isHex && !isBinary && peek() == '.') {
        isFloat = true;
        advance();
        while (isDigit(peek()) || peek() == '\'') advance();
    }

    if (peek() == 'e' || peek() == 'E') {
        char next = peek(1);
        if (isDigit(next) || ((next == '+' || next == '-') && isDigit(peek(2)))) {
            isFloat = true;
            advance();
            if (peek() == '+' || peek() == '-') advance();
            while (isDigit(peek()) || peek() == '\'') advance();
        }
    }

    if (peek() == '.' && !isHex && !isBinary && peek(1) != '.') {
        // trailing dot: 3.  (cannot be followed by another dot = ellipsis)
        isFloat = true;
        advance();
        while (isDigit(peek())) advance();
    }

    // suffix: f/F/l/L/u/U (combinations, but keep simple)
    std::string suffix;
    while (peek() == 'f' || peek() == 'F' || peek() == 'l' || peek() == 'L' ||
           peek() == 'u' || peek() == 'U') {
        suffix += peek();
        advance();
    }
    if (isFloat) t.kind = TT::FloatLit;

    t.text = src_.substr(start, pos_ - start);
    (void)suffix;  // suffix stored in text; numeric value parsing done later
    t.value = t.text;
    return t;
}

Token Lexer::lexString(char quote) {
    Token t = makeToken(quote == '"' ? TT::StringLit : TT::CharLit);
    std::size_t start = pos_;
    advance();  // consume quote
    std::string decoded;
    while (peek() != quote) {
        if (peek() == '\0' || peek() == '\n') error("unterminated string literal");
        char c = advance();
        if (c == '\\' && quote == '"') {
            char e = advance();
            switch (e) {
                case 'n': decoded += '\n'; break;
                case 't': decoded += '\t'; break;
                case 'r': decoded += '\r'; break;
                case 'a': decoded += '\a'; break;
                case 'b': decoded += '\b'; break;
                case 'f': decoded += '\f'; break;
                case 'v': decoded += '\v'; break;
                case '0': decoded += '\0'; break;
                case '\\': decoded += '\\'; break;
                case '\'': decoded += '\''; break;
                case '"': decoded += '"'; break;
                case '\0': error("unterminated string literal"); break;
                default: decoded += e; break;
            }
        } else {
            decoded += c;
        }
    }
    advance();  // closing quote
    t.text = src_.substr(start, pos_ - start);
    t.value = std::move(decoded);
    return t;
}

void Lexer::error(const std::string& msg) {
    throw LexError{msg, {line_, col_, pos_}};
}

std::vector<Token> Lexer::tokenize() {
    out_.clear();
    out_.reserve(src_.size() / 2);

    for (;;) {
        skipWhitespaceAndComments();
        char c = peek();
        if (c == '\0') {
            add(TT::End);
            return out_;
        }

        SourceLoc loc{line_, col_, pos_};

        if (isIdentStart(c)) {
            out_.push_back(lexIdentifier());
            continue;
        }
        if (isDigit(c) || (c == '.' && isDigit(peek(1)))) {
            out_.push_back(lexNumber());
            continue;
        }
        if (c == '"' || c == '\'') {
            out_.push_back(lexString(c));
            continue;
        }

        advance();
        Token t = makeToken(TT::End);
        switch (c) {
            case '[': t.kind = TT::Lbrack; break;
            case ']': t.kind = TT::Rbrack; break;
            case '(': t.kind = TT::Lparen; break;
            case ')': t.kind = TT::Rparen; break;
            case '{': t.kind = TT::Lbrace; break;
            case '}': t.kind = TT::Rbrace; break;
            case ';': t.kind = TT::Semicolon; break;
            case ':':
                t.kind = matchAndSkip(':') ? TT::ColonColon : TT::Colon;
                break;
            case ',': t.kind = TT::Comma; break;
            case '?': t.kind = TT::Question; break;
            case '~': t.kind = TT::Tilde; break;
            case '+':
                if (matchAndSkip('+')) t.kind = TT::PlusPlus;
                else if (matchAndSkip('=')) t.kind = TT::PlusEq;
                else t.kind = TT::Plus;
                break;
            case '-':
                if (matchAndSkip('>')) {
                    t.kind = matchAndSkip('*') ? TT::ArrowStar : TT::Arrow;
                } else if (matchAndSkip('-')) {
                    t.kind = TT::MinusMinus;
                } else if (matchAndSkip('=')) {
                    t.kind = TT::MinusEq;
                } else {
                    t.kind = TT::Minus;
                }
                break;
            case '*':
                t.kind = matchAndSkip('=') ? TT::StarEq : TT::Star;
                break;
            case '/':
                t.kind = matchAndSkip('=') ? TT::SlashEq : TT::Slash;
                break;
            case '%': t.kind = TT::Percent; break;
            case '&':
                if (matchAndSkip('&')) t.kind = TT::AmpAmp;
                else if (matchAndSkip('=')) t.kind = TT::AmpEq;
                else t.kind = TT::Amp;
                break;
            case '|':
                if (matchAndSkip('|')) t.kind = TT::PipePipe;
                else if (matchAndSkip('=')) t.kind = TT::PipeEq;
                else t.kind = TT::Pipe;
                break;
            case '^': t.kind = matchAndSkip('=') ? TT::CaretEq : TT::Caret; break;
            case '<':
                if (matchAndSkip('<')) {
                    t.kind = matchAndSkip('=') ? TT::ShiftLeftEq : TT::ShiftLeft;
                } else if (matchAndSkip('=')) {
                    t.kind = TT::LessEq;
                } else {
                    t.kind = TT::Less;
                }
                break;
            case '>':
                if (matchAndSkip('>')) {
                    t.kind = matchAndSkip('=') ? TT::ShiftRightEq : TT::ShiftRight;
                } else if (matchAndSkip('=')) {
                    t.kind = TT::GreaterEq;
                } else {
                    t.kind = TT::Greater;
                }
                break;
            case '=':
                t.kind = matchAndSkip('=') ? TT::EqEq : TT::Assign;
                break;
            case '!':
                t.kind = matchAndSkip('=') ? TT::NotEq : TT::Bang;
                break;
            case '.':
                if (matchAndSkip('.')) {
                    if (matchAndSkip('.')) {
                        t.kind = TT::Ellipsis;
                    } else {
                        error("expected '...'");
                    }
                } else if (matchAndSkip('*')) {
                    t.kind = TT::DotStar;
                } else {
                    t.kind = TT::Dot;
                }
                break;
            default: {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "unexpected character '%c' (0x%02x)", c,
                              static_cast<unsigned char>(c));
                error(buf);
            }
        }
        t.text = src_.substr(loc.offset, pos_ - loc.offset);
        out_.push_back(t);
    }
}

}  // namespace cppic