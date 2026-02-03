#define WITHD3D
#include "common.h"
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef EXTENDED_COLOURFILTER

#ifndef LIBRW
#error "Need librw for EXTENDED_COLOURFILTER"
#endif

#include "main.h"
#include "RwHelper.h"
#include "Camera.h"
#include "MBlur.h"
#include "postfx.h"

#ifdef RW_OPENGL
// Include librw GL implementation header for im2DVbo/im2DIbo access
#include "src/gl/rwgl3impl.h"
#endif

RwRaster *CPostFX::pFrontBuffer;
RwRaster *CPostFX::pBackBuffer;
bool CPostFX::bJustInitialised;
int CPostFX::EffectSwitch = POSTFX_NORMAL;
bool CPostFX::BlurOn = false;
bool CPostFX::MotionBlurOn = false;

// Bloom effect
RwRaster *CPostFX::pBloomBuffer;
RwRaster *CPostFX::pBloomTempBuffer;
RwTexture *CPostFX::pBloomTex;
bool CPostFX::BloomEnable = true;
float CPostFX::BloomThreshold = 0.5f;   // Lower = more glow
float CPostFX::BloomIntensity = 0.6f;   // Bloom strength

// SSAO effect
RwRaster *CPostFX::pSSAOBuffer;
RwRaster *CPostFX::pSSAOBlurBuffer;
RwRaster *CPostFX::pDepthBuffer;
RwTexture *CPostFX::pSSAOTex;
RwTexture *CPostFX::pDepthTex;
bool CPostFX::SSAOEnable = true;
float CPostFX::SSAORadius = 1.2f;      // Slightly larger radius
float CPostFX::SSAOBias = 0.025f;
float CPostFX::SSAOIntensity = 0.7f;   // Slightly higher intensity

#ifdef RW_OPENGL
// Custom float texture for depth (higher precision than RGBA8)
// Only depth needs high precision - SSAO result (0-1) is fine with 8-bit
static GLuint depthFloatTex = 0;
static GLuint depthFloatFbo = 0;
static uint32 depthFloatWidth = 0;
static uint32 depthFloatHeight = 0;
#endif

static RwIm2DVertex Vertex[4];
static RwIm2DVertex Vertex2[4];
static RwImVertexIndex Index[6] = { 0, 1, 2, 0, 2, 3 };

#ifdef RW_D3D9
void *colourfilterVC_PS;
void *contrast_PS;
void *bloomExtract_PS;
void *bloomBlur_PS;
void *bloomComposite_PS;
#endif
#ifdef RW_OPENGL
int32 u_blurcolor;
int32 u_contrastAdd;
int32 u_contrastMult;
int32 u_threshold;
int32 u_texelSize;
int32 u_horizontal;
int32 u_bloomIntensity;
int32 u_ssaoRadius;
int32 u_ssaoBias;
int32 u_ssaoIntensity;
int32 u_nearPlane;
int32 u_farPlane;
int32 u_xform;
rw::gl3::Shader *colourFilterVC;
rw::gl3::Shader *contrast;
rw::gl3::Shader *bloomExtract;
rw::gl3::Shader *bloomBlur;
rw::gl3::Shader *bloomComposite;
rw::gl3::Shader *ssaoCalc;
rw::gl3::Shader *ssaoBlur;
rw::gl3::Shader *ssaoApply;
rw::gl3::Shader *depthCopy;
#endif

void
CPostFX::InitOnce(void)
{
#ifdef RW_OPENGL
	u_blurcolor = rw::gl3::registerUniform("u_blurcolor");
	u_contrastAdd = rw::gl3::registerUniform("u_contrastAdd");
	u_contrastMult = rw::gl3::registerUniform("u_contrastMult");
	u_threshold = rw::gl3::registerUniform("u_threshold");
	u_texelSize = rw::gl3::registerUniform("u_texelSize");
	u_horizontal = rw::gl3::registerUniform("u_horizontal");
	u_bloomIntensity = rw::gl3::registerUniform("u_bloomIntensity");
	u_ssaoRadius = rw::gl3::registerUniform("u_ssaoRadius");
	u_ssaoBias = rw::gl3::registerUniform("u_ssaoBias");
	u_ssaoIntensity = rw::gl3::registerUniform("u_ssaoIntensity");
	u_nearPlane = rw::gl3::registerUniform("u_nearPlane");
	u_farPlane = rw::gl3::registerUniform("u_farPlane");
	u_xform = rw::gl3::registerUniform("u_xform", rw::gl3::UNIFORM_VEC4);  // Already registered by librw, safe to call again
#endif
}

