/*
 * Copyright (C) 2023 Simon Brown (Team Beef)
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
 * PC side of Team Beef's VR platform layer.
 *
 * On Quest their platform code lives in Q2VR_SurfaceView.c, which owns the
 * Android activity, the EGL context and the OpenXR session. None of that
 * transfers, so this file provides the same globals for the PC build and the
 * OpenXR bring-up sets them.
 *
 * =======================================================================
 */

#include <string.h>

#include "teambeef/VrCommon.h"

/*
 * Which headset we are running on. The renderer reads this in R_InitImages to
 * pick a texture intensity - 3.7 on Meta hardware against 2.5 elsewhere - so
 * getting it wrong shows up as the whole game being noticeably too dark or too
 * bright, not as a crash.
 *
 * Their default is Meta and so is ours: Quest over Virtual Desktop is the
 * target, and VDXR reports as neither Pico nor anything else special.
 */
int hmdType = XR_DEVICE_TYPE_META;

/*
 * Classify the headset from the OpenXR runtime name, matching
 * Q2VR_SurfaceView.c: anything reporting Pico is a Pico, everything else is
 * treated as Meta. Called once the instance exists and its properties can be
 * queried.
 *
 * On PC the runtime name describes the runtime rather than the headset -
 * "VirtualDesktopXR", "SteamVR/OpenXR", "Oculus" - so this is a coarser signal
 * here than it is on Android. It lands on Meta for every runtime a Quest 3
 * realistically connects through, which is the value their tuning assumes.
 */
void
VR_SetHMDTypeFromRuntimeName(const char *runtimeName)
{
	if (runtimeName == NULL)
	{
		return;
	}

	if (strstr(runtimeName, "Pico") || strstr(runtimeName, "PICO"))
	{
		hmdType = XR_DEVICE_TYPE_PICO;
	}
	else
	{
		hmdType = XR_DEVICE_TYPE_META;
	}
}
