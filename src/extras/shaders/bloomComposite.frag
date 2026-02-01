uniform sampler2D tex0;
uniform sampler2D tex1;
uniform float u_bloomIntensity;

FSIN vec4 v_color;
FSIN vec2 v_tex0;
FSIN float v_fog;

void
main(void)
{
	vec2 uv = vec2(v_tex0.x, 1.0-v_tex0.y);
	vec4 scene = texture(tex0, uv);
	vec3 bloom = texture(tex1, uv).rgb;

	vec4 color;
	color.rgb = scene.rgb + bloom * u_bloomIntensity;
	color.a = 1.0;

	FRAGCOLOR(color);
}
