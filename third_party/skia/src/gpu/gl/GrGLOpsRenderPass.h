/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrGLOpsRenderPass_DEFINED
#define GrGLOpsRenderPass_DEFINED

#include "src/gpu/GrOpsRenderPass.h"

#include "src/gpu/GrOpFlushState.h"
#include "src/gpu/gl/GrGLGpu.h"
#include "src/gpu/gl/GrGLRenderTarget.h"

class GrGLGpu;
class GrGLRenderTarget;

class GrGLOpsRenderPass : public GrOpsRenderPass {
/**
 * We do not actually buffer up draws or do any work in the this class for GL. Instead commands
 * are immediately sent to the gpu to execute. Thus all the commands in this class are simply
 * pass through functions to corresponding calls in the GrGLGpu class.
 */
public:
    GrGLOpsRenderPass(GrGLGpu* gpu) : fGpu(gpu) {}

    void begin() override {
        fGpu->beginCommandBuffer(fRenderTarget, fContentBounds, fOrigin, fColorLoadAndStoreInfo,
                                 fStencilLoadAndStoreInfo);
    }

    void end() override {
        fGpu->endCommandBuffer(fRenderTarget, fColorLoadAndStoreInfo, fStencilLoadAndStoreInfo);
    }

    void inlineUpload(GrOpFlushState* state, GrDeferredTextureUploadFn& upload) override {
        state->doUpload(upload);
    }

    void set(GrRenderTarget*, const SkIRect& contentBounds, GrSurfaceOrigin,
             const LoadAndStoreInfo&, const StencilLoadAndStoreInfo&);

    void reset() {
        fRenderTarget = nullptr;
    }

private:
    GrGpu* gpu() override { return fGpu; }

    bool onBindPipeline(const GrProgramInfo& programInfo, const SkRect& drawBounds) override {
        return fGpu->flushGLState(fRenderTarget, programInfo);
    }

    void onSetScissorRect(const SkIRect& scissor) override {
        fGpu->flushScissorRect(scissor, fRenderTarget->width(), fRenderTarget->height(), fOrigin);
    }

    bool onBindTextures(const GrPrimitiveProcessor& primProc, const GrPipeline& pipeline,
                        const GrSurfaceProxy* const primProcTextures[]) override {
        fGpu->bindTextures(primProc, pipeline, primProcTextures);
        return true;
    }

    void onDrawMesh(GrPrimitiveType primitiveType, const GrMesh& mesh) override {
        fGpu->drawMesh(fRenderTarget, primitiveType, mesh);
    }

    void onClear(const GrFixedClip& clip, const SkPMColor4f& color) override {
        fGpu->clear(clip, color, fRenderTarget, fOrigin);
    }

    void onClearStencilClip(const GrFixedClip& clip, bool insideStencilMask) override {
        fGpu->clearStencilClip(clip, insideStencilMask, fRenderTarget, fOrigin);
    }

    GrGLGpu*                fGpu;
    SkIRect                 fContentBounds;
    LoadAndStoreInfo        fColorLoadAndStoreInfo;
    StencilLoadAndStoreInfo fStencilLoadAndStoreInfo;

    typedef GrOpsRenderPass INHERITED;
};

#endif

