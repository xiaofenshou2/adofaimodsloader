/*
 * ADOFAI Mod Loader - Core
 * 功能：
 *   1. 校验文件后缀，仅 .zip 受支持，其它提示"不支持的格式"
 *   2. 检查 zip 根目录纯净度：若根目录存在文件夹 -> 通知"Mod 无法加载，请检查 zip 根目录"
 *   3. 解压根目录条目并加载 .dll / .so
 *
 * 依赖：zlib（Android NDK 自带 libz）；zipio.c（自包含 zip 读写，无 minizip 依赖）。
 */

#include "mod_loader.h"
#include "zipio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "ADOFAI_MOD", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ADOFAI_MOD", __VA_ARGS__)
#else
#define LOGD(...) fprintf(stderr, "[ADOFAI_MOD] " __VA_ARGS__)
#define LOGE(...) fprintf(stderr, "[ADOFAI_MOD][ERR] " __VA_ARGS__)
#endif

/* ---------- 内部状态 ---------- */
static void      *g_mod_host = NULL;   /* 实际为 jobject，用 void* 避免依赖 JNI 完整类型 */
static JavaVM   *g_vm        = NULL;
static char     *g_last_zip  = NULL;
static int       g_loaded    = 0;

void mod_set_java(JavaVM *vm, void *host) {
    g_vm = vm;
    g_mod_host = host;
}

/* ---------- Java 回调辅助 ---------- */
static JNIEnv *get_env(int *attached) {
#ifdef ADOFAI_NO_JNI
    (void)attached;
    return NULL;
#else
    JNIEnv *env = NULL;
    *attached = 0;
    if (!g_vm) return NULL;
    if ((*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK)
        return env;
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) == 0) {
        *attached = 1;
        return env;
    }
    return NULL;
#endif
}

static void java_notify(const char *title, const char *msg) {
    int attached = 0;
#ifndef ADOFAI_NO_JNI
    JNIEnv *env = get_env(&attached);
    if (env && g_mod_host) {
        jclass  cls  = (*env)->GetObjectClass(env, g_mod_host);
        jmethodID mid = (*env)->GetMethodID(env, cls, "notify",
                                            "(Ljava/lang/String;Ljava/lang/String;)V");
        if (mid) {
            (*env)->CallVoidMethod(env, g_mod_host, mid,
                                   (*env)->NewStringUTF(env, title),
                                   (*env)->NewStringUTF(env, msg));
        }
        (*env)->DeleteLocalRef(env, cls);
        if (attached) (*g_vm)->DetachCurrentThread(g_vm);
        return;
    }
#endif
    LOGD("%s: %s", title, msg);
}

/* ---------- 文件后缀校验 ---------- */
int mod_check_extension(const char *path) {
    if (!path) return MOD_ERR_FORMAT;
    size_t len = strlen(path);
    if (len < 5) return MOD_ERR_FORMAT;
    const char *ext = path + len - 4;
    if (strcasecmp(ext, ".zip") == 0) return MOD_OK;
    java_notify("不支持的格式",
                "仅支持 .zip 格式的 Mod 文件，其它格式暂不支持。");
    return MOD_ERR_FORMAT;
}

/* ---------- zip 根目录纯净度检查 ----------
 * 规则：根目录不得包含任何文件夹。
 *   - "abc"       -> 根目录文件（合法）
 *   - "abc/"      -> 空目录（非法）
 *   - "abc/def"   -> 子目录文件（非法）
 */
int mod_check_root_clean(const char *zip_path) {
    zip_file *zf = zip_open(zip_path);
    if (!zf) {
        java_notify("Mod 加载失败", "无法打开 zip 文件，文件可能已损坏。");
        return MOD_ERR_ZIP;
    }
    char name[ZIP_MAX_NAME];
    if (zip_read_first(zf, name, sizeof(name))) {
        do {
            const char *slash = strchr(name, '/');
            if (slash == NULL) {
                /* 根目录文件：合法 */
            } else if (slash == name + strlen(name) - 1) {
                /* "xxx/" 空目录 */
                zip_close(zf);
                java_notify("Mod 无法加载", "请检查 zip 根目录：不应包含文件夹。");
                return MOD_ERR_ROOT_DIR;
            } else {
                /* "xxx/yyy" 套文件夹 */
                zip_close(zf);
                java_notify("Mod 无法加载", "请检查 zip 根目录：DLL/资源必须放在根目录。");
                return MOD_ERR_ROOT_DIR;
            }
        } while (zip_read_next(zf, name, sizeof(name)));
    }
    zip_close(zf);
    return MOD_OK;
}

