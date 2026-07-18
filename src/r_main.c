#define GLAD_GLES2_IMPLEMENTATION
#include "r_main.h"
#include "r_texture.h"
#include "r_font.h"
#include "console.h"
#include "sys_main.h"
#include "sys_video.h"
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <stdarg.h>

/* ======== Shader Sources ======== */

static const char *s_tintedTextureVS =
	"#version 300 es\n"
	"uniform mat4 mvp_matrix;\n"
	"in vec2 a_vertex;\n"
	"in vec2 a_texcoord;\n"
	"in vec4 a_tint;\n"
	"in vec4 a_viewport;\n"
	"in vec3 a_texId;\n"
	"out vec2 v_screenPos;\n"
	"out vec2 v_texcoord;\n"
	"out vec4 v_tint;\n"
	"out vec4 v_viewport;\n"
	"out vec3 v_texId;\n"
	"void main(void) {\n"
	"  v_texcoord = a_texcoord;\n"
	"  v_tint = a_tint;\n"
	"  v_texId = a_texId;\n"
	"  vec2 vp0 = a_viewport.xy + vec2(0.0, a_viewport.w);\n"
	"  vec2 vp1 = a_viewport.xy + vec2(a_viewport.z, 0.0);\n"
	"  v_viewport = vec4(\n"
	"    (mvp_matrix * vec4(vp0, 0.0, 1.0)).xy,\n"
	"    (mvp_matrix * vec4(vp1, 0.0, 1.0)).xy);\n"
	"  vec4 pos = mvp_matrix * vec4(a_vertex + a_viewport.xy, 0.0, 1.0);\n"
	"  v_screenPos = pos.xy;\n"
	"  gl_Position = pos;\n"
	"}\n";

static const char *s_tintedTextureFS =
	"#version 300 es\n"
	"precision mediump float;\n"
	"uniform sampler2D s_tex[8];\n"
	"in vec2 v_screenPos;\n"
	"in vec2 v_texcoord;\n"
	"in vec4 v_tint;\n"
	"in vec4 v_viewport;\n"
	"in vec3 v_texId;\n"
	"out vec4 f_fragColor;\n"
	"void main(void) {\n"
	"  float x = v_screenPos.x, y = v_screenPos.y;\n"
	"  if (x < v_viewport.x || y < v_viewport.y || x >= v_viewport.z || y >= v_viewport.w) discard;\n"
	"  vec4 color;\n"
	"  int idx = int(v_texId.x + 0.5);\n"
	"  if (idx == 0) color = texture(s_tex[0], v_texcoord);\n"
	"  else if (idx == 1) color = texture(s_tex[1], v_texcoord);\n"
	"  else if (idx == 2) color = texture(s_tex[2], v_texcoord);\n"
	"  else if (idx == 3) color = texture(s_tex[3], v_texcoord);\n"
	"  else if (idx == 4) color = texture(s_tex[4], v_texcoord);\n"
	"  else if (idx == 5) color = texture(s_tex[5], v_texcoord);\n"
	"  else if (idx == 6) color = texture(s_tex[6], v_texcoord);\n"
	"  else color = texture(s_tex[7], v_texcoord);\n"
	"  f_fragColor = color * v_tint;\n"
	"}\n";

/* ======== Orthographic Matrix ======== */

static void OrthoMatrix(float *m, float left, float right, float bottom, float top, float nearVal, float farVal)
{
	memset(m, 0, 16 * sizeof(float));
	m[0]  =  2.0f / (right - left);
	m[5]  =  2.0f / (top - bottom);
	m[10] = -2.0f / (farVal - nearVal);
	m[12] = -(right + left) / (right - left);
	m[13] = -(top + bottom) / (top - bottom);
	m[14] = -(farVal + nearVal) / (farVal - nearVal);
	m[15] =  1.0f;
}

/* ======== Shader Compile Helpers ======== */

static GLuint CompileShader(GLenum type, const char *src)
{
	GLuint id = glCreateShader(type);
	glShaderSource(id, 1, &src, NULL);
	glCompileShader(id);
	GLint ok = 0;
	glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		GLint len = 0;
		glGetShaderInfoLog(id, sizeof(log) - 1, &len, log);
		log[len] = '\0';
		conWarning("Shader compile error: %s", log);
	}
	return id;
}

