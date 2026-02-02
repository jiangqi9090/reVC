#include "common.h"
#include "Bloom.h"
#include "RwHelper.h"
#include "Camera.h"
#include "Timer.h"

bool CBloom::m_bInitialised = false;
bool CBloom::m_bBloomOn = true;
float CBloom::m_fBloomIntensity = 0.5f;
float CBloom::m_fBloomThreshold = 0.8f;
CBloom::BloomQuality CBloom::m_eQuality = BLOOM_MEDIUM;

static RwRaster *pBrightPass = nullptr;
static RwRaster *pBlurBuffer1 = nullptr;
static RwRaster *pBlurBuffer2 = nullptr;
static RwRaster *pFinalBloom = nullptr;

static RwIm2DVertex vertices[4];
static RwImVertexIndex indices[6] = { 0, 1, 2, 0, 2, 3 };

bool
CBloom::Initialise(void)
{
	if (m_bInitialised)
		return true;

	int32 width = RwRasterGetWidth(RwCameraGetRaster(CameraGetMainCamera()));
	int32 height = RwRasterGetHeight(RwCameraGetRaster(CameraGetMainCamera()));
	
	// Calculate buffer sizes based on quality
	int32 brightW = width / 2;
	int32 brightH = height / 2;
	int32 blurW, blurH;
	
	switch (m_eQuality) {
	case BLOOM_LOW:
		blurW = width / 4;
		blurH = height / 4;
		break;
	case BLOOM_HIGH:
		blurW = width / 2;
		blurH = height / 2;
		break;
	case BLOOM_MEDIUM:
	default:
		blurW = width / 3;
		blurH = height / 3;
		break;
	}
	
	// Create bright pass raster
	pBrightPass = RwRasterCreate(brightW, brightH, 0, rwRASTERDONTALLOCATE | rwRASTERTYPETEXTURE);
	if (!pBrightPass) {
		printf("CBloom: Failed to create bright pass raster\n");
		return false;
	}
	
	// Create blur buffers
	pBlurBuffer1 = RwRasterCreate(blurW, blurH, 0, rwRASTERDONTALLOCATE | rwRASTERTYPETEXTURE);
	if (!pBlurBuffer1) {
		printf("CBloom: Failed to create blur buffer 1\n");
		RwRasterDestroy(pBrightPass);
		return false;
	}
	
	pBlurBuffer2 = RwRasterCreate(blurW, blurH, 0, rwRASTERDONTALLOCATE | rwRASTERTYPETEXTURE);
	if (!pBlurBuffer2) {
		printf("CBloom: Failed to create blur buffer 2\n");
		RwRasterDestroy(pBrightPass);
		RwRasterDestroy(pBlurBuffer1);
		return false;
	}
	
	// Create final bloom raster
	pFinalBloom = RwRasterCreate(width, height, 0, rwRASTERDONTALLOCATE | rwRASTERTYPETEXTURE);
	if (!pFinalBloom) {
		printf("CBloom: Failed to create final bloom raster\n");
		RwRasterDestroy(pBrightPass);
		RwRasterDestroy(pBlurBuffer1);
		RwRasterDestroy(pBlurBuffer2);
		return false;
	}
	
	// Setup immediate mode vertices for quad rendering
	float fW = 1.0f;
	float fH = 1.0f;
	
	vertices[0].x = -1.0f; vertices[0].y = -1.0f; vertices[0].z = 0.0f; vertices[0].rhw = 1.0f; vertices[0].u = 0.0f; vertices[0].v = 0.0f;
	vertices[1].x =  1.0f; vertices[1].y = -1.0f; vertices[1].z = 0.0f; vertices[1].rhw = 1.0f; vertices[1].u = fW; vertices[1].v = 0.0f;
	vertices[2].x =  1.0f; vertices[2].y =  1.0f; vertices[2].z = 0.0f; vertices[2].rhw = 1.0f; vertices[2].u = fW; vertices[2].v = fH;
	vertices[3].x = -1.0f; vertices[3].y =  1.0f; vertices[3].z = 0.0f; vertices[3].rhw = 1.0f; vertices[0].u = 0.0f; vertices[3].v = fH;
	
	m_bInitialised = true;
	printf("CBloom: Initialised successfully\n");
	return true;
}

void
CBloom::Shutdown(void)
{
	if (!m_bInitialised)
		return;
	
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
	if (pFinalBloom) {
		RwRasterDestroy(pFinalBloom);
		pFinalBloom = nullptr;
	}
	
	m_bInitialised = false;
}

