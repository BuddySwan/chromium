/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dawn/webgpu_cpp.h"
#include "dawn_native/DawnNative.h"
#include "tools/gpu/dawn/DawnTestContext.h"

#ifdef SK_BUILD_FOR_UNIX
#include "GL/glx.h"
#endif

#ifdef SK_BUILD_FOR_WIN
#include <windows.h>
#endif

#define USE_OPENGL_BACKEND 0

#ifdef SK_DAWN
#include "dawn/webgpu.h"
#include "dawn/dawn_proc.h"
#include "include/gpu/GrContext.h"
#include "tools/AutoreleasePool.h"
#if USE_OPENGL_BACKEND
#include "dawn_native/OpenGLBackend.h"
#endif

#if defined(SK_BUILD_FOR_MAC) && USE_OPENGL_BACKEND
#include <dlfcn.h>
static void* getProcAddressMacOS(const char* procName) {
    return dlsym(RTLD_DEFAULT, procName);
}
#endif

namespace {

#ifdef SK_BUILD_FOR_WIN
class ProcGetter {
public:
    typedef void(*Proc)();

    ProcGetter()
      : fModule(LoadLibraryA("opengl32.dll")) {
        SkASSERT(!fInstance);
        fInstance = this;
    }

    ~ProcGetter() {
        if (fModule) {
            FreeLibrary(fModule);
        }
        fInstance = nullptr;
    }

    static void* getProcAddress(const char* name) {
        return fInstance->getProc(name);
    }

private:
    Proc getProc(const char* name) {
        PROC proc;
        if ((proc = GetProcAddress(fModule, name))) {
            return (Proc) proc;
        }
        if ((proc = wglGetProcAddress(name))) {
            return (Proc) proc;
        }
        return nullptr;
    }

    HMODULE fModule;
    static ProcGetter* fInstance;
};

ProcGetter* ProcGetter::fInstance;
#endif

class DawnTestContextImpl : public sk_gpu_test::DawnTestContext {
public:
    static wgpu::Device createDevice(const dawn_native::Instance& instance,
                                     dawn_native::BackendType type) {
        DawnProcTable backendProcs = dawn_native::GetProcs();
        dawnProcSetProcs(&backendProcs);

        std::vector<dawn_native::Adapter> adapters = instance.GetAdapters();
        for (dawn_native::Adapter adapter : adapters) {
            if (adapter.GetBackendType() == type) {
                return adapter.CreateDevice();
            }
        }
        return nullptr;
    }

    static DawnTestContext* Create(DawnTestContext* sharedContext) {
        std::unique_ptr<dawn_native::Instance> instance = std::make_unique<dawn_native::Instance>();
        wgpu::Device device;
        if (sharedContext) {
            device = sharedContext->getDevice();
        } else {
            dawn_native::BackendType type;
#if USE_OPENGL_BACKEND
            dawn_native::opengl::AdapterDiscoveryOptions adapterOptions;
            adapterOptions.getProc = reinterpret_cast<void*(*)(const char*)>(
#if defined(SK_BUILD_FOR_UNIX)
                glXGetProcAddress
#elif defined(SK_BUILD_FOR_MAC)
                getProcAddressMacOS
#elif defined(SK_BUILD_FOR_WIN)
                ProcGetter::getProcAddress
#endif
            );
            instance->DiscoverAdapters(&adapterOptions);
            type = dawn_native::BackendType::OpenGL;
#else
            instance->DiscoverDefaultAdapters();
#if defined(SK_BUILD_FOR_MAC)
            type = dawn_native::BackendType::Metal;
#elif defined(SK_BUILD_FOR_WIN)
            type = dawn_native::BackendType::D3D12;
#elif defined(SK_BUILD_FOR_UNIX)
            type = dawn_native::BackendType::Vulkan;
#endif
#endif
            device = createDevice(*instance, type);
        }
        if (!device) {
            return nullptr;
        }
        return new DawnTestContextImpl(std::move(instance), device);
    }

    ~DawnTestContextImpl() override { this->teardown(); }

    void testAbandon() override {}

    void finish() override {}

    sk_sp<GrContext> makeGrContext(const GrContextOptions& options) override {
        return GrContext::MakeDawn(fDevice, options);
    }

protected:
    void teardown() override {
        INHERITED::teardown();
    }

private:
    DawnTestContextImpl(std::unique_ptr<dawn_native::Instance> instance,
                        const wgpu::Device& device)
            : DawnTestContext(device)
            , fInstance(std::move(instance)) {
        fFenceSupport = true;
    }

    void onPlatformMakeNotCurrent() const override {}
    void onPlatformMakeCurrent() const override {}
    std::function<void()> onPlatformGetAutoContextRestore() const override  { return nullptr; }
    void onPlatformSwapBuffers() const override {}
    std::unique_ptr<dawn_native::Instance> fInstance;

    typedef sk_gpu_test::DawnTestContext INHERITED;
};
}  // anonymous namespace

namespace sk_gpu_test {
DawnTestContext* CreatePlatformDawnTestContext(DawnTestContext* sharedContext) {
    return DawnTestContextImpl::Create(sharedContext);
}
}  // namespace sk_gpu_test

#endif
