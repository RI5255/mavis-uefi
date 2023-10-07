#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "error.h"

/*
    Use this structure whenever you access to binary data. 
    This makes the code clearer and reduces the number of function arguments.
*/

typedef struct {
    uint8_t *head;
    size_t  size;
    size_t  cursor;
} buffer_t;

error_t new_buffer(buffer_t **d, uint8_t *head, size_t size);
error_t new_stack(buffer_t **d, size_t size);
bool eof(buffer_t *buf);
error_t read_buffer(buffer_t **d, size_t size, buffer_t *buf);
error_t read_byte(uint8_t *d, buffer_t *buf);
error_t read_bytes(uint8_t **d, buffer_t *buf);
error_t read_u32(uint32_t *d, buffer_t *buf);
error_t read_i32(int32_t *d, buffer_t *buf);
error_t read_u32_leb128(uint32_t *d, buffer_t *buf);
error_t read_u64_leb128(uint64_t *d, buffer_t *buf);
error_t read_i32_leb128(int32_t *d, buffer_t *buf);
error_t read_i64_leb128(int64_t *d, buffer_t *buf);
error_t write_i32(int32_t d, buffer_t *buf);