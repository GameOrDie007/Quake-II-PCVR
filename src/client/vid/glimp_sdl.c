/*
 * Copyright (C) 2010 Yamagi Burmeister
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * This is the client side of the render backend, implemented trough SDL.
 * The SDL window and related functrion (mouse grap, fullscreen switch)
 * are implemented here, everything else is in the renderers.
 *
 * =======================================================================
 */

#include "../../common/header/common.h"
#include "header/ref.h"
#include "../../vr/vr_surface.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>

cvar_t *vid_displayrefreshrate;
int glimp_refreshRate = -1;

/*
 * The desktop mirror's own size.
 *
 * In VR viddef is the eye buffer - far larger than the monitor and nearly
 * square - so the window on the desktop cannot be sized from it, and a resize
 * of the window must not touch it. These carry the window's size, which is all
 * the mirror blit needs.
 */
int vid_mirrorwidth = 0;
int vid_mirrorheight = 0;

/*
 * What the desktop window does while in VR: 0 off, 1 window, 2 borderless full
 * screen. Applied live in VID_ApplyMirrorMode - only the window changes, so
 * unlike the eye buffer settings this needs no restart.
 */
cvar_t *vr_mirror = NULL;
cvar_t *vr_mirror_eye = NULL;
cvar_t *vr_mirror_fit = NULL;

static int last_flags = 0;
static int last_display = 0;
static int last_position_x = SDL_WINDOWPOS_UNDEFINED;
static int last_position_y = SDL_WINDOWPOS_UNDEFINED;
static SDL_Window* window = NULL;
static qboolean initSuccessful = false;

// --------

static qboolean
CreateSDLWindow(int flags, int w, int h)
{
	window = SDL_CreateWindow("Yamagi Quake II",
				  last_position_x, last_position_y,
				  w, h, flags);
	if (window)
	{
		/* save current display as default */
		last_display = SDL_GetWindowDisplayIndex(window);
		SDL_GetWindowPosition(window,
				      &last_position_x, &last_position_y);
	}

	return window != NULL;
}

static int
GetFullscreenType()
{
	if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP)
	{
		return 1;
	}
	else if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)
	{
		return 2;
	}
	else
	{
		return 0;
	}
}

static qboolean
GetWindowSize(int* w, int* h)
{
	if (window == NULL || w == NULL || h == NULL)
	{
		return false;
	}

	SDL_DisplayMode m;

	if (SDL_GetWindowDisplayMode(window, &m) != 0)
	{
		Com_Printf("Can't get Displaymode: %s\n", SDL_GetError());

		return false;
	}

	*w = m.w;
	*h = m.h;

	return true;
}

/*
 * Sets the window icon
 */
static void
SetSDLIcon()
{
	#include "icon/q2icon64.h" // 64x64 32 Bit

	/* these masks are needed to tell SDL_CreateRGBSurface(From)
	   to assume the data it gets is byte-wise RGB(A) data */
	Uint32 rmask, gmask, bmask, amask;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	int shift = (q2icon64.bytes_per_pixel == 3) ? 8 : 0;
	rmask = 0xff000000 >> shift;
	gmask = 0x00ff0000 >> shift;
	bmask = 0x0000ff00 >> shift;
	amask = 0x000000ff >> shift;
#else /* little endian, like x86 */
	rmask = 0x000000ff;
	gmask = 0x0000ff00;
	bmask = 0x00ff0000;
	amask = (q2icon64.bytes_per_pixel == 3) ? 0 : 0xff000000;
#endif

	SDL_Surface* icon = SDL_CreateRGBSurfaceFrom((void*)q2icon64.pixel_data, q2icon64.width,
		q2icon64.height, q2icon64.bytes_per_pixel*8, q2icon64.bytes_per_pixel*q2icon64.width,
		rmask, gmask, bmask, amask);
	SDL_SetWindowIcon(window, icon);
	SDL_FreeSurface(icon);
}

// FIXME: We need a header for this.
// Maybe we could put it in vid.h.
void GLimp_GrabInput(qboolean grab);

/*
 * Shuts the SDL render backend down
 */
static void
ShutdownGraphics(void)
{
	if (window)
	{
		/* save current display as default */
		last_display = SDL_GetWindowDisplayIndex(window);
		SDL_GetWindowPosition(window,
				      &last_position_x, &last_position_y);
		/* cleanly ungrab input (needs window) */
		GLimp_GrabInput(false);
		SDL_DestroyWindow(window);

		window = NULL;
	}

	// make sure that after vid_restart the refreshrate will be queried from SDL2 again.
	glimp_refreshRate = -1;

	initSuccessful = false; // not initialized anymore
}
// --------

