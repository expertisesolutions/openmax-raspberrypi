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

#ifndef GHTV_OMX_RPI_IMAGE_PIPELINE_HPP
#define GHTV_OMX_RPI_IMAGE_PIPELINE_HPP

#include <IL/OMX_Broadcom.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <boost/optional.hpp>
#include <boost/utility/typed_in_place_factory.hpp>
#include <boost/variant.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/ref.hpp>
#include <boost/function.hpp>

#include <vector>
#include <stdexcept>
#include <iostream>
#include <cassert>
#include <fstream>
#include <cstdlib>
#include <cstring>

namespace ghtv { namespace omx_rpi {

namespace detail {

inline
OMX_IMAGE_PARAM_PORTFORMATTYPE make_image_param_portformattype(OMX_U32 nPortIndex, OMX_U32 nIndex
                                                               , OMX_IMAGE_CODINGTYPE eCompressionFormat
                                                               , OMX_COLOR_FORMATTYPE eColorFormat)
{
  OMX_IMAGE_PARAM_PORTFORMATTYPE r;
  r.nSize = sizeof(r);
  r.nVersion.nVersion = OMX_VERSION;
  r.nPortIndex = nPortIndex;
  r.nIndex = nIndex;
  r.eCompressionFormat = eCompressionFormat;
  r.eColorFormat = eColorFormat;
  return r;
}
  
}

struct image_pipeline
{
  static OMX_ERRORTYPE empty_buffer
    (OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE * buffer_header)
  {
    image_pipeline* self = static_cast<image_pipeline*>(pAppData);



    boost::unique_lock<boost::mutex> l(self->mutex);


    
    assert(!!self->load_queue);
    self->load_queue->empty_buffer(buffer_header);
    

    return OMX_ErrorNone;
  }

  static OMX_ERRORTYPE filled_buffer (OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE * pBufferHeader)
  {
    image_pipeline* self = static_cast<image_pipeline*>(pAppData);



    boost::unique_lock<boost::mutex> l(self->mutex);


    
    assert(!!self->load_queue);

    boost::function<void(bool)> f = self->load_queue->callback;

    l.unlock();


    f(true);
    

    return OMX_ErrorNone;
  }

  static OMX_ERRORTYPE handler_custom
    (OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData)
  {
    image_pipeline* self = static_cast<image_pipeline*>(pAppData);



    boost::unique_lock<boost::mutex> l(self->mutex);
    if(eEvent == OMX_EventError)
    {
      std::cerr << "Error in handler_custom nData1 " << std::hex
                << (unsigned long)nData1 << " nData2 " << (unsigned long)nData2 << " pEventData "
                << pEventData
                << std::dec
                << std::endl;
      std::abort();
    }
    else if(eEvent == OMX_EventBufferFlag)
    {

      // assert(!!self->load_queue);

      // boost::function<void(bool)> f = self->load_queue->callback;
      
      // l.unlock();


      // f(true);
    

      return OMX_ErrorNone;
    }

    if(self->init_queue)
    {

      std::vector<wait_event>::iterator
        first = self->init_queue->events.begin()
        , last = self->init_queue->events.end();
      while(first != last && (first->event != eEvent || first->nData1 != nData1 || first->nData2 != nData2))
        ++first;
      if(first != last)
      {
        self->init_queue->events.erase(first);
        if(self->init_queue->events.empty())
        {

          self->condition.notify_one();

          return OMX_ErrorNone;
        }
      }
    }

    if(self->load_queue)
    {

      std::vector<wait_event>::iterator
        first = self->load_queue->events.begin()
        , last = self->load_queue->events.end();
      while(first != last && (first->event != eEvent || first->nData1 != nData1 || first->nData2 != nData2))
      {

        ++first;
      }
      if(first != last)
      {

        if(first->callback)
          (self ->* first->callback)();
        self->load_queue->events.erase(first);
        if(self->load_queue->events.empty())
        {
          self->condition.notify_one();

          return OMX_ErrorNone;
        }
      }
    }
    

    return OMX_ErrorNone;
  }
  
