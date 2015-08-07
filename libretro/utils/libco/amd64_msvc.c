/*
  libco.amd64 (2009-10-12)
  author: byuu
  license: public domain
*/

#define LIBCO_C
#include "libco.h"
#include <assert.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

static thread_local long long co_active_buffer[64];
static thread_local cothread_t co_active_handle = 0;
static void (*co_swap)(cothread_t, cothread_t) = 0;

//ABI: Win64
static unsigned char co_swap_function[] = {
  0x48, 0x89, 0x22, 0x48, 0x8B, 0x21, 0x58, 0x48, 0x89, 0x6A, 0x08, 0x48, 0x89, 0x72, 0x10, 0x48,
  0x89, 0x7A, 0x18, 0x48, 0x89, 0x5A, 0x20, 0x4C, 0x89, 0x62, 0x28, 0x4C, 0x89, 0x6A, 0x30, 0x4C,
  0x89, 0x72, 0x38, 0x4C, 0x89, 0x7A, 0x40, 0x48, 0x81, 0xC2, 0x80, 0x00, 0x00, 0x00, 0x48, 0x83,
  0xE2, 0xF0, 0x0F, 0x29, 0x32, 0x0F, 0x29, 0x7A, 0x10, 0x44, 0x0F, 0x29, 0x42, 0x20, 0x44, 0x0F,
  0x29, 0x4A, 0x30, 0x44, 0x0F, 0x29, 0x52, 0x40, 0x44, 0x0F, 0x29, 0x5A, 0x50, 0x44, 0x0F, 0x29,
  0x62, 0x60, 0x44, 0x0F, 0x29, 0x6A, 0x70, 0x44, 0x0F, 0x29, 0xB2, 0x80, 0x00, 0x00, 0x00, 0x44,
  0x0F, 0x29, 0xBA, 0x90, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x69, 0x08, 0x48, 0x8B, 0x71, 0x10, 0x48,
  0x8B, 0x79, 0x18, 0x48, 0x8B, 0x59, 0x20, 0x4C, 0x8B, 0x61, 0x28, 0x4C, 0x8B, 0x69, 0x30, 0x4C,
  0x8B, 0x71, 0x38, 0x4C, 0x8B, 0x79, 0x40, 0x48, 0x81, 0xC1, 0x80, 0x00, 0x00, 0x00, 0x48, 0x83,
  0xE1, 0xF0, 0x0F, 0x29, 0x31, 0x0F, 0x29, 0x79, 0x10, 0x44, 0x0F, 0x29, 0x41, 0x20, 0x44, 0x0F,
  0x29, 0x49, 0x30, 0x44, 0x0F, 0x29, 0x51, 0x40, 0x44, 0x0F, 0x29, 0x59, 0x50, 0x44, 0x0F, 0x29,
  0x61, 0x60, 0x44, 0x0F, 0x29, 0x69, 0x70, 0x44, 0x0F, 0x29, 0xB1, 0x80, 0x00, 0x00, 0x00, 0x44,
  0x0F, 0x29, 0xB9, 0x90, 0x00, 0x00, 0x00, 0xFF, 0xE0,
};

#include <windows.h>

void co_init() {
  DWORD old_privileges;
  VirtualProtect(co_swap_function, sizeof co_swap_function, PAGE_EXECUTE_READWRITE, &old_privileges);
}

static void crash() {
  assert(0); /* called only if cothread_t entrypoint returns */
}

cothread_t co_active() {
  if(!co_active_handle) co_active_handle = &co_active_buffer;
  return co_active_handle;
}

cothread_t co_create(unsigned int size, void (*entrypoint)(void)) {
  cothread_t handle;
  if(!co_swap) {
    co_init();
    co_swap = (void (*)(cothread_t, cothread_t))co_swap_function;
  }
  if(!co_active_handle) co_active_handle = &co_active_buffer;
  size += 512; /* allocate additional space for storage */
  size &= ~15; /* align stack to 16-byte boundary */

  if(handle = (cothread_t)malloc(size)) {
    long long *p = (long long*)((char*)handle + size); /* seek to top of stack */
    *--p = (long long)crash;                           /* crash if entrypoint returns */
    *--p = (long long)entrypoint;                      /* start of function */
    *(long long*)handle = (long long)p;                /* stack pointer */
  }

  return handle;
}

void co_delete(cothread_t handle) {
  free(handle);
}

void co_switch(cothread_t handle) {
  register cothread_t co_previous_handle = co_active_handle;
  co_swap(co_active_handle = handle, co_previous_handle);
}

#ifdef __cplusplus
}
#endif