static GLuint LinkProgram(GLuint vs, GLuint fs)
{
	GLuint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	GLint ok = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		GLint len = 0;
		glGetProgramInfoLog(prog, sizeof(log) - 1, &len, log);
		log[len] = '\0';
		conWarning("Program link error: %s", log);
	}
	return prog;
}

/* ======== Layer Helpers ======== */

static size_t CommandSize(r_layerCmd_e cmd)
{
	switch (cmd) {
	case CMD_VIEWPORT: return sizeof(r_layerCmdViewport_t);
	case CMD_BLEND:    return sizeof(r_layerCmdBlend_t);
	case CMD_BIND:     return sizeof(r_layerCmdBind_t);
	case CMD_COLOR:    return sizeof(r_layerCmdColor_t);
	case CMD_QUAD:     return sizeof(r_layerCmdQuad_t);
	default: return 0;
	}
}

static r_layer_t *LayerCreate(r_renderer_t *ren, int layer, int subLayer)
{
	r_layer_t *l = (r_layer_t *)calloc(1, sizeof(r_layer_t));
	if (!l) return NULL;
	l->renderer = ren;
	l->layer = layer;
	l->subLayer = subLayer;
	l->cmdCursor = 0;
	l->numCmd = 0;
	return l;
}

static void LayerEmit(r_layer_t *layer, r_layerCmd_e type, const void *data, size_t size)
{
	size_t end = layer->cmdCursor + size;
	if (end > LAYER_CMD_BUF_SIZE) return;
	memcpy(layer->cmdBuf + layer->cmdCursor, data, size);
	layer->cmdCursor = end;
	layer->numCmd++;
}

static void LayerSetViewport(r_layer_t *layer, r_viewport_s *vp)
{
	r_layerCmdViewport_t cmd;
	cmd.cmd = CMD_VIEWPORT;
	cmd.viewport = *vp;
	LayerEmit(layer, CMD_VIEWPORT, &cmd, sizeof(cmd));
}

static void LayerSetBlendMode(r_layer_t *layer, int mode)
{
	r_layerCmdBlend_t cmd;
	cmd.cmd = CMD_BLEND;
	cmd.blendMode = mode;
	LayerEmit(layer, CMD_BLEND, &cmd, sizeof(cmd));
}

static void LayerBind(r_layer_t *layer, r_tex_t *tex)
{
	r_layerCmdBind_t cmd;
	cmd.cmd = CMD_BIND;
	cmd.tex = tex;
	LayerEmit(layer, CMD_BIND, &cmd, sizeof(cmd));
}

static void LayerColor(r_layer_t *layer, col4_t col)
{
	r_layerCmdColor_t cmd;
	cmd.cmd = CMD_COLOR;
	Vec4Copy(col, cmd.col);
	LayerEmit(layer, CMD_COLOR, &cmd, sizeof(cmd));
}

static void LayerQuad(r_layer_t *layer,
	float s0, float t0, float x0, float y0,
	float s1, float t1, float x1, float y1,
	float s2, float t2, float x2, float y2,
	float s3, float t3, float x3, float y3,
	int stackLayer, int maskLayer)
{
	r_layerCmdQuad_t cmd;
	cmd.cmd = CMD_QUAD;
	cmd.s[0] = s0; cmd.t[0] = t0; cmd.x[0] = x0; cmd.y[0] = y0;
	cmd.s[1] = s1; cmd.t[1] = t1; cmd.x[1] = x1; cmd.y[1] = y1;
	cmd.s[2] = s2; cmd.t[2] = t2; cmd.x[2] = x2; cmd.y[2] = y2;
	cmd.s[3] = s3; cmd.t[3] = t3; cmd.x[3] = x3; cmd.y[3] = y3;
	cmd.stackLayer = stackLayer;
	cmd.maskLayer = maskLayer;
	LayerEmit(layer, CMD_QUAD, &cmd, sizeof(cmd));
}

static void LayerDiscard(r_layer_t *layer)
{
	layer->cmdCursor = 0;
	layer->numCmd = 0;
}

static int LayerCompare(const void *a, const void *b)
{
	const r_layer_t *la = *(const r_layer_t **)a;
	const r_layer_t *lb = *(const r_layer_t **)b;
	if (la->layer != lb->layer) return la->layer - lb->layer;
	return la->subLayer - lb->subLayer;
}

