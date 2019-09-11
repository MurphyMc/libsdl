/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2012 Sam Lantinga
    Copyright (C) 2019      Murphy McCauley

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

/* Being a null driver, there's no event stream. We just define stubs for
   most of the API. */

#include "SDL.h"
#include "../../events/SDL_sysevents.h"
#include "../../events/SDL_events_c.h"

#include "SDL_sdluxervideo.h"
#include "SDL_sdluxerevents_c.h"
#include "SDL_keysym.h"

#include <rfb/keysym.h>


/* The translation tables from an X11 keysym to a SDL keysym */
static SDLKey ODD_keymap[256];
static SDLKey MISC_keymap[256];
static SDLKey SDLuxer_TranslateKeycode(rfbKeySym kc);


void SDLuxer_on_key (rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
  struct SDL_PrivateVideoData * self = ((SDL_VideoDevice*)cl->screen->screenData)->hidden;
  SDL_keysym keysym = {0};
  keysym.scancode = key;
  keysym.sym = SDLuxer_TranslateKeycode(key);
  keysym.mod = KMOD_NONE; //TODO: Track state for this?
  keysym.unicode = 0;
  if ( SDL_TranslateUNICODE )
  {
    keysym.unicode = keysym.sym & 0xff; //FIXME: Very wrong
  }
  SDL_PrivateKeyboard(down ? SDL_PRESSED : SDL_RELEASED, &keysym);
}


