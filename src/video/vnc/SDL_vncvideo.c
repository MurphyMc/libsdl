/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 2003  Sam Hocevar
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
* Cursor stuff?

*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>


#include "SDL.h"
#include "SDL_error.h"
#include "SDL_video.h"
#include "SDL_mouse.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"

#include "SDL_vncvideo.h"
#include "SDL_vncevents_c.h"


/* Initialization/Query functions */
static int VNC_VideoInit(_THIS, SDL_PixelFormat *vformat);
static SDL_Rect **VNC_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags);
static SDL_Surface *VNC_SetVideoMode(_THIS, SDL_Surface *current, int width, int height, int bpp, Uint32 flags);
static void VNC_VideoQuit(_THIS);

/* Hardware surface functions */
static int VNC_AllocHWSurface(_THIS, SDL_Surface *surface);
static int VNC_LockHWSurface(_THIS, SDL_Surface *surface);
static int VNC_FlipHWSurface(_THIS, SDL_Surface *surface);
static void VNC_UnlockHWSurface(_THIS, SDL_Surface *surface);
static void VNC_FreeHWSurface(_THIS, SDL_Surface *surface);

#define DEFAULT_BPP 32

#define SELF (this->hidden)

// This is taken straight from main/macos/SDL_main.c.
// We should probably make it reusable!
static int ParseCommandLine(char *cmdline, char **argv)
{
        char *bufp;
        int argc;

        argc = 0;
        for ( bufp = cmdline; *bufp; ) {
                /* Skip leading whitespace */
                while ( SDL_isspace(*bufp) ) {
                        ++bufp;
                }
                /* Skip over argument */
                if ( *bufp == '"' ) {
                        ++bufp;
                        if ( *bufp ) {
                                if ( argv ) {
                                        argv[argc] = bufp;
                                }
                                ++argc;
                        }
                        /* Skip over word */
                        while ( *bufp && (*bufp != '"') ) {
                                ++bufp;
                        }
                } else {
                        if ( *bufp ) {
                                if ( argv ) {
                                        argv[argc] = bufp;
                                }
                                ++argc;
                        }
                        /* Skip over word */
                        while ( *bufp && ! SDL_isspace(*bufp) ) {
                                ++bufp;
                        }
                }
                if ( *bufp ) {
                        if ( argv ) {
                                *bufp = '\0';
                        }
                        ++bufp;
                }
        }
        if ( argv ) {
                argv[argc] = NULL;
        }
        return(argc);
}


static void on_client_leave (rfbClientPtr cl)
{
  SDL_VideoDevice * dev = ((SDL_VideoDevice*)cl->screen->screenData);
  dev->hidden->client_count--;
  if (dev->hidden->client_count <= 0)
  {
    SDL_PrivateQuit();
  }
}

static void on_ptr (int buttonmask, int x, int y, rfbClientPtr cl)
{
  struct SDL_PrivateVideoData * self = ((SDL_VideoDevice*)cl->screen->screenData)->hidden;
  if (buttonmask != self->last_buttonmask)
  {
    int cur = buttonmask, prev = self->last_buttonmask;
    self->last_buttonmask = buttonmask;
    for (int i = 0; i < 3; i++)
    {
      int cv = cur & 1, pv = prev & 1;
      if (cv == pv) continue;
      SDL_PrivateMouseButton(cv ? SDL_PRESSED : SDL_RELEASED, i, 0, 0);
      cur >>= 1;
      prev >>= 1;
    }
  }
  if (x != self->last_curx || y != self->last_cury)
  {
    self->last_curx = x;
    self->last_cury = y;
    SDL_PrivateMouseMotion(0, 0, x, y);
  }
  rfbDefaultPtrAddEvent(buttonmask,x,y,cl);
}

extern void VNC_on_key (rfbBool down, rfbKeySym key, rfbClientPtr cl);


static enum rfbNewClientAction on_client_join (rfbClientPtr cl)
{
  SDL_VideoDevice * dev = ((SDL_VideoDevice*)cl->screen->screenData);

  dev->hidden->client_count++;
  cl->clientGoneHook = on_client_leave;

  return RFB_CLIENT_ACCEPT;
}

static void VNC_PumpEvents(_THIS)
{
  if (!SELF->screen) return;
  if (!rfbIsActive(SELF->screen)) return;

  if (SELF->keyframe_delay >= 0)
  {
    unsigned now = SDL_GetTicks();
    if ((now - SELF->keyframe_prev) > (unsigned)SELF->keyframe_delay)
    {
      SELF->keyframe_prev = now;
      rfbMarkRectAsModified(SELF->screen, 0,0, SELF->w, SELF->h);
    }
  }

  while (rfbProcessEvents(SELF->screen, 0));
}


/* Cache the VideoDevice struct */
//static struct SDL_VideoDevice *local_this;

/* driver bootstrap functions */

static int VNC_Available(void)
{
  return 1; /* Always available ! */
}

static int getenvint (const char * var, int def)
{
  const char * v = getenv(var);
  if (!v) return def;
  return strtol(v, NULL, 10);
}
#define getenvintz(var) getenvint(var, 0)