/* ======== Layer Rendering ======== */

static void FlushBatch(r_renderer_t *ren, GLuint vbo, int *blendMode, r_tex_t **texs, int *texCount, Vertex *verts, int *vertCount)
{
	if (*vertCount == 0) return;

	if (*blendMode != -1) {
		switch (*blendMode) {
		case RB_ALPHA:
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			break;
		case RB_PRE_ALPHA:
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			break;
		case RB_ADDITIVE:
			glBlendFunc(GL_ONE, GL_ONE);
			break;
		}
	}
	for (int i = 0; i < *texCount; i++) {
		glActiveTexture(GL_TEXTURE0 + i);
		if (texs[i]) r_texBind(texs[i]);
		else glBindTexture(GL_TEXTURE_2D, 0);
	}
	glActiveTexture(GL_TEXTURE0);
	int vw = rVirtualScreenWidth(ren);
	int vh = rVirtualScreenHeight(ren);
	glViewport(0, 0, vw, vh);
	float mvp[16];
	OrthoMatrix(mvp, 0, (float)vw, (float)vh, 0, -9999, 9999);
	glUniformMatrix4fv(ren->mvpMatrixLoc, 1, GL_FALSE, mvp);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, *vertCount * sizeof(Vertex), verts, GL_STREAM_DRAW);
	glVertexAttribPointer(ren->xyAttr, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, x));
	glVertexAttribPointer(ren->uvAttr, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, u));
	glVertexAttribPointer(ren->tintAttr, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, r));
	glVertexAttribPointer(ren->viewportAttr, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, viewX));
	glVertexAttribPointer(ren->texIdAttr, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, texId));
	glEnableVertexAttribArray(ren->xyAttr);
	glEnableVertexAttribArray(ren->uvAttr);
	glEnableVertexAttribArray(ren->tintAttr);
	glEnableVertexAttribArray(ren->viewportAttr);
	glEnableVertexAttribArray(ren->texIdAttr);
	glDrawArrays(GL_TRIANGLES, 0, *vertCount);
	glDisableVertexAttribArray(ren->xyAttr);
	glDisableVertexAttribArray(ren->uvAttr);
	glDisableVertexAttribArray(ren->tintAttr);
	glDisableVertexAttribArray(ren->viewportAttr);
	glDisableVertexAttribArray(ren->texIdAttr);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	*vertCount = 0;
	*texCount = 0;
}

