#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "code-generation.h"
#include "register.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static void generate_global_variable(CcmmcAst *global_decl, CcmmcState *state)
{
    fputs("\t.data\n\t.align\t2\n", state->asm_output);
    for (CcmmcAst *var_decl = global_decl->child->right_sibling;
         var_decl != NULL; var_decl = var_decl->right_sibling) {
        CcmmcSymbol *var_sym = ccmmc_symbol_table_retrieve(state->table,
            var_decl->value_id.name);
        switch (var_decl->value_id.kind) {
            case CCMMC_KIND_ID_NORMAL:
                fprintf(state->asm_output, "\t.comm\t%s, 4\n", var_sym->name);
                break;
            case CCMMC_KIND_ID_ARRAY: {
                size_t total_elements = 1;
                assert(var_sym->type.array_dimension > 0);
                for (size_t i = 0; i < var_sym->type.array_dimension; i++)
                    total_elements *= var_sym->type.array_size[i];
                fprintf(state->asm_output, "\t.comm\t%s, %zu\n",
                    var_sym->name, total_elements * 4);
                } break;
            case CCMMC_KIND_ID_WITH_INIT: {
                CcmmcAst *init_value = var_decl->child;
                fprintf(state->asm_output,
                    "\t.type\t%s, %%object\n"
                    "\t.size\t%s, 4\n"
                    "\t.global\t%s\n",
                    var_sym->name, var_sym->name, var_sym->name);
                if (var_sym->type.type_base == CCMMC_AST_VALUE_INT) {
                    int int_value;
                    if (init_value->type_node == CCMMC_AST_NODE_CONST_VALUE) {
                        assert(init_value->value_const.kind == CCMMC_KIND_CONST_INT);
                        int_value = init_value->value_const.const_int;
                    } else if (init_value->type_node == CCMMC_AST_NODE_EXPR) {
                        assert(ccmmc_ast_expr_get_is_constant(init_value));
                        assert(ccmmc_ast_expr_get_is_int(init_value));
                        int_value = ccmmc_ast_expr_get_int(init_value);
                    } else {
                        assert(false);
                    }
                    fprintf(state->asm_output,
                        "%s:\n"
                        "\t.word\t%d\n",
                        var_sym->name, int_value);
                } else if (var_sym->type.type_base == CCMMC_AST_VALUE_FLOAT) {
                    float float_value;
                    if (init_value->type_node == CCMMC_AST_NODE_CONST_VALUE) {
                        assert(init_value->value_const.kind == CCMMC_KIND_CONST_FLOAT);
                        float_value = init_value->value_const.const_float;
                    } else if (init_value->type_node == CCMMC_AST_NODE_EXPR) {
                        assert(ccmmc_ast_expr_get_is_constant(init_value));
                        assert(ccmmc_ast_expr_get_is_float(init_value));
                        float_value = ccmmc_ast_expr_get_float(init_value);
                    } else {
                        assert(false);
                    }
                    fprintf(state->asm_output,
                        "%s:\n"
                        "\t.float\t%.9g\n",
                       var_sym->name, float_value);
                } else {
                    assert(false);
                }
                } break;
            default:
                assert(false);
        }
    }
}

static inline bool safe_immediate(uint64_t imm) {
    return imm <= 4096;
}

static void generate_block(
    CcmmcAst *block, CcmmcState *state, uint64_t current_offset);
static void generate_statement(
    CcmmcAst *stmt, CcmmcState *state, uint64_t current_offset)
{
    if (stmt->type_node == CCMMC_AST_NODE_NUL)
        return;
    if (stmt->type_node == CCMMC_AST_NODE_BLOCK) {
        generate_block(stmt, state, current_offset);
        return;
    }

    assert(stmt->type_node == CCMMC_AST_NODE_STMT);
    switch(stmt->value_stmt.kind) {
        case CCMMC_KIND_STMT_WHILE:
            generate_statement(stmt->child->right_sibling,
                state, current_offset);
            break;
        case CCMMC_KIND_STMT_FOR:
            break;
        case CCMMC_KIND_STMT_ASSIGN:
            break;
        case CCMMC_KIND_STMT_IF:
            generate_statement(stmt->child->right_sibling,
                state, current_offset);
            if (stmt->child->right_sibling->right_sibling->type_node
                != CCMMC_AST_NODE_NUL)
                generate_statement(stmt->child->right_sibling->right_sibling,
                    state, current_offset);
            break;
        case CCMMC_KIND_STMT_FUNCTION_CALL:
            break;
        case CCMMC_KIND_STMT_RETURN:
            break;
        default:
            assert(false);
    }
}