static void VNC_DeleteDevice(SDL_VideoDevice *device)
{
  free(device->hidden);
  free(device);
}
static SDL_VideoDevice *VNC_CreateDevice(int devindex)
{
  SDL_VideoDevice *device;

  /* Initialize all variables that we clean on shutdown */
  device = (SDL_VideoDevice *)malloc(sizeof(SDL_VideoDevice));
  if ( device ) {
    memset(device, 0, (sizeof *device));
    device->hidden = (struct SDL_PrivateVideoData *)
        malloc((sizeof *device->hidden));
  }
  if ( (device == NULL) || (device->hidden == NULL) ) {
    SDL_OutOfMemory();
    if ( device ) {
      free(device);
    }
    return(0);
  }
  memset(device->hidden, 0, (sizeof *device->hidden));

  /* Set the function pointers */
  device->VideoInit = VNC_VideoInit;
  device->ListModes = VNC_ListModes;
  device->SetVideoMode = VNC_SetVideoMode;
  device->CreateYUVOverlay = NULL;
  device->SetColors = NULL;
  device->UpdateRects = NULL;
  device->VideoQuit = VNC_VideoQuit;
  device->AllocHWSurface = VNC_AllocHWSurface;
  device->CheckHWBlit = NULL;
  device->FillHWRect = NULL;
  device->SetHWColorKey = NULL;
  device->SetHWAlpha = NULL;
  device->LockHWSurface = VNC_LockHWSurface;
  device->UnlockHWSurface = VNC_UnlockHWSurface;
  device->FlipHWSurface = VNC_FlipHWSurface;
  device->FreeHWSurface = VNC_FreeHWSurface;
  device->SetCaption = NULL;
  device->SetIcon = NULL;
  device->IconifyWindow = NULL;
  device->GrabInput = NULL;
  device->GetWMInfo = NULL;
  device->InitOSKeymap = VNC_InitOSKeymap;
  device->PumpEvents = VNC_PumpEvents;

  device->free = VNC_DeleteDevice;

  return device;
}

VideoBootStrap VNC_bootstrap = {
  "vnc", "SDL via VNC",
  VNC_Available, VNC_CreateDevice
};

int VNC_VideoInit(_THIS, SDL_PixelFormat *vformat)
{
  int i;

  /* Initialize all variables that we clean on shutdown */
  for ( i=0; i<SDL_NUMMODES; ++i ) {
    SDL_modelist[i] = malloc(sizeof(SDL_Rect));
    SDL_modelist[i]->x = SDL_modelist[i]->y = 0;
  }
  /* Modes sorted largest to smallest */
  SDL_modelist[0]->w = 1024; SDL_modelist[0]->h = 768;
  SDL_modelist[1]->w = 800; SDL_modelist[1]->h = 600;
  SDL_modelist[2]->w = 640; SDL_modelist[2]->h = 480;
  SDL_modelist[3]->w = 320; SDL_modelist[3]->h = 400;
  SDL_modelist[4]->w = 320; SDL_modelist[4]->h = 240;
  SDL_modelist[5]->w = 320; SDL_modelist[5]->h = 200;
  SDL_modelist[6] = NULL;

  //VNC_mutex = SDL_CreateMutex();

  /* Initialize private variables */
  //XXX VNC_lastkey = 0;
  VNC_buffer = NULL;
  VNC_buffer_size = 0;

//  local_this = this;

  /* Determine the screen depth (use default 16-bit depth) */
  int def = getenvintz("SDL_VNC_VID_BPP_HINT");
  if (def == 0) def = DEFAULT_BPP;
  vformat->BitsPerPixel = def;
  vformat->BytesPerPixel = (def + 7)/8*8;

  /* We're done! */
  return(0);
}

SDL_Rect **VNC_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags)
{
  if ( /*(format->BitsPerPixel != 8)
    &&*/ (format->BitsPerPixel != 15)
    && (format->BitsPerPixel != 24)
    && (format->BitsPerPixel != 32))
  {
    return NULL;
  }

  // Allow arbitrary width for windowed apps
  if ( (flags & SDL_FULLSCREEN) == 0) return (SDL_Rect **) -1;

  return SDL_modelist;
}

/* Various screen update functions available */
static void VNC_DirectUpdate(_THIS, int numrects, SDL_Rect *rects);