static void RenderLayer(r_renderer_t *ren, r_layer_t *layer)
{
	if (layer->numCmd == 0) return;

	GLuint prog = ren->tintedTextureProgram;
	glUseProgram(prog);

	/* Batch state */
	int curBlendMode = -1;
	r_tex_t *curTex = NULL;
	float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	r_viewport_s curVP = { 0, 0, 0, 0 };
	r_tex_t *batchTexs[8] = { NULL };
	int batchTexCount = 0;
	Vertex *batchVerts = (Vertex *)calloc(65536, sizeof(Vertex));
	int batchVertCount = 0;

	/* VBO */
	GLuint vbo;
	glGenBuffers(1, &vbo);

	/* Process commands */
	size_t offset = 0;
	while (offset < layer->cmdCursor) {
		r_layerCmd_e *type = (r_layerCmd_e *)(layer->cmdBuf + offset);

		switch (*type) {
		case CMD_VIEWPORT: {
			r_layerCmdViewport_t *cmd = (r_layerCmdViewport_t *)(layer->cmdBuf + offset);
			curVP = cmd->viewport;
			offset += sizeof(r_layerCmdViewport_t);
		} break;

		case CMD_BLEND: {
			r_layerCmdBlend_t *cmd = (r_layerCmdBlend_t *)(layer->cmdBuf + offset);
			curBlendMode = cmd->blendMode;
			offset += sizeof(r_layerCmdBlend_t);
		} break;

		case CMD_BIND: {
			r_layerCmdBind_t *cmd = (r_layerCmdBind_t *)(layer->cmdBuf + offset);
			curTex = cmd->tex;
			offset += sizeof(r_layerCmdBind_t);
		} break;

		case CMD_COLOR: {
			r_layerCmdColor_t *cmd = (r_layerCmdColor_t *)(layer->cmdBuf + offset);
			Vec4Copy(cmd->col, tint);
			offset += sizeof(r_layerCmdColor_t);
		} break;

		case CMD_QUAD: {
			r_layerCmdQuad_t *cmd = (r_layerCmdQuad_t *)(layer->cmdBuf + offset);

			/* Find or assign texture slot */
			int texSlot = -1;
			for (int i = 0; i < batchTexCount; i++) {
				if (batchTexs[i] == curTex) { texSlot = i; break; }
			}
			if (texSlot == -1) {
				if (batchTexCount >= 8) {
					FlushBatch(ren, vbo, &curBlendMode, batchTexs, &batchTexCount, batchVerts, &batchVertCount);
					texSlot = 0;
				} else {
					texSlot = batchTexCount;
				}
				batchTexs[batchTexCount++] = curTex;
			}

			/* Build 4 corner vertices, then emit 2 triangles (0-1-2, 0-2-3) */
			{
				Vertex corners[4];
				/* Flush if vertex buffer would overflow (65536 / 6 = ~10922 quads max) */
				if (batchVertCount + 6 > 65536) {
					FlushBatch(ren, vbo, &curBlendMode, batchTexs, &batchTexCount, batchVerts, &batchVertCount);
				}
				for (int v = 0; v < 4; v++) {
					corners[v].x = cmd->x[v];
					corners[v].y = cmd->y[v];
					corners[v].u = cmd->s[v];
					corners[v].v = cmd->t[v];
					corners[v].r = tint[0];
					corners[v].g = tint[1];
					corners[v].b = tint[2];
					corners[v].a = tint[3];
					corners[v].viewX = (float)curVP.x;
					corners[v].viewY = (float)curVP.y;
					corners[v].viewW = (float)curVP.width;
					corners[v].viewH = (float)curVP.height;
					corners[v].texId = (float)texSlot;
					corners[v].stackIdx = (float)cmd->stackLayer;
					corners[v].maskIdx = (float)cmd->maskLayer;
				}
				/* Triangle 1: 0,1,2  Triangle 2: 0,2,3 */
				batchVerts[batchVertCount++] = corners[0];
				batchVerts[batchVertCount++] = corners[1];
				batchVerts[batchVertCount++] = corners[2];
				batchVerts[batchVertCount++] = corners[0];
				batchVerts[batchVertCount++] = corners[2];
				batchVerts[batchVertCount++] = corners[3];
			}

			offset += sizeof(r_layerCmdQuad_t);
		} break;

		default:
			offset += CommandSize(*type);
			break;
		}
	}

	/* Flush remaining batch */
	FlushBatch(ren, vbo, &curBlendMode, batchTexs, &batchTexCount, batchVerts, &batchVertCount);

	glDeleteBuffers(1, &vbo);
	glUseProgram(0);
	free(batchVerts);
}

/* ======== Renderer Init ======== */