void
CPostFX::Open(RwCamera *cam)
{
	if(pFrontBuffer)
		Close();

	uint32 width  = Pow(2.0f, int32(log2(RwRasterGetWidth (RwCameraGetRaster(cam))))+1);
	uint32 height = Pow(2.0f, int32(log2(RwRasterGetHeight(RwCameraGetRaster(cam))))+1);
	uint32 depth  = RwRasterGetDepth(RwCameraGetRaster(cam));
	pFrontBuffer = RwRasterCreate(width, height, depth, rwRASTERTYPECAMERATEXTURE);
	pBackBuffer = RwRasterCreate(width, height, depth, rwRASTERTYPECAMERATEXTURE);
	bJustInitialised = true;

	float zero, xmax, ymax;

	if(RwRasterGetDepth(RwCameraGetRaster(cam)) == 16){
		zero = HALFPX;
		xmax = width + HALFPX;
		ymax = height + HALFPX;
	}else{
		zero = -HALFPX;
		xmax = width - HALFPX;
		ymax = height - HALFPX;
	}

	RwIm2DVertexSetScreenX(&Vertex[0], zero);
	RwIm2DVertexSetScreenY(&Vertex[0], zero);
	RwIm2DVertexSetScreenZ(&Vertex[0], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex[0], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex[0], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex[0], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex[0], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex[0], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex[1], zero);
	RwIm2DVertexSetScreenY(&Vertex[1], ymax);
	RwIm2DVertexSetScreenZ(&Vertex[1], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex[1], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex[1], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex[1], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex[1], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex[1], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex[2], xmax);
	RwIm2DVertexSetScreenY(&Vertex[2], ymax);
	RwIm2DVertexSetScreenZ(&Vertex[2], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex[2], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex[2], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex[2], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex[2], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex[2], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex[3], xmax);
	RwIm2DVertexSetScreenY(&Vertex[3], zero);
	RwIm2DVertexSetScreenZ(&Vertex[3], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex[3], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex[3], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex[3], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex[3], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex[3], 255, 255, 255, 255);


	RwIm2DVertexSetScreenX(&Vertex2[0], zero + 2.0f);
	RwIm2DVertexSetScreenY(&Vertex2[0], zero + 2.0f);
	RwIm2DVertexSetScreenZ(&Vertex2[0], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex2[0], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex2[0], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex2[0], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex2[0], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex2[0], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex2[1], 2.0f);
	RwIm2DVertexSetScreenY(&Vertex2[1], ymax + 2.0f);
	RwIm2DVertexSetScreenZ(&Vertex2[1], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex2[1], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex2[1], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex2[1], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex2[1], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex2[1], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex2[2], xmax + 2.0f);
	RwIm2DVertexSetScreenY(&Vertex2[2], ymax + 2.0f);
	RwIm2DVertexSetScreenZ(&Vertex2[2], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex2[2], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex2[2], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex2[2], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex2[2], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex2[2], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex2[3], xmax + 2.0f);
	RwIm2DVertexSetScreenY(&Vertex2[3], zero + 2.0f);
	RwIm2DVertexSetScreenZ(&Vertex2[3], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex2[3], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex2[3], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex2[3], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex2[3], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex2[3], 255, 255, 255, 255);


	// Create bloom buffers (half resolution for performance)
	uint32 bloomWidth = width / 2;
	uint32 bloomHeight = height / 2;
	pBloomBuffer = RwRasterCreate(bloomWidth, bloomHeight, depth, rwRASTERTYPECAMERATEXTURE);
	pBloomTempBuffer = RwRasterCreate(bloomWidth, bloomHeight, depth, rwRASTERTYPECAMERATEXTURE);
	pBloomTex = RwTextureCreate(nil);
	RwTextureSetFilterMode(pBloomTex, rwFILTERLINEAR);

	// Create SSAO buffers (half resolution for performance)
	uint32 ssaoWidth = width / 2;
	uint32 ssaoHeight = height / 2;
	pSSAOBuffer = RwRasterCreate(ssaoWidth, ssaoHeight, depth, rwRASTERTYPECAMERATEXTURE);
	pSSAOBlurBuffer = RwRasterCreate(ssaoWidth, ssaoHeight, depth, rwRASTERTYPECAMERATEXTURE);
	pSSAOTex = RwTextureCreate(nil);
	RwTextureSetFilterMode(pSSAOTex, rwFILTERLINEAR);

#ifdef RW_OPENGL
	// Create high-precision float texture for depth (R32F)
	// This avoids the banding artifacts from 8-bit RGBA textures
	depthFloatWidth = width;
	depthFloatHeight = height;
	
	glGenTextures(1, &depthFloatTex);
	glBindTexture(GL_TEXTURE_2D, depthFloatTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, depthFloatWidth, depthFloatHeight, 0, GL_RED, GL_FLOAT, nil);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	
	glGenFramebuffers(1, &depthFloatFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, depthFloatFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, depthFloatTex, 0);
	
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if(status != GL_FRAMEBUFFER_COMPLETE) {
		debug("SSAO: Float depth FBO incomplete, status = 0x%x\n", status);
	}
	
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
#endif

	// Keep pDepthBuffer for compatibility but we won't use it for SSAO
	pDepthBuffer = RwRasterCreate(width, height, depth, rwRASTERTYPECAMERATEXTURE);
	pDepthTex = RwTextureCreate(nil);
	RwTextureSetFilterMode(pDepthTex, rwFILTERLINEAR);

#ifdef RW_D3D9
#include "shaders/obj/colourfilterVC_PS.inc"
	colourfilterVC_PS = rw::d3d::createPixelShader(colourfilterVC_PS_cso);
#include "shaders/obj/contrastPS.inc"
	contrast_PS = rw::d3d::createPixelShader(contrastPS_cso);
#include "shaders/obj/bloomExtract_PS.inc"
	bloomExtract_PS = rw::d3d::createPixelShader(bloomExtract_PS_cso);
#include "shaders/obj/bloomBlur_PS.inc"
	bloomBlur_PS = rw::d3d::createPixelShader(bloomBlur_PS_cso);
#include "shaders/obj/bloomComposite_PS.inc"
	bloomComposite_PS = rw::d3d::createPixelShader(bloomComposite_PS_cso);
#endif
#ifdef RW_OPENGL
	using namespace rw::gl3;

	{
#include "shaders/obj/im2d_vert.inc"
#include "shaders/obj/colourfilterVC_frag.inc"
	const char *vs[] = { shaderDecl, header_vert_src, im2d_vert_src, nil };
	const char *fs[] = { shaderDecl, header_frag_src, colourfilterVC_frag_src, nil };
	colourFilterVC = Shader::create(vs, fs);
	assert(colourFilterVC);
	}

	{
#include "shaders/obj/im2d_vert.inc"
#include "shaders/obj/contrast_frag.inc"
	const char *vs[] = { shaderDecl, header_vert_src, im2d_vert_src, nil };
	const char *fs[] = { shaderDecl, header_frag_src, contrast_frag_src, nil };
	contrast = Shader::create(vs, fs);
	assert(contrast);
	}

	{
#include "shaders/obj/im2d_vert.inc"
#include "shaders/obj/bloomExtract_frag.inc"
	const char *vs[] = { shaderDecl, header_vert_src, im2d_vert_src, nil };
	const char *fs[] = { shaderDecl, header_frag_src, bloomExtract_frag_src, nil };
	bloomExtract = Shader::create(vs, fs);
	assert(bloomExtract);
	}

	{
#include "shaders/obj/im2d_vert.inc"
#include "shaders/obj/bloomBlur_frag.inc"
	const char *vs[] = { shaderDecl, header_vert_src, im2d_vert_src, nil };
	const char *fs[] = { shaderDecl, header_frag_src, bloomBlur_frag_src, nil };
	bloomBlur = Shader::create(vs, fs);
	assert(bloomBlur);
	}

	{
#include "shaders/obj/im2d_vert.inc"
#include "shaders/obj/bloomComposite_frag.inc"
	const char *vs[] = { shaderDecl, header_vert_src, im2d_vert_src, nil };
	const char *fs[] = { shaderDecl, header_frag_src, bloomComposite_frag_src, nil };
	bloomComposite = Shader::create(vs, fs);
	assert(bloomComposite);
	}

	{
#include "shaders/obj/im2d_vert.inc"
#include "shaders/obj/ssao_frag.inc"
	const char *vs[] = { shaderDecl, header_vert_src, im2d_vert_src, nil };
	const char *fs[] = { shaderDecl, header_frag_src, ssao_frag_src, nil };
	ssaoCalc = Shader::create(vs, fs);
	assert(ssaoCalc);
	}

	{
#include "shaders/obj/im2d_vert.inc"
#include "shaders/obj/ssaoBlur_frag.inc"
	const char *vs[] = { shaderDecl, header_vert_src, im2d_vert_src, nil };
	const char *fs[] = { shaderDecl, header_frag_src, ssaoBlur_frag_src, nil };
	ssaoBlur = Shader::create(vs, fs);
	assert(ssaoBlur);
	}

	{
#include "shaders/obj/im2d_vert.inc"
#include "shaders/obj/ssaoApply_frag.inc"
	const char *vs[] = { shaderDecl, header_vert_src, im2d_vert_src, nil };
	const char *fs[] = { shaderDecl, header_frag_src, ssaoApply_frag_src, nil };
	ssaoApply = Shader::create(vs, fs);
	assert(ssaoApply);
	}

	{
#include "shaders/obj/im2d_vert.inc"
#include "shaders/obj/depthCopy_frag.inc"
	const char *vs[] = { shaderDecl, header_vert_src, im2d_vert_src, nil };
	const char *fs[] = { shaderDecl, header_frag_src, depthCopy_frag_src, nil };
	depthCopy = Shader::create(vs, fs);
	assert(depthCopy);
	}

#endif
}

