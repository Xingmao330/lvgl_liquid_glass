#include "liquid_glass_shader.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LIQUID_GLASS_ENABLE_OPENGL
#define LIQUID_GLASS_ENABLE_OPENGL 0
#endif

#if LIQUID_GLASS_ENABLE_OPENGL

typedef GLuint (APIENTRYP create_shader_fn)(GLenum type);
typedef void (APIENTRYP shader_source_fn)(GLuint shader, GLsizei count,
                                          const GLchar * const * strings,
                                          const GLint * lengths);
typedef void (APIENTRYP compile_shader_fn)(GLuint shader);
typedef void (APIENTRYP get_shader_iv_fn)(GLuint shader, GLenum pname, GLint * value);
typedef void (APIENTRYP get_shader_info_log_fn)(GLuint shader, GLsizei bufsize, GLsizei * length, char * info_log);
typedef void (APIENTRYP get_program_info_log_fn)(GLuint program, GLsizei bufsize, GLsizei * length, char * info_log);
typedef void (APIENTRYP delete_shader_fn)(GLuint shader);
typedef GLuint (APIENTRYP create_program_fn)(void);
typedef void (APIENTRYP attach_shader_fn)(GLuint program, GLuint shader);
typedef void (APIENTRYP bind_attrib_location_fn)(GLuint program, GLuint index,
                                                  const GLchar * name);
typedef void (APIENTRYP link_program_fn)(GLuint program);
typedef void (APIENTRYP get_program_iv_fn)(GLuint program, GLenum pname, GLint * value);
typedef void (APIENTRYP delete_program_fn)(GLuint program);
typedef void (APIENTRYP use_program_fn)(GLuint program);
typedef GLint (APIENTRYP get_uniform_location_fn)(GLuint program, const GLchar * name);
typedef void (APIENTRYP uniform_1i_fn)(GLint location, GLint value);
typedef void (APIENTRYP uniform_1f_fn)(GLint location, GLfloat value);
typedef void (APIENTRYP uniform_2f_fn)(GLint location, GLfloat x, GLfloat y);
typedef void (APIENTRYP uniform_3f_fn)(GLint location, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP active_texture_fn)(GLenum texture);
typedef void (APIENTRYP enable_vertex_attrib_array_fn)(GLuint index);
typedef void (APIENTRYP disable_vertex_attrib_array_fn)(GLuint index);
typedef void (APIENTRYP vertex_attrib_pointer_fn)(GLuint index, GLint size, GLenum type,
                                                   GLboolean normalized, GLsizei stride,
                                                   const GLvoid * pointer);
typedef void (APIENTRYP gen_framebuffers_fn)(GLsizei count, GLuint * framebuffers);
typedef void (APIENTRYP bind_framebuffer_fn)(GLenum target, GLuint framebuffer);
typedef void (APIENTRYP framebuffer_texture_2d_fn)(GLenum target, GLenum attachment,
                                                    GLenum textarget, GLuint texture,
                                                    GLint level);
typedef GLenum (APIENTRYP check_framebuffer_status_fn)(GLenum target);
typedef void (APIENTRYP delete_framebuffers_fn)(GLsizei count,
                                                 const GLuint * framebuffers);

static create_shader_fn p_create_shader;
static shader_source_fn p_shader_source;
static compile_shader_fn p_compile_shader;
static get_shader_iv_fn p_get_shader_iv;
static get_shader_info_log_fn p_get_shader_info_log;
static get_program_info_log_fn p_get_program_info_log;
static delete_shader_fn p_delete_shader;
static create_program_fn p_create_program;
static attach_shader_fn p_attach_shader;
static bind_attrib_location_fn p_bind_attrib_location;
static link_program_fn p_link_program;
static get_program_iv_fn p_get_program_iv;
static delete_program_fn p_delete_program;
static use_program_fn p_use_program;
static get_uniform_location_fn p_get_uniform_location;
static uniform_1i_fn p_uniform_1i;
static uniform_1f_fn p_uniform_1f;
static uniform_2f_fn p_uniform_2f;
static uniform_3f_fn p_uniform_3f;
static active_texture_fn p_active_texture;
static enable_vertex_attrib_array_fn p_enable_vertex_attrib_array;
static disable_vertex_attrib_array_fn p_disable_vertex_attrib_array;
static vertex_attrib_pointer_fn p_vertex_attrib_pointer;
static gen_framebuffers_fn p_gen_framebuffers;
static bind_framebuffer_fn p_bind_framebuffer;
static framebuffer_texture_2d_fn p_framebuffer_texture_2d;
static check_framebuffer_status_fn p_check_framebuffer_status;
static delete_framebuffers_fn p_delete_framebuffers;

