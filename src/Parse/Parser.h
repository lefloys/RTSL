#pragma once

#include "AST/AST.h"
#include "Basic/Diagnostics.h"
#include "Lex/Token.h"

#include <span>

namespace rtsl {

class Parser {
public:
    Parser(SourceManager &sources, DiagnosticEngine &diagnostics, u32 file_id, std::span<const Token> tokens);

    [[nodiscard]] TranslationUnit parse_translation_unit();

private:
    [[nodiscard]] const Token &peek(std::size_t lookahead = 0) const;
    [[nodiscard]] bool at(TokenKind kind) const;
    [[nodiscard]] bool consume(TokenKind kind);
    [[nodiscard]] bool at_end() const;

    Decl parse_declaration();
    StructDecl parse_struct_decl(bool exported);
    Decl parse_import(bool exported);
    Decl parse_named_declaration(DeclKind kind, bool exported, bool entry);
    void parse_function_signature(Decl &decl);
    void parse_function_body(Decl &decl);
    void parse_uniform_scope(const Decl &decl);
    StructField parse_field_declaration();
    std::string collect_type_until(TokenKind stop_a, TokenKind stop_b);
    void skip_to_declaration_boundary();
    void skip_balanced_block();
    std::string append_token_text(std::string statement, const Token &token) const;
    std::string collect_type_tokens_until_identifier();
    void diagnose(const Token &token, std::string message);

    TranslationUnit *unit_ = nullptr;

    SourceManager &sources_;
    DiagnosticEngine &diagnostics_;
    u32 file_id_ = 0;
    std::span<const Token> tokens_;
    std::size_t cursor_ = 0;
};

} // namespace rtsl
