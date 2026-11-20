/**
 * @file cc.h
 * @brief Compiler and architecture hooks for the lwIP compile-only check.
 *
 * Only the declarations lwIP needs to parse its own headers are provided;
 * nothing here is meant to run on a target.
 */

#ifndef OSP_LWIP_PORT_ARCH_CC_H_
#define OSP_LWIP_PORT_ARCH_CC_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint8_t u8_t;
typedef int8_t s8_t;
typedef uint16_t u16_t;
typedef int16_t s16_t;
typedef uint32_t u32_t;
typedef int32_t s32_t;
typedef uintptr_t mem_ptr_t;

#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x

#define LWIP_PLATFORM_DIAG(x) \
  do {                        \
    printf x;                 \
  } while (0)
#define LWIP_PLATFORM_ASSERT(x)              \
  do {                                       \
    printf("lwip assert: %s\n", x);          \
    abort();                                 \
  } while (0)
#define LWIP_RAND() ((u32_t)rand())

#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

#endif  // OSP_LWIP_PORT_ARCH_CC_H_