static SDL_Window * gl_window;
static SDL_GLContext gl_context;
static GLuint input_texture;
static GLuint output_texture;
static GLuint framebuffer;
static uint8_t * rgba_source;
static uint8_t * rgba_output;
static size_t rgba_capacity;

static const char * vertex_source =
  "#version 120\n"
  "attribute vec2 a_pos; attribute vec2 a_uv; varying vec2 v_uv;\n"
  "void main(){v_uv=a_uv;gl_Position=vec4(a_pos,0.0,1.0);}";

static const char * fragment_source =
  "#version 120\n"
  "uniform sampler2D u_backdrop; uniform vec2 u_screen_size;\n"
  "uniform vec2 u_origin; uniform vec2 u_size; uniform float u_radius;\n"
  "uniform float u_refraction_height; uniform float u_refraction_amount;\n"
  "uniform float u_dispersion; uniform float u_depth_effect;\n"
  "uniform float u_vibrancy; uniform float u_highlight;\n"
  "uniform vec2 u_view_scale; varying vec2 v_uv;\n"
  "float sd(vec2 p,vec2 h,float r){vec2 q=abs(p)-(h-vec2(r));return length(max(q,0.0))-r+min(max(q.x,q.y),0.0);}\n"
  "vec2 normal_at(vec2 p,vec2 h,float r){vec2 q=abs(p)-(h-vec2(r));vec2 c=sign(p)*max(q,0.0);float l=length(c);if(l>0.001)return c/l;float gx=step(q.y,q.x);return sign(p)*vec2(gx,1.0-gx);}\n"
  "vec3 sample_screen(vec2 px){return texture2D(u_backdrop,clamp(px/u_screen_size,0.0,1.0)).rgb;}\n"
  "void main(){\n"
  "vec2 local=v_uv*u_size; vec2 p=local-u_size*0.5; vec2 h=u_size*0.5;\n"
  "vec2 kscale=u_view_scale*(1.0/256.0);\n"
  "vec2 base=u_origin+h+(local-h)*kscale;\n"
  "float d=sd(p,h,u_radius);\n"
  "if(d>=0.0||-d>=u_refraction_height){gl_FragColor=vec4(sample_screen(base),1.0);return;}\n"
  "float depth=min(-d/max(u_refraction_height,0.001),1.0);\n"
  "float curve=1.0-sqrt(max(0.0,1.0-(1.0-depth)*(1.0-depth)));\n"
  "vec2 n=normal_at(p,h,min(u_radius*1.5,min(h.x,h.y)));\n"
  "if(u_depth_effect>0.5)n=normalize(n+normalize(p+vec2(0.0001)));\n"
  /* Android AGSL: refractedCoord = coord + d * grad (outward). */
  "vec2 refracted=base+curve*u_refraction_amount*n*kscale;\n"
  "float dispersion_scale=min(u_dispersion,3.0)*(p.x*p.y)/(h.x*h.y);\n"
  "vec2 spread=n*(curve*u_refraction_amount)*dispersion_scale*kscale;\n"
  "vec3 color=vec3(0.0);\n"
  "vec3 red=sample_screen(refracted+spread);\n"
  "vec3 orange=sample_screen(refracted+spread*(2.0/3.0));\n"
  "vec3 yellow=sample_screen(refracted+spread*(1.0/3.0));\n"
  "vec3 green=sample_screen(refracted);\n"
  "vec3 cyan=sample_screen(refracted-spread*(1.0/3.0));\n"
  "vec3 blue=sample_screen(refracted-spread*(2.0/3.0));\n"
  "vec3 purple=sample_screen(refracted-spread);\n"
  "color.r+=red.r/3.5+orange.r/3.5+yellow.r/3.5+purple.r/7.0;\n"
  "color.g+=orange.g/7.0+yellow.g/3.5+green.g/3.5+cyan.g/3.5;\n"
  "color.b+=cyan.b/3.0+blue.b/3.0+purple.b/3.0;\n"
  "float lum=dot(color,vec3(.299,.587,.114));\n"
  "color=mix(vec3(lum),color,1.0+u_vibrancy);\n"
  "float shine=pow(max(0.0,dot(n,normalize(vec2(-.45,-.89)))),8.0);\n"
  "color+=shine*curve*u_highlight;gl_FragColor=vec4(clamp(color,0.0,1.0),1.0);}\n";

