#ifndef __GTA_BLOOM_H__
#define __GTA_BLOOM_H__

#include "common.h"

class CBloom
{
public:
	enum BloomQuality {
		BLOOM_LOW,
		BLOOM_MEDIUM,
		BLOOM_HIGH
	};

	static bool m_bInitialised;
	static bool m_bBloomOn;
	static float m_fBloomIntensity;
	static float m_fBloomThreshold;
	static float m_fBloomSoftness;
	static BloomQuality m_eQuality;
	
	static bool Initialise(void);
	static void Shutdown(void);
	static void Render(RwCamera *cam);
	
	static void SetIntensity(float intensity) { m_fBloomIntensity = intensity; }
	static void SetThreshold(float threshold) { m_fBloomThreshold = threshold; }
	static void SetSoftness(float softness) { m_fBloomSoftness = softness; }
	static void SetQuality(BloomQuality quality) { m_eQuality = quality; }
	static void Toggle(void) { m_bBloomOn = !m_bBloomOn; }
	static bool IsOn(void) { return m_bBloomOn; }

private:
	static RwRaster *pBrightPass;
	static RwRaster *pBlurBuffer1;
	static RwRaster *pBlurBuffer2;
};

#endif // __GTA_BLOOM_H__
