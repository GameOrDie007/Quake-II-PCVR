/*
 * Copyright (C) 2026 Quake2VR PCVR port
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * =======================================================================
 *
 * PC entry points into the OpenXR layer. See src/vr/vr_surface.c.
 *
 * =======================================================================
 */

#ifndef VR_SURFACE_H
#define VR_SURFACE_H

#include "../common/header/shared.h"

/* Phase one, before Qcommon_Init: instance and view configuration. Needs no
   GL context. Returns false when there is no runtime or no headset, which is
   not an error - the engine then runs flatscreen. */
qboolean TBXR_InitialiseInstance(void);

/* Phase two, after Qcommon_Init: session, swapchains and actions. Requires a
   current GL context. */
qboolean TBXR_InitialiseSession(void);

/* Eye buffer size from the view configuration. Zero until phase one has run. */
void TBXR_GetEyeResolution(int *width, int *height);

/* One VR frame. Drives Qcommon_BeginFrame, Qcommon_Frame per eye and
   Qcommon_EndFrame inside a single xrBeginFrame/xrEndFrame pair. */
void TBXR_FrameSetup(void);

qboolean TBXR_IsRunning(void);
void TBXR_ShutdownOpenXR(void);

#endif /* VR_SURFACE_H */