static void * load_gl_proc(const char * name)
{
  return SDL_GL_GetProcAddress(name);
}

static void * load_gl_proc_alternates(const char * core_name,
                                      const char * extension_name)
{
  void * proc = load_gl_proc(core_name);
  if(proc == NULL) proc = load_gl_proc(extension_name);
  return proc;
}

#define LOAD_GL_PROC(member, name) do { \
  member = (void *)load_gl_proc(name); \
  if(member == NULL) {\
    printf("liquid glass GL: entry point %s not found\n", name);\
    return false;\
  }\
} while(0)

static GLuint compile_shader(GLenum type, const char * source)
{
  GLuint shader = p_create_shader(type);
  GLint status = GL_FALSE;
  char log[1024];

  p_shader_source(shader, 1, &source, NULL);
  p_compile_shader(shader);
  p_get_shader_iv(shader, GL_COMPILE_STATUS, &status);
  if(status != GL_TRUE) {
    log[0] = '\0';
    if(p_get_shader_info_log != NULL) {
      p_get_shader_info_log(shader, sizeof(log), NULL, log);
    }
    printf("liquid glass GL: shader compile failed: %s\n",
           log[0] != '\0' ? log : "(no log)");
    p_delete_shader(shader);
    return 0;
  }
  return shader;
}

static bool load_functions(void)
{
  LOAD_GL_PROC(p_create_shader, "glCreateShader");
  LOAD_GL_PROC(p_shader_source, "glShaderSource");
  LOAD_GL_PROC(p_compile_shader, "glCompileShader");
  LOAD_GL_PROC(p_get_shader_iv, "glGetShaderiv");
  p_get_shader_info_log = (get_shader_info_log_fn)load_gl_proc("glGetShaderInfoLog");
  p_get_program_info_log = (get_program_info_log_fn)load_gl_proc("glGetProgramInfoLog");
  LOAD_GL_PROC(p_delete_shader, "glDeleteShader");
  LOAD_GL_PROC(p_create_program, "glCreateProgram");
  LOAD_GL_PROC(p_attach_shader, "glAttachShader");
  LOAD_GL_PROC(p_bind_attrib_location, "glBindAttribLocation");
  LOAD_GL_PROC(p_link_program, "glLinkProgram");
  LOAD_GL_PROC(p_get_program_iv, "glGetProgramiv");
  LOAD_GL_PROC(p_delete_program, "glDeleteProgram");
  LOAD_GL_PROC(p_use_program, "glUseProgram");
  LOAD_GL_PROC(p_get_uniform_location, "glGetUniformLocation");
  LOAD_GL_PROC(p_uniform_1i, "glUniform1i");
  LOAD_GL_PROC(p_uniform_1f, "glUniform1f");
  LOAD_GL_PROC(p_uniform_2f, "glUniform2f");
  LOAD_GL_PROC(p_uniform_3f, "glUniform3f");
  LOAD_GL_PROC(p_active_texture, "glActiveTexture");
  LOAD_GL_PROC(p_enable_vertex_attrib_array, "glEnableVertexAttribArray");
  LOAD_GL_PROC(p_disable_vertex_attrib_array, "glDisableVertexAttribArray");
  LOAD_GL_PROC(p_vertex_attrib_pointer, "glVertexAttribPointer");
  p_gen_framebuffers = (gen_framebuffers_fn)load_gl_proc_alternates(
    "glGenFramebuffers", "glGenFramebuffersEXT");
  p_bind_framebuffer = (bind_framebuffer_fn)load_gl_proc_alternates(
    "glBindFramebuffer", "glBindFramebufferEXT");
  p_framebuffer_texture_2d = (framebuffer_texture_2d_fn)load_gl_proc_alternates(
    "glFramebufferTexture2D", "glFramebufferTexture2DEXT");
  p_check_framebuffer_status = (check_framebuffer_status_fn)load_gl_proc_alternates(
    "glCheckFramebufferStatus", "glCheckFramebufferStatusEXT");
  p_delete_framebuffers = (delete_framebuffers_fn)load_gl_proc_alternates(
    "glDeleteFramebuffers", "glDeleteFramebuffersEXT");
  if(p_gen_framebuffers == NULL || p_bind_framebuffer == NULL ||
     p_framebuffer_texture_2d == NULL || p_check_framebuffer_status == NULL ||
     p_delete_framebuffers == NULL) return false;
  return true;
}