  image_pipeline()
    : init_queue(boost::in_place<initialization_queue>(boost::ref(mutex), boost::ref(condition)))
  {
    OMX_ERRORTYPE r;
    static_cast<void>(r);

    // Synchronous
    ::OMX_Init();

    // Synchronous
    OMX_CALLBACKTYPE callbacks
      = {&image_pipeline::handler_custom, &image_pipeline::empty_buffer
         , &image_pipeline::filled_buffer};
    r = OMX_GetHandle (&decoder_handle, const_cast<char*>("OMX.broadcom.image_decode"), this, &callbacks);
    assert(r == OMX_ErrorNone);

    // Synchronous
    OMX_CALLBACKTYPE renderer_callbacks
      = {&image_pipeline::handler_custom, &image_pipeline::empty_buffer
         , &image_pipeline::filled_buffer};
    r = OMX_GetHandle (&renderer_handle, const_cast<char*>("OMX.broadcom.egl_render"), this, &renderer_callbacks);
    assert(r == OMX_ErrorNone);

    {
      OMX_PORT_PARAM_TYPE port;
      port.nSize = sizeof (OMX_PORT_PARAM_TYPE);
      port.nVersion.nVersion = OMX_VERSION;

      // Synchronous
      r = OMX_GetParameter (decoder_handle, OMX_IndexParamImageInit, &port);
      assert(r == OMX_ErrorNone);
      decoder_ports.in = port.nStartPortNumber;
      decoder_ports.out = port.nStartPortNumber + 1;

    // Synchronous
      r = OMX_GetParameter (renderer_handle, OMX_IndexParamVideoInit, &port);
      assert(r == OMX_ErrorNone);
      renderer_ports.in = port.nStartPortNumber;
      renderer_ports.out = port.nStartPortNumber + 1;
    }

    // Synchronous
    OMX_IMAGE_PARAM_PORTFORMATTYPE image_port_format
      = detail::make_image_param_portformattype (decoder_ports.in, 0u, OMX_IMAGE_CodingPNG
                                                 , OMX_COLOR_FormatUnused);
    r = OMX_SetParameter (decoder_handle, OMX_IndexParamImagePortFormat, &image_port_format);
    assert(r == OMX_ErrorNone);
    
    // Assynchronous - Initialization queue
    void* null = 0;
    init_queue->add_wait_command_result(CommandPortDisable, decoder_ports.in);
    r = OMX_SendCommand (decoder_handle, OMX_CommandPortDisable, decoder_ports.in, null);
    assert(r == OMX_ErrorNone);
    
    init_queue->add_wait_command_result(CommandPortDisable, decoder_ports.out);
    r = OMX_SendCommand (decoder_handle, OMX_CommandPortDisable, decoder_ports.out, null);
    assert(r == OMX_ErrorNone);

    init_queue->add_wait_command_result(CommandPortDisable, renderer_ports.in);
    r = OMX_SendCommand (renderer_handle, OMX_CommandPortDisable, renderer_ports.in, null);
    assert(r == OMX_ErrorNone);

    init_queue->add_wait_command_result(CommandPortDisable, renderer_ports.out);
    r = OMX_SendCommand (renderer_handle, OMX_CommandPortDisable, renderer_ports.out, null);
    assert(r == OMX_ErrorNone);
    
    init_queue->add_wait_command_result(CommandStateSet, OMX_StateIdle);
    r = OMX_SendCommand (decoder_handle, OMX_CommandStateSet, OMX_StateIdle, null);
    assert(r == OMX_ErrorNone);

    r = OMX_SendCommand (renderer_handle, OMX_CommandStateSet, OMX_StateIdle, null);
    assert(r == OMX_ErrorNone);

    init_queue->add_wait_command_result(CommandStateSet, OMX_StateExecuting);
    r = OMX_SendCommand (decoder_handle,  OMX_CommandStateSet, OMX_StateExecuting, null);
    assert(r == OMX_ErrorNone);
  }

