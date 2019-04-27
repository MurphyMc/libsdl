/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 2003  Sam Hocevar

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

/* simple memory-based SDL video driver implementation.
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

#include "SDL_memvideo.h"
#include "SDL_memevents_c.h"


/* Initialization/Query functions */
static int Mem_VideoInit(_THIS, SDL_PixelFormat *vformat);
static SDL_Rect **Mem_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags);
static SDL_Surface *Mem_SetVideoMode(_THIS, SDL_Surface *current, int width, int height, int bpp, Uint32 flags);
static void Mem_VideoQuit(_THIS);

/* Hardware surface functions */
static int Mem_AllocHWSurface(_THIS, SDL_Surface *surface);
static int Mem_LockHWSurface(_THIS, SDL_Surface *surface);
static int Mem_FlipHWSurface(_THIS, SDL_Surface *surface);
static void Mem_UnlockHWSurface(_THIS, SDL_Surface *surface);
static void Mem_FreeHWSurface(_THIS, SDL_Surface *surface);

#define DEFAULT_BPP 32


/* Cache the VideoDevice struct */
//static struct SDL_VideoDevice *local_this;

/* driver bootstrap functions */

static int Mem_Available(void)
{
  return 1; /* Always available ! */
}

static int getenvint (const char * var)
{
  const char * v = getenv(var);
  if (!v) return 0;
  return strtol(v, NULL, 10);
}

static void Mem_DeleteDevice(SDL_VideoDevice *device)
{
  free(device->hidden);
  free(device);
}
static SDL_VideoDevice *Mem_CreateDevice(int devindex)
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
  device->VideoInit = Mem_VideoInit;
  device->ListModes = Mem_ListModes;
  device->SetVideoMode = Mem_SetVideoMode;
  device->CreateYUVOverlay = NULL;
  device->SetColors = NULL;
  device->UpdateRects = NULL;
  device->VideoQuit = Mem_VideoQuit;
  device->AllocHWSurface = Mem_AllocHWSurface;
  device->CheckHWBlit = NULL;
  device->FillHWRect = NULL;
  device->SetHWColorKey = NULL;
  device->SetHWAlpha = NULL;
  device->LockHWSurface = Mem_LockHWSurface;
  device->UnlockHWSurface = Mem_UnlockHWSurface;
  device->FlipHWSurface = Mem_FlipHWSurface;
  device->FreeHWSurface = Mem_FreeHWSurface;
  device->SetCaption = NULL;
  device->SetIcon = NULL;
  device->IconifyWindow = NULL;
  device->GrabInput = NULL;
  device->GetWMInfo = NULL;
  device->InitOSKeymap = Mem_InitOSKeymap;
  device->PumpEvents = Mem_PumpEvents;

  device->free = Mem_DeleteDevice;

  return device;
}

VideoBootStrap Mem_bootstrap = {
  "mem", "Simple Memory Interface",
  Mem_Available, Mem_CreateDevice
};

int Mem_VideoInit(_THIS, SDL_PixelFormat *vformat)
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

  Mem_mutex = SDL_CreateMutex();

  /* Initialize private variables */
  //XXX Mem_lastkey = 0;
  Mem_buffer = NULL;
  Mem_buffer_size = 0;

//  local_this = this;

  /* Determine the screen depth (use default 16-bit depth) */
  int def = getenvint("SDL_MEM_VID_BPP_HINT");
  if (def == 0) def = DEFAULT_BPP;
  vformat->BitsPerPixel = def;
  vformat->BytesPerPixel = (def + 7)/8*8;

  /* We're done! */
  return(0);
}

