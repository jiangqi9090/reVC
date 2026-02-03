uniform sampler2D tex0;

FSIN vec4 v_color;
FSIN vec2 v_tex0;

void main(void)
{
	// Output linear depth (0-1 range based on near/far)
	float depth = gl_FragCoord.z;
	FRAGCOLOR = vec4(depth, depth, depth, 1.0);
}
