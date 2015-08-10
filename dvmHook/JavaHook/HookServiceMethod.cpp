#include <android_runtime/AndroidRuntime.h>

#include "JavaMethodHook.h"
#include "common.h"
#include "dvm_func.h"

using android::AndroidRuntime;

#ifdef DEBUG
#define STATIC
#else
#define STATIC static
#endif

extern STATIC int calcMethodArgsSize(const char* shorty);
extern STATIC u4 dvmPlatformInvokeHints(const char* shorty);
extern STATIC int dvmComputeJniArgInfo(const char* shorty);
extern STATIC jclass dvmFindJNIClass(JNIEnv *env, const char *classDesc);
extern STATIC ClassObject* dvmFindClass(const char *classDesc);
extern STATIC ArrayObject* dvmBoxMethodArgs(const Method* method, const u4* args);
extern STATIC ArrayObject* dvmGetMethodParamTypes(const Method* method, const char* methodsig);

jclass classGlobalService = NULL;
jmethodID methodGlobalService = NULL;
static void init_global_class_and_method_service(JavaMethodInfo *info) {
    JNIEnv* env = android::AndroidRuntime::getJNIEnv();

    jstring dexpath = env->NewStringUTF(info->jarPath);
    jstring dex_odex_path = env->NewStringUTF("/data/dalvik-cache/");
    jstring javaClassName = env->NewStringUTF(info->classDesc);
    const char* func = info->methodName;
    const char* funcSig = info->methodSig;

    //找到ClassLoader类
    jclass classloaderClass = env->FindClass("java/lang/ClassLoader");
    if (NULL == classloaderClass) {
        env->DeleteLocalRef(dexpath);
        env->DeleteLocalRef(dex_odex_path);
        env->DeleteLocalRef(javaClassName);
        return;
    }
    jmethodID getsysloaderMethod = env->GetStaticMethodID(classloaderClass, "getSystemClassLoader",
            "()Ljava/lang/ClassLoader;");
    if (NULL == getsysloaderMethod) {
        env->DeleteLocalRef(dexpath);
        env->DeleteLocalRef(dex_odex_path);
        env->DeleteLocalRef(javaClassName);
        return;
    }

    jobject loader = env->CallStaticObjectMethod(classloaderClass, getsysloaderMethod);
    if (NULL == loader) {
        env->DeleteLocalRef(dexpath);
        env->DeleteLocalRef(dex_odex_path);
        env->DeleteLocalRef(javaClassName);
        return;
    }
    //找到DexClassLoader类
    jclass dexLoaderClass = env->FindClass("dalvik/system/DexClassLoader");
    if (NULL == dexLoaderClass) {
        env->DeleteLocalRef(dexpath);
        env->DeleteLocalRef(dex_odex_path);
        env->DeleteLocalRef(javaClassName);
        return;
    }

    jmethodID initDexLoaderMethod = env->GetMethodID(dexLoaderClass, "<init>",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    if (NULL == initDexLoaderMethod) {
        env->DeleteLocalRef(dexpath);
        env->DeleteLocalRef(dex_odex_path);
        env->DeleteLocalRef(javaClassName);
        return;
    }

    //新建一个DexClassLoader对象
    jobject dexLoader = env->NewObject(dexLoaderClass, initDexLoaderMethod, dexpath, dex_odex_path,
            NULL, loader);
    if (NULL == dexLoader) {
        env->DeleteLocalRef(dexpath);
        env->DeleteLocalRef(dex_odex_path);
        env->DeleteLocalRef(javaClassName);
        return;
    }
    //找到DexClassLoader中的方法findClass
    jmethodID findclassMethod = env->GetMethodID(dexLoaderClass, "findClass",
            "(Ljava/lang/String;)Ljava/lang/Class;");
    //说明：老版本的SDK中DexClassLoader有findClass方法，新版本SDK中是loadClass方法
    if (NULL == findclassMethod) {
        findclassMethod = env->GetMethodID(dexLoaderClass, "loadClass",
                "(Ljava/lang/String;)Ljava/lang/Class;");
    }
    if (NULL == findclassMethod) {
        env->DeleteLocalRef(dexpath);
        env->DeleteLocalRef(dex_odex_path);
        env->DeleteLocalRef(javaClassName);
        return;
    }

    //调用DexClassLoader的loadClass方法，加载需要调用的类
    jclass javaClientClass = (jclass) env->CallObjectMethod(dexLoader, findclassMethod,
            javaClassName);
    if (NULL == javaClientClass) {
        env->DeleteLocalRef(dexpath);
        env->DeleteLocalRef(dex_odex_path);
        env->DeleteLocalRef(javaClassName);
        return;
    }

    //获取加载的类中的方法
    jmethodID inject_method = env->GetStaticMethodID(javaClientClass, func, funcSig);
    if (NULL == inject_method) {
        env->DeleteLocalRef(dexpath);
        env->DeleteLocalRef(dex_odex_path);
        env->DeleteLocalRef(javaClassName);
        return;
    }
    //调用加载的类中的静态方法
    classGlobalService = env->NewGlobalRef(javaClientClass);
    methodGlobalService = inject_method; //env->NewGlobalRef(inject_method);

    env->DeleteLocalRef(dexpath);
    env->DeleteLocalRef(dex_odex_path);
    env->DeleteLocalRef(javaClassName);
}

static void dalvik_invoke_java_static_method_service(JavaMethodInfo *info) {
    JNIEnv* env = android::AndroidRuntime::getJNIEnv();
    LOGI( "skywalker  dalvik_invoke_java_static_method enter");
    if (NULL == classGlobalService || NULL == methodGlobalService) {
        LOGI( "skywalker  classGlobalService is null or methodGlobalService is null, init...");
        init_global_class_and_method_service(info);
    }
    if (NULL == classGlobalService || NULL == methodGlobalService) {
        LOGI( "skywalker  classGlobalService is null or methodGlobalService is null, finish...");
        return;
    }
    //调用加载的类中的静态方法
    //env->CallStaticObjectMethod(classGlobalService, methodGlobalService, objIntent);
}

void callTrojanJavaStaticMethod_service() {
    JavaMethodInfo *info = (JavaMethodInfo *) malloc(sizeof(JavaMethodInfo));

    if (NULL == info) {
        return;
    }
    info->jarPath = "/data/system/InjectHookTrojan.apk";
    info->classDesc = "com/futureagent/injecthooktrojan/HookUtils";
    info->methodName = "hookServiceMethod";
    info->methodSig = "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;";
    //info->methodSig = "([Ljava/lang/Object;)Ljava/lang/Object;";
    //info->methodSig = "(Ljava/lang/Object;)Ljava/lang/Object;";
    info->isStaticMethod = true;

    dalvik_invoke_java_static_method_service(info);
    if (NULL != info) {
        free(info);
    }
}

STATIC void method_proxy_service(const u4* args, JValue* pResult, const Method* method,
        struct Thread* self) {

    HookInfo* info = (HookInfo*) method->insns;
    LOGI("skywalker  method_proxy_service:%s->%s", info->classDesc, info->methodName);

    Method* originalMethod = reinterpret_cast<Method*>(info->originalMethod);
    Object* thisObject = !info->isStaticMethod ? (Object*) args[0] : NULL;

    ArrayObject* argList = dvmBoxMethodArgs(originalMethod, info->isStaticMethod ? args : args + 1);
    pResult->l = (void *) dvmInvokeMethod(thisObject, originalMethod, argList,
            (ArrayObject *) info->paramTypes, (ClassObject *) info->returnType, true);
    if (strcmp(info->methodName, "retrieveServiceLocked") == 0) {
        if (NULL == classGlobalService || NULL == methodGlobalService) {
            LOGI( "skywalker  classGlobalService is null or methodGlobalService is null, init...");
            callTrojanJavaStaticMethod_service();
        }
        if (NULL != classGlobalService && NULL != methodGlobalService) {
            LOGI( "skywalker  method_proxy_service call service hook method");
            JValue result;
            dvmCallMethod(self, (Method*) methodGlobalService, NULL, &result, (Object*) pResult->l,
                    argList);
            //void dvmCallMethod(Thread* self, const Method* method, Object* obj, JValue* pResult,...);
            pResult->l = result.l;
        } else {
            LOGI(
                    "skywalker  classGlobalService is null or methodGlobalService is null, finish...");
        }
    }
    dvmReleaseTrackedAlloc((Object *) argList, self);
}

extern int __attribute__ ((visibility ("hidden"))) dalvik_method_hook_service(JNIEnv* env,
        HookInfo *info) {
    const char* classDesc = info->classDesc;
    const char* methodName = info->methodName;
    const char* methodSig = info->methodSig;
    const bool isStaticMethod = info->isStaticMethod;
    jclass classObj = dvmFindJNIClass(env, classDesc);
    if (classObj == NULL) {
        LOGE("skywalker [-] %s class not found", classDesc);
        return -1;
    }
    jmethodID methodId =
            isStaticMethod ?
                    env->GetStaticMethodID(classObj, methodName, methodSig) :
                    env->GetMethodID(classObj, methodName, methodSig);

    if (methodId == NULL) {
        LOGE("skywalker [-] %s->%s method not found", classDesc, methodName);
        return -1;
    }
    // backup method
    Method* method = (Method*) methodId;
    if (method->nativeFunc == method_proxy_service) {
        LOGW( "skywalker  [*] %s->%s method had been hooked", classDesc, methodName);
        return -1;
    }
    Method* bakMethod = (Method*) malloc(sizeof(Method));
    memcpy(bakMethod, method, sizeof(Method));

    // init info
    info->originalMethod = (void *) bakMethod;
    info->returnType = (void *) dvmGetBoxedReturnType(bakMethod);
    info->paramTypes = dvmGetMethodParamTypes(bakMethod, info->methodSig);
    // hook method
    int argsSize = calcMethodArgsSize(method->shorty);
    if (!dvmIsStaticMethod(method))
        argsSize++;

    SET_METHOD_FLAG(method, ACC_NATIVE);
    method->registersSize = method->insSize = argsSize;
    method->outsSize = 0;
    method->jniArgInfo = dvmComputeJniArgInfo(method->shorty);
    // save info to insns
    method->insns = (u2*) info;

    // bind the bridge func，only one line
    method->nativeFunc = method_proxy_service;
    LOGI("skywalker  [+] %s->%s was hooked\n", classDesc, methodName);

    return 0;
}
