/*
 * 桩 jni.h：主机测试用，仅提供类型与常量，函数表用 no-op 桩。
 * Android NDK 编译时使用真正的 <jni.h>，本文件不会被包含。
 */
#ifndef _JNI_H_STUB
#define _JNI_H_STUB

#include <stddef.h>

/* 不透明类型（实际定义由 JVM/ART 提供，测试无需真实实现） */
typedef struct _JNIEnv      JNIEnv;
typedef struct _JavaVM      JavaVM;
typedef struct _jobject     jobject;
typedef struct _jclass      jclass;
typedef struct _jmethodID   jmethodID;
typedef int                 jint;
typedef int                 jboolean;

#define JNI_VERSION_1_6 0x00010006
#define JNI_OK          0
#define JNI_FALSE       0
#define JNI_TRUE        1

/*
 * 桩函数表：测试时 g_vm/g_env 可为 NULL，所有调用安全 no-op。
 * 这里用宏把 (*env)->Foo(env, ...) 展开为 ((void)0)，避免链接真实 JVM。
 */
#define JNI_CALL(...)  ((void)0)

/* 让 "(*env)->Method(env, ...)" 这种写法在测试里编译通过（展开为空） */
typedef struct {
    void *unused;
} JNINativeInterface;

struct _JNIEnv { const JNINativeInterface *funcs; };
struct _JavaVM  { const void             *funcs; };

#endif