  template <typename F>
  void load_image(std::string const& file, int texture_id, EGLDisplay* eglDisplay, EGLContext* eglContext, F f)
  {
    OMX_ERRORTYPE r = OMX_ErrorNone;
    void* null = 0;
    static_cast<void>(r);
    
    assert(!load_queue);
    {
      boost::unique_lock<boost::mutex> l(mutex);
      load_queue = boost::in_place<loading_image_queue>
        (file, boost::ref(mutex), boost::ref(condition), eglDisplay, eglContext, texture_id, f);
      assert(load_queue->events.size() == 0);
      assert(load_queue->events.empty());

      if(init_queue)
      {

        init_queue->wait(l);
        init_queue = boost::none;

      }
    }

    // We must request it before creating the buffers, but it will only complete
    // when the buffers are all created
    load_queue->add_wait_command_result(CommandPortEnable, decoder_ports.in);
    r = OMX_SendCommand (decoder_handle, OMX_CommandPortEnable, decoder_ports.in, null);
    assert(r == OMX_ErrorNone);



    {
      std::size_t number_buffers = buffer_headers.size(), buffer_alignment = 0;
      if(!number_buffers)
      {
        OMX_PARAM_PORTDEFINITIONTYPE port;
        port.nSize = sizeof(port);
        port.nVersion.nVersion = OMX_VERSION;
        port.nPortIndex = decoder_ports.in;
        // Synchronous
        r = OMX_GetParameter (decoder_handle, OMX_IndexParamPortDefinition, &port);

        assert(r == OMX_ErrorNone);
        number_buffers = port.nBufferCountActual;
        buffer_size = /*port.nBufferSize*/ 909808;
        buffer_alignment = port.nBufferAlignment;
      }

      buffer_headers.resize(number_buffers);

      if(buffers.empty())
      {
        buffers.resize(number_buffers);
        for (std::size_t i = 0; i != number_buffers; i++)
        {

          posix_memalign(reinterpret_cast<void**>(&buffers[i]), buffer_alignment, buffer_size);
        }
      }
      for (std::size_t i = 0; i != buffer_headers.size(); i++)
      {

        r = OMX_UseBuffer (decoder_handle, &buffer_headers[i].header
                           , decoder_ports.in, 0, buffer_size
                           , buffers[i]);


        assert(r == OMX_ErrorNone);
      }
    }
    load_queue->released_buffer_headers = buffer_headers;
    load_queue->used_buffer_headers.reserve(buffer_headers.size());

    {
      OMX_PARAM_PORTDEFINITIONTYPE portdef;

      portdef.nSize = sizeof (OMX_PARAM_PORTDEFINITIONTYPE);
      portdef.nVersion.nVersion = OMX_VERSION;
      portdef.nPortIndex = decoder_ports.out;
      r = OMX_GetParameter (decoder_handle, OMX_IndexParamPortDefinition, &portdef);

      assert(r == OMX_ErrorNone);
      // portdef.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
      portdef.format.image.nFrameWidth = 161;
      portdef.format.image.nFrameHeight = 64;
      portdef.format.image.nStride = 160;
      portdef.format.image.nSliceHeight = 64;
      r = OMX_SetParameter (decoder_handle, OMX_IndexParamPortDefinition, &portdef);


      assert(r == OMX_ErrorNone);
    }
    
    load_queue->add_wait_command_result(EventPortSettingsChanged
                                        , decoder_ports.out
                                        , &image_pipeline::decoder_output_port_changed);


    bool decoder_output_port_changed;
    bool first = true;
    do
    {

      load_queue->wait_buffers();

      // Assynchronous with buffers AND PortChangedStatus
      r = OMX_EmptyThisBuffer (decoder_handle, load_queue->fill_buffer(first));

      first = false;
      assert(r == OMX_ErrorNone);
      boost::unique_lock<boost::mutex> l(mutex);
      decoder_output_port_changed = load_queue->decoder_output_port_changed;
    }
    while(load_queue->file_offset != load_queue->file_size && !decoder_output_port_changed);


    load_queue->wait();


    r = OMX_SetupTunnel(decoder_handle, decoder_ports.out
                        , renderer_handle, renderer_ports.in);
    assert(r == OMX_ErrorNone);


    load_queue->wait();

    
    load_queue->add_wait_command_result(CommandPortEnable
                                        , decoder_ports.out);
    load_queue->add_wait_command_result(CommandPortEnable
                                        , renderer_ports.in);
    
    r = OMX_SendCommand (decoder_handle, OMX_CommandPortEnable, decoder_ports.out, null);
    assert(r == OMX_ErrorNone);
    r = OMX_SendCommand (renderer_handle, OMX_CommandPortEnable, renderer_ports.in, null);
    assert(r == OMX_ErrorNone);


    load_queue->wait();




    int width, height;
    {
      OMX_PARAM_PORTDEFINITIONTYPE port;
      port.nSize = sizeof (OMX_PARAM_PORTDEFINITIONTYPE);
      port.nVersion.nVersion = OMX_VERSION;
      port.nPortIndex = decoder_ports.out;
      OMX_GetParameter (decoder_handle, OMX_IndexParamPortDefinition, &port);
      width = port.format.image.nFrameWidth;
      height = port.format.image.nFrameHeight;

    }

    glBindTexture (GL_TEXTURE_2D, load_queue->texture_id);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, width, height
                  , 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    load_queue->texture_mem_handle =
      (eglCreateImageKHR
       (*load_queue->eglDisplay, *load_queue->eglContext
        , EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer) load_queue->texture_id, 0));