void rInit(r_renderer_t *ren, sys_main_t *sys, r_featureFlag_e features)
{
	memset(ren, 0, sizeof(r_renderer_t));
	ren->sys = sys;
	ren->apiDpiAware = !!(features & F_DPI_AWARE);

	/* GL strings */
	ren->st_vendor = (const char *)glGetString(GL_VENDOR);
	ren->st_renderer = (const char *)glGetString(GL_RENDERER);
	ren->st_ver = (const char *)glGetString(GL_VERSION);
	ren->st_ext = (const char *)glGetString(GL_EXTENSIONS);

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &ren->texMaxDim);
	conPrintf("GL_MAX_TEXTURE_SIZE: %d\n", ren->texMaxDim);
	conPrintf("GL_RENDERER: %s\n", ren->st_renderer);
	conPrintf("GL_VERSION: %s\n", ren->st_ver);

	/* Default GL state */
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);

	/* Compile tinted texture program */
	GLuint vs = CompileShader(GL_VERTEX_SHADER, s_tintedTextureVS);
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, s_tintedTextureFS);
	ren->tintedTextureProgram = LinkProgram(vs, fs);
	glDeleteShader(vs);
	glDeleteShader(fs);

	/* Get uniform/attribute locations */
	ren->mvpMatrixLoc = glGetUniformLocation(ren->tintedTextureProgram, "mvp_matrix");
	ren->xyAttr = glGetAttribLocation(ren->tintedTextureProgram, "a_vertex");
	ren->uvAttr = glGetAttribLocation(ren->tintedTextureProgram, "a_texcoord");
	ren->tintAttr = glGetAttribLocation(ren->tintedTextureProgram, "a_tint");
	ren->viewportAttr = glGetAttribLocation(ren->tintedTextureProgram, "a_viewport");
	ren->texIdAttr = glGetAttribLocation(ren->tintedTextureProgram, "a_texId");

	glUseProgram(ren->tintedTextureProgram);
	for (int i = 0; i < 8; i++) {
		char name[32];
		snprintf(name, sizeof(name), "s_tex[%d]", i);
		ren->texLocs[i] = glGetUniformLocation(ren->tintedTextureProgram, name);
		glUniform1i(ren->texLocs[i], i);
	}
	glUseProgram(0);

	/* Clear color */
	ren->clearColor[0] = 0.0f;
	ren->clearColor[1] = 0.0f;
	ren->clearColor[2] = 0.0f;
	ren->clearColor[3] = 1.0f;

	/* Draw state */
	ren->drawColor[0] = 1.0f;
	ren->drawColor[1] = 1.0f;
	ren->drawColor[2] = 1.0f;
	ren->drawColor[3] = 1.0f;
	ren->curBlendMode = RB_ALPHA;

	/* Initialize texture manager */
	r_texManagerInit(ren);

	/* Layer system */
	ren->numLayer = 0;
	ren->layerListSize = 16;
	ren->layerList = (r_layer_t **)calloc(ren->layerListSize, sizeof(r_layer_t *));
	ren->curLayer = NULL;

	/* Shader list */
	ren->numShader = 0;
	memset(ren->shaderList, 0, sizeof(ren->shaderList));

	/* White image */
	ren->whiteImage = rRegisterShader(ren, "@white", 0);

	/* Font init */
	for (int i = 0; i < F_NUMFONTS; i++) {
		ren->fonts[i] = NULL;
	}
	ren->fonts[F_FIXED] = r_fontCreate(ren, "Bitstream Vera Sans Mono");
	ren->fonts[F_VAR] = r_fontCreate(ren, "Liberation Sans");
	ren->fonts[F_VAR_BOLD] = r_fontCreate(ren, "Liberation Sans Bold");
	ren->fonts[F_FONTIN_SC] = r_fontCreate(ren, "Fontin SmallCaps");
	ren->fonts[F_FONTIN_SC_ITALIC] = r_fontCreate(ren, "Fontin SmallCaps Italic");
	ren->fonts[F_FONTIN] = r_fontCreate(ren, "Fontin");
	ren->fonts[F_FONTIN_ITALIC] = r_fontCreate(ren, "Fontin Italic");

	/* Default layer */
	rSetDrawLayer(ren, 0, 0);

	/* DPI scale */
	ren->dpiScale = 1.0f;
	if (ren->sys && ren->sys->video && ren->sys->video->wnd) {
		float xScale = 1.0f, yScale = 1.0f;
		glfwGetWindowContentScale((GLFWwindow *)ren->sys->video->wnd, &xScale, &yScale);
		ren->dpiScale = xScale;
	}
	ren->dpiScaleOverridePercent = 0;

	conPrintf("Renderer initialised.\n");
}

void rShutdown(r_renderer_t *ren)
{
	conPrintf("Render Shutdown\n");

	/* Free fonts */
	for (int i = 0; i < F_NUMFONTS; i++) {
		r_fontDestroy(ren->fonts[i]);
		ren->fonts[i] = NULL;
	}

	/* Free layers */
	for (int i = 0; i < ren->numLayer; i++) {
		free(ren->layerList[i]);
	}
	free(ren->layerList);
	ren->layerList = NULL;

	/* Free white image handle (before shaders, since it references a shader) */
	if (ren->whiteImage) {
		free(ren->whiteImage);
		ren->whiteImage = NULL;
	}

	/* Free shaders (including white image's shader, which is in the list) */
	for (int i = 0; i < ren->numShader; i++) {
		if (ren->shaderList[i]) {
			r_texDestroy(ren->shaderList[i]->tex);
			free(ren->shaderList[i]);
			ren->shaderList[i] = NULL;
		}
	}

	/* Shutdown texture manager */
	r_texManagerShutdown();

	/* Delete GL program */
	glDeleteProgram(ren->tintedTextureProgram);

	conPrintf("Renderer shutdown complete.\n");
}