void
CPostFX::Close(void)
{
	if(pFrontBuffer){
		RwRasterDestroy(pFrontBuffer);
		pFrontBuffer = nil;
	}
	if(pBackBuffer){
		RwRasterDestroy(pBackBuffer);
		pBackBuffer = nil;
	}
	if(pBloomTex){
		RwTextureSetRaster(pBloomTex, nil);
		RwTextureDestroy(pBloomTex);
		pBloomTex = nil;
	}
	if(pBloomBuffer){
		RwRasterDestroy(pBloomBuffer);
		pBloomBuffer = nil;
	}
	if(pBloomTempBuffer){
		RwRasterDestroy(pBloomTempBuffer);
		pBloomTempBuffer = nil;
	}
	if(pSSAOTex){
		RwTextureSetRaster(pSSAOTex, nil);
		RwTextureDestroy(pSSAOTex);
		pSSAOTex = nil;
	}
	if(pDepthTex){
		RwTextureSetRaster(pDepthTex, nil);
		RwTextureDestroy(pDepthTex);
		pDepthTex = nil;
	}
	if(pSSAOBuffer){
		RwRasterDestroy(pSSAOBuffer);
		pSSAOBuffer = nil;
	}
	if(pSSAOBlurBuffer){
		RwRasterDestroy(pSSAOBlurBuffer);
		pSSAOBlurBuffer = nil;
	}
	if(pDepthBuffer){
		RwRasterDestroy(pDepthBuffer);
		pDepthBuffer = nil;
	}
#ifdef RW_OPENGL
	if(depthFloatFbo){
		glDeleteFramebuffers(1, &depthFloatFbo);
		depthFloatFbo = 0;
	}
	if(depthFloatTex){
		glDeleteTextures(1, &depthFloatTex);
		depthFloatTex = 0;
	}
	depthFloatWidth = 0;
	depthFloatHeight = 0;
#endif
#ifdef RW_D3D9
	if(colourfilterVC_PS){
		rw::d3d::destroyPixelShader(colourfilterVC_PS);
		colourfilterVC_PS = nil;
	}
	if(contrast_PS){
		rw::d3d::destroyPixelShader(contrast_PS);
		contrast_PS = nil;
	}
	if(bloomExtract_PS){
		rw::d3d::destroyPixelShader(bloomExtract_PS);
		bloomExtract_PS = nil;
	}
	if(bloomBlur_PS){
		rw::d3d::destroyPixelShader(bloomBlur_PS);
		bloomBlur_PS = nil;
	}
	if(bloomComposite_PS){
		rw::d3d::destroyPixelShader(bloomComposite_PS);
		bloomComposite_PS = nil;
	}
#endif
#ifdef RW_OPENGL
	if(colourFilterVC){
		colourFilterVC->destroy();
		colourFilterVC = nil;
	}
	if(contrast){
		contrast->destroy();
		contrast = nil;
	}
	if(bloomExtract){
		bloomExtract->destroy();
		bloomExtract = nil;
	}
	if(bloomBlur){
		bloomBlur->destroy();
		bloomBlur = nil;
	}
	if(bloomComposite){
		bloomComposite->destroy();
		bloomComposite = nil;
	}
	if(ssaoCalc){
		ssaoCalc->destroy();
		ssaoCalc = nil;
	}
	if(ssaoBlur){
		ssaoBlur->destroy();
		ssaoBlur = nil;
	}
	if(ssaoApply){
		ssaoApply->destroy();
		ssaoApply = nil;
	}
	if(depthCopy){
		depthCopy->destroy();
		depthCopy = nil;
	}
#endif
}

