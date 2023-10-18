#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <parson.h>
#include <vector.h>
#include <error.h>
#include <print.h>
#include <exception.h>
#include <decode.h>
#include <validate.h>
#include <exec.h>

typedef struct {
    int         fd;
    size_t      size;
    uint8_t     *map;
    module_t    *mod;
    store_t     *store;
} test_ctx_t;

// helpers
// todo: consider the case where funcidx != funcaddr
error_t lookup_func_by_name(funcaddr_t *addr, const char *name, module_t *mod) {
    VECTOR_FOR_EACH(export, &mod->exports, export_t) {
        if(strcmp(export->name, name) == 0 && export->exportdesc.kind == 0) {
            *addr = export->exportdesc.funcidx;
            return ERR_SUCCESS;
        }
    }
    return ERR_FAILED;
}

error_t create_test_ctx(test_ctx_t *ctx, const char *name) {
    __try {
        // open and map file
        ctx->fd = open(name, O_RDONLY);
        __throwif(ERR_FAILED, ctx->fd == -1);
        struct stat s;
        __throwif(ERR_FAILED, fstat(ctx->fd, &s) == -1);
        ctx->size = s.st_size;
        ctx->map = mmap(
            NULL,
            ctx->size,
            PROT_READ,
            MAP_PRIVATE,
            ctx->fd, 
            0
        );
        __throwif(ERR_FAILED, ctx->map == MAP_FAILED);

        // decode
        __throwif(
            ERR_FAILED, IS_ERROR(
                decode_module(
                    &ctx->mod, ctx->map, ctx->size
                )
            )
        );

        // validate
        __throwif(ERR_FAILED, IS_ERROR(validate_module(ctx->mod)));

        // instantiate
        __throwif(ERR_FAILED, IS_ERROR(instantiate(&ctx->store, ctx->mod)));
    }
    __catch:
        return err;
}

void destroy_test_ctx(test_ctx_t *ctx) {
    free(ctx->mod);
    free(ctx->store);
    munmap(ctx->map, ctx->size);
    close(ctx->fd);
}

// ref: https://github.com/WebAssembly/wabt/blob/main/docs/wast2json.md
static void convert_to_arg(arg_t *arg, JSON_Object *obj) {
    const char *ty  =  json_object_get_string(obj, "type");
    const char *val = json_object_get_string(obj, "value");

    if(strcmp(ty, "i32") == 0) {
        arg->type           = TYPE_NUM_I32;
        arg->val.num.i32    = atoi(val);
    }
    // todo: add here
}

static void convert_to_args(args_t *args, JSON_Array *array) {
    VECTOR_INIT(args, json_array_get_count(array), arg_t);

    size_t idx = 0;
    VECTOR_FOR_EACH(arg, args, arg_t) {
        convert_to_arg(arg, json_array_get_object(array, idx++));
    }
}

static error_t run_command(test_ctx_t *ctx, JSON_Object *command) {
    const char *type    = json_object_get_string(command, "type");
    const double line   = json_object_get_number(command, "line");
    __try {
        if(strcmp(type, "module") == 0) {
            // *.wasm files expected in the same directory as the json.
            const char *fpath = json_object_get_string(command, "filename");
            __throwif(ERR_FAILED, create_test_ctx(ctx, fpath));
        }
        else if(strcmp(type, "assert_return") == 0) {
            JSON_Object *action = json_object_get_object(command, "action");
            const char *action_type = json_object_get_string(action, "type");

            if(strcmp(action_type, "invoke") == 0) {
                const char *func = json_object_get_string(action, "field");

                funcaddr_t addr;
                __throwif(
                    ERR_FAILED, 
                    IS_ERROR(
                        lookup_func_by_name(&addr, func, ctx->mod)
                    )
                );
                
                args_t args;
                convert_to_args(&args, json_object_get_array(action, "args"));

                __throwif(ERR_FAILED, IS_ERROR(invoke(ctx->store, addr, &args)));

                args_t expects;
                convert_to_args(&expects, json_object_get_array(command, "expected"));
                
                size_t idx = 0;
                VECTOR_FOR_EACH(expect, &expects, arg_t) {
                    // todo: validate type?
                    arg_t *ret = VECTOR_ELEM(&args, idx++);
                    // todo: consider the case whrere type != i32
                    __throwif(ERR_FAILED, ret->val.num.i32 != expect->val.num.i32);
                }
            }
        }
        // todo: add here
        else {
            __throw(ERR_FAILED);
        }
    }
    __catch:
        if(IS_ERROR(err)) {
            ERROR(
                "Faild: type: %s, line: %0.f",
                type,
                line
            );
        } else {
            INFO(
                "Pass: type: %s, line: %0.f",
                type,
                line
            );
        }
}

int main(int argc, char *argv[]) {
    test_ctx_t ctx = {};
    JSON_Value *root;

    __try {
        INFO("testsuite: %s", argv[1]);

        root = json_parse_file(argv[1]);
        __throwif(ERR_FAILED, !root);

        JSON_Object *obj = json_value_get_object(root);
        __throwif(ERR_FAILED, !obj);

        JSON_Array *commands = json_object_get_array(obj, "commands");
        __throwif(ERR_FAILED, !commands);

        for(int i = 0; i < json_array_get_count(commands); i++) {
            __throwif(ERR_FAILED, IS_ERROR(
                run_command(&ctx, json_array_get_object(commands, i))
            ));
        }
    }
    __catch:
        // cleanup
        json_value_free(root);
        destroy_test_ctx(&ctx);

        return err;
}