/* ======== Frame ======== */

void rBeginFrame(r_renderer_t *ren)
{
	r_texManagerProcessUploads();
	rPurgeShaders(ren);

	/* Ensure default layer exists */
	if (ren->numLayer == 0) {
		rSetDrawLayer(ren, 0, 0);
	}
	ren->curLayer = ren->layerList[0];

	rSetViewport(ren, 0, 0, 0, 0);
	rSetBlendMode(ren, RB_ALPHA);
	rDrawColor(ren, NULL);
}

void rEndFrame(r_renderer_t *ren)
{
	/* Sort and render layers */
	r_layer_t **sorted = (r_layer_t **)calloc(ren->numLayer, sizeof(r_layer_t *));
	for (int i = 0; i < ren->numLayer; i++) {
		sorted[i] = ren->layerList[i];
	}
	qsort(sorted, ren->numLayer, sizeof(r_layer_t *), LayerCompare);

	glClearColor(ren->clearColor[0], ren->clearColor[1], ren->clearColor[2], ren->clearColor[3]);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	for (int i = 0; i < ren->numLayer; i++) {
		RenderLayer(ren, sorted[i]);
		LayerDiscard(sorted[i]);
	}

	free(sorted);
}

/* ======== Shader Management ======== */

r_shaderHnd_t *rRegisterShader(r_renderer_t *ren, const char *name, int flags)
{
	if (!name || !*name) return NULL;

	dword nameHash = StringHash(name, 0xFFFF);

	/* Check existing */
	for (int i = 0; i < ren->numShader; i++) {
		r_shader_t *sh = ren->shaderList[i];
		if (sh && sh->nameHash == nameHash && strcasecmp(name, sh->name) == 0 && sh->tex->flags == flags) {
			r_shaderHnd_t *hnd = (r_shaderHnd_t *)calloc(1, sizeof(r_shaderHnd_t));
			hnd->sh = sh;
			sh->refCount++;
			return hnd;
		}
	}

	/* Find free slot */
	int slot = -1;
	for (int i = 0; i < ren->numShader; i++) {
		if (!ren->shaderList[i]) { slot = i; break; }
	}
	if (slot == -1) {
		if (ren->numShader >= R_MAXSHADERS) {
			conWarning("shader limit reached");
			return NULL;
		}
		slot = ren->numShader++;
	}

	r_shader_t *sh = (r_shader_t *)calloc(1, sizeof(r_shader_t));
	sh->renderer = ren;
	strncpy(sh->name, name, sizeof(sh->name) - 1);
	sh->nameHash = nameHash;
	sh->refCount = 1;
	sh->tex = r_texCreate(ren, name, flags);
	if (sh->tex && sh->tex->error) {
		conWarning("couldn't load texture '%s'", name);
	}
	ren->shaderList[slot] = sh;

	r_shaderHnd_t *hnd = (r_shaderHnd_t *)calloc(1, sizeof(r_shaderHnd_t));
	hnd->sh = sh;
	return hnd;
}

r_shaderHnd_t *rRegisterShaderFromImage(r_renderer_t *ren, GLuint texId, int width, int height, int flags)
{
	/* Check if this texId is already registered */
	for (int i = 0; i < ren->numShader; i++) {
		r_shader_t *sh = ren->shaderList[i];
		if (sh && sh->tex && sh->tex->texId == texId) {
			r_shaderHnd_t *hnd = (r_shaderHnd_t *)calloc(1, sizeof(r_shaderHnd_t));
			hnd->sh = sh;
			sh->refCount++;
			return hnd;
		}
	}

	int slot = -1;
	for (int i = 0; i < ren->numShader; i++) {
		if (!ren->shaderList[i]) { slot = i; break; }
	}
	if (slot == -1) {
		if (ren->numShader >= R_MAXSHADERS) {
			conWarning("shader limit reached");
			return NULL;
		}
		slot = ren->numShader++;
	}

	r_shader_t *sh = (r_shader_t *)calloc(1, sizeof(r_shader_t));
	sh->renderer = ren;
	snprintf(sh->name, sizeof(sh->name), "data:%d", slot);
	sh->nameHash = StringHash(sh->name, 0xFFFF);
	sh->refCount = 1;

	r_tex_t *tex = (r_tex_t *)calloc(1, sizeof(r_tex_t));
	tex->texId = texId;
	tex->width = width;
	tex->height = height;
	tex->fileWidth = width;
	tex->fileHeight = height;
	tex->flags = flags;
	tex->target = GL_TEXTURE_2D;
	tex->stackLayers = 1;
	sh->tex = tex;

	ren->shaderList[slot] = sh;

	r_shaderHnd_t *hnd = (r_shaderHnd_t *)calloc(1, sizeof(r_shaderHnd_t));
	hnd->sh = sh;
	return hnd;
}

