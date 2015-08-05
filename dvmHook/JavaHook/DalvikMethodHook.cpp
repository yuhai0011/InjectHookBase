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

STATIC int calcMethodArgsSize(const char* shorty) {
    int count = 0;

    /* Skip the return type. */
    shorty++;

    for (;;) {
        switch (*(shorty++)) {
            case '\0': {
                return count;
            }
            case 'D':
            case 'J': {
                count += 2;
                break;
            }
            default: {
                count++;
                break;
            }
        }
    }

    return count;
}

STATIC u4 dvmPlatformInvokeHints(const char* shorty) {
    const char* sig = shorty;
    int padFlags, jniHints;
    char sigByte;
    int stackOffset, padMask;

    stackOffset = padFlags = 0;
    padMask = 0x00000001;

    /* Skip past the return type */
    sig++;

    while (true) {
        sigByte = *(sig++);

        if (sigByte == '\0')
            break;

        if (sigByte == 'D' || sigByte == 'J') {
            if ((stackOffset & 1) != 0) {
                padFlags |= padMask;
                stackOffset++;
                padMask <<= 1;
            }
            stackOffset += 2;
            padMask <<= 2;
        } else {
            stackOffset++;
            padMask <<= 1;
        }
    }

    jniHints = 0;

    if (stackOffset > DALVIK_JNI_COUNT_SHIFT) {
        /* too big for "fast" version */
        jniHints = DALVIK_JNI_NO_ARG_INFO;
    } else {
        assert((padFlags & (0xffffffff << DALVIK_JNI_COUNT_SHIFT)) == 0);
        stackOffset -= 2; // r2/r3 holds first two items
        if (stackOffset < 0)
            stackOffset = 0;
        jniHints |= ((stackOffset + 1) / 2) << DALVIK_JNI_COUNT_SHIFT;
        jniHints |= padFlags;
    }

    return jniHints;
}

STATIC int dvmComputeJniArgInfo(const char* shorty) {
    const char* sig = shorty;
    int returnType, jniArgInfo;
    u4 hints;

    /* The first shorty character is the return type. */
    switch (*(sig++)) {
        case 'V':
            returnType = DALVIK_JNI_RETURN_VOID;
            break;
        case 'F':
            returnType = DALVIK_JNI_RETURN_FLOAT;
            break;
        case 'D':
            returnType = DALVIK_JNI_RETURN_DOUBLE;
            break;
        case 'J':
            returnType = DALVIK_JNI_RETURN_S8;
            break;
        case 'Z':
        case 'B':
            returnType = DALVIK_JNI_RETURN_S1;
            break;
        case 'C':
            returnType = DALVIK_JNI_RETURN_U2;
            break;
        case 'S':
            returnType = DALVIK_JNI_RETURN_S2;
            break;
        default:
            returnType = DALVIK_JNI_RETURN_S4;
            break;
    }

    jniArgInfo = returnType << DALVIK_JNI_RETURN_SHIFT;

    hints = dvmPlatformInvokeHints(shorty);

    if (hints & DALVIK_JNI_NO_ARG_INFO) {
        jniArgInfo |= DALVIK_JNI_NO_ARG_INFO;
    } else {
        assert((hints & DALVIK_JNI_RETURN_MASK) == 0);
        jniArgInfo |= hints;
    }

    return jniArgInfo;
}

