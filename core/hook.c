/*
 * ADOFAI Hook 模块（arm64-v8a）
 *
 * 功能：
 *   - 自动定位 libil2cpp.so 基址（解析 /proc/self/maps）
 *   - Dobby inline hook 骨架：替换目标方法，调用 original
 *   - 3.3.1 版本偏移需由 Il2CppDumper 生成的 dump.cs 填入
 *
 * 编译依赖：Dobby（https://github.com/jmpews/Dobby）
 */

#include "hook.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <android/log.h>

#define TAG "ADOFAI_HOOK"

/* ---------- 模块基址查询 ---------- */
uintptr_t get_module_base(const char *name) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[1024];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, name)) {
            uintptr_t addr;
            if (sscanf(line, "%lx-%*lx", &addr) == 1)
                base = addr;
            break;
        }
    }
    fclose(f);
    return base;
}

uintptr_t get_il2cpp_base(void) {
    static uintptr_t cached = 0;
    if (!cached) cached = get_module_base("libil2cpp.so");
    return cached;
}

/* RVA -> 绝对地址 */
uintptr_t rva2addr(uintptr_t rva) {
    return get_il2cpp_base() + rva;
}

/* ---------- Dobby hook 封装 ---------- */
int do_hook(uintptr_t rva, void *replacement, void **original) {
    uintptr_t addr = rva2addr(rva);
    if (!addr) return -1;
    /* DobbyHook 返回 0 表示成功 */
    int rc = DobbyHook((void *)addr, replacement, original);
    __android_log_print(ANDROID_LOG_DEBUG, TAG, "hook RVA=%lx -> %lx, rc=%d",
                        (long)rva, (long)addr, rc);
    return rc;
}

/* ---------- 示例 hook：游戏初始化完成后注入"添加 Mod"按钮 ----------
 *
 * 步骤：
 *   1. 用 Il2CppDumper 解包 3.3.1 的 libil2cpp.so + global-metadata.dat
 *   2. 在 dump.cs 中搜索 "Settings" / "Advanced" / "ClearData" 相关方法
 *   3. 找到"高级设置面板初始化"方法的 RVA，填入下方 HOOK_RVA
 *   4. 在 replacement 里调用 ModBridge::InjectButton
 */
#define SETTINGS_ADVANCED_INIT_RVA  0x00000000  /* TODO: 从 dump.cs 填入 */

typedef void (*settings_init_fn)(void *instance);
static settings_init_fn g_orig_settings_init = NULL;

static void hook_settings_init(void *instance) {
    /* 先调用原逻辑，保证面板正常创建 */
    if (g_orig_settings_init)
        g_orig_settings_init(instance);

    /* 延迟一帧，等 UI 布局完成后再注入按钮 */
    /* ModBridge::InjectButton(advancedPanelTransform); */
    __android_log_print(ANDROID_LOG_DEBUG, TAG, "settings advanced init done, ready to inject button");
}

void install_hooks(void) {
    if (SETTINGS_ADVANCED_INIT_RVA == 0) {
        __android_log_print(ANDROID_LOG_WARN, TAG,
                            "HOOK_RVA not set, skip hook. Run Il2CppDumper first.");
        return;
    }
    do_hook(SETTINGS_ADVANCED_INIT_RVA,
            (void *)hook_settings_init,
            (void **)&g_orig_settings_init);
}

/* ---------- 自动 dump 辅助（首次运行生成 dump.cs 偏移参考）---------- */
void auto_dump_il2cpp(const char *out_dir) {
    /* 通过 IL2CPP API 遍历所有类型，输出类名+方法名+RVA 到文件，
     * 供没有 PC 端 Il2CppDumper 时快速定位目标方法。 */
    /* 实现依赖 il2cpp-api.h，此处为骨架 */
}