void
CPostFX::RenderOverlayBlur(RwCamera *cam, int32 r, int32 g, int32 b, int32 a)
{
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);

	RwIm2DVertexSetIntRGBA(&Vertex[0], r*2, g*2, b*2, 30);
	RwIm2DVertexSetIntRGBA(&Vertex[1], r*2, g*2, b*2, 30);
	RwIm2DVertexSetIntRGBA(&Vertex[2], r*2, g*2, b*2, 30);
	RwIm2DVertexSetIntRGBA(&Vertex[3], r*2, g*2, b*2, 30);
	RwIm2DVertexSetIntRGBA(&Vertex2[0], r*2, g*2, b*2, 30);
	RwIm2DVertexSetIntRGBA(&Vertex2[1], r*2, g*2, b*2, 30);
	RwIm2DVertexSetIntRGBA(&Vertex2[2], r*2, g*2, b*2, 30);
	RwIm2DVertexSetIntRGBA(&Vertex2[3], r*2, g*2, b*2, 30);

	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, BlurOn ? Vertex2 : Vertex, 4, Index, 6);


	RwIm2DVertexSetIntRGBA(&Vertex2[0], r, g, b, a);
	RwIm2DVertexSetIntRGBA(&Vertex[0], r, g, b, a);
	RwIm2DVertexSetIntRGBA(&Vertex2[1], r, g, b, a);
	RwIm2DVertexSetIntRGBA(&Vertex[1], r, g, b, a);
	RwIm2DVertexSetIntRGBA(&Vertex2[2], r, g, b, a);
	RwIm2DVertexSetIntRGBA(&Vertex[2], r, g, b, a);
	RwIm2DVertexSetIntRGBA(&Vertex2[3], r, g, b, a);
	RwIm2DVertexSetIntRGBA(&Vertex[3], r, g, b, a);

	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, BlurOn ? Vertex2 : Vertex, 4, Index, 6);
}

void
CPostFX::RenderOverlaySniper(RwCamera *cam, int32 r, int32 g, int32 b, int32 a)
{
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);

	RwIm2DVertexSetIntRGBA(&Vertex[0], r, g, b, 80);
	RwIm2DVertexSetIntRGBA(&Vertex[1], r, g, b, 80);
	RwIm2DVertexSetIntRGBA(&Vertex[2], r, g, b, 80);
	RwIm2DVertexSetIntRGBA(&Vertex[3], r, g, b, 80);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
}

float CPostFX::Intensity = 1.0f;

void
CPostFX::RenderOverlayShader(RwCamera *cam, int32 r, int32 g, int32 b, int32 a)
{
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pBackBuffer);

	if(EffectSwitch == POSTFX_MOBILE){
		float mult[3], add[3];
		mult[0] = (r-64)/256.0f + 1.4f;
		mult[1] = (g-64)/256.0f + 1.4f;
		mult[2] = (b-64)/256.0f + 1.4f;
		add[0] = r/1536.f - 0.05f;
		add[1] = g/1536.f - 0.05f;
		add[2] = b/1536.f - 0.05f;
#ifdef RW_D3D9
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, mult, 1);
		rw::d3d::d3ddevice->SetPixelShaderConstantF(11, add, 1);

		rw::d3d::im2dOverridePS = contrast_PS;
#endif
#ifdef RW_OPENGL
		rw::gl3::im2dOverrideShader = contrast;
		contrast->use();
		glUniform3fv(contrast->uniformLocations[u_contrastMult], 1, mult);
		glUniform3fv(contrast->uniformLocations[u_contrastAdd], 1, add);
#endif
	}else{
		float f = Intensity;
		float blurcolors[4];
		blurcolors[0] = r*f/255.0f;
		blurcolors[1] = g*f/255.0f;
		blurcolors[2] = b*f/255.0f;
		blurcolors[3] = 30/255.0f;
#ifdef RW_D3D9
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, blurcolors, 1);
		rw::d3d::im2dOverridePS = colourfilterVC_PS;
#endif
#ifdef RW_OPENGL
		rw::gl3::im2dOverrideShader = colourFilterVC;
		colourFilterVC->use();
		glUniform4fv(colourFilterVC->uniformLocations[u_blurcolor], 1, blurcolors);
#endif
	}
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
#ifdef RW_D3D9
	rw::d3d::im2dOverridePS = nil;
#endif
#ifdef RW_OPENGL
	rw::gl3::im2dOverrideShader = nil;
#endif
}

void
CPostFX::RenderMotionBlur(RwCamera *cam, uint32 blur)
{
	if(blur == 0)
		return;

	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	RwIm2DVertexSetIntRGBA(&Vertex[0], 255, 255, 255, blur);
	RwIm2DVertexSetIntRGBA(&Vertex[1], 255, 255, 255, blur);
	RwIm2DVertexSetIntRGBA(&Vertex[2], 255, 255, 255, blur);
	RwIm2DVertexSetIntRGBA(&Vertex[3], 255, 255, 255, blur);

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
}

static RwIm2DVertex BloomVertex[4];

