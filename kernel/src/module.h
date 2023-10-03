#pragma once

// ref: https://webassembly.github.io/spec/core/

#include <stdint.h>
#include "vector.h"

typedef struct {
    vector_t rt1;
    vector_t rt2;
} functype_t;

enum op {
   OP_END = 0x0b
};

typedef struct instr {
    struct instr    *next;
    uint8_t         op;
} instr_t;

typedef struct {
    uint32_t    n;
    uint8_t     type;
} locals_t;

typedef struct {
    vector_t    locals;
    instr_t     *expr;
} func_t;

typedef struct {
    uint32_t    size;
    func_t      func;
} code_t;

typedef struct {
    union {
        // typesec
        vector_t functypes;
        
        // funcsec
        vector_t typeidxes;

        // codesec
        vector_t codes;
    };
} section_t ;

typedef struct {
    section_t *known_sections[12];
} module_t;