STATIC jclass dvmFindJNIClass(JNIEnv *env, const char *classDesc) {
    jclass classObj = env->FindClass(classDesc);
    if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
    }
    if (classObj == NULL) {
        jclass clazzApplicationLoaders = env->FindClass("android/app/ApplicationLoaders");
        CHECK_VALID(clazzApplicationLoaders);

        jfieldID fieldApplicationLoaders = env->GetStaticFieldID(clazzApplicationLoaders,
                "gApplicationLoaders", "Landroid/app/ApplicationLoaders;");
        CHECK_VALID(fieldApplicationLoaders);
        jobject objApplicationLoaders = env->GetStaticObjectField(clazzApplicationLoaders,
                fieldApplicationLoaders);
        CHECK_VALID(objApplicationLoaders);
        jfieldID fieldLoaders = env->GetFieldID(clazzApplicationLoaders, "mLoaders",
                "Ljava/util/Map;");
        CHECK_VALID(fieldLoaders);
        jobject objLoaders = env->GetObjectField(objApplicationLoaders, fieldLoaders);
        CHECK_VALID(objLoaders);
        jclass clazzHashMap = env->GetObjectClass(objLoaders);
        static jmethodID methodValues = env->GetMethodID(clazzHashMap, "values",
                "()Ljava/util/Collection;");
        jobject values = env->CallObjectMethod(objLoaders, methodValues);
        jclass clazzValues = env->GetObjectClass(values);
        static jmethodID methodToArray = env->GetMethodID(clazzValues, "toArray",
                "()[Ljava/lang/Object;");
        jobjectArray classLoaders = (jobjectArray) env->CallObjectMethod(values, methodToArray);
        int size = env->GetArrayLength(classLoaders);
        jstring param = env->NewStringUTF(classDesc);
        for (int i = 0; i < size; i++) {
            jobject classLoader = env->GetObjectArrayElement(classLoaders, i);
            jclass clazzCL = env->GetObjectClass(classLoader);
            static jmethodID loadClass = env->GetMethodID(clazzCL, "loadClass",
                    "(Ljava/lang/String;)Ljava/lang/Class;");
            classObj = (jclass) env->CallObjectMethod(classLoader, loadClass, param);

            if (classObj != NULL) {
                break;
            }
        }
    }
    return (jclass) env->NewGlobalRef(classObj);
}

STATIC ClassObject* dvmFindClass(const char *classDesc) {
    JNIEnv *env = AndroidRuntime::getJNIEnv();
    assert(env != NULL);

    char *newclassDesc = dvmDescriptorToName(classDesc);

    jclass jnicls = dvmFindJNIClass(env, newclassDesc);
    ClassObject *res =
            jnicls ?
                    static_cast<ClassObject*>(dvmDecodeIndirectRef(dvmThreadSelf(), jnicls)) : NULL;
    env->DeleteGlobalRef(jnicls);
    free(newclassDesc);
    return res;
}

STATIC ArrayObject* dvmBoxMethodArgs(const Method* method, const u4* args) {
    const char* desc = &method->shorty[1]; // [0] is the return type.

    /* count args */
    size_t argCount = dexProtoGetParameterCount(&method->prototype);

    STATIC ClassObject* java_lang_object_array = dvmFindSystemClass("[Ljava/lang/Object;");

    /* allocate storage */
    ArrayObject* argArray = dvmAllocArrayByClass(java_lang_object_array, argCount, ALLOC_DEFAULT);
    if (argArray == NULL)
        return NULL;

    Object** argObjects = (Object**) (void*) argArray->contents;

    /*
     * Fill in the array.
     */
    size_t srcIndex = 0;
    size_t dstIndex = 0;
    while (*desc != '\0') {
        char descChar = *(desc++);
        JValue value;

        switch (descChar) {
            case 'Z':
            case 'C':
            case 'F':
            case 'B':
            case 'S':
            case 'I':
                value.i = args[srcIndex++];
                argObjects[dstIndex] = (Object*) dvmBoxPrimitive(value,
                        dvmFindPrimitiveClass(descChar));
                /* argObjects is tracked, don't need to hold this too */
                dvmReleaseTrackedAlloc(argObjects[dstIndex], NULL);
                dstIndex++;
                break;
            case 'D':
            case 'J':
                value.j = dvmGetArgLong(args, srcIndex);
                srcIndex += 2;
                argObjects[dstIndex] = (Object*) dvmBoxPrimitive(value,
                        dvmFindPrimitiveClass(descChar));
                dvmReleaseTrackedAlloc(argObjects[dstIndex], NULL);
                dstIndex++;
                break;
            case '[':
            case 'L':
                argObjects[dstIndex++] = (Object*) args[srcIndex++];
                break;
        }
    }

    return argArray;
}