void
CPostFX::RenderBloom(RwCamera *cam)
{
	if(!BloomEnable || !pBloomBuffer || !pBloomTempBuffer)
		return;

	PUSH_RENDERGROUP("CPostFX::RenderBloom");

#ifdef RW_OPENGL
	using namespace rw::gl3;

	uint32 bloomWidth = RwRasterGetWidth(pBloomBuffer);
	uint32 bloomHeight = RwRasterGetHeight(pBloomBuffer);
	uint32 screenWidth = RwRasterGetWidth(RwCameraGetRaster(cam));
	uint32 screenHeight = RwRasterGetHeight(RwCameraGetRaster(cam));
	float texelSizeX = 1.0f / bloomWidth;
	float texelSizeY = 1.0f / bloomHeight;
	float recipCamZ = 1.0f / RwCameraGetNearClipPlane(cam);

	// Setup bloom vertices for bloom buffer size
	float zero = -HALFPX;
	float xmax = bloomWidth - HALFPX;
	float ymax = bloomHeight - HALFPX;

	RwIm2DVertexSetScreenX(&BloomVertex[0], zero);
	RwIm2DVertexSetScreenY(&BloomVertex[0], zero);
	RwIm2DVertexSetScreenZ(&BloomVertex[0], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&BloomVertex[0], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&BloomVertex[0], recipCamZ);
	RwIm2DVertexSetU(&BloomVertex[0], 0.0f, recipCamZ);
	RwIm2DVertexSetV(&BloomVertex[0], 0.0f, recipCamZ);
	RwIm2DVertexSetIntRGBA(&BloomVertex[0], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&BloomVertex[1], zero);
	RwIm2DVertexSetScreenY(&BloomVertex[1], ymax);
	RwIm2DVertexSetScreenZ(&BloomVertex[1], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&BloomVertex[1], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&BloomVertex[1], recipCamZ);
	RwIm2DVertexSetU(&BloomVertex[1], 0.0f, recipCamZ);
	RwIm2DVertexSetV(&BloomVertex[1], 1.0f, recipCamZ);
	RwIm2DVertexSetIntRGBA(&BloomVertex[1], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&BloomVertex[2], xmax);
	RwIm2DVertexSetScreenY(&BloomVertex[2], ymax);
	RwIm2DVertexSetScreenZ(&BloomVertex[2], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&BloomVertex[2], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&BloomVertex[2], recipCamZ);
	RwIm2DVertexSetU(&BloomVertex[2], 1.0f, recipCamZ);
	RwIm2DVertexSetV(&BloomVertex[2], 1.0f, recipCamZ);
	RwIm2DVertexSetIntRGBA(&BloomVertex[2], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&BloomVertex[3], xmax);
	RwIm2DVertexSetScreenY(&BloomVertex[3], zero);
	RwIm2DVertexSetScreenZ(&BloomVertex[3], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&BloomVertex[3], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&BloomVertex[3], recipCamZ);
	RwIm2DVertexSetU(&BloomVertex[3], 1.0f, recipCamZ);
	RwIm2DVertexSetV(&BloomVertex[3], 0.0f, recipCamZ);
	RwIm2DVertexSetIntRGBA(&BloomVertex[3], 255, 255, 255, 255);

	// Get FBO handles from rasters
	Gl3Raster *bloomNatRas = GETGL3RASTEREXT(pBloomBuffer);
	Gl3Raster *bloomTempNatRas = GETGL3RASTEREXT(pBloomTempBuffer);
	Gl3Raster *camNatRas = GETGL3RASTEREXT(RwCameraGetRaster(cam));

	// Save original camera frameBuffer dimensions
	rw::Raster *origFrameBuffer = cam->frameBuffer;
	
	// Pass 1: Extract bright areas from pBackBuffer -> pBloomBuffer
	bindFramebuffer(bloomNatRas->fbo);
	glViewport(0, 0, bloomWidth, bloomHeight);
	
	// Temporarily set camera frameBuffer to bloom buffer so im2DSetXform uses correct size
	cam->frameBuffer = pBloomBuffer;

	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pBackBuffer);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);

	im2dOverrideShader = bloomExtract;
	bloomExtract->use();
	glUniform1f(bloomExtract->uniformLocations[u_threshold], BloomThreshold);

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, BloomVertex, 4, Index, 6);

	// Multi-pass blur for better bloom spread (3 iterations)
	for(int blurPass = 0; blurPass < 3; blurPass++) {
		// Horizontal blur pBloomBuffer -> pBloomTempBuffer
		bindFramebuffer(bloomTempNatRas->fbo);

		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pBloomBuffer);

		im2dOverrideShader = bloomBlur;
		bloomBlur->use();
		glUniform2f(bloomBlur->uniformLocations[u_texelSize], texelSizeX, texelSizeY);
		glUniform1f(bloomBlur->uniformLocations[u_horizontal], 1.0f);

		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, BloomVertex, 4, Index, 6);

		// Vertical blur pBloomTempBuffer -> pBloomBuffer
		bindFramebuffer(bloomNatRas->fbo);

		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pBloomTempBuffer);

		im2dOverrideShader = bloomBlur;
		bloomBlur->use();
		glUniform2f(bloomBlur->uniformLocations[u_texelSize], texelSizeX, texelSizeY);
		glUniform1f(bloomBlur->uniformLocations[u_horizontal], 0.0f);

		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, BloomVertex, 4, Index, 6);
	}

	// Restore original camera frameBuffer
	cam->frameBuffer = origFrameBuffer;
	
	// Restore original framebuffer and viewport
	bindFramebuffer(camNatRas->fbo);
	glViewport(0, 0, screenWidth, screenHeight);

	// Pass 4: Composite bloom with original scene
	RwTextureSetRaster(pBloomTex, pBloomBuffer);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pBackBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	rw::gl3::setTexture(1, pBloomTex);
	im2dOverrideShader = bloomComposite;
	bloomComposite->use();
	glUniform1f(bloomComposite->uniformLocations[u_bloomIntensity], BloomIntensity);

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);

	rw::gl3::setTexture(1, nil);
	im2dOverrideShader = nil;
#endif

	POP_RENDERGROUP();
}

static RwIm2DVertex SSAOVertex[4];
static RwIm2DVertex DepthCopyVertex[4];