    r = OMX_SendCommand (renderer_handle, OMX_CommandPortEnable, renderer_ports.out, null);
    assert(r == OMX_ErrorNone);

    /// should probably wait
    
    r = OMX_UseEGLImage (renderer_handle, &load_queue->texture_buffer_header
                         , renderer_ports.out, null, load_queue->texture_mem_handle);
    assert(r == OMX_ErrorNone);

    load_queue->add_wait_command_result(CommandStateSet, OMX_StateExecuting);
    r = OMX_SendCommand (renderer_handle,  OMX_CommandStateSet, OMX_StateExecuting, null);
    assert(r == OMX_ErrorNone);


    load_queue->wait();


    while(load_queue->file_offset != load_queue->file_size)
    {

      load_queue->wait_buffers();


      r = OMX_EmptyThisBuffer (decoder_handle, load_queue->fill_buffer(false));
      assert(r == OMX_ErrorNone);

    }


    
    r = OMX_FillThisBuffer (renderer_handle, load_queue->texture_buffer_header);

  }
  
  struct CommandPortDisable_type {} CommandPortDisable;
  struct CommandPortEnable_type {} CommandPortEnable;
  struct CommandStateSet_type {} CommandStateSet;
  struct EventPortSettingsChanged_type {} EventPortSettingsChanged;

  boost::mutex mutex;
  boost::condition_variable condition;

  struct wait_event
  {
    OMX_EVENTTYPE event;
    unsigned int nData1;
    unsigned int nData2;
    void (image_pipeline::* callback)();
  };

  struct wait_functions
  {
    static void add_event_result
      (boost::mutex& mutex, std::vector<wait_event>& events, CommandPortDisable_type, int p
       , void (image_pipeline::* callback)() = 0)
    {
      boost::unique_lock<boost::mutex> l(mutex);
      wait_event e = {OMX_EventCmdComplete, OMX_CommandPortDisable, (unsigned int)p, callback};
      events.push_back(e);
    }
    
    static void add_event_result
      (boost::mutex& mutex, std::vector<wait_event>& events, CommandPortEnable_type, int p
       , void (image_pipeline::* callback)() = 0)
    {
      boost::unique_lock<boost::mutex> l(mutex);
      wait_event e = {OMX_EventCmdComplete, OMX_CommandPortEnable, (unsigned int)p, callback};
      events.push_back(e);
    }
    
    static void add_event_result
      (boost::mutex& mutex, std::vector<wait_event>& events, CommandStateSet_type, OMX_STATETYPE s
       , void (image_pipeline::* callback)() = 0)
    {
      boost::unique_lock<boost::mutex> l(mutex);
      wait_event e = {OMX_EventCmdComplete, OMX_CommandStateSet, s, callback};
      events.push_back(e);
    }
    static void add_event_result(boost::mutex& mutex, std::vector<wait_event>& events
                                 , EventPortSettingsChanged_type c, int port
                                 , void (image_pipeline::* callback)() = 0)
    {
      boost::unique_lock<boost::mutex> l(mutex);
      wait_event e = {OMX_EventPortSettingsChanged, (unsigned int)port, 0u, callback};
      events.push_back(e);
    }

    static void wait(boost::mutex& mutex, boost::unique_lock<boost::mutex>& l
                     , boost::condition_variable& condition
                     , std::vector<wait_event>& events)
    {

      while(!events.empty())
      {
        condition.wait(l);
      }
    }
  };

  struct initialization_queue
  {
    boost::mutex& mutex;
    boost::condition_variable& condition;

    initialization_queue(boost::mutex& m, boost::condition_variable& c)
      : mutex(m), condition(c) {}
    
    std::vector<wait_event> events;

    void add_wait_command_result(CommandPortDisable_type c, int p)
    {
      wait_functions::add_event_result(mutex, events, c, p);
    }
    
    void add_wait_command_result(CommandPortEnable_type c, int p)
    {
      wait_functions::add_event_result(mutex, events, c, p);
    }
    
    void add_wait_command_result(CommandStateSet_type c, OMX_STATETYPE s)
    {
      wait_functions::add_event_result(mutex, events, c, s);
    }