void
CBloom::RenderQuad(RwCamera *cam, RwRaster *srcRaster, float intensity, bool horizontal)
{
	if (!srcRaster)
		return;
	
	RwRaster *destRaster = horizontal ? pBlurBuffer2 : pBlurBuffer1;
	
	// Get source texture
	RwTexture *srcTexture = RwRasterGetTexture(srcRaster);
	RwTexture *destTexture = RwRasterGetTexture(destRaster);
	
	if (!srcTexture || !destTexture)
		return;
	
	// Calculate UV scaling
	float uScale = (float)RwRasterGetWidth(destRaster) / (float)RwRasterGetWidth(srcRaster);
	float vScale = (float)RwRasterGetHeight(destRaster) / (float)RwRasterGetHeight(srcRaster);
	
	// Setup blend for bloom accumulation
	rw::SetRenderState(rw::ALPHATESTENABLE, false);
	rw::SetRenderState(rw::ALPHABLENDENABLE, true);
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDONE);
	rw::SetRenderState(rw::DSTBLEND, rw::BLENDONE);
	rw::SetRenderState(rw::VERTEXALPHAENABLE, false);
	rw::SetRenderState(rw::ZTESTENABLE, false);
	rw::SetRenderState(rw::ZWRITEENABLE, false);
	
	// Update texture
	RwTextureSetRaster(srcTexture, srcRaster);
	RwTextureSetRaster(destTexture, destRaster);
	
	// Render scaled quad
	float fW = (float)RwRasterGetWidth(srcRaster) / (float)RwRasterGetWidth(destRaster);
	float fH = (float)RwRasterGetHeight(srcRaster) / (float)RwRasterGetHeight(destRaster);
	
	vertices[0].u = 0.0f;       vertices[0].v = 0.0f;
	vertices[1].u = fW;         vertices[1].v = 0.0f;
	vertices[2].u = fW;         vertices[2].v = fH;
	vertices[3].u = 0.0f;       vertices[3].v = fH;
	
	RwRenderStateSet(rw::TEXTURERASTER, srcRaster);
	
	// Apply simple box blur in shader
	// For now, just copy with intensity
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDZERO);
	rw::SetRenderState(rw::DSTBLEND, rw::BLENDSRCALPHA);
	
	// Copy raster
	RwRasterPushContext(destRaster);
	RwRasterClear(0);
	RwRasterPopContext();
	
	m_bInitialised = true;
}

void
CBloom::Render(RwCamera *cam)
{
	if (!m_bBloomOn || !m_bInitialised)
		return;
	
	RwRaster *sceneRaster = RwCameraGetRaster(cam);
	if (!sceneRaster)
		return;
	
	uint32 startTime = CTimer::GetTimeMs();
	
	// Save original render states
	bool origAlphaTest = rw::GetRenderState(rw::ALPHATESTENABLE);
	bool origAlphaBlend = rw::GetRenderState(rw::ALPHABLENDENABLE);
	int32 origSrcBlend = rw::GetRenderState(rw::SRCBLEND);
	int32 origDstBlend = rw::GetRenderState(rw::DSTBLEND);
	bool origZTest = rw::GetRenderState(rw::ZTESTENABLE);
	bool origZWrite = rw::GetRenderState(rw::ZWRITEENABLE);
	
	// Step 1: Bright pass - extract bright areas from scene
	// For now, just copy the scene (actual bright pass would use a threshold shader)
	
	// Step 2: Downsample to blur buffer
	RwRasterPushContext(pBlurBuffer1);
	RwRasterClear(0);
	RwRasterPopContext();
	
	// Simple blur - just copy for now (real bloom would use gaussian blur)
	RenderQuad(cam, sceneRaster, m_fBloomIntensity, false);
	
	// Step 3: Horizontal blur
	RenderQuad(cam, pBlurBuffer1, m_fBloomIntensity, true);
	
	// Step 4: Vertical blur
	RenderQuad(cam, pBlurBuffer2, m_fBloomIntensity, false);
	
	// Step 5: Composite bloom with scene
	// Add bloom on top of original scene with additive blending
	rw::SetRenderState(rw::ALPHATESTENABLE, false);
	rw::SetRenderState(rw::ALPHABLENDENABLE, true);
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDONE);
	rw::SetRenderState(rw::DSTBLEND, rw::BLENDONE);
	rw::SetRenderState(rw::ZTESTENABLE, false);
	rw::SetRenderState(rw::ZWRITEENABLE, false);
	
	// Render final bloom on top
	// (Actual compositing would happen here)
	
	// Restore original render states
	rw::SetRenderState(rw::ALPHATESTENABLE, origAlphaTest);
	rw::SetRenderState(rw::ALPHABLENDENABLE, origAlphaBlend);
	rw::SetRenderState(rw::SRCBLEND, origSrcBlend);
	rw::SetRenderState(rw::DSTBLEND, origDstBlend);
	rw::SetRenderState(rw::ZTESTENABLE, origZTest);
	rw::SetRenderState(rw::ZWRITEENABLE, origZWrite);
	
	uint32 endTime = CTimer::GetTimeMs();
	// printf("CBloom: Render took %d ms\n", endTime - startTime);
}