/*
 * Initializes the SDL video subsystem. Must
 * be called before anything else.
 */
qboolean
GLimp_Init(void)
{
	vid_displayrefreshrate = Cvar_Get("vid_displayrefreshrate", "-1", CVAR_ARCHIVE);
	vr_mirror = Cvar_Get("vr_mirror", "2", CVAR_ARCHIVE);
	/* Right by default: most people are right-eye dominant, and the mirror is
	   for other people to watch rather than for the player, who is wearing the
	   headset. An eye buffer is nearly square and a monitor is not, so one has
	   to give: crop loses the top and bottom, fit keeps it all and adds bars. */
	vr_mirror_eye = Cvar_Get("vr_mirror_eye", "1", CVAR_ARCHIVE);
	vr_mirror_fit = Cvar_Get("vr_mirror_fit", "1", CVAR_ARCHIVE);

	if (!SDL_WasInit(SDL_INIT_VIDEO))
	{
		if (SDL_Init(SDL_INIT_VIDEO) == -1)
		{
			Com_Printf("Couldn't init SDL video: %s.\n", SDL_GetError());

			return false;
		}

		SDL_version version;

		SDL_GetVersion(&version);
		Com_Printf("SDL version is: %i.%i.%i\n", (int)version.major, (int)version.minor, (int)version.patch);
		Com_Printf("SDL video driver is \"%s\".\n", SDL_GetCurrentVideoDriver());
	}

	return true;
}

/*
 * Shuts the SDL video subsystem down. Must
 * be called after evrything's finished and
 * clean up.
 */
void
GLimp_Shutdown(void)
{
	ShutdownGraphics();

	if (SDL_WasInit(SDL_INIT_EVERYTHING) == SDL_INIT_VIDEO)
	{
		SDL_Quit();
	}
	else
	{
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	}
}

/*
 * (Re)initializes the actual window.
 */
