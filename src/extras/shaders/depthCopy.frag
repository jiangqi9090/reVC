uniform sampler2D tex0;

FSIN vec4 v_color;
FSIN vec2 v_tex0;

void main(void)
{
	// Sample depth texture and output as color
	// tex0 is bound manually via raw GL calls to bypass librw state management
	// For GL_DEPTH_STENCIL format, depth is returned in .r component when
	// GL_TEXTURE_COMPARE_MODE is set to GL_NONE
	// Y-flip: The depth buffer from glBlitFramebuffer has OpenGL bottom-up orientation,
	// but we need top-down to match the color buffer orientation used by postfx.
	float depth = texture(tex0, vec2(v_tex0.x, 1.0 - v_tex0.y)).r;
	FRAGCOLOR(vec4(depth, depth, depth, 1.0));
}
