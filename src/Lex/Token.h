#pragma once

#include "Basic/SourceManager.h"
#include "Basic/Types.h"

#include <string_view>

namespace rtsl {

#define RTSL_KEYWORD_TOKENS(X) \
    X(Import, "import") \
    X(Export, "export") \
    X(Namespace, "namespace") \
    X(Struct, "struct") \
    X(Using, "using") \
    X(Uniform, "uniform") \
    X(Varying, "varying") \
    X(Entry, "entry") \
    X(Function, "fn") \
    X(Const, "const") \
    X(Auto, "auto") \
    X(Void, "void") \
    X(If, "if") \
    X(Else, "else") \
    X(While, "while") \
    X(Do, "do") \
    X(For, "for") \
    X(Return, "return") \
    X(Clip, "clip") \
    X(Smooth, "smooth") \
    X(Flat, "flat") \
    X(ReadOnly, "readonly") \
    X(WriteOnly, "writeonly") \
    X(True, "true") \
    X(False, "false") \
    X(InOut, "inout")

enum class TokenKind : u16 {
    invalid,
    end_of_file,
    identifier,
    integer_literal,
    float_literal,
    string_literal,

#define RTSL_KEYWORD_ENUM(name, spelling) kw_##name,
    RTSL_KEYWORD_TOKENS(RTSL_KEYWORD_ENUM)
#undef RTSL_KEYWORD_ENUM

    plus,
    minus,
    star,
    slash,
    percent,
    equal,
    equal_equal,
    bang_equal,
    less,
    less_equal,
    greater,
    greater_equal,
    bang,
    amp_amp,
    pipe_pipe,
    amp,
    pipe,
    caret,
    tilde,
    arrow,
    colon_colon,
    left_paren,
    right_paren,
    left_brace,
    right_brace,
    left_bracket,
    right_bracket,
    comma,
    semicolon,
    dot,
};

struct Token {
    TokenKind kind = TokenKind::invalid;
    std::string_view text{};
    SourceSpan span{};
};

[[nodiscard]] std::string_view token_spelling(TokenKind kind);
[[nodiscard]] TokenKind keyword_kind(std::string_view text);

} // namespace rtsl