void SDLuxer_InitOSKeymap(_THIS)
{
  int i;

  /* Odd keys used in international keyboards */
  for ( i=0; i<SDL_arraysize(ODD_keymap); ++i )
    ODD_keymap[i] = SDLK_UNKNOWN;

   /* Some of these might be mappable to an existing SDLK_ code */
   ODD_keymap[XK_dead_grave&0xFF] = SDLK_COMPOSE;
   ODD_keymap[XK_dead_acute&0xFF] = SDLK_COMPOSE;
   ODD_keymap[XK_dead_tilde&0xFF] = SDLK_COMPOSE;
   ODD_keymap[XK_dead_macron&0xFF] = SDLK_COMPOSE;
   ODD_keymap[XK_dead_breve&0xFF] = SDLK_COMPOSE;
   ODD_keymap[XK_dead_abovedot&0xFF] = SDLK_COMPOSE;
   ODD_keymap[XK_dead_diaeresis&0xFF] = SDLK_COMPOSE;
   ODD_keymap[XK_dead_abovering&0xFF] = SDLK_COMPOSE;
   ODD_keymap[XK_dead_doubleacute&0xFF] = SDLK_COMPOSE;
   ODD_keymap[XK_dead_caron&0xFF] = SDLK_COMPOSE;
   ODD_keymap[XK_dead_cedilla&0xFF] = SDLK_COMPOSE;
   ODD_keymap[XK_dead_ogonek&0xFF] = SDLK_COMPOSE;
   ODD_keymap[XK_dead_iota&0xFF] = SDLK_COMPOSE;
   ODD_keymap[XK_dead_voiced_sound&0xFF] = SDLK_COMPOSE;
   ODD_keymap[XK_dead_semivoiced_sound&0xFF] = SDLK_COMPOSE;
   ODD_keymap[XK_dead_belowdot&0xFF] = SDLK_COMPOSE;
#ifdef XK_dead_hook
   ODD_keymap[XK_dead_hook&0xFF] = SDLK_COMPOSE;
#endif
#ifdef XK_dead_horn
   ODD_keymap[XK_dead_horn&0xFF] = SDLK_COMPOSE;
#endif

#ifdef XK_dead_circumflex
  /* These X keysyms have 0xFE as the high byte */
  ODD_keymap[XK_dead_circumflex&0xFF] = SDLK_CARET;
#endif
#ifdef XK_ISO_Level3_Shift
  ODD_keymap[XK_ISO_Level3_Shift&0xFF] = SDLK_MODE; /* "Alt Gr" key */
#endif

  /* Map the miscellaneous keys */
  for ( i=0; i<SDL_arraysize(MISC_keymap); ++i )
    MISC_keymap[i] = SDLK_UNKNOWN;

  /* These X keysyms have 0xFF as the high byte */
  MISC_keymap[XK_BackSpace&0xFF] = SDLK_BACKSPACE;
  MISC_keymap[XK_Tab&0xFF] = SDLK_TAB;
  MISC_keymap[XK_Clear&0xFF] = SDLK_CLEAR;
  MISC_keymap[XK_Return&0xFF] = SDLK_RETURN;
  MISC_keymap[XK_Pause&0xFF] = SDLK_PAUSE;
  MISC_keymap[XK_Escape&0xFF] = SDLK_ESCAPE;
  MISC_keymap[XK_Delete&0xFF] = SDLK_DELETE;

  MISC_keymap[XK_KP_0&0xFF] = SDLK_KP0;    /* Keypad 0-9 */
  MISC_keymap[XK_KP_1&0xFF] = SDLK_KP1;
  MISC_keymap[XK_KP_2&0xFF] = SDLK_KP2;
  MISC_keymap[XK_KP_3&0xFF] = SDLK_KP3;
  MISC_keymap[XK_KP_4&0xFF] = SDLK_KP4;
  MISC_keymap[XK_KP_5&0xFF] = SDLK_KP5;
  MISC_keymap[XK_KP_6&0xFF] = SDLK_KP6;
  MISC_keymap[XK_KP_7&0xFF] = SDLK_KP7;
  MISC_keymap[XK_KP_8&0xFF] = SDLK_KP8;
  MISC_keymap[XK_KP_9&0xFF] = SDLK_KP9;
  MISC_keymap[XK_KP_Insert&0xFF] = SDLK_KP0;
  MISC_keymap[XK_KP_End&0xFF] = SDLK_KP1;
  MISC_keymap[XK_KP_Down&0xFF] = SDLK_KP2;
  MISC_keymap[XK_KP_Page_Down&0xFF] = SDLK_KP3;
  MISC_keymap[XK_KP_Left&0xFF] = SDLK_KP4;
  MISC_keymap[XK_KP_Begin&0xFF] = SDLK_KP5;
  MISC_keymap[XK_KP_Right&0xFF] = SDLK_KP6;
  MISC_keymap[XK_KP_Home&0xFF] = SDLK_KP7;
  MISC_keymap[XK_KP_Up&0xFF] = SDLK_KP8;
  MISC_keymap[XK_KP_Page_Up&0xFF] = SDLK_KP9;
  MISC_keymap[XK_KP_Delete&0xFF] = SDLK_KP_PERIOD;
  MISC_keymap[XK_KP_Decimal&0xFF] = SDLK_KP_PERIOD;
  MISC_keymap[XK_KP_Divide&0xFF] = SDLK_KP_DIVIDE;
  MISC_keymap[XK_KP_Multiply&0xFF] = SDLK_KP_MULTIPLY;
  MISC_keymap[XK_KP_Subtract&0xFF] = SDLK_KP_MINUS;
  MISC_keymap[XK_KP_Add&0xFF] = SDLK_KP_PLUS;
  MISC_keymap[XK_KP_Enter&0xFF] = SDLK_KP_ENTER;
  MISC_keymap[XK_KP_Equal&0xFF] = SDLK_KP_EQUALS;

  MISC_keymap[XK_Up&0xFF] = SDLK_UP;
  MISC_keymap[XK_Down&0xFF] = SDLK_DOWN;
  MISC_keymap[XK_Right&0xFF] = SDLK_RIGHT;
  MISC_keymap[XK_Left&0xFF] = SDLK_LEFT;
  MISC_keymap[XK_Insert&0xFF] = SDLK_INSERT;
  MISC_keymap[XK_Home&0xFF] = SDLK_HOME;
  MISC_keymap[XK_End&0xFF] = SDLK_END;
  MISC_keymap[XK_Page_Up&0xFF] = SDLK_PAGEUP;
  MISC_keymap[XK_Page_Down&0xFF] = SDLK_PAGEDOWN;

  MISC_keymap[XK_F1&0xFF] = SDLK_F1;
  MISC_keymap[XK_F2&0xFF] = SDLK_F2;
  MISC_keymap[XK_F3&0xFF] = SDLK_F3;
  MISC_keymap[XK_F4&0xFF] = SDLK_F4;
  MISC_keymap[XK_F5&0xFF] = SDLK_F5;
  MISC_keymap[XK_F6&0xFF] = SDLK_F6;
  MISC_keymap[XK_F7&0xFF] = SDLK_F7;
  MISC_keymap[XK_F8&0xFF] = SDLK_F8;
  MISC_keymap[XK_F9&0xFF] = SDLK_F9;
  MISC_keymap[XK_F10&0xFF] = SDLK_F10;
  MISC_keymap[XK_F11&0xFF] = SDLK_F11;
  MISC_keymap[XK_F12&0xFF] = SDLK_F12;
  MISC_keymap[XK_F13&0xFF] = SDLK_F13;
  MISC_keymap[XK_F14&0xFF] = SDLK_F14;
  MISC_keymap[XK_F15&0xFF] = SDLK_F15;

  MISC_keymap[XK_Num_Lock&0xFF] = SDLK_NUMLOCK;
  MISC_keymap[XK_Caps_Lock&0xFF] = SDLK_CAPSLOCK;
  MISC_keymap[XK_Scroll_Lock&0xFF] = SDLK_SCROLLOCK;
  MISC_keymap[XK_Shift_R&0xFF] = SDLK_RSHIFT;
  MISC_keymap[XK_Shift_L&0xFF] = SDLK_LSHIFT;
  MISC_keymap[XK_Control_R&0xFF] = SDLK_RCTRL;
  MISC_keymap[XK_Control_L&0xFF] = SDLK_LCTRL;
  MISC_keymap[XK_Alt_R&0xFF] = SDLK_RALT;
  MISC_keymap[XK_Alt_L&0xFF] = SDLK_LALT;
  MISC_keymap[XK_Meta_R&0xFF] = SDLK_RMETA;
  MISC_keymap[XK_Meta_L&0xFF] = SDLK_LMETA;
  MISC_keymap[XK_Super_L&0xFF] = SDLK_LSUPER; /* Left "Windows" */
  MISC_keymap[XK_Super_R&0xFF] = SDLK_RSUPER; /* Right "Windows */
  MISC_keymap[XK_Mode_switch&0xFF] = SDLK_MODE; /* "Alt Gr" key */
  MISC_keymap[XK_Multi_key&0xFF] = SDLK_COMPOSE; /* Multi-key compose */

  MISC_keymap[XK_Help&0xFF] = SDLK_HELP;
  MISC_keymap[XK_Print&0xFF] = SDLK_PRINT;
  MISC_keymap[XK_Sys_Req&0xFF] = SDLK_SYSREQ;
  MISC_keymap[XK_Break&0xFF] = SDLK_BREAK;
  MISC_keymap[XK_Menu&0xFF] = SDLK_MENU;
  MISC_keymap[XK_Hyper_R&0xFF] = SDLK_MENU;   /* Windows "Menu" key */
}

