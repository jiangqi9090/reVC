#include "common.h"
#include "Bloom.h"
#include "RwHelper.h"
#include "Camera.h"

bool CBloom::m_bInitialised = false;
bool CBloom::m_bBloomOn = true;
float CBloom::m_fBloomIntensity = 0.5f;
float CBloom::m_fBloomThreshold = 0.8f;
CBloom::BloomQuality CBloom::m_eQuality = BLOOM_MEDIUM;

static RwRaster *pBlurBuffer1 = nullptr;
static RwRaster *pBlurBuffer2 = nullptr;

bool
CBloom::Initialise(void)
{
	// Buffers are initialised lazily in Render()
	return true;
}

void
CBloom::Shutdown(void)
{
	if (pBlurBuffer1) {
		RwRasterDestroy(pBlurBuffer1);
		pBlurBuffer1 = nullptr;
	}
	if (pBlurBuffer2) {
		RwRasterDestroy(pBlurBuffer2);
		pBlurBuffer2 = nullptr;
	}
	
	m_bInitialised = false;
}

void
CBloom::Render(RwCamera *cam)
{
	if (!m_bBloomOn)
		return;
	
	RwRaster *sceneRaster = RwCameraGetRaster(cam);
	if (!sceneRaster)
		return;
	
	// Lazy initialization
	if (!m_bInitialised) {
		int32 width = RwRasterGetWidth(RwCameraGetRaster(cam));
		int32 height = RwRasterGetHeight(RwCameraGetRaster(cam));
		
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
		
		pBlurBuffer1 = RwRasterCreate(blurW, blurH, 0, rwRASTERDONTALLOCATE | rwRASTERTYPETEXTURE);
		if (pBlurBuffer1) {
			pBlurBuffer2 = RwRasterCreate(blurW, blurH, 0, rwRASTERDONTALLOCATE | rwRASTERTYPETEXTURE);
		}
		
		if (pBlurBuffer1 && pBlurBuffer2) {
			m_bInitialised = true;
			printf("CBloom: Initialised successfully\n");
		} else {
			printf("CBloom: Failed to initialise buffers\n");
		}
	}
	
	if (!m_bInitialised)
		return;
	
	// Setup render states
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	
	// Step 1: Downsample to blur buffer1 (overwrite)
	RwRasterPushContext(pBlurBuffer1);
	RwRasterRenderFast(sceneRaster, 0, 0);
	RwRasterPopContext();
	
	// Step 2: Copy buffer1 to buffer2 (overwrite)
	RwRasterPushContext(pBlurBuffer2);
	RwRasterRenderFast(pBlurBuffer1, 0, 0);
	RwRasterPopContext();
	
	// Step 3: Copy buffer2 back to buffer1 (accumulate)
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	
	RwRasterPushContext(pBlurBuffer1);
	RwRasterRenderFast(pBlurBuffer2, 0, 0);
	RwRasterPopContext();
}
