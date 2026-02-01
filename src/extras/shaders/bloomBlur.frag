uniform sampler2D tex0;
uniform vec2 u_texelSize;
uniform float u_horizontal;

FSIN vec4 v_color;
FSIN vec2 v_tex0;
FSIN float v_fog;

void
main(void)
{
	vec2 uv = vec2(v_tex0.x, 1.0-v_tex0.y);

	// 9-tap Gaussian weights
	float weights[5];
	weights[0] = 0.227027;
	weights[1] = 0.1945946;
	weights[2] = 0.1216216;
	weights[3] = 0.054054;
	weights[4] = 0.016216;

	vec3 result = texture(tex0, uv).rgb * weights[0];

	vec2 offset;
	if(u_horizontal > 0.5)
		offset = vec2(u_texelSize.x, 0.0);
	else
		offset = vec2(0.0, u_texelSize.y);

	for(int i = 1; i < 5; i++) {
		result += texture(tex0, uv + offset * float(i)).rgb * weights[i];
		result += texture(tex0, uv - offset * float(i)).rgb * weights[i];
	}

	FRAGCOLOR(vec4(result, 1.0));
}