void
CPostFX::RenderSSAO(RwCamera *cam)
{
	if(!SSAOEnable || !pSSAOBuffer)
		return;

#ifdef RW_OPENGL
	// Need the float depth texture
	if(!depthFloatTex || !depthFloatFbo)
		return;
#endif

	// Check if camera has a valid z-buffer
	RwRaster *zRaster = RwCameraGetZRaster(cam);
	if(!zRaster)
		return;

#ifdef RW_OPENGL
	using namespace rw::gl3;

	// SSAO requires sampling depth texture, which is not possible on GLES (uses RBO)
	if(gl3Caps.gles)
		return;

	PUSH_RENDERGROUP("CPostFX::RenderSSAO");

	uint32 screenWidth = RwRasterGetWidth(RwCameraGetRaster(cam));
	uint32 screenHeight = RwRasterGetHeight(RwCameraGetRaster(cam));
	float recipCamZ = 1.0f / RwCameraGetNearClipPlane(cam);
	float nearPlane = RwCameraGetNearClipPlane(cam);
	float farPlane = RwCameraGetFarClipPlane(cam);

	// Setup depth copy vertices covering only the screen-sized area.
	// This matches the placement of glCopyTexSubImage2D used by GetBackBuffer
	// (via rasterRenderFast), which places scene data at a y-offset in the
	// power-of-2 texture. By using screen dimensions here, the depth occupies
	// the same region as the scene in pBackBuffer.
	float zero = -HALFPX;
	float depthXmax = screenWidth - HALFPX;
	float depthYmax = screenHeight - HALFPX;

	RwIm2DVertexSetScreenX(&DepthCopyVertex[0], zero);
	RwIm2DVertexSetScreenY(&DepthCopyVertex[0], zero);
	RwIm2DVertexSetScreenZ(&DepthCopyVertex[0], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&DepthCopyVertex[0], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&DepthCopyVertex[0], recipCamZ);
	RwIm2DVertexSetU(&DepthCopyVertex[0], 0.0f, recipCamZ);
	RwIm2DVertexSetV(&DepthCopyVertex[0], 0.0f, recipCamZ);
	RwIm2DVertexSetIntRGBA(&DepthCopyVertex[0], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&DepthCopyVertex[1], zero);
	RwIm2DVertexSetScreenY(&DepthCopyVertex[1], depthYmax);
	RwIm2DVertexSetScreenZ(&DepthCopyVertex[1], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&DepthCopyVertex[1], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&DepthCopyVertex[1], recipCamZ);
	RwIm2DVertexSetU(&DepthCopyVertex[1], 0.0f, recipCamZ);
	RwIm2DVertexSetV(&DepthCopyVertex[1], 1.0f, recipCamZ);
	RwIm2DVertexSetIntRGBA(&DepthCopyVertex[1], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&DepthCopyVertex[2], depthXmax);
	RwIm2DVertexSetScreenY(&DepthCopyVertex[2], depthYmax);
	RwIm2DVertexSetScreenZ(&DepthCopyVertex[2], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&DepthCopyVertex[2], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&DepthCopyVertex[2], recipCamZ);
	RwIm2DVertexSetU(&DepthCopyVertex[2], 1.0f, recipCamZ);
	RwIm2DVertexSetV(&DepthCopyVertex[2], 1.0f, recipCamZ);
	RwIm2DVertexSetIntRGBA(&DepthCopyVertex[2], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&DepthCopyVertex[3], depthXmax);
	RwIm2DVertexSetScreenY(&DepthCopyVertex[3], zero);
	RwIm2DVertexSetScreenZ(&DepthCopyVertex[3], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&DepthCopyVertex[3], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&DepthCopyVertex[3], recipCamZ);
	RwIm2DVertexSetU(&DepthCopyVertex[3], 1.0f, recipCamZ);
	RwIm2DVertexSetV(&DepthCopyVertex[3], 0.0f, recipCamZ);
	RwIm2DVertexSetIntRGBA(&DepthCopyVertex[3], 255, 255, 255, 255);

	// Get FBO handles
	Gl3Raster *camNatRas = GETGL3RASTEREXT(RwCameraGetRaster(cam));
	// Use parent raster for z-buffer (same as librw does internally)
	RwRaster *zBufParent = zRaster->parent;
	Gl3Raster *zNatRas = GETGL3RASTEREXT(zBufParent);

	// Verify depth texture is valid
	assert(zNatRas->texid != 0 && "Depth buffer texture ID is invalid");

	rw::Raster *origFrameBuffer = cam->frameBuffer;

	// ==========================================
	// Pass 0: Copy depth buffer to float texture (high precision)
	// ==========================================

	// When the camera uses the default framebuffer (fbo=0), the z-buffer texture
	// created by librw is allocated but never filled - the window system manages
	// the actual depth buffer. We need to blit the real depth data into the texture.
	if(camNatRas->fbo == 0) {
		static GLuint depthBlitFbo = 0;
		if(!depthBlitFbo)
			glGenFramebuffers(1, &depthBlitFbo);

		// Attach z-buffer texture as depth/stencil target on temp FBO
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, depthBlitFbo);
		glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
			GL_TEXTURE_2D, zNatRas->texid, 0);

		// Blit depth from default framebuffer to the z-buffer texture
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		glBlitFramebuffer(0, 0, screenWidth, screenHeight,
			0, 0, screenWidth, screenHeight,
			GL_DEPTH_BUFFER_BIT, GL_NEAREST);

		// Detach so the texture can be sampled freely
		glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
			GL_TEXTURE_2D, 0, 0);
	}

	// Render depth to our custom R32F float texture for high precision
	glBindFramebuffer(GL_FRAMEBUFFER, depthFloatFbo);
	glViewport(0, 0, depthFloatWidth, depthFloatHeight);

	// Clear to 1.0 (far plane) so unused areas don't cause artifacts
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// Disable depth test/write
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	// Use depthCopy shader
	depthCopy->use();

	// Set up the xform uniform manually (same as im2DSetXform)
	GLfloat xform[4];
	xform[0] = 2.0f / depthFloatWidth;
	xform[1] = -2.0f / depthFloatHeight;
	xform[2] = -1.0f;
	xform[3] = 1.0f;
	glUniform4fv(depthCopy->uniformLocations[u_xform], 1, xform);

	// Bind the depth texture to texture unit 0
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, zNatRas->texid);
	// Set texture parameters for sampling depth (not shadow comparison)
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// Upload vertex data to im2D buffers and draw
	glBindBuffer(GL_ARRAY_BUFFER, im2DVbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, 4*sizeof(RwIm2DVertex), DepthCopyVertex);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, im2DIbo);
	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, 6*sizeof(RwImVertexIndex), Index);

	// Set up vertex attributes manually
	// ATTRIB_POS = 0, ATTRIB_COLOR = 2, ATTRIB_TEXCOORDS0 = 5
	glEnableVertexAttribArray(0); // position
	glEnableVertexAttribArray(2); // color
	glEnableVertexAttribArray(5); // texcoord
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(RwIm2DVertex), (void*)0);
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(RwIm2DVertex), (void*)16);
	glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(RwIm2DVertex), (void*)20);

	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nil);

	// Disable vertex attributes we enabled manually
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(2);
	glDisableVertexAttribArray(5);

	// Restore depth state to librw's expected values
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);

	// Get FBO handles for librw rasters
	Gl3Raster *ssaoNatRas = GETGL3RASTEREXT(pSSAOBuffer);
	Gl3Raster *ssaoBlurNatRas = GETGL3RASTEREXT(pSSAOBlurBuffer);

	uint32 ssaoWidth = RwRasterGetWidth(pSSAOBuffer);
	uint32 ssaoHeight = RwRasterGetHeight(pSSAOBuffer);
	float texelSizeX = 1.0f / ssaoWidth;
	float texelSizeY = 1.0f / ssaoHeight;

	// Setup SSAO vertices for SSAO buffer size
	float ssaoZero = -HALFPX;
	float ssaoXmax = ssaoWidth - HALFPX;
	float ssaoYmax = ssaoHeight - HALFPX;

	RwIm2DVertexSetScreenX(&SSAOVertex[0], ssaoZero);
	RwIm2DVertexSetScreenY(&SSAOVertex[0], ssaoZero);
	RwIm2DVertexSetScreenZ(&SSAOVertex[0], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&SSAOVertex[0], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&SSAOVertex[0], recipCamZ);
	RwIm2DVertexSetU(&SSAOVertex[0], 0.0f, recipCamZ);
	RwIm2DVertexSetV(&SSAOVertex[0], 0.0f, recipCamZ);
	RwIm2DVertexSetIntRGBA(&SSAOVertex[0], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&SSAOVertex[1], ssaoZero);
	RwIm2DVertexSetScreenY(&SSAOVertex[1], ssaoYmax);
	RwIm2DVertexSetScreenZ(&SSAOVertex[1], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&SSAOVertex[1], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&SSAOVertex[1], recipCamZ);
	RwIm2DVertexSetU(&SSAOVertex[1], 0.0f, recipCamZ);
	RwIm2DVertexSetV(&SSAOVertex[1], 1.0f, recipCamZ);
	RwIm2DVertexSetIntRGBA(&SSAOVertex[1], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&SSAOVertex[2], ssaoXmax);
	RwIm2DVertexSetScreenY(&SSAOVertex[2], ssaoYmax);
	RwIm2DVertexSetScreenZ(&SSAOVertex[2], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&SSAOVertex[2], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&SSAOVertex[2], recipCamZ);
	RwIm2DVertexSetU(&SSAOVertex[2], 1.0f, recipCamZ);
	RwIm2DVertexSetV(&SSAOVertex[2], 1.0f, recipCamZ);
	RwIm2DVertexSetIntRGBA(&SSAOVertex[2], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&SSAOVertex[3], ssaoXmax);
	RwIm2DVertexSetScreenY(&SSAOVertex[3], ssaoZero);
	RwIm2DVertexSetScreenZ(&SSAOVertex[3], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&SSAOVertex[3], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&SSAOVertex[3], recipCamZ);
	RwIm2DVertexSetU(&SSAOVertex[3], 1.0f, recipCamZ);
	RwIm2DVertexSetV(&SSAOVertex[3], 0.0f, recipCamZ);
	RwIm2DVertexSetIntRGBA(&SSAOVertex[3], 255, 255, 255, 255);

	// ==========================================
	// Pass 1: Calculate SSAO using the float depth texture
	// ==========================================
	bindFramebuffer(ssaoNatRas->fbo);
	glViewport(0, 0, ssaoWidth, ssaoHeight);
	cam->frameBuffer = pSSAOBuffer;

	// Use the float depth texture directly via GL calls
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, depthFloatTex);

	im2dOverrideShader = ssaoCalc;
	ssaoCalc->use();
	
	// Set uniforms
	glUniform2f(ssaoCalc->uniformLocations[u_texelSize], texelSizeX, texelSizeY);
	glUniform1f(ssaoCalc->uniformLocations[u_ssaoRadius], SSAORadius);
	glUniform1f(ssaoCalc->uniformLocations[u_ssaoBias], SSAOBias);
	glUniform1f(ssaoCalc->uniformLocations[u_ssaoIntensity], SSAOIntensity);
	glUniform1f(ssaoCalc->uniformLocations[u_nearPlane], nearPlane);
	glUniform1f(ssaoCalc->uniformLocations[u_farPlane], farPlane);

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, SSAOVertex, 4, Index, 6);
	
	im2dOverrideShader = nil;

	// ==========================================
	// Pass 2: Blur SSAO
	// ==========================================
	bindFramebuffer(ssaoBlurNatRas->fbo);

	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pSSAOBuffer);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);

	im2dOverrideShader = ssaoBlur;
	ssaoBlur->use();
	glUniform2f(ssaoBlur->uniformLocations[u_texelSize], texelSizeX, texelSizeY);

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, SSAOVertex, 4, Index, 6);

	// Restore original camera frameBuffer
	cam->frameBuffer = origFrameBuffer;

	// Restore original framebuffer and viewport
	bindFramebuffer(camNatRas->fbo);
	glViewport(0, 0, screenWidth, screenHeight);

	// ==========================================
	// Pass 3: Apply SSAO to scene
	// ==========================================
	RwTextureSetRaster(pSSAOTex, pSSAOBlurBuffer);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pBackBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	rw::gl3::setTexture(1, pSSAOTex);
	im2dOverrideShader = ssaoApply;
	ssaoApply->use();

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);

	rw::gl3::setTexture(1, nil);
	im2dOverrideShader = nil;