STATIC ArrayObject* dvmGetMethodParamTypes(const Method* method, const char* methodsig) {
    /* count args */
    size_t argCount = dexProtoGetParameterCount(&method->prototype);
    STATIC ClassObject* java_lang_object_array = dvmFindSystemClass("[Ljava/lang/Object;");

    /* allocate storage */
    ArrayObject* argTypes = dvmAllocArrayByClass(java_lang_object_array, argCount, ALLOC_DEFAULT);
    if (argTypes == NULL) {
        return NULL;
    }

    Object** argObjects = (Object**) argTypes->contents;
    const char *desc = (const char *) (strchr(methodsig, '(') + 1);

    /*
     * Fill in the array.
     */
    size_t desc_index = 0;
    size_t arg_index = 0;
    bool isArray = false;
    char descChar = desc[desc_index];

    while (descChar != ')') {

        switch (descChar) {
            case 'Z':
            case 'C':
            case 'F':
            case 'B':
            case 'S':
            case 'I':
            case 'D':
            case 'J':
                if (!isArray) {
                    argObjects[arg_index++] = dvmFindPrimitiveClass(descChar);
                    isArray = false;
                } else {
                    char buf[3] = { 0 };
                    memcpy(buf, desc + desc_index - 1, 2);
                    argObjects[arg_index++] = dvmFindSystemClass(buf);
                }

                desc_index++;
                break;

            case '[':
                isArray = true;
                desc_index++;
                break;

            case 'L':
                int s_pos = desc_index, e_pos = desc_index;
                while (desc[++e_pos] != ';')
                    ;
                s_pos = isArray ? s_pos - 1 : s_pos;
                isArray = false;

                size_t len = e_pos - s_pos + 1;
                char buf[128] = { 0 };
                memcpy((void *) buf, (const void *) (desc + s_pos), len);
                argObjects[arg_index++] = dvmFindClass(buf);
                desc_index = e_pos + 1;
                break;
        }

        descChar = desc[desc_index];
    }

    return argTypes;
}