/* ---------- 解压 zip 根目录条目到 dest_dir（跳过子目录文件）---------- */
static int extract_root_entries(const char *zip_path, const char *dest_dir) {
    zip_file *zf = zip_open(zip_path);
    if (!zf) return 0;
    mkdir(dest_dir, 0755);
    char name[ZIP_MAX_NAME];
    int n = 0;
    if (zip_read_first(zf, name, sizeof(name))) {
        do {
            /* 只处理根目录文件（不含 /） */
            if (strchr(name, '/') == NULL) {
                zip_extract_current(zf, dest_dir);
                n++;
            }
        } while (zip_read_next(zf, name, sizeof(name)));
    }
    zip_close(zf);
    return n;
}

/* ---------- 加载单个 .so（原版 IL2CPP 可直接 dlopen）---------- */
static int load_one_so(const char *so_path) {
    void *handle = dlopen(so_path, RTLD_NOW);
    if (!handle) {
        LOGE("dlopen failed: %s (%s)", so_path, dlerror());
        return MOD_ERR_LOAD;
    }
    typedef void (*init_fn)(void);
    init_fn init = (init_fn)dlsym(handle, "Init");
    if (init) init();
    return MOD_OK;
}

/* ---------- 加载单个 .dll（需游戏集成 HybridCLR）---------- */
static int load_one_dll(const char *dll_path) {
#ifdef ADOFAI_NO_JNI
    (void)dll_path;
    LOGD("load_one_dll: HybridCLR 不可用（主机测试），跳过 %s", dll_path);
    return MOD_ERR_LOAD;
#else
    int attached = 0;
    JNIEnv *env = get_env(&attached);
    if (!env || !g_mod_host) return MOD_ERR_LOAD;
    jclass  cls  = (*env)->GetObjectClass(env, g_mod_host);
    jmethodID mid = (*env)->GetMethodID(env, cls, "loadDllHybridCLR",
                                        "(Ljava/lang/String;)Z");
    jboolean ok = JNI_FALSE;
    if (mid) {
        ok = (*env)->CallBooleanMethod(env, g_mod_host, mid,
                                       (*env)->NewStringUTF(env, dll_path));
    }
    (*env)->DeleteLocalRef(env, cls);
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
    return ok ? MOD_OK : MOD_ERR_LOAD;
#endif
}

/* ---------- 遍历解压目录，加载 .so / .dll ---------- */
static int scan_and_load(const char *dir) {
    /* 简单实现：遍历 dir 下一级文件 */
    char path[1024];
    snprintf(path, sizeof(path), "ls %s/*.so %s/*.dll 2>/dev/null", dir, dir);
    FILE *p = popen(path, "r");
    if (!p) return 0;
    char line[1024];
    int loaded = 0;
    while (fgets(line, sizeof(line), p)) {
        line[strcspn(line, "\n")] = 0;
        if (strstr(line, ".so"))  { if (load_one_so(line)  == MOD_OK) loaded++; }
        if (strstr(line, ".dll")) { if (load_one_dll(line) == MOD_OK) loaded++; }
    }
    pclose(p);
    return loaded;
}

/* ---------- 对外入口 ---------- */
void mod_on_file_selected(const char *path) {
    if (!path) return;
    if (g_last_zip) free(g_last_zip);
    g_last_zip = strdup(path);
    LOGD("File selected: %s", path);

    if (mod_check_extension(path) != MOD_OK) return;
    if (mod_check_root_clean(path) != MOD_OK) return;

    /* 解压到私有目录 files/mods/<stem>/ */
    char dest[1024] = "/data/data/PLACEHOLDER/files/mods/last";
    /* 实际使用 Java 侧 getFilesDir() 传入；此处仅示意 */
    extract_root_entries(path, dest);
    int n = scan_and_load(dest);
    if (n > 0) {
        g_loaded += n;
        java_notify("Mod 已加载", "成功加载 Mod");
    } else {
        java_notify("Mod 加载提示", "未找到有效的 .dll 或 .so（.dll 需 HybridCLR）");
    }
}

void mod_init(JavaVM *vm, void *mod_host) {
    g_vm       = vm;
    g_mod_host = mod_host;  /* 调用方应传 NewGlobalRef */
    g_loaded   = 0;
    LOGD("ADOFAI Mod Loader initialized");
}

int mod_get_loaded_count(void) { return g_loaded; }
