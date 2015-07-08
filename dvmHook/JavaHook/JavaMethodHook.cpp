#include <cutils/properties.h>

#include "common.h"
#include "JavaMethodHook.h"

extern int dalvik_java_method_hook(JNIEnv*, HookInfo *);
extern int dalvik_invoke_java_method(JNIEnv*, JavaMethodInfo *);

static bool isArt() {
    char value[PROPERTY_VALUE_MAX];
    property_get("persist.sys.dalvik.vm.lib", value, "");
    LOGI("skywalker  [+] persist.sys.dalvik.vm.lib = %s", value);
    return strncmp(value, "libart.so", strlen("libart.so")) == 0;
}

int java_method_hook(JNIEnv* env, HookInfo *info) {
    LOGI("skywalker java_method_hook enter");
    if (!isArt()) {
        return dalvik_java_method_hook(env, info);
    }
    return -1;
}

void invoke_java_method(JNIEnv* env, JavaMethodInfo *info) {
    if (!isArt()) {
        dalvik_invoke_java_method(env, info);
    }
}