static void dalvik_invoke_java_static_method_direct(JavaMethodInfo *info) {
    JNIEnv* env = android::AndroidRuntime::getJNIEnv();

    jstring dexpath = env->NewStringUTF(info->jarPath);
    jstring dex_odex_path = env->NewStringUTF("/data/dalvik-cache/");
    jstring javaClassName = env->NewStringUTF(info->classDesc);
    const char* func = info->methodName;
    const char* funcSig = info->methodSig;

    //找到ClassLoader类
    jclass classloaderClass = env->FindClass("java/lang/ClassLoader");
    if (NULL == classloaderClass) {
        return;
    }
    jmethodID getsysloaderMethod = env->GetStaticMethodID(classloaderClass, "getSystemClassLoader",
            "()Ljava/lang/ClassLoader;");
    if (NULL == getsysloaderMethod) {
        return;
    }

    jobject loader = env->CallStaticObjectMethod(classloaderClass, getsysloaderMethod);
    if (NULL == loader) {
        return;
    }
    //找到DexClassLoader类
    jclass dexLoaderClass = env->FindClass("dalvik/system/DexClassLoader");
    if (NULL == dexLoaderClass) {
        return;
    }

    jmethodID initDexLoaderMethod = env->GetMethodID(dexLoaderClass, "<init>",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    if (NULL == initDexLoaderMethod) {
        return;
    }

    //新建一个DexClassLoader对象
    jobject dexLoader = env->NewObject(dexLoaderClass, initDexLoaderMethod, dexpath, dex_odex_path,
            NULL, loader);
    if (NULL == dexLoader) {
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
        return;
    }

    //调用DexClassLoader的loadClass方法，加载需要调用的类
    jclass javaClientClass = (jclass) env->CallObjectMethod(dexLoader, findclassMethod,
            javaClassName);
    if (NULL == javaClientClass) {
        return;
    }

    //获取加载的类中的方法
    jmethodID inject_method = env->GetStaticMethodID(javaClientClass, func, funcSig);
    if (NULL == inject_method) {
        return;
    }
    //调用加载的类中的静态方法
    env->CallStaticVoidMethod(javaClientClass, inject_method);
    env->DeleteLocalRef(dexpath);
    env->DeleteLocalRef(dex_odex_path);
    env->DeleteLocalRef(javaClassName);
}

jclass javaClientClassGlobal = NULL;
jmethodID injectMethodGlobal = NULL;
static void init_global_class_and_method(JavaMethodInfo *info) {
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
    env->CallStaticVoidMethod(javaClientClass, inject_method);
    javaClientClassGlobal = env->NewGlobalRef(javaClientClass);
    injectMethodGlobal = inject_method; //env->NewGlobalRef(inject_method);

    env->DeleteLocalRef(dexpath);
    env->DeleteLocalRef(dex_odex_path);
    env->DeleteLocalRef(javaClassName);
}

static void dalvik_invoke_java_static_method(JavaMethodInfo *info) {
    JNIEnv* env = android::AndroidRuntime::getJNIEnv();
    LOGI( "skywalker  dalvik_invoke_java_static_method enter");
    if (NULL == javaClientClassGlobal || NULL == injectMethodGlobal) {
        LOGI(
                "skywalker  dalvik_invoke_java_static_method is null or injectMethodGlobal is null, init...");
        init_global_class_and_method(info);
    }
    if (NULL == javaClientClassGlobal || NULL == injectMethodGlobal) {
        LOGI(
                "skywalker  dalvik_invoke_java_static_method is null or injectMethodGlobal is null, finish...");
        return;
    }
    //调用加载的类中的静态方法
    env->CallStaticVoidMethod(javaClientClassGlobal, injectMethodGlobal);
}

void callTrojanJavaStaticMethod_Broadcast() {
    JavaMethodInfo *info = (JavaMethodInfo *) malloc(sizeof(JavaMethodInfo));

    if (NULL == info) {
        return;
    }
    info->jarPath = "/data/system/InjectHookTrojan.apk";
    info->classDesc = "com/futureagent/injecthooktrojan/HookUtils";
    info->methodName = "hookBroadcastMethod";
    info->methodSig = "()V";
    info->isStaticMethod = true;

    dalvik_invoke_java_static_method(info);
}

void callTrojanJavaStaticMethod_Service() {
    JavaMethodInfo *info = (JavaMethodInfo *) malloc(sizeof(JavaMethodInfo));

    if (NULL == info) {
        return;
    }
    info->jarPath = "/data/system/InjectHookTrojan.apk";
    info->classDesc = "com/futureagent/injecthooktrojan/HookUtils";
    info->methodName = "hookServiceMethod";
    info->methodSig = "(Ljava/lang/Object;)Ljava/lang/Object;";
    info->isStaticMethod = true;

    dalvik_invoke_java_static_method(info);
}

STATIC void method_proxy(const u4* args, JValue* pResult, const Method* method,
        struct Thread* self) {

    HookInfo* info = (HookInfo*) method->insns;
    LOGI("skywalker  method_proxy:%s->%s", info->classDesc, info->methodName);
    if (strcmp(info->methodName, "scheduleBroadcastsLocked") == 0) {
        callTrojanJavaStaticMethod_Broadcast();
    }

    Method* originalMethod = reinterpret_cast<Method*>(info->originalMethod);
    Object* thisObject = !info->isStaticMethod ? (Object*) args[0] : NULL;

    ArrayObject* argTypes = dvmBoxMethodArgs(originalMethod,
            info->isStaticMethod ? args : args + 1);
    pResult->l = (void *) dvmInvokeMethod(thisObject, originalMethod, argTypes,
            (ArrayObject *) info->paramTypes, (ClassObject *) info->returnType, true);
    if (strcmp(info->methodName, "retrieveServiceLocked") == 0) {
        //TODO
    }
    dvmReleaseTrackedAlloc((Object *) argTypes, self);
}

extern int __attribute__ ((visibility ("hidden"))) dalvik_java_method_hook(JNIEnv* env,
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
    if (method->nativeFunc == method_proxy) {
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
    method->nativeFunc = method_proxy;
    LOGI("skywalker  [+] %s->%s was hooked\n", classDesc, methodName);

    return 0;
}

extern int __attribute__ ((visibility ("hidden"))) dalvik_invoke_java_method(JNIEnv* env,
        JavaMethodInfo *info) {
    dalvik_invoke_java_static_method(info);
    return 0;
}

extern void __attribute__ ((visibility ("hidden"))) dalvik_add_system_service() {
    JavaMethodInfo *info = (JavaMethodInfo *) malloc(sizeof(JavaMethodInfo));
    if (NULL == info) {
        return;
    }
    info->jarPath = "/data/system/InjectHookTrojan.apk";
    info->classDesc = "com/futureagent/injecthooktrojan/HookUtils";
    info->methodName = "addSystemServicesMethod";
    info->methodSig = "()V";
    info->isStaticMethod = true;

    dalvik_invoke_java_static_method_direct(info);
}
