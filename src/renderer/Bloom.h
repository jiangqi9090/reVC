#ifndef __GTA_BLOOM_H__
#define __GTA_BLOOM_H__

#include "common.h"
#include <rw.h>

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
	static BloomQuality m_eQuality;
	
	static bool Initialise(void);
	static void Shutdown(void);
	
	static void Render(RwCamera *cam);
	static void RenderQuad(RwCamera *cam, RwRaster *srcRaster, float intensity, bool horizontal);
	
	static void SetIntensity(float intensity) { m_fBloomIntensity = intensity; }
	static void SetThreshold(float threshold) { m_fBloomThreshold = threshold; }
	static void SetQuality(BloomQuality quality) { m_eQuality = quality; }
	static void Toggle(void) { m_bBloomOn = !m_bBloomOn; }
	static bool IsOn(void) { return m_bBloomOn; }
};

#endif // __GTA_BLOOM_H__
