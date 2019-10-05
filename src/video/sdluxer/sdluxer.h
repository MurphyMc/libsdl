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
*/

/*
This file is derived from the one that is part of SDLuxer itself.  It
must be in sync with the version of SDLuxer you are using.
*/

typedef struct // CS
{
  int w;
  int h;
  bool double_buf;
  bool resizable;
} SetVideoModeMsg;

typedef struct // CS
{
  int x;
  int y;
} WarpMouseMsg;

typedef struct // CS
{
  char caption[0];
} WM_SetCaptionMsg;

typedef struct // SC
{
  bool success;
  bool double_buf;
  int w;
  int h;
  int pitch;
  int depth;
  uint32_t rmask;
  uint32_t gmask;
  uint32_t bmask;
  char name[0];
} VideoModeSetMsg;

typedef struct // CS - request a flip
{
  bool flip; // Otherwise, it's just a draw
} DrawMsg;

typedef struct // SC - flip done
{
} FlippedMsg;

typedef struct // SC - SDL event
{
  SDL_KeyboardEvent event;
} KeyEventMsg;

typedef struct // SC - SDL event
{
  SDL_MouseButtonEvent event;
} MouseButtonEventMsg;

typedef struct // SC - SDL event
{
  SDL_MouseMotionEvent event;
} MouseMoveEventMsg;

typedef struct // SC - SDL event
{
} QuitEventMsg;

typedef struct // SC - SDL event
{
  SDL_ResizeEvent event;
} ResizedEventMsg;

typedef struct // SC - SDL event
{
  SDL_ActiveEvent event;
} ActiveEventMsg;

typedef struct // CS
{
  int w, h;
  int hotx, hoty;
  char data[0]; // And the mask
} AddCursorMsg;

typedef struct // CS
{
  int op; // 0 = set cursor, 1 = del cursor, 2 = show, 3 = hide
  int index; // For 0 and 1
} ManageCursorMsg;

typedef enum
{
  CursorOpSet = 0,
  CursorOpDel = 1,
  CursorOpShow = 2,
  CursorOpHide = 3,
} ManageCursorOps;

typedef struct // SC
{
  int index; // -1 on error
} CursorAddedMsg;

typedef enum MsgType
{
  Dummy=0,
  SetVideoMode=1,
  VideoModeSet=2,
  Draw=4,
  Flipped=8,
  WarpMouse=16,
  WM_SetCaption=32,
  KeyEvent=64,
  MouseButtonEvent=128,
  MouseMoveEvent=256,
  ResizedEvent=512,
  ActiveEvent=1024,
  QuitEvent=2048,
  AddCursor=4096,
  CursorAdded=8192,
  ManageCursor=16384,
} MsgType;
