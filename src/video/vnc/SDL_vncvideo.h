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

#ifndef _SDL_vncvideo_h
#define _SDL_vncvideo_h

#include "SDL_mouse.h"
#include "../SDL_sysvideo.h"
#include "SDL_mutex.h"

#include <sys/time.h>
#include <time.h>
#include <rfb/rfb.h>


/* Hidden "this" pointer for the video functions */
#define _THIS	SDL_VideoDevice *this

#define SDL_NUMMODES 6

/* Private display data */
struct SDL_PrivateVideoData {
  SDL_Rect *SDL_modelist[SDL_NUMMODES+1];

  void *buffer;
  int buffer_size;
  int w, h;

  int client_count;
  rfbScreenInfoPtr screen;

  int last_curx;
  int last_cury;
  int last_buttonmask;

  unsigned keyframe_prev;
  int keyframe_delay;
};

/* Old variable names */
#define SDL_modelist		(this->hidden->SDL_modelist)
#define VNC_palette		    (this->hidden->palette)
#define VNC_buffer		    (this->hidden->buffer)
#define VNC_buffer_size	    (this->hidden->buffer_size)

#define VNC_w		    (this->hidden->w)
#define VNC_h		    (this->hidden->h)

#endif /* _SDL_vncvideo_h */