void rGetShaderImageSize(r_shaderHnd_t *hnd, int *width, int *height)
{
	if (hnd && hnd->sh && hnd->sh->tex) {
		if (width) *width = hnd->sh->tex->fileWidth;
		if (height) *height = hnd->sh->tex->fileHeight;
	} else {
		if (width) *width = 0;
		if (height) *height = 0;
	}
}

void rSetShaderLoadingPriority(r_shaderHnd_t *hnd, int pri)
{
	(void)hnd; (void)pri;
}

void rPurgeShaders(r_renderer_t *ren)
{
	for (int i = 0; i < ren->numShader; i++) {
		r_shader_t *sh = ren->shaderList[i];
		if (sh && sh->refCount == 0) {
			r_texDestroy(sh->tex);
			free(sh);
			ren->shaderList[i] = NULL;
		}
	}
}

int rGetAsyncCount(r_renderer_t *ren)
{
	(void)ren;
	return 0;
}

/* ======== Draw State ======== */

void rSetClearColor(r_renderer_t *ren, const col4_t col)
{
	if (col) {
		ren->clearColor[0] = col[0];
		ren->clearColor[1] = col[1];
		ren->clearColor[2] = col[2];
		ren->clearColor[3] = col[3];
	}
}

void rSetDrawLayer(r_renderer_t *ren, int layer, int subLayer)
{
	/* Find existing layer */
	for (int i = 0; i < ren->numLayer; i++) {
		if (ren->layerList[i]->layer == layer && ren->layerList[i]->subLayer == subLayer) {
			ren->curLayer = ren->layerList[i];
			LayerSetViewport(ren->curLayer, &ren->curViewport);
			LayerSetBlendMode(ren->curLayer, ren->curBlendMode);
			return;
		}
	}
	/* Create new layer */
	if (ren->numLayer >= ren->layerListSize) {
		ren->layerListSize *= 2;
		ren->layerList = (r_layer_t **)realloc(ren->layerList, ren->layerListSize * sizeof(r_layer_t *));
	}
	r_layer_t *l = LayerCreate(ren, layer, subLayer);
	ren->layerList[ren->numLayer++] = l;
	ren->curLayer = l;
	LayerSetViewport(ren->curLayer, &ren->curViewport);
	LayerSetBlendMode(ren->curLayer, ren->curBlendMode);
}

void rSetDrawSubLayer(r_renderer_t *ren, int subLayer)
{
	if (!ren->curLayer) return;
	rSetDrawLayer(ren, ren->curLayer->layer, subLayer);
}

int rGetDrawLayer(r_renderer_t *ren)
{
	return ren->curLayer ? ren->curLayer->subLayer : 0;
}

void rSetViewport(r_renderer_t *ren, int x, int y, int width, int height)
{
	if (width == 0 && height == 0) {
		width = rVirtualScreenWidth(ren);
		height = rVirtualScreenHeight(ren);
	}
	ren->curViewport.x = x;
	ren->curViewport.y = y;
	ren->curViewport.width = width;
	ren->curViewport.height = height;
	LayerSetViewport(ren->curLayer, &ren->curViewport);
}

void rSetBlendMode(r_renderer_t *ren, int mode)
{
	ren->curBlendMode = mode;
	LayerSetBlendMode(ren->curLayer, mode);
}

void rDrawColor(r_renderer_t *ren, const col4_t col)
{
	if (col) {
		Vec4Copy(col, ren->drawColor);
	} else {
		ren->drawColor[0] = 1.0f;
		ren->drawColor[1] = 1.0f;
		ren->drawColor[2] = 1.0f;
		ren->drawColor[3] = 1.0f;
	}
}

