#include "Parse/Parser.h"

namespace rtsl {

Parser::Parser(SourceManager &sources, DiagnosticEngine &diagnostics, u32 file_id, std::span<const Token> tokens)
    : sources_(sources), diagnostics_(diagnostics), file_id_(file_id), tokens_(tokens) {}

TranslationUnit Parser::parse_translation_unit() {
    TranslationUnit unit{.file_id = file_id_};
    unit_ = &unit;
    while (!at_end()) {
        auto decl = parse_declaration();
        if (decl.kind != DeclKind::unknown) {
            unit.declarations.push_back(std::move(decl));
        }
    }
    unit_ = nullptr;
    return unit;
}

const Token &Parser::peek(std::size_t lookahead) const {
    const auto index = cursor_ + lookahead;
    return tokens_[index < tokens_.size() ? index : tokens_.size() - 1];
}

bool Parser::at(TokenKind kind) const {
    return peek().kind == kind;
}

bool Parser::consume(TokenKind kind) {
    if (!at(kind)) {
        return false;
    }
    ++cursor_;
    return true;
}

bool Parser::at_end() const {
    return at(TokenKind::end_of_file);
}

Decl Parser::parse_declaration() {
    bool exported = consume(TokenKind::kw_Export);
    bool entry = consume(TokenKind::kw_Entry);

    if (at(TokenKind::kw_Import)) {
        return parse_import(exported);
    }
    if (at(TokenKind::kw_Function)) {
        return parse_named_declaration(DeclKind::function, exported, entry);
    }
    if (at(TokenKind::kw_Struct)) {
        auto decl = parse_named_declaration(DeclKind::struct_decl, exported, entry);
        return decl;
    }
    if (at(TokenKind::kw_Uniform)) {
        return parse_named_declaration(DeclKind::uniform, exported, entry);
    }
    if (at(TokenKind::kw_Varying)) {
        return parse_named_declaration(DeclKind::varying, exported, entry);
    }
    if (at(TokenKind::kw_Namespace)) {
        return parse_named_declaration(DeclKind::namespace_decl, exported, entry);
    }

    if (entry) {
        diagnose(peek(), "expected function declaration after 'entry'");
    } else {
        diagnose(peek(), "unsupported top-level declaration");
    }
    skip_to_declaration_boundary();
    return {};
}

Decl Parser::parse_import(bool exported) {
    const Token start = peek();
    const bool consumed_import = consume(TokenKind::kw_Import);
    (void)consumed_import;
    std::string name;

    if (consume(TokenKind::less)) {
        while (!at_end() && !at(TokenKind::greater)) {
            name += std::string(peek().text);
            ++cursor_;
        }
        if (!consume(TokenKind::greater)) {
            diagnose(peek(), "unterminated import path");
        }
    } else {
        diagnose(peek(), "expected '<' after import");
    }

    if (!consume(TokenKind::semicolon)) {
        diagnose(peek(), "expected ';' after import");
        skip_to_declaration_boundary();
    }

    return Decl{.kind = DeclKind::import, .name = std::move(name), .span = start.span, .exported = exported};
}

Decl Parser::parse_named_declaration(DeclKind kind, bool exported, bool entry) {
    const Token start = peek();
    ++cursor_;

    std::string name;
    if (at(TokenKind::identifier)) {
        name = std::string(peek().text);
        ++cursor_;
        while (consume(TokenKind::colon_colon)) {
            if (consume(TokenKind::tilde)) {
                name += "::~";
                if (!at(TokenKind::identifier)) {
                    diagnose(peek(), "expected destructor name after '~'");
                    break;
                }
                name += std::string(peek().text);
                ++cursor_;
                break;
            }
            if (!at(TokenKind::identifier)) {
                diagnose(peek(), "expected identifier after '::'");
                break;
            }
            name += "::";
            name += std::string(peek().text);
            ++cursor_;
        }
    } else if (kind != DeclKind::uniform) {
        diagnose(peek(), "expected declaration name");
    }

    Decl decl{.kind = kind, .name = std::move(name), .span = start.span, .exported = exported, .entry = entry};
    if (kind == DeclKind::function) {
        parse_function_signature(decl);
        parse_function_body(decl);
    } else if (kind == DeclKind::struct_decl) {
        StructDecl struct_decl{.name = decl.name};
        if (consume(TokenKind::left_brace)) {
            while (!at_end() && !at(TokenKind::right_brace)) {
                auto field = parse_field_declaration();
                if (!field.type.empty() && !field.name.empty()) {
                    struct_decl.fields.push_back(std::move(field));
                    continue;
                }
                skip_to_declaration_boundary();
            }
            const bool consumed_right_brace = consume(TokenKind::right_brace);
            (void)consumed_right_brace;
        }
        if (unit_) {
            unit_->structs.push_back(std::move(struct_decl));
        }
    } else if (kind == DeclKind::uniform) {
        parse_uniform_scope(decl);
    } else {
        skip_balanced_block();
    }
    if (at(TokenKind::semicolon)) {
        ++cursor_;
    }

    return decl;
}

void Parser::parse_uniform_scope(const Decl &decl) {
    if (!consume(TokenKind::left_brace)) {
        return;
    }

    while (!at_end() && !at(TokenKind::right_brace)) {
        std::string access;
        if (consume(TokenKind::kw_ReadOnly)) {
            access = "readonly";
        } else if (consume(TokenKind::kw_WriteOnly)) {
            access = "writeonly";
        }

        if (consume(TokenKind::kw_Struct) && consume(TokenKind::left_brace)) {
            UniformBinding binding{.scope_name = decl.name, .access = std::move(access)};
            while (!at_end() && !at(TokenKind::right_brace)) {
                auto field = parse_field_declaration();
                if (!field.type.empty() && !field.name.empty()) {
                    binding.inline_fields.push_back(std::move(field));
                    continue;
                }
                skip_to_declaration_boundary();
            }
            const bool consumed_struct_end = consume(TokenKind::right_brace);
            (void)consumed_struct_end;
            if (at(TokenKind::identifier)) {
                binding.name = std::string(peek().text);
                ++cursor_;
            }
            binding.type = "struct";
            if (consume(TokenKind::semicolon) && unit_ && !binding.name.empty()) {
                unit_->uniforms.push_back(std::move(binding));
            }
            continue;
        }

        auto field = parse_field_declaration();
        if (!field.type.empty() && !field.name.empty() && unit_) {
            unit_->uniforms.push_back(UniformBinding{
                .scope_name = decl.name,
                .name = std::move(field.name),
                .type = std::move(field.type),
                .access = std::move(access),
            });
            continue;
        }
        skip_to_declaration_boundary();
    }

    const bool consumed_uniform_end = consume(TokenKind::right_brace);
    (void)consumed_uniform_end;
}

StructField Parser::parse_field_declaration() {
    auto field_type = collect_type_tokens_until_identifier();
    if (field_type.empty() || !at(TokenKind::identifier)) {
        return {};
    }
    auto field_name = std::string(peek().text);
    ++cursor_;
    if (!consume(TokenKind::semicolon)) {
        return {};
    }
    return StructField{.type = std::move(field_type), .name = std::move(field_name)};
}

std::string Parser::collect_type_tokens_until_identifier() {
    std::string text;
    int angle_depth = 0;
    while (!at_end()) {
        if (angle_depth == 0 && peek().kind == TokenKind::identifier && !text.empty()) {
            break;
        }
        if (peek().kind == TokenKind::less) {
            ++angle_depth;
        } else if (peek().kind == TokenKind::greater && angle_depth > 0) {
            --angle_depth;
        }
        text += std::string(peek().text);
        ++cursor_;
    }
    return text;
}

void Parser::parse_function_body(Decl &decl) {
    if (!consume(TokenKind::left_brace)) {
        return;
    }

    int brace_depth = 1;
    int paren_depth = 0;
    int bracket_depth = 0;
    std::string statement;

    while (!at_end() && brace_depth > 0) {
        const Token token = peek();
        ++cursor_;

        if (token.kind == TokenKind::left_brace) {
            ++brace_depth;
            statement = append_token_text(std::move(statement), token);
            continue;
        }
        if (token.kind == TokenKind::right_brace) {
            --brace_depth;
            if (brace_depth == 0) {
                break;
            }
            statement = append_token_text(std::move(statement), token);
            continue;
        }

        if (token.kind == TokenKind::left_paren) ++paren_depth;
        else if (token.kind == TokenKind::right_paren && paren_depth > 0) --paren_depth;
        else if (token.kind == TokenKind::left_bracket) ++bracket_depth;
        else if (token.kind == TokenKind::right_bracket && bracket_depth > 0) --bracket_depth;

        statement = append_token_text(std::move(statement), token);
        if (brace_depth == 1 && paren_depth == 0 && bracket_depth == 0 && token.kind == TokenKind::semicolon) {
            decl.body_statements.push_back(std::move(statement));
            statement.clear();
        }
    }

    if (!statement.empty()) {
        decl.body_statements.push_back(std::move(statement));
    }
}

void Parser::parse_function_signature(Decl &decl) {
    if (!consume(TokenKind::left_paren)) {
        diagnose(peek(), "expected '(' after function name");
        return;
    }

    while (!at_end() && !at(TokenKind::right_paren)) {
        if (consume(TokenKind::kw_InOut)) {
        }

        std::vector<Token> parameter_tokens;
        int angle_depth = 0;
        while (!at_end()) {
            const auto kind = peek().kind;
            if (angle_depth == 0 && (kind == TokenKind::comma || kind == TokenKind::right_paren)) {
                break;
            }
            if (kind == TokenKind::less) {
                ++angle_depth;
            } else if (kind == TokenKind::greater && angle_depth > 0) {
                --angle_depth;
            }
            parameter_tokens.push_back(peek());
            ++cursor_;
        }

        if (!parameter_tokens.empty()) {
            std::string param_name;
            if (parameter_tokens.size() >= 2 && parameter_tokens.back().kind == TokenKind::identifier) {
                param_name = std::string(parameter_tokens.back().text);
                parameter_tokens.pop_back();
            }
            std::string param_type;
            for (const auto &token : parameter_tokens) {
                param_type += std::string(token.text);
            }
            if (!param_type.empty()) {
                decl.parameters.push_back(ParameterDecl{.type = std::move(param_type), .name = std::move(param_name)});
            }
        }

        if (!consume(TokenKind::comma)) {
            break;
        }
    }

    if (!consume(TokenKind::right_paren)) {
        diagnose(peek(), "expected ')' after function parameters");
        return;
    }

    if (consume(TokenKind::arrow)) {
        decl.return_type = collect_type_until(TokenKind::left_brace, TokenKind::semicolon);
    } else {
        decl.return_type = "void";
    }
}

std::string Parser::collect_type_until(TokenKind stop_a, TokenKind stop_b) {
    std::string text;
    int angle_depth = 0;
    while (!at_end()) {
        const auto kind = peek().kind;
        if (angle_depth == 0 && (kind == stop_a || kind == stop_b || kind == TokenKind::right_paren)) {
            break;
        }
        if (kind == TokenKind::less) {
            ++angle_depth;
        } else if (kind == TokenKind::greater && angle_depth > 0) {
            --angle_depth;
        }
        text += std::string(peek().text);
        ++cursor_;
    }
    return text;
}

void Parser::skip_to_declaration_boundary() {
    while (!at_end() && !at(TokenKind::semicolon) && !at(TokenKind::right_brace)) {
        ++cursor_;
    }
    if (at(TokenKind::semicolon)) {
        ++cursor_;
    }
}

void Parser::skip_balanced_block() {
    int parens = 0;
    int brackets = 0;
    int braces = 0;

    while (!at_end()) {
        const auto kind = peek().kind;
        ++cursor_;

        if (kind == TokenKind::left_paren) ++parens;
        else if (kind == TokenKind::right_paren && parens > 0) --parens;
        else if (kind == TokenKind::left_bracket) ++brackets;
        else if (kind == TokenKind::right_bracket && brackets > 0) --brackets;
        else if (kind == TokenKind::left_brace) ++braces;
        else if (kind == TokenKind::right_brace && braces > 0) --braces;

        if (parens == 0 && brackets == 0 && braces == 0 &&
            (kind == TokenKind::semicolon || kind == TokenKind::right_brace)) {
            break;
        }
    }
}

std::string Parser::append_token_text(std::string statement, const Token &token) const {
    if (!statement.empty()) {
        const char last = statement.back();
        const bool last_ident = (last >= 'a' && last <= 'z') || (last >= 'A' && last <= 'Z') ||
                                (last >= '0' && last <= '9') || last == '_';
        const bool next_ident = token.kind == TokenKind::identifier ||
                                token.kind == TokenKind::integer_literal ||
                                token.kind == TokenKind::float_literal ||
                                (token.kind >= TokenKind::kw_Import && token.kind <= TokenKind::kw_InOut);
        if (last_ident && next_ident) {
            statement.push_back(' ');
        }
    }
    statement += std::string(token.text);
    return statement;
}

void Parser::diagnose(const Token &token, std::string message) {
    diagnostics_.report(2001, DiagnosticSeverity::error, token.span.begin,
                        std::string(sources_.name(file_id_)), std::move(message));
}

} // namespace rtsl