SDL_Rect **Mem_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags)
{
  if ( (format->BitsPerPixel != 8)
    && (format->BitsPerPixel != 15)
    && (format->BitsPerPixel != 16)
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
static void Mem_DirectUpdate(_THIS, int numrects, SDL_Rect *rects);

SDL_Surface *Mem_SetVideoMode(_THIS, SDL_Surface *current,
        int width, int height, int bpp, Uint32 flags)
{
  if ( Mem_buffer ) {
    munmap( Mem_buffer, Mem_buffer_size );
    Mem_buffer = NULL;
    Mem_buffer_size = 0;
  }

  int realbpp = bpp;
  switch (bpp)
  {
    case 0:
      bpp = realbpp = DEFAULT_BPP;
      break;
    case 16:
      //bpp = 15; // Force to 15 bit mode
      break;
    case 8:
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
  int pad = getenvint("SDL_MEM_VID_PAD_PIX");
  if (pad) realwidth = (realwidth+(pad-1))/pad*pad;

  int pitch = realwidth * (realbpp / 8);
  pad = getenvint("SDL_MEM_VID_PAD_PITCH");
  if (pad) pitch = (pitch+(pad-1))/pad*pad;

  Mem_buffer_size = pitch * height;
  //printf("Video format - size:%ix%i bpp:%i realbpp:%i realwidth:%i pitch:%i\n", width, height, bpp, realbpp, realwidth, pitch);

  char * filename = getenv("SDL_MEM_VID_FILE");
  if (!filename) filename = "sdl_vid_mem";
  int fd = open(filename, O_CREAT | O_RDWR, S_IRUSR|S_IWUSR);
  if (!fd)
  {
    SDL_SetError("Couldn't open file for memory mapped video");
    return NULL;
  }
  ftruncate(fd, Mem_buffer_size);
  Mem_buffer = mmap(NULL, Mem_buffer_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if ( ! Mem_buffer ) {
    SDL_SetError("Couldn't allocate buffer for requested mode");
    return NULL;
  }

  memset(Mem_buffer, 0, Mem_buffer_size);

  static int mode8[] = {0, 0, 0};
  static int mode15[] = {0xfc00, 0x03e0, 0x001f};
  static int mode16[] = {0xf800, 0x07e0, 0x001f};
  static int mode24[] = {0x0000ff, 0x00ff00, 0xff0000};
  static int mode32[] = {0x0000ff, 0x00ff00, 0xff0000};
  int R = 0, G = 1, B = 2;

  int swap_rb = getenvint("SDL_MEM_VID_SWAP_RB");
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
  Mem_w = current->w = width;
  Mem_h = current->h = height;
  current->pitch = pitch;
  current->pixels = Mem_buffer;

  /* Set the blit function */
  this->UpdateRects = Mem_DirectUpdate;

  /* We're done */
  return current;
}

/* We don't actually allow hardware surfaces other than the main one */
static int Mem_AllocHWSurface(_THIS, SDL_Surface *surface)
{
  return -1;
}
static void Mem_FreeHWSurface(_THIS, SDL_Surface *surface)
{
  return;
}

/* We need to wait for vertical retrace on page flipped displays */
static int Mem_LockHWSurface(_THIS, SDL_Surface *surface)
{
  /* TODO ? */
  return 0;
}
static int Mem_FlipHWSurface(_THIS, SDL_Surface *surface)
{
  return 0; // Lie
}

static void Mem_UnlockHWSurface(_THIS, SDL_Surface *surface)
{
  return;
}

static void Mem_DirectUpdate(_THIS, int numrects, SDL_Rect *rects)
{
  return;
}

/* Note:  If we are terminated, this could be called in the middle of
   another SDL video routine -- notably UpdateRects.
*/
void Mem_VideoQuit(_THIS)
{
  int i;

  /* Free video mode lists */
  for ( i=0; i<SDL_NUMMODES; ++i ) {
    if ( SDL_modelist[i] != NULL ) {
      free(SDL_modelist[i]);
      SDL_modelist[i] = NULL;
    }
  }

  if ( Mem_buffer ) {
    munmap( Mem_buffer, Mem_buffer_size );
    Mem_buffer = NULL;
  }

  SDL_DestroyMutex(Mem_mutex);
}