bool liquid_glass_shader_available(void)
{
  return gl_context != NULL;
}

bool liquid_glass_shader_init(liquid_glass_shader_t * shader)
{
  GLuint vertex;
  GLuint fragment;
  GLint status = GL_FALSE;

  if(shader == NULL || SDL_WasInit(SDL_INIT_VIDEO) == 0) return false;
  memset(shader, 0, sizeof(*shader));

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);
  gl_window = SDL_CreateWindow("Liquid Glass GPU",
                               SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED,
                               1, 1,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if(gl_window == NULL) {
    printf("liquid glass GL: window creation failed: %s\n", SDL_GetError());
    return false;
  }
  gl_context = SDL_GL_CreateContext(gl_window);
  if(gl_context == NULL) {
    /* Some drivers reject an explicit 2.1 compatibility request: retry
     * with the default context. */
    printf("liquid glass GL: 2.1 context failed (%s), retrying default\n", SDL_GetError());
    SDL_GL_ResetAttributes();
    gl_context = SDL_GL_CreateContext(gl_window);
  }
  if(gl_context == NULL || SDL_GL_MakeCurrent(gl_window, gl_context) != 0) {
    printf("liquid glass GL: context creation failed: %s\n", SDL_GetError());
    liquid_glass_shader_deinit(shader);
    return false;
  }
  {
    const char * gl_ver = (const char *)glGetString(GL_VERSION);
    printf("liquid glass GL: context ready (%s)\n",
           gl_ver != NULL ? gl_ver : "?");
  }
  if(!load_functions()) {
    liquid_glass_shader_deinit(shader);
    return false;
  }

  vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
  fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
  if(vertex == 0 || fragment == 0) {
    if(vertex != 0) p_delete_shader(vertex);
    if(fragment != 0) p_delete_shader(fragment);
    liquid_glass_shader_deinit(shader);
    return false;
  }
  shader->program = p_create_program();
  p_attach_shader(shader->program, vertex);
  p_attach_shader(shader->program, fragment);
  p_bind_attrib_location(shader->program, 0, "a_pos");
  p_bind_attrib_location(shader->program, 1, "a_uv");
  p_link_program(shader->program);
  p_get_program_iv(shader->program, GL_LINK_STATUS, &status);
  p_delete_shader(vertex);
  p_delete_shader(fragment);
  if(status != GL_TRUE) {
    char log[1024];

    log[0] = '\0';
    if(p_get_program_info_log != NULL) {
      p_get_program_info_log(shader->program, sizeof(log), NULL, log);
    }
    printf("liquid glass GL: program link failed: %s\n",
           log[0] != '\0' ? log : "(no log)");
    liquid_glass_shader_deinit(shader);
    return false;
  }

  shader->backdrop = p_get_uniform_location(shader->program, "u_backdrop");
  shader->screen_size = p_get_uniform_location(shader->program, "u_screen_size");
  shader->origin = p_get_uniform_location(shader->program, "u_origin");
  shader->view_scale = p_get_uniform_location(shader->program, "u_view_scale");
  shader->size = p_get_uniform_location(shader->program, "u_size");
  shader->radius = p_get_uniform_location(shader->program, "u_radius");
  shader->refraction_height = p_get_uniform_location(shader->program, "u_refraction_height");
  shader->refraction_amount = p_get_uniform_location(shader->program, "u_refraction_amount");
  shader->dispersion = p_get_uniform_location(shader->program, "u_dispersion");
  shader->depth_effect = p_get_uniform_location(shader->program, "u_depth_effect");
  shader->tint = p_get_uniform_location(shader->program, "u_tint");
  shader->opacity = p_get_uniform_location(shader->program, "u_opacity");
  return true;
}

void liquid_glass_shader_deinit(liquid_glass_shader_t * shader)
{
  if(gl_context != NULL && gl_window != NULL) {
    SDL_GL_MakeCurrent(gl_window, gl_context);
    if(shader != NULL && shader->program != 0 && p_delete_program != NULL) {
      p_delete_program(shader->program);
    }
    if(input_texture != 0) glDeleteTextures(1, &input_texture);
    if(output_texture != 0) glDeleteTextures(1, &output_texture);
    if(framebuffer != 0 && p_delete_framebuffers != NULL) {
      p_delete_framebuffers(1, &framebuffer);
    }
    SDL_GL_DeleteContext(gl_context);
    gl_context = NULL;
  }
  if(gl_window != NULL) {
    SDL_DestroyWindow(gl_window);
    gl_window = NULL;
  }
  free(rgba_source);
  free(rgba_output);
  rgba_source = NULL;
  rgba_output = NULL;
  rgba_capacity = 0;
  input_texture = 0;
  output_texture = 0;
  framebuffer = 0;
  if(shader != NULL) memset(shader, 0, sizeof(*shader));
}