qboolean
GLimp_InitGraphics(int fullscreen, int *pwidth, int *pheight)
{
	int flags;
	int curWidth, curHeight;
	int width = *pwidth;
	int height = *pheight;
	unsigned int fs_flag = 0;

	if (fullscreen == 1)
	{
		fs_flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
	}
	else if (fullscreen == 2)
	{
		fs_flag = SDL_WINDOW_FULLSCREEN;
	}

	/* Only do this if we already have a working window and a fully
	initialized rendering backend GLimp_InitGraphics() is also
	called when recovering if creating GL context fails or the
	one we got is unusable. */
	if (initSuccessful && GetWindowSize(&curWidth, &curHeight)
			&& (curWidth == width) && (curHeight == height))
	{
		/* If we want fullscreen, but aren't */
		if (GetFullscreenType())
		{
			SDL_SetWindowFullscreen(window, fs_flag);
			Cvar_SetValue("vid_fullscreen", fullscreen);
		}

		/* Are we now? */
		if (GetFullscreenType())
		{
			return true;
		}
	}

	/* Is the surface used? */
	if (window)
	{
		re.ShutdownContext();
		ShutdownGraphics();

		window = NULL;
	}

	/* We need the window size for the menu, the HUD, etc. */
	viddef.width = width;
	viddef.height = height;

	if(last_flags != -1 && (last_flags & SDL_WINDOW_OPENGL))
	{
		/* Reset SDL. */
		SDL_GL_ResetAttributes();
	}

	/* Let renderer prepare things (set OpenGL attributes).
	   FIXME: This is no longer necessary, the renderer
	   could and should pass the flags when calling this
	   function. */
	flags = re.PrepareForWindow();

	if (flags == -1)
	{
		/* It's PrepareForWindow() job to log an error */
		return false;
	}

	if (fs_flag)
	{
		flags |= fs_flag;
	}

	/*
	 * In VR the requested size is the per-eye buffer, which is much larger
	 * than the monitor - a Quest 3 through VDXR asks for about 3379x3590. On
	 * Android that was fine, because the "window" was the headset surface and
	 * there was no desktop. Here it would open a window bigger than the screen
	 * and bury everything else on the desktop.
	 *
	 * viddef keeps the eye resolution, because that is what the renderer
	 * projects and lays the HUD out for, and it matches the eye framebuffer
	 * the frame loop renders into. Only the desktop window shrinks; it is just
	 * a mirror. Aspect is preserved so the mirror is not distorted.
	 */
	{
		int vrWidth = 0;
		int vrHeight = 0;

		TBXR_GetEyeResolution(&vrWidth, &vrHeight);

		if (vrWidth > 0 && vrHeight > 0 && width == vrWidth && height == vrHeight)
		{
			const int maxDimension = 900;
			int longest = (width > height) ? width : height;

			if (longest > maxDimension)
			{
				width = (width * maxDimension) / longest;
				height = (height * maxDimension) / longest;
				Com_Printf("VR: mirror window %dx%d (eye buffer stays %dx%d)\n",
						width, height, vrWidth, vrHeight);
			}

			/*
			 * Resizable, so the mirror can be dragged about and maximised.
			 * Only the window ever changes size; the eye buffers never do.
			 */
			flags |= SDL_WINDOW_RESIZABLE;

			vid_mirrorwidth = width;
			vid_mirrorheight = height;
		}
	}

	/* Mkay, now the hard work. Let's create the window. */
	cvar_t *gl_msaa_samples = Cvar_Get("gl_msaa_samples", "0", CVAR_ARCHIVE);

	while (1)
	{
		if (!CreateSDLWindow(flags, width, height))
		{
			if((flags & SDL_WINDOW_OPENGL) && gl_msaa_samples->value)
			{
				Com_Printf("SDL SetVideoMode failed: %s\n", SDL_GetError());
				Com_Printf("Reverting to %s r_mode %i (%ix%i) without MSAA.\n",
					        (flags & fs_flag) ? "fullscreen" : "windowed",
					        (int) Cvar_VariableValue("r_mode"), width, height);

				/* Try to recover */
				Cvar_SetValue("gl_msaa_samples", 0);

				SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
				SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
			}
			else if (width != 640 || height != 480 || (flags & fs_flag))
			{
				Com_Printf("SDL SetVideoMode failed: %s\n", SDL_GetError());
				Com_Printf("Reverting to windowed r_mode 4 (640x480).\n");

				/* Try to recover */
				Cvar_SetValue("r_mode", 4);
				Cvar_SetValue("vid_fullscreen", 0);

				*pwidth = width = 640;
				*pheight = height = 480;
				flags &= ~fs_flag;
			}
			else
			{
				Com_Error(ERR_FATAL, "Failed to revert to r_mode 4. Exiting...\n");
				return false;
			}
		}
		else
		{
			break;
		}
	}

	last_flags = flags;

	if (!re.InitContext(window))
	{
		/* InitContext() should have logged an error. */
		return false;
	}

	/* Set the window icon - For SDL2, this must be done after creating the window */
	SetSDLIcon();

	/* No cursor */
	SDL_ShowCursor(0);

	initSuccessful = true;

	return true;
}

/*
 * Shuts the window down.
 */
void
GLimp_ShutdownGraphics(void)
{
	SDL_GL_ResetAttributes();
	ShutdownGraphics();
}

/*
 * (Un)grab Input
 */
void
GLimp_GrabInput(qboolean grab)
{
	if(window != NULL)
	{
		SDL_SetWindowGrab(window, grab ? SDL_TRUE : SDL_FALSE);
	}

	if(SDL_SetRelativeMouseMode(grab ? SDL_TRUE : SDL_FALSE) < 0)
	{
		Com_Printf("WARNING: Setting Relative Mousemode failed, reason: %s\n", SDL_GetError());
		Com_Printf("         You should probably update to SDL 2.0.3 or newer!\n");
	}
}

/*
 * Returns the current display refresh rate. There're 2 limitations:
 *
 * * The timing code in frame.c only understands full integers, so
 *   values given by vid_displayrefreshrate are always round up. For
 *   example 59.95 become 60. Rounding up is the better choice for
 *   most users because assuming a too high display refresh rate
 *   avoids micro stuttering caused by missed frames if the vsync
 *   is enabled. The price are small and hard to notice timing
 *   problems.
 *
 * * SDL returns only full integer. In most cases they're rounded
 *   up, but in some cases - likely depending on the GPU driver -
 *   they're rounded down. If the value is rounded up, we'll see
 *   some small and nard to notice timing problems. If the value
 *   is rounded down frames will be missed. Both is only relevant
 *   if the vsync is enabled.
 */
