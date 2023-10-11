#include "exec.h"
#include "print.h"

// stack
void new_stack(stack_t **d) {
    stack_t *stack = *d = malloc(sizeof(stack_t));
    
    if(!stack)
        PANIC("out of memory");
    
    *stack = (stack_t) {
        .idx        = -1,
        .pool       = malloc(STACK_SIZE)
    };

    LIST_INIT(&stack->frames);
}

void push_val(val_t val, stack_t *stack) {
    if(full(stack))
        PANIC("stack is full");
    
    stack->pool[++stack->idx] = (obj_t) {
        .type   = TYPE_VAL,
        .val    = val 
    };
    printf("push val %x %x\n", val.type, val.num.i32);
}

void push_vals(vals_t vals, stack_t *stack) {
    VECTOR_FOR_EACH(val, &vals, val_t) {
        push_val(*val, stack);
    }
}

void push_label(label_t label, stack_t *stack) {
    if(full(stack))
        PANIC("stack is full");
    
    stack->pool[++stack->idx] = (obj_t) {
        .type   = TYPE_LABEL,
        .label  = label 
    };
    puts("push label");
}

void push_frame(frame_t frame, stack_t *stack) {
    if(full(stack))
        PANIC("stack is full");
    
    obj_t *obj = &stack->pool[++stack->idx];

    *obj = (obj_t) {
        .type   = TYPE_FRAME,
        .frame  = frame 
    };
    list_push_back(&stack->frames, &obj->frame.link);
    puts("push frame");
}

void pop_val(val_t *val, stack_t *stack) {    
    *val = stack->pool[stack->idx].val;
    stack->idx--;
    printf("pop val %x %x\n", val->type, val->num.i32);
}

// pop all values from the stack top
void pop_vals(vals_t *vals, stack_t *stack) {
    // count values
    size_t num_vals = 0;
    size_t i = stack->idx;
    while(stack->pool[i].type == TYPE_VAL) {
        num_vals++;
        i--;
    }
    
    // init vector
    VECTOR_INIT(vals, num_vals, val_t);
    
    // pop values
    VECTOR_FOR_EACH(val, vals, val_t) {
        pop_val(val, stack);
    }
}

void pop_label(label_t *label, stack_t *stack) {
    *label = stack->pool[stack->idx].label;
    stack->idx--;
    puts("pop label");
}

error_t try_pop_label(label_t *label, stack_t *stack) { 
    obj_t obj = stack->pool[stack->idx];

    if(obj.type != TYPE_LABEL)
        return ERR_FAILED;
    
    *label = obj.label;
    stack->idx--;
    puts("pop label");
    return ERR_SUCCESS;
}

void pop_frame(frame_t *frame, stack_t *stack) {
    *frame = stack->pool[stack->idx].frame;
    stack->idx--;
    list_pop_tail(&stack->frames);
    puts("pop frame");
}

// There is no need to use append when instantiating, since everything we need (functions, imports, etc.) is known to us.
// see Note of https://webassembly.github.io/spec/core/exec/modules.html#instantiation

// todo: add externval(support imports)

// There must be enough space in S to allocate all functions.
funcaddr_t allocfunc(store_t *S, func_t *func, moduleinst_t *moduleinst) {
    static funcaddr_t next_addr = 0;

    funcinst_t *funcinst = VECTOR_ELEM(&S->funcs, next_addr);
    functype_t *functype = &moduleinst->types[func->type];

    funcinst->type   = functype;
    funcinst->module = moduleinst;
    funcinst->code   = func;

    return next_addr++; 
}

moduleinst_t *allocmodule(store_t *S, module_t *module) {
    // allocate moduleinst
    moduleinst_t *moduleinst = malloc(sizeof(moduleinst_t));
    
    moduleinst->types = module->types.elem;

    // allocate funcs
    // In this implementation, the index always matches the address.
    uint32_t num_funcs = module->funcs.n;
    moduleinst->funcaddrs = malloc(sizeof(funcaddr_t) * num_funcs);
    for(uint32_t i = 0; i < num_funcs; i++) {
        func_t *func = VECTOR_ELEM(&module->funcs, i);
        moduleinst->funcaddrs[i] = allocfunc(S, func, moduleinst);
    }

    moduleinst->exports = module->exports.elem;

    return moduleinst;
}

error_t instantiate(store_t **S, module_t *module) {
    // allocate store
    store_t *store = *S = malloc(sizeof(store_t));
    
    // allocate stack
    new_stack(&store->stack);

    // allocate funcs
    VECTOR_INIT(&store->funcs, module->funcs.n, funcinst_t);

    moduleinst_t *moduleinst = allocmodule(store, module);

    // todo: support start section

    return ERR_SUCCESS;
}