bool liquid_glass_shader_upload_screen(const liquid_glass_shader_t * shader,
                                      const uint8_t * screen,
                                      uint32_t screen_stride,
                                      int32_t screen_w,
                                      int32_t screen_h)
{
  size_t screen_pixels;
  int32_t row;
  uint32_t col;

  (void)shader;
  if(screen == NULL || screen_w <= 0 || screen_h <= 0) return false;
  if(SDL_GL_MakeCurrent(gl_window, gl_context) != 0) return false;

  screen_pixels = (size_t)screen_w * (size_t)screen_h;
  if(screen_pixels > rgba_capacity) {
    uint8_t * new_source = malloc(screen_pixels * 4U);
    uint8_t * new_output = malloc(screen_pixels * 4U);
    if(new_source == NULL || new_output == NULL) {
      free(new_source);
      free(new_output);
      return false;
    }
    free(rgba_source);
    free(rgba_output);
    rgba_source = new_source;
    rgba_output = new_output;
    rgba_capacity = screen_pixels;
  }

  /* BGRA -> RGBA: keep G and A, swap R and B (bswap32 would land the
   * alpha in the red channel and wash everything red/pink). */
  for(row = 0; row < screen_h; row++) {
    const uint32_t * src_row =
      (const uint32_t *)(screen + (size_t)row * screen_stride);
    uint32_t * rgba_row = (uint32_t *)(rgba_source +
                                       (size_t)row * (size_t)screen_w * 4U);

    for(col = 0; col < (uint32_t)screen_w; col++) {
      uint32_t v = src_row[col];
      rgba_row[col] = (v & 0xFF00FF00u) |
                      ((v >> 16) & 0x000000FFu) |
                      ((v << 16) & 0x00FF0000u);
    }
  }

  if(input_texture == 0) glGenTextures(1, &input_texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glBindTexture(GL_TEXTURE_2D, input_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, screen_w, screen_h, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, rgba_source);
  return true;
}

bool liquid_glass_shader_process_argb8888(const liquid_glass_shader_t * shader,
                                         const uint8_t * screen,
                                         uint32_t screen_stride,
                                         int32_t screen_w,
                                         int32_t screen_h,
                                         int32_t origin_x,
                                         int32_t origin_y,
                                         int32_t view_scale_x,
                                         int32_t view_scale_y,
                                         uint8_t * destination,
                                         uint32_t dst_stride,
                                         int32_t width,
                                         int32_t height,
                                         const liquid_glass_params_t * params)
{
  const float vertices[] = {
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 1.0f
  };
  if(shader == NULL || shader->program == 0 || input_texture == 0 ||
     destination == NULL || params == NULL || width <= 0 || height <= 0) {
    return false;
  }
  if(SDL_GL_MakeCurrent(gl_window, gl_context) != 0) return false;

  if(output_texture == 0) glGenTextures(1, &output_texture);
  if(framebuffer == 0) p_gen_framebuffers(1, &framebuffer);
  glBindTexture(GL_TEXTURE_2D, output_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  p_bind_framebuffer(GL_FRAMEBUFFER, framebuffer);
  p_framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, output_texture, 0);
  if(p_check_framebuffer_status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    p_bind_framebuffer(GL_FRAMEBUFFER, 0);
    return false;
  }

  glViewport(0, 0, width, height);
  p_use_program(shader->program);
  p_active_texture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, input_texture);
  p_uniform_1i(shader->backdrop, 0);
  p_uniform_2f(shader->screen_size, (float)screen_w, (float)screen_h);
  p_uniform_2f(shader->origin, (float)origin_x, (float)origin_y);
  p_uniform_2f(shader->view_scale, (float)view_scale_x, (float)view_scale_y);
  p_uniform_2f(shader->size, (float)width, (float)height);
  p_uniform_1f(shader->radius, (float)params->corner_radius);
  p_uniform_1f(shader->refraction_height, (float)params->refraction_height);
  p_uniform_1f(shader->refraction_amount, params->refraction_amount);
  p_uniform_1f(shader->dispersion, params->dispersion);
  p_uniform_1f(shader->depth_effect, params->depth_effect ? 1.0f : 0.0f);
  p_uniform_1f(p_get_uniform_location(shader->program, "u_vibrancy"), params->vibrancy);
  p_uniform_1f(p_get_uniform_location(shader->program, "u_highlight"), params->highlight);
  p_enable_vertex_attrib_array(0);
  p_enable_vertex_attrib_array(1);
  p_vertex_attrib_pointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), vertices);
  p_vertex_attrib_pointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), vertices + 2);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba_output);
  p_disable_vertex_attrib_array(0);
  p_disable_vertex_attrib_array(1);
  p_use_program(0);
  p_bind_framebuffer(GL_FRAMEBUFFER, 0);
  {
    int32_t r;
    uint32_t c;
    for(r = 0; r < height; r++) {
      const uint32_t * src32 =
        (const uint32_t *)(rgba_output + (size_t)r * (size_t)width * 4U);
      uint32_t * dst32 = (uint32_t *)(destination + (size_t)r * dst_stride);

      for(c = 0; c < (uint32_t)width; c++) {
        /* GL RGBA bytes [R G B A] -> LVGL ARGB8888 bytes [B G R A]:
         * keep A and G in place, swap R and B. */
        uint32_t v = src32[c];
        dst32[c] = (v & 0xFF00FF00u) |
                   ((v >> 16) & 0x000000FFu) |
                   ((v << 16) & 0x00FF0000u);
      }
    }
  }
  return true;
}

