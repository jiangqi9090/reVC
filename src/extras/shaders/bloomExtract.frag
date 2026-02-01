uniform sampler2D tex0;
uniform float u_threshold;

FSIN vec4 v_color;
FSIN vec2 v_tex0;
FSIN float v_fog;

void
main(void)
{
	vec4 color = texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));
	float brightness = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));

	if(brightness > u_threshold)
		FRAGCOLOR(vec4(color.rgb, 1.0));
	else
		FRAGCOLOR(vec4(0.0, 0.0, 0.0, 1.0));
}
