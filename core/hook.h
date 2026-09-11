#ifndef ADOFAI_HOOK_H
#define ADOFAI_HOOK_H

#include <stdint.h>

uintptr_t get_module_base(const char *name);
uintptr_t get_il2cpp_base(void);
uintptr_t rva2addr(uintptr_t rva);
int  do_hook(uintptr_t rva, void *replacement, void **original);
void install_hooks(void);
void auto_dump_il2cpp(const char *out_dir);

#endif /* ADOFAI_HOOK_H */