bool liquid_glass_shader_draw(const liquid_glass_shader_t * shader,
                             uint32_t backdrop_texture,
                             int32_t x, int32_t y,
                             int32_t width, int32_t height,
                             int32_t screen_width, int32_t screen_height,
                             const liquid_glass_params_t * params,
                             float tint_r, float tint_g, float tint_b,
                             float opacity)
{
  (void)shader;
  (void)backdrop_texture;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  (void)screen_width;
  (void)screen_height;
  (void)params;
  (void)tint_r;
  (void)tint_g;
  (void)tint_b;
  (void)opacity;
  return false;
}

#else

bool liquid_glass_shader_available(void)
{
  return false;
}

bool liquid_glass_shader_init(liquid_glass_shader_t * shader)
{
  (void)shader;
  return false;
}

void liquid_glass_shader_deinit(liquid_glass_shader_t * shader)
{
  (void)shader;
}

bool liquid_glass_shader_upload_screen(const liquid_glass_shader_t * shader,
                                      const uint8_t * screen,
                                      uint32_t screen_stride,
                                      int32_t screen_w,
                                      int32_t screen_h)
{
  (void)shader;
  (void)screen;
  (void)screen_stride;
  (void)screen_w;
  (void)screen_h;
  return false;
}

bool liquid_glass_shader_process_argb8888(const liquid_glass_shader_t * shader,
                                         const uint8_t * screen,
                                         uint32_t screen_stride,
                                         int32_t screen_w,
                                         int32_t screen_h,
                                         int32_t origin_x,
                                         int32_t origin_y,
                                         int32_t view_scale_x,
                                         int32_t view_scale_y,
                                         uint8_t * destination,
                                         uint32_t dst_stride,
                                         int32_t width,
                                         int32_t height,
                                         const liquid_glass_params_t * params)
{
  (void)shader;
  (void)screen;
  (void)screen_stride;
  (void)screen_w;
  (void)screen_h;
  (void)origin_x;
  (void)origin_y;
  (void)view_scale_x;
  (void)view_scale_y;
  (void)destination;
  (void)dst_stride;
  (void)width;
  (void)height;
  (void)params;
  return false;
}

bool liquid_glass_shader_draw(const liquid_glass_shader_t * shader,
                             uint32_t backdrop_texture,
                             int32_t x, int32_t y,
                             int32_t width, int32_t height,
                             int32_t screen_width, int32_t screen_height,
                             const liquid_glass_params_t * params,
                             float tint_r, float tint_g, float tint_b,
                             float opacity)
{
  (void)shader;
  (void)backdrop_texture;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  (void)screen_width;
  (void)screen_height;
  (void)params;
  (void)tint_r;
  (void)tint_g;
  (void)tint_b;
  (void)opacity;
  return false;
}

#endif