#endif

	POP_RENDERGROUP();
}

bool
CPostFX::NeedBackBuffer(void)
{
	// Bloom always needs back buffer
	if(BloomEnable)
		return true;
	// SSAO always needs back buffer
	if(SSAOEnable)
		return true;

	// Current frame -- needed for non-blur effect
	switch(EffectSwitch){
	case POSTFX_OFF:
	case POSTFX_SIMPLE:
		// no actual rendering here
		return false;
	case POSTFX_NORMAL:
		if(MotionBlurOn)
			return false;
		else
			return true;
	case POSTFX_MOBILE:
		return true;
	}
	return false;
}

bool
CPostFX::NeedFrontBuffer(int32 type)
{
	// Last frame -- needed for motion blur
	if(CMBlur::Drunkness > 0.0f)
		return true;
	if(type == MOTION_BLUR_SNIPER)
		return true;

	switch(EffectSwitch){
	case POSTFX_OFF:
	case POSTFX_SIMPLE:
		// no actual rendering here
		return false;
	case POSTFX_NORMAL:
		if(MotionBlurOn)
			return true;
		else
			return false;
	case POSTFX_MOBILE:
		return false;
	}
	return false;
}

void
CPostFX::GetBackBuffer(RwCamera *cam)
{
	RwRasterPushContext(pBackBuffer);
	RwRasterRenderFast(RwCameraGetRaster(cam), 0, 0);
	RwRasterPopContext();
}