SDL_Surface *VNC_SetVideoMode(_THIS, SDL_Surface *current,
        int width, int height, int bpp, Uint32 flags)
{
  if ( VNC_buffer ) {
    munmap( VNC_buffer, VNC_buffer_size );
    VNC_buffer = NULL;
    VNC_buffer_size = 0;
  }

  int realbpp = bpp;
  // realbpp determines number of bytes in memory
  // bpp is the layout of pixel components
  switch (bpp)
  {
    case 0:
      bpp = realbpp = DEFAULT_BPP;
      break;
    case 16:
      bpp = 15; // Force to 15 bit mode
      break;
    //case 8:
    case 24:
    case 32:
      break;
    case 15:
      realbpp = 16;
      break;
    default:
      SDL_SetError("Couldn't allocate buffer for requested mode");
      return(NULL);
  }
  int realwidth = width;
  int pad = 4; // Some vnc clients are picky
  if (pad) realwidth = (realwidth+(pad-1))/pad*pad;

  int pitch = realwidth * (realbpp / 8);
  pad = 0;
  if (pad) pitch = (pitch+(pad-1))/pad*pad;

  VNC_buffer_size = pitch * height;

  VNC_buffer = malloc(pitch * height);

  memset(VNC_buffer, 0, VNC_buffer_size);

  static int mode8[] = {0, 0, 0};
  static int mode15[] = {0x7c00, 0x03e0, 0x001f};
  static int mode16[] = {0xf800, 0x07e0, 0x001f};
  static int mode24[] = {0x0000ff, 0x00ff00, 0xff0000};
  static int mode32[] = {0x0000ff, 0x00ff00, 0xff0000};
  int R = 0, G = 1, B = 2;

  int swap_rb = getenvintz("SDL_VID_VNC_SWAP_RB");
  if (swap_rb) { R = 2; B = 0; }
  int * mode = NULL;
  switch (bpp)
  {
    case 8: mode = mode8; break;
    case 15: mode = mode15; break;
    case 16: mode = mode16; break;
    case 24: mode = mode24; break;
    case 32: mode = mode32; break;
  }

  /* Allocate the new pixel format for the screen */
  if ( ! SDL_ReallocFormat(current, bpp, mode[R], mode[G], mode[B], (bpp==32) ? 0xff000000 : 0) )
  {
    return NULL;
  }

  /* Set up the new mode framebuffer */
  current->flags = SDL_FULLSCREEN;
  VNC_w = current->w = width;
  VNC_h = current->h = height;
  current->pitch = pitch;
  current->pixels = VNC_buffer;

  /* Set the blit function */
  this->UpdateRects = VNC_DirectUpdate;

  this->hidden->keyframe_delay = getenvint("SDL_VID_VNC_FRAMEDELAY", -1);


  char * cmdline = SDL_getenv("SDL_VID_VNC_CMDLINE");
  if (!cmdline) cmdline = "";
  char ** argv = malloc(sizeof(char*) * strlen(cmdline)/2 + 2);
  argv[0] = "sdl_app";
  int argc = ParseCommandLine(cmdline, argv+1);
  argc += 1;
  int bits_per_component = (bpp == 15)  ? 5 : 8;
  rfbScreenInfoPtr sc = rfbGetScreen(&argc, argv, width, height, bits_per_component, 3, realbpp/8);
  this->hidden->screen = sc;
  free(argv);
  if (!sc)
  {
    SDL_SetError("Couldn't get VNC screen");
    return NULL;
  }

  sc->frameBuffer = VNC_buffer;
  sc->desktopName = "SDL App";
  //sc->alwaysShared = TRUE;
  sc->ptrAddEvent = on_ptr;
  sc->kbdAddEvent = VNC_on_key;
  sc->newClientHook = on_client_join;
  sc->screenData = (void*)this;

  rfbInitServer(sc);

  printf("VNC Video format - size:%ix%i bpp:%i realbpp:%i realwidth:%i pitch:%i\n", width, height, bpp, realbpp, realwidth, pitch);

  /* We're done */
  return current;
}

/* We don't actually allow hardware surfaces other than the main one */
static int VNC_AllocHWSurface(_THIS, SDL_Surface *surface)
{
  return -1;
}
static void VNC_FreeHWSurface(_THIS, SDL_Surface *surface)
{
  return;
}

/* We need to wait for vertical retrace on page flipped displays */
static int VNC_LockHWSurface(_THIS, SDL_Surface *surface)
{
  /* TODO ? */
  return 0;
}
static int VNC_FlipHWSurface(_THIS, SDL_Surface *surface)
{
  rfbMarkRectAsModified(this->hidden->screen, 0,0, SELF->w, SELF->h);
  //TODO: Should we claim SDL_DOUBLEBUF and throttle the frame rate here
  //      by processing VNC events for as long as the "rest of the frame"?
  return 0;
}

static void VNC_UnlockHWSurface(_THIS, SDL_Surface *surface)
{
  return;
}

static void VNC_DirectUpdate(_THIS, int numrects, SDL_Rect *rects)
{
  for ( int i = 0; i < numrects; i++ )
  {
    SDL_Rect * r = rects+i;
    rfbMarkRectAsModified(this->hidden->screen, r->x, r->y, r->x+r->w,r->y+r->h);
  }

  return;
}

/* Note:  If we are terminated, this could be called in the middle of
   another SDL video routine -- notably UpdateRects.
*/
void VNC_VideoQuit(_THIS)
{
  int i;

  /* Free video mode lists */
  for ( i=0; i<SDL_NUMMODES; ++i ) {
    if ( SDL_modelist[i] != NULL ) {
      free(SDL_modelist[i]);
      SDL_modelist[i] = NULL;
    }
  }

  if ( VNC_buffer ) {
    munmap( VNC_buffer, VNC_buffer_size );
    VNC_buffer = NULL;
  }

  //SDL_DestroyMutex(VNC_mutex);
}