int
GLimp_GetRefreshRate(void)
{

	if (vid_displayrefreshrate->value > -1 ||
			vid_displayrefreshrate->modified)
	{
		glimp_refreshRate = ceil(vid_displayrefreshrate->value);
		vid_displayrefreshrate->modified = false;
	}

	if (glimp_refreshRate == -1)
	{
		SDL_DisplayMode mode;

		int i = SDL_GetWindowDisplayIndex(window);

		if (i >= 0 && SDL_GetCurrentDisplayMode(i, &mode) == 0)
		{
			glimp_refreshRate = mode.refresh_rate;
		}

		// Something went wrong, use default.
		if (glimp_refreshRate <= 0)
		{
			glimp_refreshRate = 60;
		}
	}

	return glimp_refreshRate;
}

/*
 * Detect current desktop mode
 */
qboolean
GLimp_GetDesktopMode(int *pwidth, int *pheight)
{
	// Declare display mode structure to be filled in.
	SDL_DisplayMode mode;

	/*
	 * In VR this is the eye buffer size, not the monitor. Team Beef's change
	 * to VID_GetModeInfo makes every video mode resolve to the desktop mode,
	 * and on Android their glimp_android.c answered it from Quest_GetScreenRes
	 * so the engine rendered straight at the headset's per-eye resolution.
	 * Same thing here, from the OpenXR view configuration.
	 *
	 * Zero means OpenXR has not come up - no runtime, no headset, or this is a
	 * flatscreen run - so fall through to the real desktop mode.
	 */
	{
		int vrWidth = 0;
		int vrHeight = 0;

		TBXR_GetEyeResolution(&vrWidth, &vrHeight);

		if (vrWidth > 0 && vrHeight > 0)
		{
			*pwidth = vrWidth;
			*pheight = vrHeight;
			return true;
		}
	}

	if (window)
	{
		/* save current display as default */
		last_display = SDL_GetWindowDisplayIndex(window);
		SDL_GetWindowPosition(window,
				      &last_position_x, &last_position_y);
	}

	if (last_display < 0)
	{
		// In case of error...
		Com_Printf("Can't detect current desktop.\n");
		last_display = 0;
	}

	// We can't get desktop where we start, so use first desktop
	if(SDL_GetDesktopDisplayMode(last_display, &mode) != 0)
	{
		// In case of error...
		Com_Printf("Can't detect default desktop mode: %s\n",
				SDL_GetError());
		return false;
	}
	*pwidth = mode.w;
	*pheight = mode.h;
	return true;
}

/*
 * Put the desktop window into the shape vr_mirror asks for.
 *
 * Called once a frame from the VR loop rather than from the renderer, so no
 * SDL call ever lands in the middle of an eye's render.
 */
void
VID_ApplyMirrorMode(void)
{
	static int applied = -1;
	int want;

	if (window == NULL || vr_mirror == NULL)
	{
		return;
	}

	want = (int)vr_mirror->value;

	if (want < 0)
	{
		want = 0;
	}
	else if (want > 2)
	{
		want = 2;
	}

	if (want == applied)
	{
		return;
	}

	{
		qboolean isfull = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
		qboolean wantfull = (want == 2);

		/*
		 * Only touched when it genuinely has to change.
		 *
		 * SDL_SetWindowFullscreen runs its display mode handling even when the
		 * window is already in the state being asked for, and on Windows that
		 * can restore the desktop mode, which shoves every other window onto
		 * another monitor.
		 *
		 * Borderless (FULLSCREEN_DESKTOP) rather than a real mode change, for
		 * the same reason: the monitor is only showing a mirror of what is in
		 * the headset and has no business rearranging the desktop.
		 */
		if (isfull != wantfull)
		{
			SDL_SetWindowFullscreen(window, wantfull ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
			SDL_GetWindowSize(window, &vid_mirrorwidth, &vid_mirrorheight);
		}
	}

	applied = want;
}

/*
 * The window's size, for the mirror blit. Not viddef, which is the eye buffer.
 */
void
VID_GetMirrorSize(int *width, int *height)
{
	*width = vid_mirrorwidth;
	*height = vid_mirrorheight;
}

/*
 * A resize moves the mirror only.
 */
void
VID_SetMirrorSize(int width, int height)
{
	vid_mirrorwidth = width;
	vid_mirrorheight = height;
}

/*
 * Present the window. In VR the renderer's own swap is skipped, because it runs
 * once per eye and would present a buffer the mirror had not been drawn into
 * yet; the mirror blit is followed by exactly one swap per frame.
 */
void
VID_PresentMirror(void)
{
	if (window != NULL)
	{
		SDL_GL_SwapWindow(window);
	}
}
