uniform sampler2D tex0;  // depth texture (R32F float)
uniform vec2 u_texelSize;
uniform float u_ssaoRadius;
uniform float u_ssaoBias;
uniform float u_ssaoIntensity;
uniform float u_nearPlane;
uniform float u_farPlane;

FSIN vec4 v_color;
FSIN vec2 v_tex0;

// Linearize depth from [0,1] range to view space distance
float linearizeDepth(float depth)
{
	float z = depth * 2.0 - 1.0;
	return (2.0 * u_nearPlane * u_farPlane) / (u_farPlane + u_nearPlane - z * (u_farPlane - u_nearPlane));
}

void main(void)
{
	vec2 uv = vec2(v_tex0.x, 1.0 - v_tex0.y);
	float depth = texture(tex0, uv).r;
	
	// Sky check - no occlusion for sky/far plane
	if(depth >= 0.9999) {
		FRAGCOLOR(vec4(1.0, 1.0, 1.0, 1.0));
		return;
	}
	
	float centerDepth = linearizeDepth(depth);
	
	float occlusion = 0.0;
	int validSamples = 0;
	
	// Radius scales with distance - farther objects get smaller screen-space radius
	float radius = u_ssaoRadius * 20.0 / centerDepth;
	radius = clamp(radius, 2.0, 30.0);
	
	// Sample in concentric rings for better coverage
	const int RINGS = 3;
	const int SAMPLES_PER_RING = 8;
	
	for(int ring = 1; ring <= RINGS; ring++) {
		float ringRadius = radius * float(ring) / float(RINGS);
		
		for(int i = 0; i < SAMPLES_PER_RING; i++) {
			float angle = float(i) * 6.28318 / float(SAMPLES_PER_RING);
			// Offset each ring slightly to avoid pattern
			angle += float(ring) * 0.5;
			
			vec2 offset = vec2(cos(angle), sin(angle)) * ringRadius * u_texelSize;
			vec2 sampleUV = uv + offset;
			
			// Bounds check
			if(sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
				continue;
			
			float sampleDepth = texture(tex0, sampleUV).r;
			
			// Skip sky
			if(sampleDepth >= 0.9999)
				continue;
			
			float sampleLinearDepth = linearizeDepth(sampleDepth);
			float depthDiff = centerDepth - sampleLinearDepth;
			
			// Only count occlusion if sample is in front and within range
			float falloff = u_ssaoRadius * 100.0;
			if(depthDiff > u_ssaoBias && depthDiff < falloff) {
				// Weight by how close the sample depth is
				float weight = 1.0 - (depthDiff / falloff);
				occlusion += weight;
			}
			validSamples++;
		}
	}
	
	// Normalize
	if(validSamples > 0) {
		occlusion /= float(validSamples);
	}
	
	// Apply intensity and clamp
	float ao = 1.0 - (occlusion * u_ssaoIntensity);
	ao = clamp(ao, 0.3, 1.0);  // Never go below 0.3 to avoid overly dark areas
	
	FRAGCOLOR(vec4(ao, ao, ao, 1.0));
}