    void wait(boost::unique_lock<boost::mutex>& l)
    {
      wait_functions::wait(mutex, l, condition, events);
    }
    void wait()
    {
      boost::unique_lock<boost::mutex> l(mutex);
      wait(l);
    }
  };

  boost::optional<initialization_queue> init_queue;
  struct buffer_header
  {
    OMX_BUFFERHEADERTYPE* header;

    buffer_header() : header(0)
    {}
  };

  struct loading_image_queue
  {
    boost::mutex& mutex;
    boost::condition_variable& condition;
    std::ifstream file_stream;
    std::vector<buffer_header> used_buffer_headers;
    std::vector<buffer_header> released_buffer_headers;

    std::size_t file_size;
    std::size_t file_offset;

    EGLDisplay* eglDisplay;
    EGLContext* eglContext;
    int texture_id;

    boost::function<void(bool)> callback;

    std::vector<wait_event> events;

    bool decoder_output_port_changed;

    OMX_BUFFERHEADERTYPE* texture_buffer_header;
    void* texture_mem_handle;
    
    bool has_released_buffers() const
    {
      boost::unique_lock<boost::mutex> l(mutex);
      return !released_buffer_headers.empty();
    }

    template <typename F>
    loading_image_queue(std::string const& file_path
                        , boost::mutex& mutex
                        , boost::condition_variable& condition
                        , EGLDisplay* eglDisplay
                        , EGLContext* eglContext
                        , int texture_id
                        , F f)
      : mutex(mutex), condition(condition)
      , file_stream(file_path.c_str()), file_size(0u), file_offset(0u)
      , eglDisplay(eglDisplay), eglContext(eglContext)
      , texture_id(texture_id)
      , callback(f)
      , decoder_output_port_changed(false)
      , texture_buffer_header(0)
    {
      file_stream.seekg(0, std::ios::end);
      file_size = file_stream.tellg();
      file_stream.seekg(0, std::ios::beg);

    }

    void add_wait_command_result(CommandStateSet_type c, OMX_STATETYPE s)
    {
      wait_functions::add_event_result(mutex, events, c, s);
    }

    void add_wait_command_result(EventPortSettingsChanged_type c, int port
                                 , void (image_pipeline::* callback)() = 0)
    {
      wait_functions::add_event_result(mutex, events, c, port, callback);
    }
    void add_wait_command_result(CommandPortEnable_type c, int p
                                 , void (image_pipeline::* callback)() = 0)
    {
      wait_functions::add_event_result(mutex, events, c, p, callback);
    }
    void wait()
    {
      boost::unique_lock<boost::mutex> lock(mutex);
      wait_functions::wait(mutex, lock, condition, events);
    }
    void wait_buffers()
    {
      boost::unique_lock<boost::mutex> l(mutex);
      while(released_buffer_headers.empty())
        condition.wait(l);
    }

    void wait_all_buffers()
    {
      boost::unique_lock<boost::mutex> l(mutex);
      while(!used_buffer_headers.empty())
        condition.wait(l);
    }
    
    void empty_buffer(OMX_BUFFERHEADERTYPE* header)
    {
      std::size_t index = reinterpret_cast<std::size_t>(header->pAppPrivate);
      std::vector<buffer_header>::iterator iterator = used_buffer_headers.begin() + index;
      released_buffer_headers.push_back(*iterator);
      used_buffer_headers.erase(iterator);

      header->nOffset = 0;
      header->nFilledLen = 0;
      header->nFlags = 0;

      if(released_buffer_headers.size() == 1 || used_buffer_headers.empty())
        condition.notify_one();
    }
    
    OMX_BUFFERHEADERTYPE* fill_buffer(bool first)
    {

      boost::unique_lock<boost::mutex> l(mutex);

      
      assert(!released_buffer_headers.empty());
      used_buffer_headers.push_back(released_buffer_headers.back());
      released_buffer_headers.pop_back();

      buffer_header& header = used_buffer_headers.back();
      header.header->pAppPrivate = (void*)(used_buffer_headers.size() - 1);



      try
      {
        unsigned const small_file_size = 8750;
        assert(file_stream.is_open());
        std::size_t read = file_stream.rdbuf()->sgetn
          (static_cast<char*>(static_cast<void*>(header.header->pBuffer))
           , std::min(file_size - file_offset, header.header->nAllocLen));
        bool small_image = first && file_size < small_file_size;
        header.header->nFilledLen = small_image ? small_file_size : read ;
        if(small_image)
          std::memset(header.header->pBuffer + read
                      , 0, small_file_size - read);

        file_offset += read;
      }
      catch(...)
      {
        strerror(errno);
        throw;
      }

      header.header->nFlags = file_size == file_offset
                                ? OMX_BUFFERFLAG_EOS | OMX_BUFFERFLAG_ENDOFFRAME : 0;


      return header.header;
    }
  };

  void reset()
  {
    assert(!!load_queue);
    assert(!init_queue);


    
    OMX_ERRORTYPE r;
    static_cast<void>(r);
    void* null = 0;
    init_queue = boost::in_place<initialization_queue>(boost::ref(mutex), boost::ref(condition));


    
    r = OMX_SendCommand (renderer_handle, OMX_CommandFlush, renderer_ports.out, null);
    assert(r == OMX_ErrorNone);
    // r = OMX_SendCommand (decoder_handle, OMX_CommandFlush, decoder_ports.in, null);
    // assert(r == OMX_ErrorNone);
    // r = OMX_SendCommand (decoder_handle, OMX_CommandFlush, decoder_ports.in, null);
    // assert(r == OMX_ErrorNone);
    r = OMX_SendCommand (decoder_handle, OMX_CommandFlush, decoder_ports.in, null);
    assert(r == OMX_ErrorNone);


    load_queue->wait_all_buffers();


    init_queue->add_wait_command_result(CommandStateSet, OMX_StateIdle);

    r = OMX_SendCommand (decoder_handle, OMX_CommandStateSet, OMX_StateIdle, null);
    assert(r == OMX_ErrorNone);


    init_queue->add_wait_command_result(CommandStateSet, OMX_StateIdle);

    r = OMX_SendCommand (renderer_handle, OMX_CommandStateSet, OMX_StateIdle, null);
    assert(r == OMX_ErrorNone);


    init_queue->wait();    


    
    
    init_queue->add_wait_command_result(CommandPortDisable, decoder_ports.out);
    r = OMX_SendCommand (decoder_handle, OMX_CommandPortDisable, decoder_ports.out, null);
    assert(r == OMX_ErrorNone);

    init_queue->add_wait_command_result(CommandPortDisable, renderer_ports.in);
    r = OMX_SendCommand (renderer_handle, OMX_CommandPortDisable, renderer_ports.in, null);
    assert(r == OMX_ErrorNone);

    r = OMX_SetupTunnel(decoder_handle, decoder_ports.out
                        , 0, 0);
    assert(r == OMX_ErrorNone);

    init_queue->add_wait_command_result(CommandPortDisable, renderer_ports.out);
    r = OMX_SendCommand (renderer_handle, OMX_CommandPortDisable, renderer_ports.out, null);
    assert(r == OMX_ErrorNone);

    r = OMX_FreeBuffer (renderer_handle, renderer_ports.out, load_queue->texture_buffer_header);
    assert(r == OMX_ErrorNone);


    init_queue->wait();    


    

    eglDestroyImageKHR (*load_queue->eglDisplay, load_queue->texture_mem_handle);

    
    // Assynchronous - Initialization queue
    init_queue->add_wait_command_result(CommandPortDisable, decoder_ports.in);

    r = OMX_SendCommand (decoder_handle, OMX_CommandPortDisable, decoder_ports.in, null);
    assert(r == OMX_ErrorNone);


    for(std::vector<buffer_header>::iterator first = buffer_headers.begin()
          , last = buffer_headers.end(); first != last; ++first)
      OMX_FreeBuffer(decoder_handle, decoder_ports.in, first->header);
    buffer_headers.clear();

    
    init_queue->add_wait_command_result(CommandStateSet, OMX_StateExecuting);
    r = OMX_SendCommand (decoder_handle,  OMX_CommandStateSet, OMX_StateExecuting, null);
    assert(r == OMX_ErrorNone);


    load_queue = boost::none;
  }
  
  void decoder_output_port_changed() // Already locked
  {

    assert(!!load_queue);
    load_queue->decoder_output_port_changed = true;
  }

  std::size_t buffer_size;
  std::vector<buffer_header> buffer_headers;
  std::vector<unsigned char*> buffers;
  boost::optional<loading_image_queue> load_queue;

  struct ports
  {
    int in;
    int out;
  };
  
  OMX_HANDLETYPE decoder_handle, renderer_handle;
  ports decoder_ports, renderer_ports;
};

} }

#endif
