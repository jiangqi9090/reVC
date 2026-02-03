uniform sampler2D tex0;  // scene color
uniform sampler2D tex1;  // SSAO texture

FSIN vec4 v_color;
FSIN vec2 v_tex0;

void main(void)
{
	vec2 uv = vec2(v_tex0.x, 1.0 - v_tex0.y);
	vec4 color = texture(tex0, uv);
	float ao = texture(tex1, uv).r;

	// Apply ambient occlusion to scene
	color.rgb *= ao;

	FRAGCOLOR(color);
}