void
CPostFX::Render(RwCamera *cam, uint32 red, uint32 green, uint32 blue, uint32 blur, int32 type, uint32 bluralpha)
{
	PUSH_RENDERGROUP("CPostFX::Render");

	if(pFrontBuffer == nil)
		Open(cam);
	assert(pFrontBuffer);
	assert(pBackBuffer);

	if(type == MOTION_BLUR_LIGHT_SCENE){
		SmoothColor(red, green, blue, blur);
		red = AvgRed;
		green = AvgGreen;
		blue = AvgBlue;
		blur = AvgAlpha;
	}

	if(NeedBackBuffer())
		GetBackBuffer(cam);

	DefinedState();

	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);

	// Render SSAO effect (before bloom so darkened areas can be bloomed)
	RenderSSAO(cam);

	// Need to re-capture back buffer after SSAO has been applied
	if(SSAOEnable && BloomEnable)
		GetBackBuffer(cam);

	// Render bloom effect
	RenderBloom(cam);

	if(type == MOTION_BLUR_SNIPER){
		if(!bJustInitialised)
			RenderOverlaySniper(cam, red, green, blue, blur);
	}else switch(EffectSwitch){
	case POSTFX_OFF:
	case POSTFX_SIMPLE:
		// no actual rendering here
		break;
	case POSTFX_NORMAL:
		if(MotionBlurOn){
			if(!bJustInitialised)
				RenderOverlayBlur(cam, red, green, blue, blur);
		}else{
			RenderOverlayShader(cam, red, green, blue, blur);
		}
		break;
	case POSTFX_MOBILE:
		RenderOverlayShader(cam, red, green, blue, blur);
		break;
	}

	if(!bJustInitialised)
		RenderMotionBlur(cam, 175.0f * CMBlur::Drunkness);

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nil);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	if(NeedFrontBuffer(type)){
		RwRasterPushContext(pFrontBuffer);
		RwRasterRenderFast(RwCameraGetRaster(cam), 0, 0);
		RwRasterPopContext();
		bJustInitialised = false;
	}else
		bJustInitialised = true;

	POP_RENDERGROUP();
}

int CPostFX::PrevRed[NUMAVERAGE], CPostFX::AvgRed;
int CPostFX::PrevGreen[NUMAVERAGE], CPostFX::AvgGreen;
int CPostFX::PrevBlue[NUMAVERAGE], CPostFX::AvgBlue;
int CPostFX::PrevAlpha[NUMAVERAGE], CPostFX::AvgAlpha;
int CPostFX::Next;
int CPostFX::NumValues;

// This is rather annoying...the blur color can flicker slightly
// which becomes very visible when amplified by the shader
void
CPostFX::SmoothColor(uint32 red, uint32 green, uint32 blue, uint32 alpha)
{
	PrevRed[Next] = red;
	PrevGreen[Next] = green;
	PrevBlue[Next] = blue;
	PrevAlpha[Next] = alpha;
	Next = (Next+1) % NUMAVERAGE;
	NumValues = Min(NumValues+1, NUMAVERAGE);

	AvgRed = 0;
	AvgGreen = 0;
	AvgBlue = 0;
	AvgAlpha = 0;
	for(int i = 0; i < NumValues; i++){
		AvgRed += PrevRed[i];
		AvgGreen += PrevGreen[i];
		AvgBlue += PrevBlue[i];
		AvgAlpha += PrevAlpha[i];
	}
	AvgRed /= NumValues;
	AvgGreen /= NumValues;
	AvgBlue /= NumValues;
	AvgAlpha /= NumValues;
}

#endif