// execute a sequence of instructions
static void exec_instrs(instr_t * ent, store_t *S) {
    instr_t *ip = ent;

    // current frame
    frame_t *F = LIST_TAIL(&S->stack->frames, frame_t, link);

    while(ip) {
        instr_t *next_ip = ip->next;

        switch(ip->op) {
            // todo: consider the case where blocktype is typeidx.
            case OP_BLOCK: {
                label_t L = {
                    .arity = ip->bt.valtype == 0x40 ? 0 : 1,
                    .continuation = ip->next,
                };
                push_label(L, S->stack);
                next_ip = ip->in1;
                break;
            }

            case OP_LOOP: {
               label_t L = {
                    .arity = 0,
                    .continuation = ip,
                };
                push_label(L, S->stack);
                next_ip = ip->in1;
                break; 
            }

            case OP_IF: {
                int32_t c;
                pop_i32(&c, S->stack);

                // enter instr* with label L 
                label_t L = {.arity = 1, .continuation = ip->next};
                push_label(L, S->stack);

                if(c) {
                    next_ip = ip->in1;
                } 
                else {
                    next_ip = ip->in2;
                }
                break;
            }

            // ref: https://webassembly.github.io/spec/core/exec/instructions.html#returning-from-a-function
            // ref: https://webassembly.github.io/spec/core/exec/instructions.html#exiting-xref-syntax-instructions-syntax-instr-mathit-instr-ast-with-label-l
            // The else instruction is treated as an exit from the instruction sequence with a label
            case OP_ELSE:
            case OP_END: {
                // pop all values from stack
                vals_t vals;
                pop_vals(&vals, S->stack);

                // Divide the cases according to whether there is a label or frame on the stack top.
                label_t L;
                error_t err;
                err = try_pop_label(&L, S->stack);

                switch(err) {
                    case ERR_SUCCESS:
                        // exit instr* with label L
                        push_vals(vals, S->stack);
                        next_ip = L.continuation;
                        break;
                    
                    default: {
                        // return from function
                        frame_t frame;
                        pop_frame(&frame, S->stack);
                        push_vals(vals, S->stack);
                        break;
                    }
                }            
                break;
            }

            case OP_BR_IF: {
                int32_t c;
                pop_i32(&c, S->stack);

                if(c == 0) {
                    break;
                }
            }
            
            case OP_BR: {
                vals_t vals;
                pop_vals(&vals, S->stack);
                label_t L;
                for(int i = 0; i <= ip->labelidx; i++) {
                    vals_t tmp;
                    pop_vals(&tmp, S->stack);
                    pop_label(&L, S->stack);
                }
                push_vals(vals, S->stack);
                next_ip = L.continuation;
                break;
            }

            case OP_CALL:
                invoke_func(S, F->module->funcaddrs[ip->funcidx]);
                break;

            case OP_LOCAL_GET: {
                localidx_t x = ip->localidx;
                val_t val = F->locals[x];
                push_val(val, S->stack);
                break;
            }

            case OP_LOCAL_SET: {
                localidx_t x = ip->localidx;
                val_t val;
                pop_val(&val, S->stack);
                F->locals[x] = val;
                break;
            }
            
            case OP_I32_CONST:
                push_i32(ip->c.i32, S->stack);
                break;
            
            case OP_I32_GE_S: {
                int32_t rhs, lhs;
                pop_i32(&rhs, S->stack);
                pop_i32(&lhs, S->stack);
                push_i32(lhs >= rhs, S->stack);
                break;
            }

            case OP_I32_ADD: {
                int32_t rhs, lhs;
                pop_i32(&rhs, S->stack);
                pop_i32(&lhs, S->stack);
                push_i32(lhs + rhs, S->stack);
                break;
            }
        }
        
        // update ip
        ip = next_ip;
    }
}

// ref: https://webassembly.github.io/spec/core/exec/instructions.html#function-calls
void invoke_func(store_t *S, funcaddr_t funcaddr) {
    funcinst_t *funcinst = VECTOR_ELEM(&S->funcs, funcaddr);
    functype_t *functype = funcinst->type;

    // create new frame
    frame_t frame;
    frame.module = funcinst->module;
    frame.locals = malloc(sizeof(val_t) * funcinst->code->locals.n);

    // pop args
    for(uint32_t i = 0; i < functype->rt1.n; i++) {
        pop_val(&frame.locals[i], S->stack);
    }

    // push activation frame
    frame.arity  = functype->rt2.n;
    push_frame(frame, S->stack);

    // create label L
    static instr_t end = {.op = OP_END};
    label_t L = {.arity = functype->rt2.n, .continuation = &end};

    // enter instr* with label L
    push_label(L, S->stack);

    // execute
    exec_instrs(funcinst->code->body ,S);
}

// The args is a reference to args_t. 
// This is because args is also used to return results.
error_t invoke(store_t *S, funcaddr_t funcaddr, args_t *args) {
    if(funcaddr < 0 || (S->funcs.n - 1) < funcaddr) {
        return ERR_FAILED;
    }

    funcinst_t *funcinst = VECTOR_ELEM(&S->funcs, funcaddr);
    functype_t *functype = funcinst->type;

    if(args->n != functype->rt1.n)
        return ERR_FAILED;
   
    int idx = 0;
    VECTOR_FOR_EACH(val, args, val_t) {
        if(val->type != *VECTOR_ELEM(&functype->rt1, idx++))
            return ERR_FAILED;
    }

    // push dummy frame
    frame_t frame = {
        .arity  = 0,
        .locals = NULL,
        .module = NULL
    };
    push_frame(frame, S->stack);
    
    // push args
    VECTOR_FOR_EACH(val, args, val_t) {
        push_val(*val, S->stack);
    }

    // invoke func
    invoke_func(S, funcaddr);

    // reuse args to return results since it is no longer used.
    //free(args->elem);
    VECTOR_INIT(args, functype->rt2.n, val_t);
    VECTOR_FOR_EACH(ret, args, val_t) {
        pop_val(ret, S->stack);
    }

    return ERR_SUCCESS;
}