/* Get the translated SDL virtual keysym */
static SDLKey SDLuxer_TranslateKeycode(rfbKeySym xsym)
{
  SDLKey key = SDLK_UNKNOWN;

#ifdef DEBUG_KEYS
  fprintf(stderr, "Translating key code %d -> 0x%.4x\n", kc, xsym);
#endif
  switch (xsym>>8) {
    case 0x1005FF:
#ifdef SunXK_F36
      if ( xsym == SunXK_F36 )
        key = SDLK_F11;
#endif
#ifdef SunXK_F37
      if ( xsym == SunXK_F37 )
        key = SDLK_F12;
#endif
      break;
    case 0x00:  /* Latin 1 */
      key = (SDLKey)(xsym & 0xFF);
      break;
    case 0x01:  /* Latin 2 */
    case 0x02:  /* Latin 3 */
    case 0x03:  /* Latin 4 */
    case 0x04:  /* Katakana */
    case 0x05:  /* Arabic */
    case 0x06:  /* Cyrillic */
    case 0x07:  /* Greek */
    case 0x08:  /* Technical */
    case 0x0A:  /* Publishing */
    case 0x0C:  /* Hebrew */
    case 0x0D:  /* Thai */
      /* These are wrong, but it's better than nothing */
      key = (SDLKey)(xsym & 0xFF);
      break;
    case 0xFE:
      key = ODD_keymap[xsym&0xFF];
      break;
    case 0xFF:
      key = MISC_keymap[xsym&0xFF];
      break;
    default:
      /*
      fprintf(stderr, "X11: Unhandled xsym, sym = 0x%04x\n",
          (unsigned int)xsym);
      */
      break;
  }
  return key;
}