void rDrawColorDword(r_renderer_t *ren, dword col)
{
	ren->drawColor[0] = ((col >> 16) & 0xFF) / 255.0f;
	ren->drawColor[1] = ((col >> 8) & 0xFF) / 255.0f;
	ren->drawColor[2] = (col & 0xFF) / 255.0f;
	ren->drawColor[3] = (col >> 24) / 255.0f;
}

void rGetDrawColor(r_renderer_t *ren, col4_t color)
{
	Vec4Copy(ren->drawColor, color);
}

/* ======== Drawing ======== */

void rDrawImage(r_renderer_t *ren, r_shaderHnd_t *hnd, float px, float py, float ex, float ey,
                 float uv0x, float uv0y, float uv2x, float uv2y,
                 int stackLayer, int maskLayer)
{
	rDrawImageQuad(ren, hnd,
		px, py,
		px + ex, py,
		px + ex, py + ey,
		px, py + ey,
		uv0x, uv0y,
		uv2x, uv0y,
		uv2x, uv2y,
		uv0x, uv2y,
		stackLayer, maskLayer);
}

void rDrawImageQuad(r_renderer_t *ren, r_shaderHnd_t *hnd,
                     float p0x, float p0y, float p1x, float p1y,
                     float p2x, float p2y, float p3x, float p3y,
                     float uv0x, float uv0y, float uv1x, float uv1y,
                     float uv2x, float uv2y, float uv3x, float uv3y,
                     int stackLayer, int maskLayer)
{
	r_tex_t *tex = NULL;
	if (hnd && hnd->sh) {
		tex = hnd->sh->tex;
		if (stackLayer < 0) stackLayer = 0;
		if (stackLayer >= (int)tex->stackLayers) stackLayer = (int)tex->stackLayers - 1;
	} else {
		tex = ren->whiteImage->sh->tex;
		stackLayer = 0;
	}

	LayerBind(ren->curLayer, tex);
	LayerColor(ren->curLayer, ren->drawColor);
	LayerQuad(ren->curLayer,
		uv0x, uv0y, p0x, p0y,
		uv1x, uv1y, p1x, p1y,
		uv2x, uv2y, p2x, p2y,
		uv3x, uv3y, p3x, p3y,
		stackLayer, maskLayer);
}

/* ======== String Drawing ======== */

void rDrawString(r_renderer_t *ren, float x, float y, int align, int height,
                  const col4_t col, int font, const char *str)
{
	if (font < 0 || font >= F_NUMFONTS) font = F_FIXED;
	if (!ren->fonts[font]) return;

	const col4_t *useCol = col ? (const col4_t *)col : &ren->drawColor;
	r_fontDrawString(ren->fonts[font], x, y, align, height, *useCol, str);
}

int rDrawStringWidth(r_renderer_t *ren, int height, int font, const char *str)
{
	if (!str || !*str) return 0;
	if (font < 0 || font >= F_NUMFONTS) font = F_FIXED;
	if (!ren->fonts[font]) return 0;
	return r_fontStringWidth(ren->fonts[font], height, str);
}

int rDrawStringCursorIndex(r_renderer_t *ren, int height, int font, const char *str, int curX, int curY)
{
	if (!str || !*str) return 0;
	if (font < 0 || font >= F_NUMFONTS) font = F_FIXED;
	if (!ren->fonts[font]) return (int)strlen(str);
	return r_fontCursorIndex(ren->fonts[font], height, str, curX, curY);
}

/* ======== Virtual Screen ======== */

int rVirtualScreenWidth(r_renderer_t *ren)
{
	int properWidth = ren->sys->video->scrSize[0];
	if (ren->apiDpiAware) {
		return properWidth;
	}
	if (ren->dpiScale > 0.001f) {
		return (int)((float)properWidth / ren->dpiScale);
	}
	return properWidth;
}

int rVirtualScreenHeight(r_renderer_t *ren)
{
	int properHeight = ren->sys->video->scrSize[1];
	if (ren->apiDpiAware) {
		return properHeight;
	}
	if (ren->dpiScale > 0.001f) {
		return (int)((float)properHeight / ren->dpiScale);
	}
	return properHeight;
}

float rVirtualScreenScaleFactor(r_renderer_t *ren)
{
	if (ren->apiDpiAware) {
		if (ren->dpiScaleOverridePercent > 0) {
			return (float)ren->dpiScaleOverridePercent / 100.0f;
		}
		return ren->dpiScale;
	}
	return 1.0f;
}
