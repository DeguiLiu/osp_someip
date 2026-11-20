/**
 * @file rtthread.h
 * @brief Minimal RT-Thread declarations for a compile-only portability check.
 *
 * This is not an RT-Thread port: it only declares the symbols newosp's
 * platform layer uses, so the core headers can be compiled with
 * -DOSP_PLATFORM_RTTHREAD=1 on a host without the SDK. Linking against it is
 * meaningless; the check stops at syntax and type checking.
 */

#ifndef OSP_RTTHREAD_STUB_H_
#define OSP_RTTHREAD_STUB_H_

#include <stdarg.h>
#include <stdint.h>

typedef int32_t rt_err_t;
typedef uint32_t rt_uint32_t;
typedef int32_t rt_int32_t;
typedef uint32_t rt_tick_t;

#ifndef RT_TICK_PER_SECOND
#define RT_TICK_PER_SECOND 1000
#endif

#ifdef __cplusplus
extern "C" {
#endif

void rt_kprintf(const char* fmt, ...);
rt_err_t rt_thread_delay(rt_tick_t tick);
rt_err_t rt_thread_mdelay(rt_int32_t ms);
rt_err_t rt_thread_yield(void);
rt_tick_t rt_tick_get(void);

#ifdef __cplusplus
}
#endif

#endif  // OSP_RTTHREAD_STUB_H_
