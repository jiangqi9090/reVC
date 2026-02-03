uniform sampler2D tex0;  // SSAO texture
uniform vec2 u_texelSize;

FSIN vec4 v_color;
FSIN vec2 v_tex0;

void main(void)
{
	vec2 uv = vec2(v_tex0.x, 1.0 - v_tex0.y);
	
	// Larger blur kernel (7x7 gaussian approximation)
	float result = 0.0;
	float totalWeight = 0.0;
	
	// Gaussian weights for 7x7 kernel (sigma ~= 2)
	for(int x = -3; x <= 3; x++) {
		for(int y = -3; y <= 3; y++) {
			vec2 offset = vec2(float(x), float(y)) * u_texelSize;
			float weight = exp(-float(x*x + y*y) / 8.0);
			result += texture(tex0, uv + offset).r * weight;
			totalWeight += weight;
		}
	}

	result /= totalWeight;
	FRAGCOLOR(vec4(result, result, result, 1.0));
}
