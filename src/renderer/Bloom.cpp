#include "common.h"
#include "Bloom.h"
#include "RwHelper.h"
#include "Camera.h"
#include "Frontend.h"
#include "vendor/librw/src/gl/glad/glad.h"

bool CBloom::m_bInitialised = false;
bool CBloom::m_bBloomOn = true;
float CBloom::m_fBloomIntensity = 1.2f;
float CBloom::m_fBloomThreshold = 0.8f;
float CBloom::m_fBloomSoftness = 0.5f;
CBloom::BloomQuality CBloom::m_eQuality = BLOOM_MEDIUM;

RwRaster *CBloom::pBrightPass = nullptr;
RwRaster *CBloom::pBlurBuffer1 = nullptr;
RwRaster *CBloom::pBlurBuffer2 = nullptr;

static GLuint sceneFBO = 0, bloomFBO = 0;
static GLuint sceneTex = 0, brightTex = 0, blurTex = 0;
static GLuint quadVAO = 0, quadVBO = 0;
static GLuint brightProg = 0, blurProg = 0, compProg = 0;
static int32 lastW = 0, lastH = 0;

static const char *vs = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

static const char *brightFS = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D tex;
uniform float threshold;
void main() {
    vec4 c = texture(tex, TexCoord);
    float lum = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec3 b = c.rgb * smoothstep(threshold, threshold + 0.1, lum);
    FragColor = vec4(b * 1.2, 1.0);
}
)";

static const char *blurFS = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D tex;
uniform vec2 res;
uniform bool horiz;
void main() {
    vec2 off = 1.0 / res;
    vec3 sum = vec3(0.0);
    float w[9] = float[](0.05,0.09,0.12,0.15,0.16,0.15,0.12,0.09,0.05);
    for(int i = -4; i <= 4; i++) {
        vec2 o = horiz ? vec2(float(i)*off.x,0) : vec2(0,float(i)*off.y);
        sum += texture(tex, TexCoord + o).rgb * w[i+4];
    }
    FragColor = vec4(sum, 1.0);
}
)";

static const char *compFS = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D sceneTex;
uniform sampler2D bloomTex;
uniform float intensity;
void main() {
    vec3 scene = texture(sceneTex, TexCoord).rgb;
    vec3 bloom = texture(bloomTex, TexCoord).rgb;
    vec3 c = scene + bloom * intensity;
    c = c / (c + vec3(1.0));
    c = pow(c, vec3(1.0/2.2));
    FragColor = vec4(c, 1.0);
}
)";

static GLuint compileShader(GLenum t, const char *s)
{
    GLuint sh = glCreateShader(t);
    glShaderSource(sh, 1, &s, nullptr);
    glCompileShader(sh);
    return sh;
}

static GLuint createProgram(const char *v, const char *f)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, compileShader(GL_VERTEX_SHADER, v));
    glAttachShader(p, compileShader(GL_FRAGMENT_SHADER, f));
    glLinkProgram(p);
    return p;
}

static void createQuad()
{
    float v[] = {-1,1,0,1, -1,-1,0,0, 1,-1,1,0, 1,1,1,1};
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, 0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)(8));
}

static GLuint createFBO(int w, int h, GLuint *texture)
{
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    
    glGenTextures(1, texture);
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texture, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fbo;
}

bool CBloom::Initialise(void)
{
    if (m_bInitialised) return true;
    
    printf("CBloom: Initialising complete FBO pipeline...\n");
    
    brightProg = createProgram(vs, brightFS);
    blurProg = createProgram(vs, blurFS);
    compProg = createProgram(vs, compFS);
    
    createQuad();
    
    sceneFBO = createFBO(1920, 1080, &sceneTex);
    bloomFBO = createFBO(480, 270, &brightTex);
    blurTex = brightTex;
    
    m_bInitialised = true;
    printf("CBloom: FBO pipeline ready\n");
    return true;
}

void CBloom::Shutdown(void)
{
    if (sceneFBO) glDeleteFramebuffers(1, &sceneFBO);
    if (bloomFBO) glDeleteFramebuffers(1, &bloomFBO);
    if (sceneTex) glDeleteTextures(1, &sceneTex);
    if (brightTex) glDeleteTextures(1, &brightTex);
    if (blurTex && blurTex != brightTex) glDeleteTextures(1, &blurTex);
    if (quadVAO) glDeleteVertexArrays(1, &quadVAO);
    if (brightProg) glDeleteProgram(brightProg);
    if (blurProg) glDeleteProgram(blurProg);
    if (compProg) glDeleteProgram(compProg);
    
    sceneFBO = bloomFBO = sceneTex = brightTex = blurTex = quadVAO = brightProg = blurProg = compProg = 0;
    m_bInitialised = false;
}

void CBloom::Render(RwCamera *cam)
{
    if (!m_bBloomOn || !m_bInitialised || !cam) return;
    
    RwRaster *sceneRaster = RwCameraGetRaster(cam);
    int32 w = RwRasterGetWidth(sceneRaster);
    int32 h = RwRasterGetHeight(sceneRaster);
    
    if (w != lastW || h != lastH) {
        if (sceneTex) glDeleteTextures(1, &sceneTex);
        if (brightTex) glDeleteTextures(1, &brightTex);
        if (blurTex && blurTex != brightTex) glDeleteTextures(1, &blurTex);
        
        int bw = w/4, bh = h/4;
        sceneFBO = createFBO(w, h, &sceneTex);
        bloomFBO = createFBO(bw, bh, &brightTex);
        blurTex = brightTex;
        lastW = w; lastH = h;
    }
    
    // Step 1: Bright pass (downsample + threshold)
    glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO);
    glViewport(0, 0, w/4, h/4);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(brightProg);
    glUniform1f(glGetUniformLocation(brightProg, "threshold"), m_fBloomThreshold);
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    
    // Step 2: Blur passes
    int bw = w/4, bh = h/4;
    for (int i = 0; i < 3; i++) {
        GLuint src = (i % 2 == 0) ? brightTex : blurTex;
        glBindTexture(GL_TEXTURE_2D, src);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        GLuint tmp = brightTex; brightTex = blurTex; blurTex = tmp;
    }
    
    // Step 3: Composite to screen
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glUseProgram(compProg);
    glUniform1f(glGetUniformLocation(compProg, "intensity"), m_fBloomIntensity);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, blurTex);
    
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    
    printf("CBloom: Render complete\n");
}