static uint64_t generate_local_variable(
    CcmmcAst *local_decl, CcmmcState *state, uint64_t current_offset)
{
    for (CcmmcAst *var_decl = local_decl->child->right_sibling;
         var_decl != NULL; var_decl = var_decl->right_sibling) {
        CcmmcSymbol *var_sym = ccmmc_symbol_table_retrieve(state->table,
            var_decl->value_id.name);
        switch (var_decl->value_id.kind) {
            case CCMMC_KIND_ID_NORMAL:
                current_offset += 4;
                var_sym->attr.addr = current_offset;
                break;
            case CCMMC_KIND_ID_ARRAY: {
                size_t total_elements = 1;
                assert(var_sym->type.array_dimension > 0);
                for (size_t i = 0; i < var_sym->type.array_dimension; i++)
                    total_elements *= var_sym->type.array_size[i];
                current_offset += total_elements * 4;
                var_sym->attr.addr = current_offset;
                } break;
            case CCMMC_KIND_ID_WITH_INIT: {
                current_offset += 4;
                var_sym->attr.addr = current_offset;
                } break;
            default:
                assert(false);
        }
    }
    return current_offset;
}

static void generate_block(
    CcmmcAst *block, CcmmcState *state, uint64_t current_offset)
{
    ccmmc_symbol_table_reopen_scope(state->table);

    CcmmcAst *child = block->child;
    uint64_t orig_offset = current_offset;
    uint64_t offset_diff = 0;
    if (child != NULL && child->type_node == CCMMC_AST_NODE_VARIABLE_DECL_LIST) {
        for (CcmmcAst *local = child->child; local != NULL; local = local->right_sibling)
            current_offset = generate_local_variable(local, state, current_offset);
        offset_diff = current_offset - orig_offset;
        if (offset_diff > 0) {
            if (safe_immediate(offset_diff)) {
                fprintf(state->asm_output, "\tsub\tsp, sp, #%" PRIu64 "\n",
                    offset_diff);
            } else {
                CcmmcTmp *tmp = ccmmc_register_alloc(state->reg_pool, &current_offset);
                const char *reg_name = ccmmc_register_lock(state->reg_pool, tmp);
                fprintf(state->asm_output,
                    "\tldr\t%s, =%" PRIu64 "\n"
                    "\tsub\tsp, sp, %s\n", reg_name, offset_diff, reg_name);
                ccmmc_register_unlock(state->reg_pool, tmp);
                ccmmc_register_free(state->reg_pool, tmp, &current_offset);
            }
        }
        child = child->right_sibling;
    }
    if (child != NULL && child->type_node == CCMMC_AST_NODE_STMT_LIST) {
        for (CcmmcAst *stmt = child->child; stmt != NULL; stmt = stmt->right_sibling)
            generate_statement(stmt, state, current_offset);
    }
    if (offset_diff > 0) {
        if (safe_immediate(offset_diff)) {
            fprintf(state->asm_output, "\tadd\tsp, sp, #%" PRIu64 "\n",
                offset_diff);
        } else {
            CcmmcTmp *tmp = ccmmc_register_alloc(state->reg_pool, &current_offset);
            const char *reg_name = ccmmc_register_lock(state->reg_pool, tmp);
            fprintf(state->asm_output,
                "\tldr\t%s, =%" PRIu64 "\n"
                "\tadd\tsp, sp, %s\n", reg_name, offset_diff, reg_name);
            ccmmc_register_unlock(state->reg_pool, tmp);
            ccmmc_register_free(state->reg_pool, tmp, &current_offset);
        }
    }

    ccmmc_symbol_table_close_scope(state->table);
}

static void generate_function(CcmmcAst *function, CcmmcState *state)
{
    fputs("\t.text\n\t.align\t2\n", state->asm_output);
    fprintf(state->asm_output,
        "\t.type\t%s, %%function\n"
        "\t.global\t%s\n"
        "%s:\n",
        function->child->right_sibling->value_id.name,
        function->child->right_sibling->value_id.name,
        function->child->right_sibling->value_id.name);
    CcmmcAst *param_node = function->child->right_sibling->right_sibling;
    CcmmcAst *block_node = param_node->right_sibling;
    generate_block(block_node, state, 0);
    fprintf(state->asm_output,
        "\t.size\t%s, .-%s\n",
        function->child->right_sibling->value_id.name,
        function->child->right_sibling->value_id.name);
}

static void generate_program(CcmmcState *state)
{
    for (CcmmcAst *global_decl = state->ast->child; global_decl != NULL;
         global_decl = global_decl->right_sibling) {
        switch (global_decl->value_decl.kind) {
            case CCMMC_KIND_DECL_VARIABLE:
                generate_global_variable(global_decl, state);
                break;
            case CCMMC_KIND_DECL_FUNCTION:
                generate_function(global_decl, state);
                break;
            case CCMMC_KIND_DECL_FUNCTION_PARAMETER:
            case CCMMC_KIND_DECL_TYPE:
            default:
                assert(false);
        }
    }
}

void ccmmc_code_generation(CcmmcState *state)
{
    state->table->this_scope = NULL;
    state->table->current = NULL;
    ccmmc_symbol_table_reopen_scope(state->table);
    state->reg_pool = ccmmc_register_init(state->asm_output);
    generate_program(state);
    ccmmc_register_fini(state->reg_pool);
    state->reg_pool = NULL;
}

// vim: set sw=4 ts=4 sts=4 et:
