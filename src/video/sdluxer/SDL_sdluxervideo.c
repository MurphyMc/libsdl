/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 2019  Murphy McCauley

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Hocevar
    sam@zoy.org
*/

#ifdef SAVE_RCSID
static char rcsid =
 "";
#endif

/*
TODO
====
* Framerate throttle?
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>


#include "SDL.h"
#include "SDL_error.h"
#include "SDL_video.h"
#include "SDL_mouse.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"

#include "SDL_sdluxervideo.h"

#include "sdluxer.h"

#if 1
#define LOG(level, fmt, ...) fprintf(stderr, "   ???   " fmt "\n", ##__VA_ARGS__);
#define LOG_INFO(fmt, ...) fprintf(stderr, "   ---   " fmt "\n", ##__VA_ARGS__);
#define LOG_WARN(fmt, ...) fprintf(stderr, "   !!!   " fmt "\n", ##__VA_ARGS__);
#define LOG_ERROR(fmt, ...) fprintf(stderr, "   ***   " fmt "\n", ##__VA_ARGS__);
#define LOG_DEBUG(fmt, ...) fprintf(stderr, "   ...   " fmt "\n", ##__VA_ARGS__);
#else
#define LOG(level, fmt, ...)
#define LOG_INFO(fmt, ...)
#define LOG_WARN(fmt, ...)
#define LOG_ERROR(fmt, ...)
#define LOG_DEBUG(fmt, ...)
#endif

#define ME (this->hidden)


static int VideoInit(SDL_VideoDevice * this, SDL_PixelFormat *vformat);
static SDL_Rect **ListModes(SDL_VideoDevice * this, SDL_PixelFormat *format, Uint32 flags);
static SDL_Surface *DoSetVideoMode(SDL_VideoDevice * this, SDL_Surface *current, int width, int height, int bpp, Uint32 flags);
static void VideoQuit(SDL_VideoDevice * this);

static int AllocHWSurface(SDL_VideoDevice * this, SDL_Surface *surface);
static int LockHWSurface(SDL_VideoDevice * this, SDL_Surface *surface);
static int FlipHWSurface(SDL_VideoDevice * this, SDL_Surface *surface);
static void UnlockHWSurface(SDL_VideoDevice * this, SDL_Surface *surface);
static void FreeHWSurface(SDL_VideoDevice * this, SDL_Surface *surface);

static void UpdateRects(SDL_VideoDevice * this, int numrects, SDL_Rect *rects);



static int do_connect ()
{
  LOG_INFO("Connecting...");
  struct sockaddr_un addr = {0};
  const char * path = getenv("SDLUXER_SERVER");
  if (!path) return -1;
  if (strlen(path) >= sizeof(addr.sun_path)) return -1;
  int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (fd < 0) return -1;
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, path);

  if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0)
  {
    close(fd);
    return -1;
  }

  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  LOG_INFO("Connected.");

  return fd;
}

static bool has_screen (SDL_VideoDevice * this)
{
  return ME->surf1;
}

typedef struct SavedBuffer_tag
{
  struct SavedBuffer_tag * next;
  int size;
  char data[0];
} SavedBuffer;

static SavedBuffer * in_buf;
static SavedBuffer * out_buf;

static bool buffer_packet (SavedBuffer ** head, char * data, int size)
{
  SavedBuffer * b = malloc(size + sizeof(SavedBuffer));
  if (!b) return false;
  memcpy(b->data, data, size);
  b->size = size;
  if (*head == NULL)
  {
    b->next = NULL;
    *head = b;
  }
  else
  {
    SavedBuffer * old = *head;
    while (old->next) old = old->next;
    old->next = b;
  }
  return true;
}

static bool buffer_in (char * data, int size)
{
  return buffer_packet(&in_buf, data, size);
}

static bool buffer_out (char * data, int size)
{
  return buffer_packet(&out_buf, data, size);
}

// Returns false on error
static bool do_out (SDL_VideoDevice * this)
{
  while (out_buf)
  {
    LOG_DEBUG("Sending buffered output");
    int r = write(ME->sock, out_buf->data, out_buf->size);
    if (r == out_buf->size)
    {
      void * old = out_buf;
      out_buf = out_buf->next;
      free(old);
      continue;
    }
    else if (r == -1)
    {
      if (errno == EINTR)
      {
        continue;
      }
      else if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        return true;
      }
    }
    return false;
  }
  return true;
}

typedef struct
{
  int32_t type;
  char data[1024];
} MsgBuf __attribute__ ((aligned (4)));

static MsgBuf obuf, ibuf;

bool send_msg (SDL_VideoDevice * this, int size)
{
  void * buf = &obuf;
//  LOG_DEBUG("Sending message of type:%i size:%i (+4)", obuf.type, size);
  size += 4;
  if (ME->sock == -1) return false;
  if (out_buf) return buffer_out(buf, size);

  while (true)
  {
    int r = write(ME->sock, buf, size);
    if (r == size) return true;
    if (r == -1 && errno == EINTR) continue;

    if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
      return buffer_out(buf, size);
    }
  }

  // Unreachable
}

#define START_HANDLERS if (false) {
#define HANDLE(T) } else if (buf->type == T && length >= sizeof(T ## Msg)) { T##Msg * msg = (void*)buf->data; (void)msg;
#define OMSG(T,V) obuf.type = T; T##Msg * V = (void*)&obuf.data[0];

static bool process_in (MsgBuf * buf, int length);

static bool do_in (SDL_VideoDevice * this)
{
  while (in_buf)
  {
    LOG_DEBUG("Processing buffered input");
    bool r = process_in((MsgBuf*)in_buf->data, in_buf->size);
    void * old = in_buf;
    in_buf = in_buf->next;
    free(old);
    if (!r) return false;
  }
  while (true)
  {
    int r = read(ME->sock, &ibuf, sizeof(ibuf));
    if (r == -1)
    {
      if (errno == EINTR)
      {
        continue;
      }
      else if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        return true;
      }
    }
    if (r == 0) return false;
    if (r < 4)
    {
      LOG_ERROR("Read too short");
      return false;
    }

    if (!process_in(&ibuf, r)) return false;
  }
  return false; // unreachable
}

static bool block_for (SDL_VideoDevice * this, MsgType m, MsgType drop)
{
  LOG_DEBUG("Blocking for message of type(s) %i", m);
  while (true)
  {
    int r = read(ME->sock, &ibuf, sizeof(ibuf));
    if (r == -1)
    {
      if (errno == EINTR)
      {
        continue;
      }
      else if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        struct pollfd pfd;
        pfd.fd = ME->sock;
        pfd.events = POLLIN|POLLERR|POLLHUP;
        LOG_DEBUG("Polling...");
        r = poll(&pfd, 1, -1);
        LOG_DEBUG("Poll returned");
        if (r == -1 && errno == EINTR) continue;
        if (r == 1) continue;
        return false;
      }
      else
      {
        return false;
      }
    }
    if (r == 0) return false;
    if (r < 4)
    {
      LOG_ERROR("Read too short");
      return false;
    }

    LOG_DEBUG("RX message of type:%i size:%i while blocking for %i", ibuf.type, r, m);
    if (ibuf.type & m)
    {
      process_in(&ibuf, r);
      return true;
    }

    if (m & drop) continue;

    if (!buffer_in((void*)&ibuf, r)) return false; // Or is this okay?
  }
  return false; // unreachable
}



static bool process_in (MsgBuf * buf, int length)
{
  if (length < 4) return false;

  START_HANDLERS
  HANDLE(VideoModeSet)
    // Do nothing -- Should only be blocking
  HANDLE(Flipped)
    // Do nothing -- Should only be blocking
  HANDLE(KeyEvent)
    SDL_PrivateKeyboard(msg->event.state, &msg->event.keysym);
  HANDLE(MouseButtonEvent)
    SDL_PrivateMouseButton(msg->event.state, msg->event.button, msg->event.x, msg->event.y);
  HANDLE(MouseMoveEvent)
    SDL_PrivateMouseMotion(msg->event.state, 0, msg->event.x, msg->event.y);
  HANDLE(QuitEvent)
    SDL_PrivateQuit();
  HANDLE(ResizedEvent)
    SDL_PrivateResize(msg->event.w, msg->event.h);
  HANDLE(ActiveEvent)
    SDL_PrivateAppActive(msg->event.gain, msg->event.state);
  } else {
    LOG_INFO("Unhandled message");
    return true; // Or false?
  }

  // Normal exit

  return true;
}

static void quit (SDL_VideoDevice * this)
{
  SDL_PrivateQuit();
  if (ME->sock != -1)
  {
    close(ME->sock);
    ME->sock = -1;
  }
}

static void close_session (SDL_VideoDevice * this)
{
  quit(this);
}

static void free_video_mode (SDL_VideoDevice * this)
{
  if (!ME) return;

  if (ME->shmem)
  {
    int memsize = ME->pitch * ME->h * (ME->double_buf ? 2 : 1);
    munmap(ME->shmem, memsize);
    ME->shmem = NULL;
  }

  if (ME->surf1) SDL_FreeSurface(ME->surf1);
  if (ME->surf2) SDL_FreeSurface(ME->surf2);
  ME->surf2 = ME->surf1 = NULL;
}






static void PumpEvents(SDL_VideoDevice * this)
{
  if (ME->sock != -1)
  {
    if (!do_out(this)) goto err;
    if (!do_in(this)) goto err;
  }

  return;
err:
  LOG_INFO("Error exit from PumpEvents");
  quit(this);
}

static int Available(void)
{
  const char * v = getenv("SDLUXER_SERVER");
  bool avail = v && strlen(v);
  LOG_DEBUG("SDLuxer %savailable", avail ? "" : "un");
  return avail;
}

static void DeleteDevice (SDL_VideoDevice * this)
{
  LOG_INFO("DeleteDevice");
  if (!this) return;
  if (ME)
  {
    free(ME->caption);
    ME->caption = NULL;

    for (int i=0; i<SDLuxer_NUMMODES; ++i )
    {
      if (ME->modelist[i])
      {
        free(ME->modelist[i]);
        ME->modelist[i] = NULL;
      }
    }

    free_video_mode(this);
    free(this->hidden);
    this->hidden = NULL;
  }
  //free(this);
}

static void SetCaption (SDL_VideoDevice * this, const char * title, const char * icon)
{
  if (!title) return;
  if (title != ME->caption)
  {
    free(ME->caption);
    ME->caption = strdup(title);
  }
  if (!has_screen(this)) return;

  int len = strlen(title);
  if (len >= 128) return; //TODO: Clean this up
  OMSG(WM_SetCaption, out);
  strcpy(out->caption, title);
  if (!send_msg(this, sizeof(*out) + len + 1)) close_session(this);
}

static void WarpWMCursor (SDL_VideoDevice * this, Uint16 x, Uint16 y)
{
  OMSG(WarpMouse, out);
  out->x = x;
  out->y = y;
  if (!send_msg(this, sizeof(*out))) close_session(this);
}

static int ShowWMCursor (SDL_VideoDevice * this, WMcursor * cursor)
{
  if (!this->screen) return 0;
  int index = ((int)(intptr_t)cursor) - 1;
  OMSG(ManageCursor, mc);
  if (index != -1)
  {
    mc->op = CursorOpSet;
    mc->index = index;
    if (!send_msg(this, sizeof(*mc))) close_session(this);
  }
  mc->op = (index != -1) ? CursorOpShow : CursorOpHide;
  if (!send_msg(this, sizeof(*mc))) close_session(this);
  return 1;
}

static void FreeWMCursor (SDL_VideoDevice * this, WMcursor * cursor)
{
  if (!cursor) return;
  int index = ((int)(intptr_t)cursor) - 1;
  OMSG(ManageCursor, mc);
  mc->op = CursorOpDel;
  mc->index = index;
  if (!send_msg(this, sizeof(*mc))) close_session(this);
}

static WMcursor * CreateWMCursor (SDL_VideoDevice * this, Uint8 * data, Uint8 * mask, int w, int h, int hot_x, int hot_y)
{
  OMSG(AddCursor, ac);
  ac->w = w;
  ac->h = h;
  ac->hotx = hot_x;
  ac->hoty = hot_y;
  memcpy(ac->data, data, w/8*h);
  memcpy(ac->data+w/8*h, mask, w/8*h);
  if (!send_msg(this, sizeof(*ac)+2*w/8*h)) close_session(this);
  if (!block_for(this, CursorAdded, 0))
  {
    LOG_ERROR("Couldn't add cursor");
    close_session(this);
    return NULL;
  }
  CursorAddedMsg * ca = (CursorAddedMsg *)ibuf.data;
  if (ca->index == -1)
  {
    LOG_ERROR("Cursor add failed");
    return NULL;
  }
  return (WMcursor*)(intptr_t)(ca->index + 1);
}

static void InitOSKeymap (SDL_VideoDevice * this)
{
 // Nothing to do
}

static SDL_VideoDevice * CreateDevice(int devindex)
{
  SDL_VideoDevice * device;

  /* Initialize all variables that we clean on shutdown */
  device = (SDL_VideoDevice *)malloc(sizeof(SDL_VideoDevice));
  if ( device )
  {
    memset(device, 0, (sizeof *device));
    device->hidden = (struct SDL_PrivateVideoData *)
        malloc((sizeof *device->hidden));
  }
  if ( (device == NULL) || (device->hidden == NULL) ) {
    SDL_OutOfMemory();
    if ( device )
    {
      free(device);
    }
    return 0;
  }

  /* Initialize private data */
  memset(device->hidden, 0, (sizeof *device->hidden));
  device->hidden->sock = -1;

  /* Set the function pointers */
  device->VideoInit = VideoInit;
  device->ListModes = ListModes;
  device->SetVideoMode = DoSetVideoMode;
  device->CreateYUVOverlay = NULL;
  device->SetColors = NULL;
  device->UpdateRects = UpdateRects;
  device->VideoQuit = VideoQuit;
  device->AllocHWSurface = AllocHWSurface;
  device->CheckHWBlit = NULL;
  device->FillHWRect = NULL;
  device->SetHWColorKey = NULL;
  device->SetHWAlpha = NULL;
  device->LockHWSurface = LockHWSurface;
  device->UnlockHWSurface = UnlockHWSurface;
  device->FlipHWSurface = FlipHWSurface;
  device->FreeHWSurface = FreeHWSurface;
  device->SetCaption = SetCaption;
  device->WarpWMCursor = WarpWMCursor;
  device->FreeWMCursor = FreeWMCursor;
  device->CreateWMCursor = CreateWMCursor;
  device->ShowWMCursor = ShowWMCursor;
  device->SetIcon = NULL;
  device->IconifyWindow = NULL;
  device->GrabInput = NULL;
  device->GetWMInfo = NULL;
  device->InitOSKeymap = InitOSKeymap;
  device->PumpEvents = PumpEvents;

  device->free = DeleteDevice;

  device->hidden->sock = do_connect();
  if (device->hidden->sock == -1) return NULL;

  return device;
}

VideoBootStrap SDLuxer_bootstrap = {
  "sdluxer", "SDL via SDLuxer",
  Available, CreateDevice
};

static int VideoInit (SDL_VideoDevice * this, SDL_PixelFormat * vformat)
{
  int i;

  // Initialize all variables that we clean on shutdown
  for ( i=0; i<SDLuxer_NUMMODES; ++i ) {
    ME->modelist[i] = malloc(sizeof(SDL_Rect));
    ME->modelist[i]->x = ME->modelist[i]->y = 0;
  }
  /* Modes sorted largest to smallest */
  ME->modelist[0]->w = 1024; ME->modelist[0]->h = 768;
  ME->modelist[1]->w = 800; ME->modelist[1]->h = 600;
  ME->modelist[2]->w = 640; ME->modelist[2]->h = 480;
  ME->modelist[3]->w = 320; ME->modelist[3]->h = 400;
  ME->modelist[4]->w = 320; ME->modelist[4]->h = 240;
  ME->modelist[5]->w = 320; ME->modelist[5]->h = 200;
  ME->modelist[6] = NULL;

  //ME->mutex = SDL_CreateMutex();

  /* Initialize private variables */
  ME->surf1 = ME->surf2 = NULL;
  ME->double_buf = false;

  // Set native video format
  vformat->BitsPerPixel = 32;
  vformat->BytesPerPixel = (vformat->BitsPerPixel + 7)/8;

  // We're done!
  return 0;
}

static SDL_Rect ** ListModes (SDL_VideoDevice * this, SDL_PixelFormat * format, Uint32 flags)
{
  if ( (format->BitsPerPixel != 15)
    && (format->BitsPerPixel != 16)
    && (format->BitsPerPixel != 24)
    && (format->BitsPerPixel != 32))
  {
    return NULL;
  }

  // Allow arbitrary size for windowed apps
  if ( (flags & SDL_FULLSCREEN) == 0) return (SDL_Rect **) -1;

  return ME->modelist;
}

static SDL_Surface * DoSetVideoMode (SDL_VideoDevice * this, SDL_Surface * current,
        int width, int height, int bpp, Uint32 flags)
{
  if (bpp == 0) bpp = 32;
  bool double_buf = flags & SDL_DOUBLEBUF;
  bool resizable = flags & SDL_RESIZABLE;

  OMSG(SetVideoMode, out);
  out->w = width;
  out->h = height;
  out->double_buf = double_buf;
  out->resizable = resizable;

  LOG_DEBUG("Requesting SetVideoMode");
  if (!send_msg(this, sizeof(*out)))
  {
    close_session(this);
    return NULL;
  }

  LOG_DEBUG("Waiting for reply");
  if (!block_for(this, VideoModeSet, 0))
  {
    close_session(this);
    return NULL;
  }

  VideoModeSetMsg * vms = (VideoModeSetMsg *)ibuf.data;

  if (!vms->success)
  {
    LOG_DEBUG("Server reported failure");
    return NULL; // Or return old video mode?
  }

  LOG_DEBUG("Getting shared memory '%s'", vms->name);
  int shmfd = shm_open(vms->name, O_RDWR, 0);
  if (shmfd < 0)
  {
    LOG_ERROR("Couldn't open shared memory");
    return NULL; // Or return old video mode?
  }
  shm_unlink(vms->name);

  int memsize = vms->pitch * vms->h * (vms->double_buf ? 2 : 1);

  char * pixels = mmap(NULL, memsize, PROT_WRITE|PROT_READ, MAP_SHARED, shmfd, 0);

  close(shmfd);

  if (!pixels)
  {
    return NULL; // Or return old video mode?
  }

  LOG_DEBUG("Creating surfaces");
  SDL_Surface * surf1 = SDL_CreateRGBSurfaceFrom(pixels, vms->w, vms->h, vms->depth, vms->pitch, vms->rmask, vms->gmask, vms->bmask, 0);
  if (!surf1)
  {
    munmap(pixels, memsize);
    return NULL; // Or return old video mode?
  }

  SDL_Surface * surf2 = NULL;
  if (vms->double_buf)
  {
    surf2 = SDL_CreateRGBSurfaceFrom(pixels + vms->pitch*vms->h, vms->w, vms->h, vms->depth, vms->pitch, vms->rmask, vms->gmask, vms->bmask, 0);
    if (!surf2)
    {
      SDL_FreeSurface(surf1);
      munmap(pixels, memsize);
      return NULL; // Or return old video mode?
    }
  }

  LOG_DEBUG("Reallocating format");
  if ( ! SDL_ReallocFormat(current, vms->depth, vms->rmask, vms->gmask, vms->bmask, 0) )
  {
    LOG_ERROR("Couldn't reallocate format");
    SDL_FreeSurface(surf1);
    if (surf2) SDL_FreeSurface(surf2);
    munmap(pixels, memsize);
    return NULL;
  }

  current->flags = 0;
  if (resizable) current->flags |= SDL_RESIZABLE;
  if (vms->double_buf) current->flags |= SDL_DOUBLEBUF;
  current->flags |= SDL_SWSURFACE; //???
  current->flags |= SDL_PREALLOC; // Don't try to free memory

  current->w = vms->w;
  current->h = vms->h;
  current->pitch = vms->pitch;
  current->pixels = surf1->pixels;

  free_video_mode(this);

  ME->surf1 = surf1;
  ME->surf2 = surf2;
  ME->pitch = current->pitch;
  ME->w = current->w;
  ME->h = current->h;

  SetCaption(this, ME->caption, NULL);

  LOG_INFO("Set video mode");

  return current;
}

/* We don't actually allow hardware surfaces other than the main one */
static int AllocHWSurface (SDL_VideoDevice * this, SDL_Surface * surface)
{
  return -1;
}

static void FreeHWSurface (SDL_VideoDevice * this, SDL_Surface * surface)
{
  return;
}

/* We need to wait for vertical retrace on page flipped displays */
static int LockHWSurface (SDL_VideoDevice * this, SDL_Surface * surface)
{
  /* TODO ? */
  return 0;
}

static void UnlockHWSurface (SDL_VideoDevice * this, SDL_Surface * surface)
{
  return;
}

static int FlipHWSurface (SDL_VideoDevice * this, SDL_Surface * surface)
{
  if ( (surface->pixels == ME->surf1->pixels)
    || ( ME->surf2 && surface->pixels == ME->surf2->pixels ) )
  {
    // Good
  }
  else
  {
    return -1;
  }

  if (!has_screen(this)) return -1;

  LOG_DEBUG("Flip");
  OMSG(Draw, out);

  out->flip = ME->double_buf;

  if (!send_msg(this, sizeof(*out)))
  {
    close_session(this);
    return -1;
  }

  if (out->flip)
  {
    if (!block_for(this, Flipped, 0))
    {
      close_session(this);
      return -1;
    }

    if (surface->pixels == ME->surf1->pixels)
    {
      surface->pixels = ME->surf2->pixels;
    }
    else
    {
      surface->pixels = ME->surf1->pixels;
    }
  }

  return 0;
}

static void UpdateRects (SDL_VideoDevice * this, int numrects, SDL_Rect * rects)
{
  if (!numrects) return;
  if (!has_screen(this)) return;

  if (ME->double_buf)
  {
    for ( int i = 0; i < numrects; i++ )
    {
      SDL_Rect * r = rects+i;
      if (!r || (r->x == 0 && r->y == 0 && r->w == 0 && r->h == 0))
      {
        SDL_BlitSurface(ME->surf1, NULL, ME->surf2, NULL);
      }
      else
      {
        SDL_BlitSurface(ME->surf1, r, ME->surf2, r);
      }
    }
  }

//  LOG_DEBUG("UpdateRects");
  OMSG(Draw, out);

  out->flip = false;

  if (!send_msg(this, sizeof(*out)))
  {
    close_session(this);
  }

  return;
}

// According to a note in many of the other backends, if we are terminated,
// this could be called in the middle of another SDL video routine, notably
// UpdateRects.  Is that actually true?  If so, maybe there are race
// conditions and maybe we actually need a mutex.  On the other hand, for
// modern platforms, we probably don't need to actually do anything to
// clean up, so maybe this whole thing could be empty.
static void VideoQuit (SDL_VideoDevice * this)
{
  LOG_INFO("VideoQuit");
  DeleteDevice(this);

  //SDL_DestroyMutex(ME->mutex);
}
