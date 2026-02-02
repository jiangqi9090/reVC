#include "common.h"
#include "Bloom.h"
#include "RwHelper.h"
#include "Camera.h"
#include "Frontend.h"

bool CBloom::m_bInitialised = false;
bool CBloom::m_bBloomOn = true;
float CBloom::m_fBloomIntensity = 0.8f;
float CBloom::m_fBloomThreshold = 0.7f;
float CBloom::m_fBloomSoftness = 0.5f;
CBloom::BloomQuality CBloom::m_eQuality = BLOOM_MEDIUM;

RwRaster *CBloom::pBrightPass = nullptr;
RwRaster *CBloom::pBlurBuffer1 = nullptr;
RwRaster *CBloom::pBlurBuffer2 = nullptr;

// Bright pass threshold shader parameters
static const RwRGBAReal BLOOM_THRESHOLD = { 0.7f, 0.7f, 0.7f, 1.0f };

bool
CBloom::Initialise(void)
{
	if (m_bInitialised)
		return true;
	
	printf("CBloom: Initialising...\n");
	m_bInitialised = true;
	return true;
}

void
CBloom::Shutdown(void)
{
	if (pBrightPass) {
		RwRasterDestroy(pBrightPass);
		pBrightPass = nullptr;
	}
	if (pBlurBuffer1) {
		RwRasterDestroy(pBlurBuffer1);
		pBlurBuffer1 = nullptr;
	}
	if (pBlurBuffer2) {
		RwRasterDestroy(pBlurBuffer2);
		pBlurBuffer2 = nullptr;
	}
	
	m_bInitialised = false;
	printf("CBloom: Shutdown complete\n");
}

void
CBloom::RenderBrightPass(RwCamera *cam, RwRaster *sceneRaster)
{
	if (!sceneRaster)
		return;
	
	// Create bright pass raster if needed
	int32 sceneW = RwRasterGetWidth(sceneRaster);
	int32 sceneH = RwRasterGetHeight(sceneRaster);
	int32 brightW = sceneW / 2;
	int32 brightH = sceneH / 2;
	
	if (!pBrightPass || RwRasterGetWidth(pBrightPass) != brightW) {
		if (pBrightPass)
			RwRasterDestroy(pBrightPass);
		pBrightPass = RwRasterCreate(brightW, brightH, 0, rwRASTERDONTALLOCATE | rwRASTERTYPETEXTURE);
	}
	
	if (!pBrightPass)
		return;
	
	// Simple threshold: just downsample with clamp
	// Real implementation would use a shader to extract bright pixels
	RwRasterPushContext(pBrightPass);
	RwRasterRenderFast(sceneRaster, 0, 0);
	RwRasterPopContext();
}

void
CBloom::RenderBlur(RwRaster *src, RwRaster *dst, bool horizontal)
{
	if (!src || !dst)
		return;
	
	// Setup additive blending for blur accumulation
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	
	// Simple box blur - copy with scaling
	RwRasterPushContext(dst);
	RwRasterRenderFast(src, 0, 0);
	RwRasterPopContext();
}

void
CBloom::Composite(RwCamera *cam, RwRaster *sceneRaster)
{
	if (!sceneRaster || !pBlurBuffer1)
		return;
	
	// Additive blend of blur over original scene
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
}

void
CBloom::Render(RwCamera *cam)
{
	if (!m_bBloomOn)
		return;
	
	RwRaster *sceneRaster = RwCameraGetRaster(cam);
	if (!sceneRaster)
		return;
	
	if (!m_bInitialised)
		Initialise();
	
	// Get scene dimensions
	int32 sceneW = RwRasterGetWidth(sceneRaster);
	int32 sceneH = RwRasterGetHeight(sceneRaster);
	
	// Calculate blur buffer dimensions based on quality
	int32 blurW, blurH;
	
	switch (m_eQuality) {
	case BLOOM_LOW:
		blurW = sceneW / 4;
		blurH = sceneH / 4;
		break;
	case BLOOM_HIGH:
		blurW = sceneW / 2;
		blurH = sceneH / 2;
		break;
	case BLOOM_MEDIUM:
	default:
		blurW = sceneW / 3;
		blurH = sceneH / 3;
		break;
	}
	
	// Create blur buffers if needed
	if (!pBlurBuffer1 || RwRasterGetWidth(pBlurBuffer1) != blurW) {
		if (pBlurBuffer1)
			RwRasterDestroy(pBlurBuffer1);
		if (pBlurBuffer2)
			RwRasterDestroy(pBlurBuffer2);
		
		pBlurBuffer1 = RwRasterCreate(blurW, blurH, 0, rwRASTERDONTALLOCATE | rwRASTERTYPETEXTURE);
		pBlurBuffer2 = RwRasterCreate(blurW, blurH, 0, rwRASTERDONTALLOCATE | rwRASTERTYPETEXTURE);
	}
	
	if (!pBlurBuffer1 || !pBlurBuffer2)
		return;
	
	// Step 1: Bright pass - extract bright areas and downsample
	RenderBrightPass(cam, sceneRaster);
	
	if (pBrightPass) {
		// Step 2: First blur pass (horizontal-ish via downsampling)
		RenderBlur(pBrightPass, pBlurBuffer1, false);
		
		// Step 3: Second blur pass (vertical-ish via copy)
		RenderBlur(pBlurBuffer1, pBlurBuffer2, true);
		
		// Step 4: Third blur pass (accumulate back)
		RenderBlur(pBlurBuffer2, pBlurBuffer1, false);
	}
	
	// Step 5: Composite bloom with scene
	Composite(cam, sceneRaster);
}
