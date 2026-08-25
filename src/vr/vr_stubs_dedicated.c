/*
 * Copyright (C) 2026 Quake2VR PCVR port
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * =======================================================================
 *
 * VR stubs for the dedicated server.
 *
 * Team Beef's changes call into the VR layer from two files the dedicated
 * server also compiles: frame.c starts VR during common init, and sv_game.c
 * puts getVROrigins, getFOV and Android_Vibrate into the game_import_t table
 * so the game DLL can reach them. Android never built a dedicated server, so
 * this never came up for them.
 *
 * q2ded has no headset, no window and no GL context, so the honest
 * implementation is to do nothing. The game DLL still finds the entry points
 * it expects in the import table; they report a stationary player at the
 * origin and a conventional field of view, which is what a headless server
 * should see. Linking the real vr_surface.c here instead would drag OpenXR and
 * a GL context into a build that has neither.
 *
 * =======================================================================
 */

#include "../common/header/shared.h"

void
VR_Init(void)
{
}

void
Android_Vibrate(float duration, int channel, float intensity)
{
	(void)duration;
	(void)channel;
	(void)intensity;
}

float
getFOV(void)
{
	return 90.0f;
}

void
getVROrigins(vec3_t _weaponoffset, vec3_t _weaponangles, vec3_t _hmdPosition)
{
	VectorClear(_weaponoffset);
	VectorClear(_weaponangles);
	VectorClear(_hmdPosition);
}
