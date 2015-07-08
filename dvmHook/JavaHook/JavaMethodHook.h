/*
 * JavaMethodHook.h
 *
 *  Created on: 2014-9-18
 *      Author: boyliang
 */

#ifndef __JAVA_METHOD_HOOK__H__
#define __JAVA_METHOD_HOOK__H__

#include <jni.h>
#include <stddef.h>
#include <elf.h>

#define RETURN_NULL_IF(cond, ...) \
    if (CONDITION(cond)) { \
        LOGE(__VA_ARGS__); \
        return NULL; }

struct HookInfo {
    char *classDesc;
    char *methodName;
    char *methodSig;

    // for dalvik jvm
    bool isStaticMethod;
    void *originalMethod;
    void *returnType;
    void *paramTypes;

    // for art jvm
    const void *nativecode;
    const void *entrypoint;
};

struct JavaMethodInfo {
    char *jarPath;
    char *classDesc;
    char *methodName;
    char *methodSig;
    bool isStaticMethod;
};

int java_method_hook(JNIEnv* env, HookInfo *info);
void invoke_java_method(JNIEnv* env, JavaMethodInfo *info);

#endif //end of __JAVA_METHOD_HOOK__H__
