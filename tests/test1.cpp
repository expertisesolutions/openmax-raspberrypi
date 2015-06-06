/* (c) Copyright 2011-2014 Felipe Magno de Almeida
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <bcm_host.h>

#include <ghtv/omx-rpi/image_pipeline.hpp>
#include <ghtv/opengl/texture.hpp>

#include <boost/bind.hpp>

#include <cstdlib>
#include <cassert>

bool continue_ = false;
boost::mutex mutex;
boost::condition_variable condition;

void done_function(bool, ghtv::omx_rpi::image_pipeline& pipeline)
{
  std::cout << "done function" << std::endl;
  boost::unique_lock<boost::mutex> l(mutex);
  ::continue_ = true;
  condition.notify_one();
  std::cout << "done function return" << std::endl;
}

void draw_texture(ghtv::opengl::texture& texture
                  , GLint projection_location
                  , GLint texture_location
                  , EGLDisplay display
                  , EGLSurface surface
                  , GLint width, GLint height)
{
  glClear(GL_COLOR_BUFFER_BIT);
  glUniform1i(texture_location, 0);
  texture.bind();
  glActiveTexture(GL_TEXTURE0);

  GLfloat vertices[][2] = {  {0           , 0}
                             , {(float)width, 0}
                             , {0           , (float)height}
                             , {(float)width, (float)height}};
 
  GLfloat texture_coords[][2] =  {  {0.0f, 0.0f}
                                    , {1.0f, 0.0f}
                                    , {0.0f, 1.0f}
                                    , {1.0f, 1.0f}};
  
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, texture_coords);
  glEnableVertexAttribArray(1);
  GLubyte indices[] = {0, 1, 2, 1, 2, 3};
  glDrawElements(GL_TRIANGLES, sizeof(indices)/sizeof(GLubyte)
                 , GL_UNSIGNED_BYTE, indices);
  assert(glGetError() == GL_NO_ERROR);
  eglSwapBuffers(display, surface);

  std::cout << "drawed texture " << texture.raw() << std::endl;
}

int main(int argc, char** argv)
{
  assert(argc >= 2);

  EGLDisplay display;
  EGLContext context;
  EGLConfig config;
  EGLSurface surface;
  EGL_DISPMANX_WINDOW_T nativewindow;
  std::size_t width, height;

  {
    bcm_host_init();
    std::atexit(bcm_host_deinit);
  
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert( display != EGL_NO_DISPLAY);

    EGLint major, minor;
    if(!eglInitialize(display, &major, &minor))
    {
      std::cout << "Failed initializing display" << std::endl;
      return 1;
    }

    const EGLint attribs[] =
      {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_NONE
      };
    // EGLint w, h, dummy, format;
    EGLint numConfigs;

    if(!eglChooseConfig(display, attribs, &config, 1, &numConfigs))
    {
      std::cout << "Choosing config failed" << std::endl;
      return 1;
    }

    EGLBoolean result = eglBindAPI(EGL_OPENGL_ES_API);
    assert(EGL_FALSE != result);
    assert(glGetError() == 0);
    static_cast<void>(result);

    {
      EGLint attribute_list[] = 
        {
          EGL_CONTEXT_CLIENT_VERSION, 2
          , EGL_NONE
        };

      // create an EGL rendering context
      context = eglCreateContext( display, config, EGL_NO_CONTEXT, attribute_list);
      assert(context!=EGL_NO_CONTEXT);

      uint32_t screen_width, screen_height;

      // create an EGL window surface
      int32_t success = graphics_get_display_size(0 /* LCD */, &screen_width, &screen_height);
      assert( success >= 0 );
      static_cast<void>(success);

      VC_RECT_T dst_rect;
      VC_RECT_T src_rect;

      dst_rect.x = 0;
      dst_rect.y = 0;
      dst_rect.width = screen_width;
      dst_rect.height = screen_height;
      
      src_rect.x = 0;
      src_rect.y = 0;
      src_rect.width = screen_width << 16;
      src_rect.height = screen_height << 16;        

      DISPMANX_DISPLAY_HANDLE_T dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
      DISPMANX_UPDATE_HANDLE_T dispman_update = vc_dispmanx_update_start( 0 );
         
      DISPMANX_ELEMENT_HANDLE_T dispman_element = vc_dispmanx_element_add
        ( dispman_update, dispman_display
          , 0/*layer*/, &dst_rect, 0/*src*/
          , &src_rect, DISPMANX_PROTECTION_NONE, 0 /*alpha*/, 0/*clamp*/, DISPMANX_TRANSFORM_T(0)/*transform*/);

      
      nativewindow.element = dispman_element;
      nativewindow.width = screen_width;
      nativewindow.height = screen_height;
      vc_dispmanx_update_submit_sync( dispman_update );

      assert(glGetError() == 0);

      width = screen_width;
      height = screen_height;
      // global_state.width = screen_width;
      // global_state.height = screen_height;
    }  

    surface = eglCreateWindowSurface(display, config, & nativewindow, NULL);

    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE)
    {
      std::cout << "Failed making current surface" << std::endl;
      return 1;
    }

    glClearColor(0.15f, 0.25f, 0.35f, 1.0f);
    glClear( GL_COLOR_BUFFER_BIT );
    assert(glGetError() == 0);
  }

  //{
    GLuint program_object;
    GLint projection_location, texture_location;

    glClearColor(0.15f, 0.25f, 0.35f, 1.0f);
    glClear( GL_COLOR_BUFFER_BIT );
    assert(glGetError() == 0);
  
    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLchar vertex_shader_src[]
      = "attribute vec2 vPosition;                            \n"
      "attribute vec2 a_texCoord;                           \n"
      "uniform float zindex;                                \n"
      "uniform mat4 projection_matrix;                      \n"
      "varying vec2 v_texCoord;                             \n"
      "void main()                                          \n"
      "{                                                    \n"
      "    vec4 position = vec4(vPosition, zindex, 1.0);    \n"
      "    gl_Position = projection_matrix*position;        \n"
      "    v_texCoord = a_texCoord;                         \n"
      "}                                                    \n"
    ;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);

    GLchar const* vertex_shader_src_tmp = vertex_shader_src;
    glShaderSource(vertex_shader, 1, &vertex_shader_src_tmp, 0);
    glCompileShader(vertex_shader);
    GLint compiled = 0;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &compiled);
    assert(!!compiled);

    GLchar fragment_shader_src[]
      = "uniform vec4 color;                           \n"
      "uniform sampler2D s_texture;                  \n"
      "varying vec2 v_texCoord;                      \n"
      "void main()                                   \n"
      "{                                             \n"
      "    vec4 color = texture2D(s_texture, v_texCoord); \n"
      "    gl_FragColor = color;                     \n"
      "}                                             \n"
    ;

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);

    GLchar const* fragment_shader_src_tmp = fragment_shader_src;
    glShaderSource(fragment_shader, 1, &fragment_shader_src_tmp, 0);
    glCompileShader(fragment_shader);
    compiled = 0;
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &compiled);
    assert(!!compiled);

    program_object = glCreateProgram();
    assert(!!program_object);
    
    glAttachShader(program_object, vertex_shader);
    glAttachShader(program_object, fragment_shader);

    glBindAttribLocation(program_object, 0, "vPosition");
    glBindAttribLocation(program_object, 1, "a_texCoord");

    glLinkProgram(program_object);

    GLint linked;
    glGetProgramiv(program_object, GL_LINK_STATUS, &linked);
    assert(!!linked);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    projection_location = glGetUniformLocation(program_object
                                             , "projection_matrix");
    assert(projection_location != -1);

    // global_state.z_location = glGetUniformLocation(global_state.program_object, "zindex");
    // assert(global_state.z_location != -1);

    texture_location = glGetUniformLocation(program_object, "s_texture");
    assert(texture_location != -1);

    glUseProgram(program_object);

    assert(glGetAttribLocation(program_object, "vPosition") == 0);
    assert(glGetAttribLocation(program_object, "a_texCoord") == 1);

  float right = width//, left = 0
    , bottom = height//, top = 0
    ;

  float projection_matrix[16] =
    {  2.0f/right, 0.0f        , 0.0f, 0.0f
       , 0.0f      , -2.0f/bottom, 0.0f, 0.0f
       , 0.0f      , 0.0f        , 1.0f, 0.0f
       , -1.0f     , 1.0f        , 0.0f, 1.0f };
  glUniformMatrix4fv(projection_location, 1, false, projection_matrix);

  glViewport(0, 0, width, height);
  
    //}  
  std::vector<ghtv::opengl::texture> textures;

  ghtv::omx_rpi::image_pipeline pipeline;
  for(int i = 1; i != argc; ++i)
  {
    textures.resize(textures.size()+1);
    textures.back().bind();

    pipeline.load_image(argv[i], textures.back().raw(), &display, &context
                        , boost::bind(&done_function, _1, boost::ref(pipeline)));

    {
      boost::unique_lock<boost::mutex> l( ::mutex);
      while(! ::continue_)
        ::condition.wait(l);

      ::continue_ = false;
    }
    pipeline.reset();

    draw_texture(textures.back(), projection_location, texture_location, display, surface, width, height);
  }
  draw_texture(textures.front(), projection_location, texture_location, display, surface, width, height);

  while(true) sleep(5);
}

