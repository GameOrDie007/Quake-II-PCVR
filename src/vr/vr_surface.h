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

/* True while an in-game menu is being shown without dropping out of VR: the
 * world keeps its projection layer, the head keeps moving the view, and the
 * menu draws as a stereo overlay instead of the whole scene collapsing onto a
 * flat quad. Off unless vr_menu_in_world says otherwise, which keeps Team
 * Beef's behaviour the default. Several places have to agree about this, so
 * they all ask here rather than each testing key_dest for themselves. */
qboolean VR_MenuInWorld(void);
/* Takes the current head height as the standing height, so a seated player
   is not permanently crouched. PC Options, and the vr_recenter command. */
void VR_RecenterHeight(void);

/* The shared half of that test - the feature is on, a session is running, there
 * is a world at ca_active, and no cinematic is playing - without asking what
 * key_dest is. The attract demo qualifies: it is a real map being rendered, so
 * it is also what says the demo may keep the projection layer and take its
 * orientation from the head. */
qboolean VR_InWorldEligible(void);

/* The yaw the attract demo's view is turned by, on top of the head's own. It is
 * anchored once per demo so the player starts facing where the demo faces, and
 * the turn stick moves it from there. Zero outside the demo. */
float VR_DemoYaw(void);

/* True while the menu is being drawn onto a composition layer of its own rather
 * than into the eye buffers - vr_menu_in_world 2. That makes it stay where it
 * was put instead of riding the head, and makes it monoscopic: the compositor
 * gives the quad its stereo, so nothing drawn onto it takes a per-eye offset.
 * Latched once a frame, so the client and the VR side cannot disagree about it
 * halfway through one. False unless a menu framebuffer was actually created. */
qboolean VR_MenuOwnLayer(void);

/* Everything that belongs on that layer, drawn once into a transparent target.
 * Called from the VR frame loop, after both eyes and outside the engine's
 * frame. */
void SCR_DrawMenuLayer(void);
void TBXR_ShutdownOpenXR(void);

/*
 * Turning. One cvar, vr_snapturn_angle, carries two different quantities, and
 * its own magnitude says which: above VR_TURN_SNAP_THRESHOLD it is a snap in
 * degrees, at or below it is the divisor of a continuous turn, where a *lower*
 * number turns faster. That is Team Beef's design and the turning code in
 * VrInputDefault.c reads it directly, so the threshold here has to keep
 * agreeing with the two `> 10.0f` tests in that file.
 *
 * vr_smoothturn is ours and is only the Options page's display flag - it picks
 * which control that page shows. It cannot decide how the engine turns, so the
 * page has to keep vr_snapturn_angle inside the range of whichever mode it is
 * showing, or it will show a Turn Speed slider over a value that is still a
 * snap angle.
 *
 * The smooth default is a third of the way along the 1..10 slider: slider 4,
 * so 11 - 4.
 */
#define VR_TURN_SNAP_THRESHOLD      10.0f
#define VR_TURN_SNAP_DEFAULT        45.0f
#define VR_TURN_SNAP_DEFAULT_STR    "45"
#define VR_TURN_SMOOTH_DEFAULT      7.0f
#define VR_TURN_SMOOTH_DEFAULT_STR  "7"

#endif /* VR_SURFACE_H */
