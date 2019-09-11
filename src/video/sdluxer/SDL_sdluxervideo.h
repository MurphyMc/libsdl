/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 2019  Murphy McCauley
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

#ifndef _SDL_sdluxervideo_h
#define _SDL_sdluxervideo_h

#include "SDL_mouse.h"
#include "../SDL_sysvideo.h"
//#include "SDL_mutex.h"

#include <sys/time.h>
#include <time.h>
#include <stdbool.h>


#define SDLuxer_NUMMODES 6

/* Private display data */
struct SDL_PrivateVideoData {
  SDL_Rect * modelist[SDLuxer_NUMMODES+1];

  // w/h/pitch/etc. are all duplicated by surf, etc.
  // Maybe we don't need them?
  int w, h;
  bool double_buf;
  int pitch;
  SDL_Surface * surf1;
  SDL_Surface * surf2;

  void * shmem;
  char * caption;

  int sock;
};

#endif

