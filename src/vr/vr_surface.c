/************************************************************************************

OpenXR entry point for the PCVR port of Quake2Quest.

This is the PC counterpart of Team Beef's Q2VR_SurfaceView.c. Their file is the
Android entry point: it owns the JNI surface, the EGL context, the app thread
and the OpenXR session, and it drives the engine rather than being called by it.

The OpenXR half of that file is almost entirely portable and is reproduced here
as closely as it can be - same action set, same bindings, same frame structure,
same pose conventions, same cvar defaults. What changes is only the platform
seam:

  - EGL and the Android app thread become the SDL window and GL context that
    7.41 already creates, bound through XrGraphicsBindingOpenGLWin32KHR.
  - XR_KHR_opengl_es_enable becomes XR_KHR_opengl_enable, and swapchain images
    become XrSwapchainImageOpenGLKHR.
  - Their multisampled framebuffer uses GL_EXT_multisampled_render_to_texture,
    a GLES extension with no desktop equivalent, so the same sample count is
    reached here with an explicit multisample framebuffer and a resolve blit.
  - JNI lifecycle, the Android loader init and their command-line parsing are
    dropped; the Windows backend already provides all three.

Everything else - the maths, the ordering, the tuned values - is theirs.

Copyright (C) 2023 Simon Brown (Team Beef)
Copyright (C) 2026 Quake2VR PCVR port

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

*************************************************************************************/

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <windows.h>
#include <GL/gl.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

/*
 * These two have to be defined before openxr_platform.h to get
 * XrGraphicsBindingOpenGLWin32KHR and XrSwapchainImageOpenGLKHR. Theirs
 * defines the Android and GLES equivalents instead.
 */
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "teambeef/VrInput.h"
#include "teambeef/VrCvars.h"
#include "teambeef/VrCommon.h"

#include "../client/header/client.h"

#define Q2XR_CHECK_XR(call) q2xr_CheckXr((call), #call, __LINE__)
#define Q2XR_SWAPCHAIN_TIMEOUT 1000000000LL

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Desktop GL constants and entry points this file needs. The Windows
 * opengl32 import library stops at GL 1.1, so anything newer has to come
 * through wglGetProcAddress.
 */
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER				0x8D40
#define GL_READ_FRAMEBUFFER			0x8CA8
#define GL_DRAW_FRAMEBUFFER			0x8CA9
#define GL_RENDERBUFFER				0x8D41
#define GL_COLOR_ATTACHMENT0			0x8CE0
#define GL_DEPTH_ATTACHMENT			0x8D00
#define GL_FRAMEBUFFER_COMPLETE			0x8CD5
#define GL_DEPTH_COMPONENT24			0x81A6
#endif

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8				0x8C43
#endif

#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB			0x8DB9
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE			0x812F
#endif

typedef void (APIENTRY *PFN_glGenFramebuffers)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_glDeleteFramebuffers)(GLsizei, const GLuint *);
typedef void (APIENTRY *PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (APIENTRY *PFN_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef GLenum (APIENTRY *PFN_glCheckFramebufferStatus)(GLenum);
typedef void (APIENTRY *PFN_glGenRenderbuffers)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_glDeleteRenderbuffers)(GLsizei, const GLuint *);
typedef void (APIENTRY *PFN_glBindRenderbuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void (APIENTRY *PFN_glRenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
typedef void (APIENTRY *PFN_glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);

static struct
{
	PFN_glGenFramebuffers GenFramebuffers;
	PFN_glDeleteFramebuffers DeleteFramebuffers;
	PFN_glBindFramebuffer BindFramebuffer;
	PFN_glFramebufferTexture2D FramebufferTexture2D;
	PFN_glFramebufferRenderbuffer FramebufferRenderbuffer;
	PFN_glCheckFramebufferStatus CheckFramebufferStatus;
	PFN_glGenRenderbuffers GenRenderbuffers;
	PFN_glDeleteRenderbuffers DeleteRenderbuffers;
	PFN_glBindRenderbuffer BindRenderbuffer;
	PFN_glRenderbufferStorage RenderbufferStorage;
	PFN_glRenderbufferStorageMultisample RenderbufferStorageMultisample;
	PFN_glBlitFramebuffer BlitFramebuffer;
	qboolean Loaded;
} gl;

/*
 * Their tuning knobs, unchanged. SS_MULTIPLIER scales the runtime's
 * recommended eye resolution.
 */
int CPU_LEVEL = 4;
int GPU_LEVEL = 4;
int NUM_MULTI_SAMPLES = 2;
float SS_MULTIPLIER = 1.1f;
vec2_t cylinderSize = {1280, 720};

extern cvar_t *r_lefthand;
extern cvar_t *cl_paused;

cvar_t *vr_snapturn_angle;
cvar_t *vr_smoothturn;
cvar_t *vr_walkdirection;
cvar_t *vr_weapon_pitchadjust;
cvar_t *vr_lasersight;
cvar_t *vr_control_scheme;
cvar_t *vr_height_adjust;
cvar_t *vr_worldscale;
cvar_t *vr_weaponscale;
cvar_t *vr_weapon_stabilised;
cvar_t *vr_comfort_mask;
cvar_t *vr_turn_deadzone;
cvar_t *vr_framerate;
cvar_t *vr_use_wheels;
cvar_t *vr_jump_sound;
char **refresh_names;
float *refresh_values;

enum control_scheme
{
	RIGHT_HANDED_DEFAULT = 0,
	LEFT_HANDED_DEFAULT = 10,
	LEFT_HANDED_SWITCH_STICKS = 11,
	GAMEPAD = 20
};

typedef struct
{
	int Width;
	int Height;
	uint32_t Length;
	uint32_t Index;
	XrSwapchain Handle;
	XrSwapchainImageOpenGLKHR *Images;
	GLuint *FrameBuffers;		/* one per swapchain image, resolve target */
	GLuint MsaaColour;		/* multisample colour renderbuffer */
	GLuint MsaaDepth;		/* multisample depth renderbuffer */
	GLuint MsaaFrameBuffer;		/* what the engine actually renders into */
	int Samples;
} q2xrFramebuffer;

typedef struct
{
	XrInstance Instance;
	XrSession Session;
	XrSystemId SystemId;
	XrSpace LocalSpace;
	XrSpace StageSpace;
	XrSpace ViewSpace;
	XrViewConfigurationView ViewConfig[NUM_EYES];
	XrView Views[NUM_EYES];
	XrFrameState FrameState;
	q2xrFramebuffer Eye[NUM_EYES];
	qboolean SessionRunning;
	qboolean Focused;
	int Width;
	int Height;
	int RefreshRate;
} q2xrApp;

static q2xrApp gApp;
static int oldtime = 0;
static int q2xrFrameLogCount = 0;
static XrPosef q2xrHeadPoseStage;
static qboolean q2xrWasUsingScreenLayer = false;
static XrPosef q2xrScreenLayerPose;
static qboolean q2xrInitialised = false;
static qboolean q2xrInstanceReady = false;

static XrActionSet actionSet = XR_NULL_HANDLE;
static XrAction gripPoseAction = XR_NULL_HANDLE;
static XrAction aimPoseAction = XR_NULL_HANDLE;
static XrAction hapticAction = XR_NULL_HANDLE;
static XrAction triggerAction = XR_NULL_HANDLE;
static XrAction squeezeAction = XR_NULL_HANDLE;
static XrAction thumbstickAction = XR_NULL_HANDLE;
static XrAction thumbstickClickAction = XR_NULL_HANDLE;
static XrAction thumbstickTouchAction = XR_NULL_HANDLE;
static XrAction aAction = XR_NULL_HANDLE;
static XrAction bAction = XR_NULL_HANDLE;
static XrAction xAction = XR_NULL_HANDLE;
static XrAction yAction = XR_NULL_HANDLE;
static XrAction menuAction = XR_NULL_HANDLE;
static XrSpace gripSpace[NUM_EYES] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
static XrSpace aimSpace[NUM_EYES] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
static XrPath handPath[NUM_EYES] = {XR_NULL_PATH, XR_NULL_PATH};

void setWorldPosition(float x, float y, float z);
void setHMDPosition(float x, float y, float z, float yaw);
void VR_SetHMDTypeFromRuntimeName(const char *runtimeName);

/* ------------------------------------------------------------------------- */
/* Helpers                                                                     */
/* ------------------------------------------------------------------------- */

float
radians(float deg)
{
	return (deg * M_PI) / 180.0f;
}

float
degrees(float rad)
{
	return (rad * 180.0f) / M_PI;
}

double
GetTimeInMilliSeconds(void)
{
	return (double)Sys_Milliseconds();
}

static void
q2xr_CheckXr(XrResult result, const char *call, int line)
{
	if (XR_FAILED(result))
	{
		char buffer[XR_MAX_RESULT_STRING_SIZE] = {0};

		if (gApp.Instance != XR_NULL_HANDLE)
		{
			xrResultToString(gApp.Instance, result, buffer);
		}

		Com_Printf("OpenXR error at line %d: %s -> %s (%d)\n", line, call, buffer, result);
	}
}

static XrPosef
q2xr_IdentityPose(void)
{
	XrPosef pose;

	memset(&pose, 0, sizeof(pose));
	pose.orientation.w = 1.0f;
	return pose;
}

static XrQuaternionf
q2xr_QuatFromYaw(float yawDegrees)
{
	const float halfYaw = radians(yawDegrees) * 0.5f;
	XrQuaternionf quat = {0.0f, sinf(halfYaw), 0.0f, cosf(halfYaw)};

	return quat;
}

static XrVector3f
q2xr_QuatRotateVector(XrQuaternionf q, XrVector3f v)
{
	XrVector3f u = {q.x, q.y, q.z};
	XrVector3f uv = {
		u.y * v.z - u.z * v.y,
		u.z * v.x - u.x * v.z,
		u.x * v.y - u.y * v.x
	};
	XrVector3f uuv = {
		u.y * uv.z - u.z * uv.y,
		u.z * uv.x - u.x * uv.z,
		u.x * uv.y - u.y * uv.x
	};
	XrVector3f out;

	out.x = v.x + ((uv.x * q.w) + uuv.x) * 2.0f;
	out.y = v.y + ((uv.y * q.w) + uuv.y) * 2.0f;
	out.z = v.z + ((uv.z * q.w) + uuv.z) * 2.0f;
	return out;
}

/*
 * Their q2xr_LoadRawGles pulled two GLES extension entry points out of
 * libGLESv2.so with dlsym. The desktop equivalents are core GL 3.0 but absent
 * from the Windows opengl32 import library, so they come from wglGetProcAddress
 * instead. Requires a current GL context.
 */
static qboolean
q2xr_LoadGL(void)
{
	if (gl.Loaded)
	{
		return true;
	}

	gl.GenFramebuffers = (PFN_glGenFramebuffers)wglGetProcAddress("glGenFramebuffers");
	gl.DeleteFramebuffers = (PFN_glDeleteFramebuffers)wglGetProcAddress("glDeleteFramebuffers");
	gl.BindFramebuffer = (PFN_glBindFramebuffer)wglGetProcAddress("glBindFramebuffer");
	gl.FramebufferTexture2D = (PFN_glFramebufferTexture2D)wglGetProcAddress("glFramebufferTexture2D");
	gl.FramebufferRenderbuffer = (PFN_glFramebufferRenderbuffer)wglGetProcAddress("glFramebufferRenderbuffer");
	gl.CheckFramebufferStatus = (PFN_glCheckFramebufferStatus)wglGetProcAddress("glCheckFramebufferStatus");
	gl.GenRenderbuffers = (PFN_glGenRenderbuffers)wglGetProcAddress("glGenRenderbuffers");
	gl.DeleteRenderbuffers = (PFN_glDeleteRenderbuffers)wglGetProcAddress("glDeleteRenderbuffers");
	gl.BindRenderbuffer = (PFN_glBindRenderbuffer)wglGetProcAddress("glBindRenderbuffer");
	gl.RenderbufferStorage = (PFN_glRenderbufferStorage)wglGetProcAddress("glRenderbufferStorage");
	gl.RenderbufferStorageMultisample = (PFN_glRenderbufferStorageMultisample)wglGetProcAddress("glRenderbufferStorageMultisample");
	gl.BlitFramebuffer = (PFN_glBlitFramebuffer)wglGetProcAddress("glBlitFramebuffer");

	if (!gl.GenFramebuffers || !gl.BindFramebuffer || !gl.FramebufferTexture2D ||
		!gl.CheckFramebufferStatus || !gl.GenRenderbuffers || !gl.BindRenderbuffer ||
		!gl.RenderbufferStorage || !gl.FramebufferRenderbuffer || !gl.BlitFramebuffer)
	{
		Com_Printf("VR: required OpenGL framebuffer entry points are missing\n");
		return false;
	}

	gl.Loaded = true;
	return true;
}

/* ------------------------------------------------------------------------- */
/* Eye framebuffers                                                            */
/* ------------------------------------------------------------------------- */

/*
 * Their version attaches the swapchain texture through
 * GL_EXT_multisampled_render_to_texture, which resolves multisampling
 * implicitly and costs almost nothing on a tile-based mobile GPU. Desktop GL
 * has no such extension, so the same NUM_MULTI_SAMPLES is reached explicitly:
 * render into a multisample framebuffer, then blit it into the swapchain image
 * before the eye is released. Sample count and result match; only the
 * mechanism differs.
 */
static qboolean
q2xrFramebuffer_Create(q2xrFramebuffer *fb, int width, int height)
{
	uint32_t i;
	XrSwapchainCreateInfo sci;
	XrResult result;

	memset(fb, 0, sizeof(*fb));

	if (!q2xr_LoadGL())
	{
		return false;
	}

	fb->Width = width;
	fb->Height = height;
	fb->Samples = (NUM_MULTI_SAMPLES > 1) ? NUM_MULTI_SAMPLES : 0;

	memset(&sci, 0, sizeof(sci));
	sci.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
	sci.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	sci.format = GL_SRGB8_ALPHA8;
	sci.sampleCount = 1;
	sci.width = width;
	sci.height = height;
	sci.faceCount = 1;
	sci.arraySize = 1;
	sci.mipCount = 1;

	Com_Printf("VR: creating swapchain %dx%d\n", width, height);
	result = xrCreateSwapchain(gApp.Session, &sci, &fb->Handle);

	if (XR_FAILED(result))
	{
		q2xr_CheckXr(result, "xrCreateSwapchain", __LINE__);
		return false;
	}

	result = xrEnumerateSwapchainImages(fb->Handle, 0, &fb->Length, NULL);

	if (XR_FAILED(result) || fb->Length == 0)
	{
		q2xr_CheckXr(result, "xrEnumerateSwapchainImages", __LINE__);
		return false;
	}

	Com_Printf("VR: swapchain image count %u\n", fb->Length);
	fb->Images = calloc(fb->Length, sizeof(*fb->Images));
	fb->FrameBuffers = calloc(fb->Length, sizeof(*fb->FrameBuffers));

	if (!fb->Images || !fb->FrameBuffers)
	{
		return false;
	}

	for (i = 0; i < fb->Length; ++i)
	{
		fb->Images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
	}

	Q2XR_CHECK_XR(xrEnumerateSwapchainImages(
			fb->Handle, fb->Length, &fb->Length,
			(XrSwapchainImageBaseHeader *)fb->Images));

	/* One resolve framebuffer per swapchain image. */
	for (i = 0; i < fb->Length; ++i)
	{
		GLuint colour = fb->Images[i].image;
		GLenum status;

		glBindTexture(GL_TEXTURE_2D, colour);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glBindTexture(GL_TEXTURE_2D, 0);

		gl.GenFramebuffers(1, &fb->FrameBuffers[i]);
		gl.BindFramebuffer(GL_FRAMEBUFFER, fb->FrameBuffers[i]);
		gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0);
		status = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
		gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			Com_Printf("VR: incomplete eye framebuffer: 0x%x\n", status);
			return false;
		}
	}

	/*
	 * The target the engine renders into. One per eye is enough, because it is
	 * resolved into the swapchain image before the eye ends.
	 */
	gl.GenRenderbuffers(1, &fb->MsaaColour);
	gl.BindRenderbuffer(GL_RENDERBUFFER, fb->MsaaColour);

	if (fb->Samples > 1 && gl.RenderbufferStorageMultisample)
	{
		gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, fb->Samples,
				GL_SRGB8_ALPHA8, width, height);
	}
	else
	{
		fb->Samples = 0;
		gl.RenderbufferStorage(GL_RENDERBUFFER, GL_SRGB8_ALPHA8, width, height);
	}

	gl.GenRenderbuffers(1, &fb->MsaaDepth);
	gl.BindRenderbuffer(GL_RENDERBUFFER, fb->MsaaDepth);

	if (fb->Samples > 1)
	{
		gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, fb->Samples,
				GL_DEPTH_COMPONENT24, width, height);
	}
	else
	{
		gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
	}

	gl.BindRenderbuffer(GL_RENDERBUFFER, 0);

	gl.GenFramebuffers(1, &fb->MsaaFrameBuffer);
	gl.BindFramebuffer(GL_FRAMEBUFFER, fb->MsaaFrameBuffer);
	gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_RENDERBUFFER, fb->MsaaColour);
	gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			GL_RENDERBUFFER, fb->MsaaDepth);

	if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		Com_Printf("VR: incomplete multisample framebuffer\n");
		gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
		return false;
	}

	gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
	Com_Printf("VR: eye framebuffer ready, %d samples\n", fb->Samples ? fb->Samples : 1);
	return true;
}

static void
q2xrFramebuffer_Destroy(q2xrFramebuffer *fb)
{
	if (fb->FrameBuffers)
	{
		gl.DeleteFramebuffers(fb->Length, fb->FrameBuffers);
	}

	if (fb->MsaaFrameBuffer)
	{
		gl.DeleteFramebuffers(1, &fb->MsaaFrameBuffer);
	}

	if (fb->MsaaColour)
	{
		gl.DeleteRenderbuffers(1, &fb->MsaaColour);
	}

	if (fb->MsaaDepth)
	{
		gl.DeleteRenderbuffers(1, &fb->MsaaDepth);
	}

	if (fb->Handle != XR_NULL_HANDLE)
	{
		xrDestroySwapchain(fb->Handle);
	}

	free(fb->Images);
	free(fb->FrameBuffers);
	memset(fb, 0, sizeof(*fb));
}

static qboolean
q2xrFramebuffer_Acquire(q2xrFramebuffer *fb)
{
	XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
	XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
	XrResult result = xrAcquireSwapchainImage(fb->Handle, &acquireInfo, &fb->Index);

	if (XR_FAILED(result))
	{
		q2xr_CheckXr(result, "xrAcquireSwapchainImage", __LINE__);
		return false;
	}

	waitInfo.timeout = Q2XR_SWAPCHAIN_TIMEOUT;
	result = xrWaitSwapchainImage(fb->Handle, &waitInfo);

	if (XR_FAILED(result))
	{
		q2xr_CheckXr(result, "xrWaitSwapchainImage", __LINE__);
		return false;
	}

	return true;
}

/*
 * Resolve the multisample target into the swapchain image. This is the step
 * their GLES extension performed implicitly.
 */
static void
q2xrFramebuffer_Resolve(q2xrFramebuffer *fb)
{
	gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fb->MsaaFrameBuffer);
	gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, fb->FrameBuffers[fb->Index]);
	gl.BlitFramebuffer(0, 0, fb->Width, fb->Height,
			0, 0, fb->Width, fb->Height,
			GL_COLOR_BUFFER_BIT, GL_NEAREST);
	gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

static void
q2xrFramebuffer_Release(q2xrFramebuffer *fb)
{
	XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};

	Q2XR_CHECK_XR(xrReleaseSwapchainImage(fb->Handle, &releaseInfo));
}

/* ------------------------------------------------------------------------- */
/* Actions - ported unchanged                                                  */
/* ------------------------------------------------------------------------- */

static void
q2xr_Path(const char *path, XrPath *xrPath)
{
	Q2XR_CHECK_XR(xrStringToPath(gApp.Instance, path, xrPath));
}

static XrActionSuggestedBinding
q2xr_Binding(XrAction action, XrPath binding)
{
	XrActionSuggestedBinding result;

	result.action = action;
	result.binding = binding;
	return result;
}

static void
q2xr_CreateAction(XrActionType type, const char *name, const char *label, XrAction *action)
{
	XrActionCreateInfo aci;

	memset(&aci, 0, sizeof(aci));
	aci.type = XR_TYPE_ACTION_CREATE_INFO;
	aci.actionType = type;
	aci.countSubactionPaths = NUM_EYES;
	aci.subactionPaths = handPath;
	Q_strlcpy(aci.actionName, name, sizeof(aci.actionName));
	Q_strlcpy(aci.localizedActionName, label, sizeof(aci.localizedActionName));
	Q2XR_CHECK_XR(xrCreateAction(actionSet, &aci, action));
}

static XrActionSuggestedBinding
q2xr_BindingFromString(XrAction action, const char *bindingString)
{
	XrPath bindingPath = XR_NULL_PATH;

	q2xr_Path(bindingString, &bindingPath);
	return q2xr_Binding(action, bindingPath);
}

/*
 * Note that gripPoseAction is bound to aim/pose, not grip/pose. That is
 * deliberate on their part and is left exactly as they had it - the weapon
 * alignment they tuned depends on it.
 */
static void
q2xr_SuggestTouchBindings(void)
{
	XrPath profile = XR_NULL_PATH;
	XrActionSuggestedBinding bindings[32];
	XrInteractionProfileSuggestedBinding suggested;
	uint32_t n = 0;

	if (hmdType == XR_DEVICE_TYPE_PICO)
	{
		q2xr_Path("/interaction_profiles/pico/neo3_controller", &profile);
	}
	else
	{
		q2xr_Path("/interaction_profiles/oculus/touch_controller", &profile);
	}

	bindings[n++] = q2xr_BindingFromString(gripPoseAction, "/user/hand/left/input/aim/pose");
	bindings[n++] = q2xr_BindingFromString(gripPoseAction, "/user/hand/right/input/aim/pose");
	bindings[n++] = q2xr_BindingFromString(aimPoseAction, "/user/hand/left/input/aim/pose");
	bindings[n++] = q2xr_BindingFromString(aimPoseAction, "/user/hand/right/input/aim/pose");
	bindings[n++] = q2xr_BindingFromString(hapticAction, "/user/hand/left/output/haptic");
	bindings[n++] = q2xr_BindingFromString(hapticAction, "/user/hand/right/output/haptic");

	if (hmdType == XR_DEVICE_TYPE_PICO)
	{
		bindings[n++] = q2xr_BindingFromString(menuAction, "/user/hand/left/input/back/click");
		bindings[n++] = q2xr_BindingFromString(menuAction, "/user/hand/right/input/back/click");
		bindings[n++] = q2xr_BindingFromString(triggerAction, "/user/hand/left/input/trigger/click");
		bindings[n++] = q2xr_BindingFromString(triggerAction, "/user/hand/right/input/trigger/click");
	}
	else
	{
		bindings[n++] = q2xr_BindingFromString(menuAction, "/user/hand/left/input/menu/click");
		bindings[n++] = q2xr_BindingFromString(triggerAction, "/user/hand/left/input/trigger");
		bindings[n++] = q2xr_BindingFromString(triggerAction, "/user/hand/right/input/trigger");
	}

	bindings[n++] = q2xr_BindingFromString(squeezeAction, "/user/hand/left/input/squeeze/value");
	bindings[n++] = q2xr_BindingFromString(squeezeAction, "/user/hand/right/input/squeeze/value");
	bindings[n++] = q2xr_BindingFromString(thumbstickAction, "/user/hand/left/input/thumbstick");
	bindings[n++] = q2xr_BindingFromString(thumbstickAction, "/user/hand/right/input/thumbstick");
	bindings[n++] = q2xr_BindingFromString(thumbstickClickAction, "/user/hand/left/input/thumbstick/click");
	bindings[n++] = q2xr_BindingFromString(thumbstickClickAction, "/user/hand/right/input/thumbstick/click");

	if (hmdType != XR_DEVICE_TYPE_PICO)
	{
		bindings[n++] = q2xr_BindingFromString(thumbstickTouchAction, "/user/hand/left/input/thumbstick/touch");
		bindings[n++] = q2xr_BindingFromString(thumbstickTouchAction, "/user/hand/right/input/thumbstick/touch");
	}

	bindings[n++] = q2xr_BindingFromString(xAction, "/user/hand/left/input/x/click");
	bindings[n++] = q2xr_BindingFromString(yAction, "/user/hand/left/input/y/click");
	bindings[n++] = q2xr_BindingFromString(aAction, "/user/hand/right/input/a/click");
	bindings[n++] = q2xr_BindingFromString(bAction, "/user/hand/right/input/b/click");

	memset(&suggested, 0, sizeof(suggested));
	suggested.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
	suggested.interactionProfile = profile;
	suggested.countSuggestedBindings = n;
	suggested.suggestedBindings = bindings;
	Q2XR_CHECK_XR(xrSuggestInteractionProfileBindings(gApp.Instance, &suggested));
}

static qboolean
q2xr_CreateActions(void)
{
	XrActionSetCreateInfo asci;
	XrSessionActionSetsAttachInfo attachInfo;
	int hand;

	q2xr_Path("/user/hand/left", &handPath[0]);
	q2xr_Path("/user/hand/right", &handPath[1]);

	memset(&asci, 0, sizeof(asci));
	asci.type = XR_TYPE_ACTION_SET_CREATE_INFO;
	Q_strlcpy(asci.actionSetName, "gameplay", sizeof(asci.actionSetName));
	Q_strlcpy(asci.localizedActionSetName, "Gameplay", sizeof(asci.localizedActionSetName));
	Q2XR_CHECK_XR(xrCreateActionSet(gApp.Instance, &asci, &actionSet));

	q2xr_CreateAction(XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip Pose", &gripPoseAction);
	q2xr_CreateAction(XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim Pose", &aimPoseAction);
	q2xr_CreateAction(XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Haptic", &hapticAction);
	q2xr_CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "trigger", "Trigger", &triggerAction);
	q2xr_CreateAction(XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Squeeze", &squeezeAction);
	q2xr_CreateAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick", &thumbstickAction);
	q2xr_CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_click", "Thumbstick Click", &thumbstickClickAction);
	q2xr_CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_touch", "Thumbstick Touch", &thumbstickTouchAction);
	q2xr_CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "a_click", "A", &aAction);
	q2xr_CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "b_click", "B", &bAction);
	q2xr_CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "x_click", "X", &xAction);
	q2xr_CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "y_click", "Y", &yAction);
	q2xr_CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "menu_click", "Menu", &menuAction);

	q2xr_SuggestTouchBindings();

	for (hand = 0; hand < NUM_EYES; ++hand)
	{
		XrActionSpaceCreateInfo asciSpace;

		memset(&asciSpace, 0, sizeof(asciSpace));
		asciSpace.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
		asciSpace.poseInActionSpace = q2xr_IdentityPose();
		asciSpace.subactionPath = handPath[hand];
		asciSpace.action = gripPoseAction;
		Q2XR_CHECK_XR(xrCreateActionSpace(gApp.Session, &asciSpace, &gripSpace[hand]));
		asciSpace.action = aimPoseAction;
		Q2XR_CHECK_XR(xrCreateActionSpace(gApp.Session, &asciSpace, &aimSpace[hand]));
	}

	memset(&attachInfo, 0, sizeof(attachInfo));
	attachInfo.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &actionSet;
	Q2XR_CHECK_XR(xrAttachSessionActionSets(gApp.Session, &attachInfo));
	return true;
}

static XrActionStateBoolean
q2xr_GetBoolean(XrAction action, int hand)
{
	XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
	XrActionStateBoolean state = {XR_TYPE_ACTION_STATE_BOOLEAN};

	getInfo.action = action;
	getInfo.subactionPath = handPath[hand];
	Q2XR_CHECK_XR(xrGetActionStateBoolean(gApp.Session, &getInfo, &state));
	return state;
}

static XrActionStateFloat
q2xr_GetFloat(XrAction action, int hand)
{
	XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
	XrActionStateFloat state = {XR_TYPE_ACTION_STATE_FLOAT};

	getInfo.action = action;
	getInfo.subactionPath = handPath[hand];
	Q2XR_CHECK_XR(xrGetActionStateFloat(gApp.Session, &getInfo, &state));
	return state;
}

static XrActionStateVector2f
q2xr_GetVector2(XrAction action, int hand)
{
	XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
	XrActionStateVector2f state = {XR_TYPE_ACTION_STATE_VECTOR2F};

	getInfo.action = action;
	getInfo.subactionPath = handPath[hand];
	Q2XR_CHECK_XR(xrGetActionStateVector2f(gApp.Session, &getInfo, &state));
	return state;
}

void
TBXR_UpdateControllers(void)
{
	XrActiveActionSet activeSet;
	XrActionsSyncInfo syncInfo;
	XrResult syncResult;
	ovrInputStateTrackedRemote *states[NUM_EYES];
	ovrTracking *tracking[NUM_EYES];
	int hand;

	if (gApp.Session == XR_NULL_HANDLE || !gApp.Focused || actionSet == XR_NULL_HANDLE)
	{
		return;
	}

	memset(&activeSet, 0, sizeof(activeSet));
	activeSet.actionSet = actionSet;
	memset(&syncInfo, 0, sizeof(syncInfo));
	syncInfo.type = XR_TYPE_ACTIONS_SYNC_INFO;
	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &activeSet;
	syncResult = xrSyncActions(gApp.Session, &syncInfo);

	if (XR_FAILED(syncResult))
	{
		q2xr_CheckXr(syncResult, "xrSyncActions", __LINE__);
		return;
	}

	states[0] = &leftTrackedRemoteState_new;
	states[1] = &rightTrackedRemoteState_new;
	tracking[0] = &leftRemoteTracking_new;
	tracking[1] = &rightRemoteTracking_new;

	for (hand = 0; hand < NUM_EYES; ++hand)
	{
		XrActionStateBoolean trigger;
		XrActionStateFloat squeeze;
		XrActionStateVector2f stick;
		XrSpaceLocation aimLocation = {XR_TYPE_SPACE_LOCATION};
		XrSpaceVelocity velocity = {XR_TYPE_SPACE_VELOCITY};
		float joyX, joyY, joyLen;

		memset(states[hand], 0, sizeof(*states[hand]));
		trigger = q2xr_GetBoolean(triggerAction, hand);
		squeeze = q2xr_GetFloat(squeezeAction, hand);
		stick = q2xr_GetVector2(thumbstickAction, hand);
		states[hand]->IndexTrigger = trigger.currentState ? 1.0f : 0.0f;
		states[hand]->GripTrigger = squeeze.currentState;
		joyX = stick.currentState.x;
		joyY = stick.currentState.y;

		joyLen = sqrtf((joyX * joyX) + (joyY * joyY));

		if (joyLen > 1.0f)
		{
			joyX /= joyLen;
			joyY /= joyLen;
		}

		states[hand]->Joystick.x = joyX;
		states[hand]->Joystick.y = joyY;

		if (trigger.currentState)
		{
			states[hand]->Buttons |= ovrButton_Trigger;
		}

		if (squeeze.currentState > 0.5f)
		{
			states[hand]->Buttons |= ovrButton_GripTrigger;
		}

		if (q2xr_GetBoolean(thumbstickClickAction, hand).currentState)
		{
			states[hand]->Buttons |= ovrButton_Joystick | (hand == 0 ? ovrButton_LThumb : ovrButton_RThumb);
		}

		if (q2xr_GetBoolean(thumbstickTouchAction, hand).currentState)
		{
			states[hand]->Touches |= ovrTouch_ThumbRest;
		}

		if (q2xr_GetBoolean(menuAction, hand).currentState)
		{
			states[hand]->Buttons |= ovrButton_Enter;
		}

		if (hand == 0)
		{
			if (q2xr_GetBoolean(xAction, hand).currentState)
			{
				states[hand]->Buttons |= ovrButton_X;
			}

			if (q2xr_GetBoolean(yAction, hand).currentState)
			{
				states[hand]->Buttons |= ovrButton_Y;
			}
		}
		else
		{
			if (q2xr_GetBoolean(aAction, hand).currentState)
			{
				states[hand]->Buttons |= ovrButton_A;
			}

			if (q2xr_GetBoolean(bAction, hand).currentState)
			{
				states[hand]->Buttons |= ovrButton_B;
			}
		}

		aimLocation.next = &velocity;
		Q2XR_CHECK_XR(xrLocateSpace(aimSpace[hand], gApp.StageSpace,
				gApp.FrameState.predictedDisplayTime, &aimLocation));
		tracking[hand]->Active = (aimLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
		tracking[hand]->HeadPose.Pose.Position = aimLocation.pose.position;
		tracking[hand]->HeadPose.Pose.Orientation = aimLocation.pose.orientation;
		tracking[hand]->Velocity = velocity;
	}
}

/* ------------------------------------------------------------------------- */
/* Haptics - ported unchanged                                                  */
/* ------------------------------------------------------------------------- */

float vibration_channel_duration[2] = {0.0f, 0.0f};
float vibration_channel_intensity[2] = {0.0f, 0.0f};

void
Android_Vibrate(float duration, int channel, float intensity)
{
	if (channel < 0 || channel >= 2)
	{
		return;
	}

	if (vibration_channel_duration[channel] > 0.0f)
	{
		return;
	}

	if (vibration_channel_duration[channel] == -1.0f && duration != 0.0f)
	{
		return;
	}

	vibration_channel_duration[channel] = duration;
	vibration_channel_intensity[channel] = intensity;
}

static void
q2xr_ProcessHaptics(float frameTimeMs)
{
	int hand;

	if (gApp.Session == XR_NULL_HANDLE || hapticAction == XR_NULL_HANDLE)
	{
		return;
	}

	for (hand = 0; hand < 2; ++hand)
	{
		if (vibration_channel_duration[hand] > 0.0f ||
			vibration_channel_duration[hand] == -1.0f)
		{
			XrHapticVibration vibration = {XR_TYPE_HAPTIC_VIBRATION};
			XrHapticActionInfo info = {XR_TYPE_HAPTIC_ACTION_INFO};

			vibration.amplitude = vibration_channel_intensity[hand];
			vibration.duration = XR_MIN_HAPTIC_DURATION;
			vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
			info.action = hapticAction;
			info.subactionPath = handPath[hand];
			xrApplyHapticFeedback(gApp.Session, &info, (const XrHapticBaseHeader *)&vibration);

			if (vibration_channel_duration[hand] != -1.0f)
			{
				vibration_channel_duration[hand] -= frameTimeMs;

				if (vibration_channel_duration[hand] < 0.0f)
				{
					vibration_channel_duration[hand] = 0.0f;
					vibration_channel_intensity[hand] = 0.0f;
				}
			}
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Session bring-up                                                            */
/* ------------------------------------------------------------------------- */

static qboolean
q2xr_ExtensionSupported(const XrExtensionProperties *props, uint32_t count, const char *name)
{
	uint32_t i;

	for (i = 0; i < count; i++)
	{
		if (strcmp(props[i].extensionName, name) == 0)
		{
			return true;
		}
	}

	return false;
}

/*
 * Find the window SDL created and hand OpenXR the GL context bound to it.
 * Their EGL equivalent built its own context on a pbuffer surface; here the
 * engine's context is reused directly, so the renderer and the compositor
 * share objects without any sharing setup.
 */
static qboolean
q2xr_GetGraphicsBinding(XrGraphicsBindingOpenGLWin32KHR *binding)
{
	HGLRC context = wglGetCurrentContext();
	HDC dc = wglGetCurrentDC();

	if (context == NULL || dc == NULL)
	{
		Com_Printf("VR: no current OpenGL context - the window must exist before VR starts\n");
		return false;
	}

	memset(binding, 0, sizeof(*binding));
	binding->type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR;
	binding->hDC = dc;
	binding->hGLRC = context;
	return true;
}

/*
 * Bring-up is split in two on PC, which is the one structural change to their
 * ordering. Theirs runs as: create the EGL context, initialise all of OpenXR,
 * then Qcommon_Init. That works because on Android the context exists before
 * the engine starts and the surface is sized from Quest_GetScreenRes.
 *
 * On PC the GL context is created by the engine, inside Qcommon_Init, so the
 * session cannot exist beforehand. But the engine also needs the eye
 * resolution while it is starting up, because VID_GetModeInfo returns the
 * "desktop" mode and their change makes that the eye buffer size.
 *
 * The split resolves the circularity along the line OpenXR already draws:
 * instance, system and view configuration need no graphics binding, while
 * session, swapchains and actions do. Phase one runs before Qcommon_Init and
 * yields the eye size; phase two runs once the context exists.
 */
static qboolean
q2xr_InitInstance(void)
{
	uint32_t availableCount = 0;
	XrExtensionProperties *available = NULL;
	const char *extensions[8];
	uint32_t extensionCount = 0;
	XrApplicationInfo appInfo;
	XrInstanceCreateInfo createInfo;
	XrInstanceProperties props = {XR_TYPE_INSTANCE_PROPERTIES};
	XrSystemGetInfo systemInfo = {XR_TYPE_SYSTEM_GET_INFO};
	PFN_xrGetOpenGLGraphicsRequirementsKHR getGlRequirements = NULL;
	XrResult result;
	uint32_t viewCount = 0;
	uint32_t i;

	/*
	 * No loader init here. Theirs passes the JavaVM and activity through
	 * XrLoaderInitInfoAndroidKHR, which the desktop loader neither needs nor
	 * has.
	 */
	xrEnumerateInstanceExtensionProperties(NULL, 0, &availableCount, NULL);

	if (availableCount > 0)
	{
		available = (XrExtensionProperties *)malloc(availableCount * sizeof(XrExtensionProperties));
	}

	if (available)
	{
		for (i = 0; i < availableCount; i++)
		{
			memset(&available[i], 0, sizeof(available[i]));
			available[i].type = XR_TYPE_EXTENSION_PROPERTIES;
		}

		if (XR_FAILED(xrEnumerateInstanceExtensionProperties(NULL, availableCount,
				&availableCount, available)))
		{
			availableCount = 0;
		}
	}
	else
	{
		availableCount = 0;
	}

	/* Desktop GL rather than GLES, and no Android instance-create extension. */
	extensions[extensionCount++] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;

	if (!q2xr_ExtensionSupported(available, availableCount, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME))
	{
		Com_Printf("VR: the OpenXR runtime does not support %s\n",
				XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
		free(available);
		return false;
	}

	if (q2xr_ExtensionSupported(available, availableCount,
			XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
	{
		extensions[extensionCount++] = XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
	}

	free(available);

	memset(&appInfo, 0, sizeof(appInfo));
	Q_strlcpy(appInfo.applicationName, "Quake2Quest", sizeof(appInfo.applicationName));
	appInfo.applicationVersion = 20;
	Q_strlcpy(appInfo.engineName, "Yamagi Quake II", sizeof(appInfo.engineName));
	appInfo.engineVersion = 1;

	/*
	 * 1.0 deliberately, as theirs does. VDXR rejects an apiVersion of 1.1
	 * outright, and nothing here needs a 1.1 entry point.
	 */
	appInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);

	memset(&createInfo, 0, sizeof(createInfo));
	createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
	createInfo.applicationInfo = appInfo;
	createInfo.enabledExtensionCount = extensionCount;
	createInfo.enabledExtensionNames = extensions;

	result = xrCreateInstance(&createInfo, &gApp.Instance);

	if (XR_FAILED(result))
	{
		q2xr_CheckXr(result, "xrCreateInstance", __LINE__);
		return false;
	}

	Q2XR_CHECK_XR(xrGetInstanceProperties(gApp.Instance, &props));
	Com_Printf("VR: OpenXR runtime is %s\n", props.runtimeName);
	VR_SetHMDTypeFromRuntimeName(props.runtimeName);

	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	result = xrGetSystem(gApp.Instance, &systemInfo, &gApp.SystemId);

	if (XR_FAILED(result))
	{
		q2xr_CheckXr(result, "xrGetSystem", __LINE__);
		return false;
	}

	/*
	 * Required by the spec before xrCreateSession: the runtime will fail
	 * session creation if the graphics requirements were never queried.
	 */
	Q2XR_CHECK_XR(xrGetInstanceProcAddr(gApp.Instance, "xrGetOpenGLGraphicsRequirementsKHR",
			(PFN_xrVoidFunction *)&getGlRequirements));

	if (getGlRequirements)
	{
		XrGraphicsRequirementsOpenGLKHR requirements = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};

		Q2XR_CHECK_XR(getGlRequirements(gApp.Instance, gApp.SystemId, &requirements));
	}

	Q2XR_CHECK_XR(xrEnumerateViewConfigurationViews(gApp.Instance, gApp.SystemId,
			XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, NULL));

	if (viewCount != NUM_EYES)
	{
		Com_Printf("VR: expected stereo OpenXR views, got %u\n", viewCount);
		return false;
	}

	for (i = 0; i < NUM_EYES; ++i)
	{
		gApp.ViewConfig[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		gApp.Views[i].type = XR_TYPE_VIEW;
	}

	Q2XR_CHECK_XR(xrEnumerateViewConfigurationViews(gApp.Instance, gApp.SystemId,
			XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, NUM_EYES, &viewCount, gApp.ViewConfig));

	gApp.Width = (int)(gApp.ViewConfig[0].recommendedImageRectWidth * SS_MULTIPLIER);
	gApp.Height = (int)(gApp.ViewConfig[0].recommendedImageRectHeight * SS_MULTIPLIER);

	/*
	 * They pin the display to 72Hz, because that is what a Quest runs at and
	 * their frame timing assumes it. On PC the refresh rate belongs to the
	 * runtime and to whatever the user has configured in Virtual Desktop or
	 * SteamVR, and forcing 72 would override a deliberate choice. So the rate
	 * is read rather than requested. This is a platform difference, not a
	 * change of behaviour: on a headset that reports 72 the result is the same
	 * number their code hard-codes.
	 */
	gApp.RefreshRate = 90;

	Com_Printf("VR: eye buffer %dx%d (runtime recommends %ux%u)\n",
			gApp.Width, gApp.Height,
			gApp.ViewConfig[0].recommendedImageRectWidth,
			gApp.ViewConfig[0].recommendedImageRectHeight);

	return true;
}

static qboolean
q2xr_InitSession(void)
{
	XrGraphicsBindingOpenGLWin32KHR graphicsBinding;
	XrSessionCreateInfo sessionInfo = {XR_TYPE_SESSION_CREATE_INFO};
	XrReferenceSpaceCreateInfo spaceInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	XrResult result;
	int eye;

	if (!q2xr_GetGraphicsBinding(&graphicsBinding))
	{
		return false;
	}

	sessionInfo.next = &graphicsBinding;
	sessionInfo.systemId = gApp.SystemId;
	result = xrCreateSession(gApp.Instance, &sessionInfo, &gApp.Session);

	if (XR_FAILED(result))
	{
		q2xr_CheckXr(result, "xrCreateSession", __LINE__);
		return false;
	}

	Com_Printf("VR: session created\n");

	{
		PFN_xrGetDisplayRefreshRateFB pfnGetDisplayRefreshRate = NULL;

		if (XR_SUCCEEDED(xrGetInstanceProcAddr(gApp.Instance, "xrGetDisplayRefreshRateFB",
				(PFN_xrVoidFunction *)&pfnGetDisplayRefreshRate)) && pfnGetDisplayRefreshRate)
		{
			float rate = 0.0f;

			if (XR_SUCCEEDED(pfnGetDisplayRefreshRate(gApp.Session, &rate)) && rate > 0.0f)
			{
				gApp.RefreshRate = (int)(rate + 0.5f);
			}
		}

		Com_Printf("VR: display refresh rate %dHz\n", gApp.RefreshRate);
	}

	spaceInfo.poseInReferenceSpace = q2xr_IdentityPose();
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	Q2XR_CHECK_XR(xrCreateReferenceSpace(gApp.Session, &spaceInfo, &gApp.LocalSpace));
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	result = xrCreateReferenceSpace(gApp.Session, &spaceInfo, &gApp.StageSpace);

	if (XR_FAILED(result))
	{
		gApp.StageSpace = gApp.LocalSpace;
	}

	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	Q2XR_CHECK_XR(xrCreateReferenceSpace(gApp.Session, &spaceInfo, &gApp.ViewSpace));

	for (eye = 0; eye < NUM_EYES; ++eye)
	{
		if (!q2xrFramebuffer_Create(&gApp.Eye[eye], gApp.Width, gApp.Height))
		{
			return false;
		}
	}

	q2xr_CreateActions();
	Com_Printf("VR: action setup complete\n");
	return true;
}

static void
q2xr_DestroyOpenXR(void)
{
	int hand, eye;

	for (hand = 0; hand < NUM_EYES; ++hand)
	{
		if (gripSpace[hand])
		{
			xrDestroySpace(gripSpace[hand]);
		}

		if (aimSpace[hand])
		{
			xrDestroySpace(aimSpace[hand]);
		}

		gripSpace[hand] = XR_NULL_HANDLE;
		aimSpace[hand] = XR_NULL_HANDLE;
	}

	if (actionSet)
	{
		xrDestroyActionSet(actionSet);
		actionSet = XR_NULL_HANDLE;
	}

	for (eye = 0; eye < NUM_EYES; ++eye)
	{
		q2xrFramebuffer_Destroy(&gApp.Eye[eye]);
	}

	if (gApp.ViewSpace && gApp.ViewSpace != gApp.LocalSpace)
	{
		xrDestroySpace(gApp.ViewSpace);
	}

	if (gApp.StageSpace && gApp.StageSpace != gApp.LocalSpace)
	{
		xrDestroySpace(gApp.StageSpace);
	}

	if (gApp.LocalSpace)
	{
		xrDestroySpace(gApp.LocalSpace);
	}

	if (gApp.Session)
	{
		xrDestroySession(gApp.Session);
	}

	if (gApp.Instance)
	{
		xrDestroyInstance(gApp.Instance);
	}

	memset(&gApp, 0, sizeof(gApp));
}

static void
q2xr_ProcessEvents(void)
{
	XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};

	while (xrPollEvent(gApp.Instance, &event) == XR_SUCCESS)
	{
		switch (event.type)
		{
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
			{
				XrEventDataSessionStateChanged *changed = (XrEventDataSessionStateChanged *)&event;

				if (changed->state == XR_SESSION_STATE_READY)
				{
					XrSessionBeginInfo beginInfo = {XR_TYPE_SESSION_BEGIN_INFO};

					beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
					Q2XR_CHECK_XR(xrBeginSession(gApp.Session, &beginInfo));
					gApp.SessionRunning = true;
				}
				else if (changed->state == XR_SESSION_STATE_STOPPING)
				{
					Q2XR_CHECK_XR(xrEndSession(gApp.Session));
					gApp.SessionRunning = false;
				}
				else if (changed->state == XR_SESSION_STATE_FOCUSED)
				{
					gApp.Focused = true;
				}
				else if (changed->state == XR_SESSION_STATE_VISIBLE)
				{
					gApp.Focused = false;
				}
				else if (changed->state == XR_SESSION_STATE_EXITING ||
						changed->state == XR_SESSION_STATE_LOSS_PENDING)
				{
					gApp.SessionRunning = false;
				}

				break;
			}

			default:
				break;
		}

		event.type = XR_TYPE_EVENT_DATA_BUFFER;
		event.next = NULL;
	}
}

static void
q2xr_UpdateHeadPose(void)
{
	XrSpaceLocation location = {XR_TYPE_SPACE_LOCATION};

	if (XR_SUCCEEDED(xrLocateSpace(gApp.ViewSpace, gApp.StageSpace,
			gApp.FrameState.predictedDisplayTime, &location)))
	{
		vec3_t orientation;

		q2xrHeadPoseStage = location.pose;
		QuatToYawPitchRoll(location.pose.orientation, 0.0f, orientation);
		VectorCopy(orientation, hmdorientation);
		setHMDPosition(-location.pose.position.x, location.pose.position.y,
				-location.pose.position.z, orientation[YAW]);
		setWorldPosition(-location.pose.position.x, location.pose.position.y,
				-location.pose.position.z);
	}
}

/* ------------------------------------------------------------------------- */
/* The frame                                                                   */
/* ------------------------------------------------------------------------- */

/*
 * This drives the engine rather than being called by it: Qcommon_BeginFrame,
 * then Qcommon_Frame once per eye, then Qcommon_EndFrame, all inside one
 * xrBeginFrame/xrEndFrame pair. That inversion is theirs and is the reason the
 * Windows backend hands its main loop over to this function.
 */
void
TBXR_FrameSetup(void)
{
	int time;
	XrFrameWaitInfo waitInfo = {XR_TYPE_FRAME_WAIT_INFO};
	XrFrameBeginInfo beginInfo = {XR_TYPE_FRAME_BEGIN_INFO};
	XrViewLocateInfo viewLocateInfo = {XR_TYPE_VIEW_LOCATE_INFO};
	XrViewState viewState = {XR_TYPE_VIEW_STATE};
	XrCompositionLayerProjection projectionLayer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
	XrCompositionLayerProjectionView projectionViews[NUM_EYES];
	XrCompositionLayerQuad quadLayer = {XR_TYPE_COMPOSITION_LAYER_QUAD};
	XrFrameEndInfo endInfo = {XR_TYPE_FRAME_END_INFO};
	const XrCompositionLayerBaseHeader *layers[2];
	uint32_t layerCount = 0;
	uint32_t viewCount = 0;
	qboolean endedQuakeFrame = false;
	qboolean screenLayer;
	int eyeCount;
	int eye;

	do
	{
		global_time = Sys_Milliseconds();
		time = (int)(global_time - oldtime);
	}
	while (time < 1);

	q2xr_ProcessEvents();

	if (!gApp.SessionRunning)
	{
		oldtime = global_time;
		return;
	}

	memset(&gApp.FrameState, 0, sizeof(gApp.FrameState));
	gApp.FrameState.type = XR_TYPE_FRAME_STATE;
	Q2XR_CHECK_XR(xrWaitFrame(gApp.Session, &waitInfo, &gApp.FrameState));

	Q2XR_CHECK_XR(xrBeginFrame(gApp.Session, &beginInfo));

	q2xr_UpdateHeadPose();
	q2xr_ProcessHaptics((float)time);

	if (vr_control_scheme != NULL)
	{
		acquireTrackedRemotesData();

		switch ((int)vr_control_scheme->value)
		{
			case RIGHT_HANDED_DEFAULT:
				HandleInput_Default(&rightTrackedRemoteState_new, &rightTrackedRemoteState_old,
						&rightRemoteTracking_new,
						&leftTrackedRemoteState_new, &leftTrackedRemoteState_old,
						&leftRemoteTracking_new,
						ovrButton_A, ovrButton_B, ovrButton_X, ovrButton_Y);
				break;

			case LEFT_HANDED_DEFAULT:
			case LEFT_HANDED_SWITCH_STICKS:
				HandleInput_Default(&leftTrackedRemoteState_new, &leftTrackedRemoteState_old,
						&leftRemoteTracking_new,
						&rightTrackedRemoteState_new, &rightTrackedRemoteState_old,
						&rightRemoteTracking_new,
						ovrButton_X, ovrButton_Y, ovrButton_A, ovrButton_B);
				break;
		}

		rightTrackedRemoteState_old = rightTrackedRemoteState_new;
		leftTrackedRemoteState_old = leftTrackedRemoteState_new;
	}

	Qcommon_BeginFrame(time * 1000);

	viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	viewLocateInfo.displayTime = gApp.FrameState.predictedDisplayTime;
	viewLocateInfo.space = gApp.StageSpace;
	Q2XR_CHECK_XR(xrLocateViews(gApp.Session, &viewLocateInfo, &viewState, NUM_EYES,
			&viewCount, gApp.Views));

	/*
	 * The renderer picks the asymmetric projection up from these cvars rather
	 * than through the refimport table - their approach, kept as-is.
	 */
	for (eye = 0; eye < NUM_EYES; ++eye)
	{
		Cvar_SetValue(va("gl1_openxr_fov_left_%d", eye), tanf(gApp.Views[eye].fov.angleLeft));
		Cvar_SetValue(va("gl1_openxr_fov_right_%d", eye), tanf(gApp.Views[eye].fov.angleRight));
		Cvar_SetValue(va("gl1_openxr_fov_up_%d", eye), tanf(gApp.Views[eye].fov.angleUp));
		Cvar_SetValue(va("gl1_openxr_fov_down_%d", eye), tanf(gApp.Views[eye].fov.angleDown));
	}

	screenLayer = useScreenLayer();

	if (screenLayer && !q2xrWasUsingScreenLayer)
	{
		float screenDistance = Cvar_VariableValue("vr_screen_depth");
		const float yaw = hmdorientation[YAW];

		if (screenDistance <= 0.0f)
		{
			screenDistance = 3.5f;
		}

		q2xrScreenLayerPose.orientation = q2xr_QuatFromYaw(yaw);
		q2xrScreenLayerPose.position.x = q2xrHeadPoseStage.position.x - sinf(radians(yaw)) * screenDistance;
		q2xrScreenLayerPose.position.y = q2xrHeadPoseStage.position.y;
		q2xrScreenLayerPose.position.z = q2xrHeadPoseStage.position.z - cosf(radians(yaw)) * screenDistance;
	}

	q2xrWasUsingScreenLayer = screenLayer;

	memset(projectionViews, 0, sizeof(projectionViews));
	eyeCount = screenLayer ? 1 : NUM_EYES;

	for (eye = 0; eye < eyeCount; ++eye)
	{
		q2xrFramebuffer *fb = &gApp.Eye[eye];

		if (!q2xrFramebuffer_Acquire(fb))
		{
			continue;
		}

		/* Render into the multisample target, not the swapchain image. */
		gl.BindFramebuffer(GL_FRAMEBUFFER, fb->MsaaFrameBuffer);
		glViewport(0, 0, fb->Width, fb->Height);
		glScissor(0, 0, fb->Width, fb->Height);
		glEnable(GL_SCISSOR_TEST);
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_FRAMEBUFFER_SRGB);

		Qcommon_Frame(screenLayer ? 0 : eye);

		if (!endedQuakeFrame && eye == eyeCount - 1)
		{
			Qcommon_EndFrame(time * 1000);
			endedQuakeFrame = true;
		}

		q2xrFramebuffer_Resolve(fb);
		gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

		q2xrFramebuffer_Release(fb);

		if (!screenLayer)
		{
			projectionViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			projectionViews[eye].pose = gApp.Views[eye].pose;
			projectionViews[eye].fov = gApp.Views[eye].fov;
			projectionViews[eye].subImage.swapchain = fb->Handle;
			projectionViews[eye].subImage.imageRect.offset.x = 0;
			projectionViews[eye].subImage.imageRect.offset.y = 0;
			projectionViews[eye].subImage.imageRect.extent.width = fb->Width;
			projectionViews[eye].subImage.imageRect.extent.height = fb->Height;
		}
		else
		{
			const float screenAspect = (float)fb->Width / (float)fb->Height;

			quadLayer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
			quadLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
					XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
			quadLayer.space = gApp.StageSpace;
			quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			quadLayer.subImage.swapchain = fb->Handle;
			quadLayer.subImage.imageRect.offset.x = 0;
			quadLayer.subImage.imageRect.offset.y = 0;
			quadLayer.subImage.imageRect.extent.width = fb->Width;
			quadLayer.subImage.imageRect.extent.height = fb->Height;
			quadLayer.pose = q2xrScreenLayerPose;
			quadLayer.size.width = 3.0f;
			quadLayer.size.height = quadLayer.size.width / screenAspect;
		}
	}

	if (!endedQuakeFrame)
	{
		Qcommon_EndFrame(time * 1000);
	}

	oldtime = global_time;

	if (!screenLayer)
	{
		projectionLayer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
		projectionLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
				XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
		projectionLayer.space = gApp.StageSpace;
		projectionLayer.viewCount = NUM_EYES;
		projectionLayer.views = projectionViews;
		layers[layerCount++] = (const XrCompositionLayerBaseHeader *)&projectionLayer;
	}
	else if (quadLayer.subImage.swapchain != XR_NULL_HANDLE)
	{
		layers[layerCount++] = (const XrCompositionLayerBaseHeader *)&quadLayer;
	}

	endInfo.displayTime = gApp.FrameState.predictedDisplayTime;
	endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	endInfo.layerCount = gApp.FrameState.shouldRender ? layerCount : 0;
	endInfo.layers = gApp.FrameState.shouldRender ? layers : NULL;
	Q2XR_CHECK_XR(xrEndFrame(gApp.Session, &endInfo));
	q2xrFrameLogCount++;
}

/* bool, not qboolean - VrCommon.h declares it with the C99 type. */
bool
useScreenLayer(void)
{
	return ((cls.state != ca_connected && cls.state != ca_active) ||
			cls.key_dest != key_game ||
			cl.attractloop ||
			cl.cinematictime != 0);
}

void
Q2VR_exit(int exitCode)
{
	(void)exitCode;
}

/* ------------------------------------------------------------------------- */
/* Quake-facing surface                                                        */
/* ------------------------------------------------------------------------- */

void
Quest_GetScreenRes(int *width, int *height)
{
	if (useScreenLayer())
	{
		*width = (int)cylinderSize[0];
		*height = (int)cylinderSize[1];
	}
	else
	{
		*width = gApp.Width ? gApp.Width : 1024;
		*height = gApp.Height ? gApp.Height : 1024;
	}
}

int
Quest_GetRefresh(void)
{
	return gApp.RefreshRate ? gApp.RefreshRate : 90;
}

float
getFOV(void)
{
	return 90.0f;
}

void
Quest_MessageBox(const char *title, const char *text)
{
	Com_Printf("%s %s\n", title, text);
}

/*
 * Their defaults, copied exactly. These values are tuned against real
 * geometry - vr_worldscale is units per metre, vr_weapon_pitchadjust
 * compensates for how a Touch controller sits in the hand - so they are not
 * arbitrary and must not be "tidied".
 */
void
VR_Init(void)
{
	playerYaw = 0.0f;
	showingScreenLayer = false;
	remote_movementSideways = 0.0f;
	remote_movementForward = 0.0f;
	remote_movementUp = 0.0f;
	positional_movementSideways = 0.0f;
	positional_movementForward = 0.0f;
	snapTurn = 0.0f;
	ducked = DUCK_NOTDUCKED;

	srand(time(NULL));

	vr_snapturn_angle = Cvar_Get("vr_snapturn_angle", "45", CVAR_ARCHIVE);
	vr_smoothturn = Cvar_Get("vr_smoothturn", "0", CVAR_ARCHIVE);
	vr_walkdirection = Cvar_Get("vr_walkdirection", "1", CVAR_ARCHIVE); /* 1 = gaze/HMD direction (default), 0 = off-hand controller */
	vr_weapon_pitchadjust = Cvar_Get("vr_weapon_pitchadjust", "-20.0", CVAR_ARCHIVE);
	vr_control_scheme = Cvar_Get("vr_control_scheme", "0", CVAR_ARCHIVE);
	vr_height_adjust = Cvar_Get("vr_height_adjust", "0.0", CVAR_ARCHIVE);
	vr_weaponscale = Cvar_Get("vr_weaponscale", "0.56", CVAR_ARCHIVE);
	vr_weapon_stabilised = Cvar_Get("vr_weapon_stabilised", "0.0", CVAR_LATCH);
	vr_comfort_mask = Cvar_Get("vr_comfort_mask", "0.0", CVAR_ARCHIVE);
	vr_turn_deadzone = Cvar_Get("vr_turn_deadzone", "0.2", CVAR_ARCHIVE);
	vr_framerate = Cvar_Get("vr_framerate", "0", CVAR_ARCHIVE);
	vr_use_wheels = Cvar_Get("vr_use_wheels", "1", CVAR_ARCHIVE);
	vr_jump_sound = Cvar_Get("vr_jump_sound", "1", CVAR_ARCHIVE);
	vr_worldscale = Cvar_Get("vr_worldscale", "26.2467", CVAR_ARCHIVE);
	vr_lasersight = Cvar_Get("vr_lasersight", "2", CVAR_ARCHIVE); /* 2 = simple laser dot */
	Cvar_Get("vr_hud_depth", "0.5", CVAR_ARCHIVE);
	Cvar_Get("vr_hud_ipd", "0.064", CVAR_ARCHIVE);
	Cvar_Get("vr_screen_depth", "3.5", CVAR_ARCHIVE);

	refresh_names = malloc(2 * sizeof(char *));
	refresh_values = malloc(sizeof(float));
	refresh_names[0] = "90Hz";
	refresh_names[1] = NULL;
	refresh_values[0] = 90.0f;
}

void
QuatToYawPitchRoll(ovrQuatf q, float pitchAdjust, vec3_t out)
{
	XrQuaternionf adjusted = q;
	XrVector3f forwardInVr, rightInVr, upInVr;
	vec3_t forward, right, up;
	float sp, cp_x_cy, cp_x_sy, cp_x_sr, cp_x_cr;
	float yaw, roll, cy, sy, cr, sr, cp;

	if (pitchAdjust != 0.0f)
	{
		XrQuaternionf pitchQuat = {
			sinf(radians(pitchAdjust) * 0.5f), 0.0f, 0.0f,
			cosf(radians(pitchAdjust) * 0.5f)
		};

		adjusted.x = q.w * pitchQuat.x + q.x * pitchQuat.w + q.y * pitchQuat.z - q.z * pitchQuat.y;
		adjusted.y = q.w * pitchQuat.y - q.x * pitchQuat.z + q.y * pitchQuat.w + q.z * pitchQuat.x;
		adjusted.z = q.w * pitchQuat.z + q.x * pitchQuat.y - q.y * pitchQuat.x + q.z * pitchQuat.w;
		adjusted.w = q.w * pitchQuat.w - q.x * pitchQuat.x - q.y * pitchQuat.y - q.z * pitchQuat.z;
	}

	forwardInVr = q2xr_QuatRotateVector(adjusted, (XrVector3f){0.0f, 0.0f, -1.0f});
	rightInVr = q2xr_QuatRotateVector(adjusted, (XrVector3f){1.0f, 0.0f, 0.0f});
	upInVr = q2xr_QuatRotateVector(adjusted, (XrVector3f){0.0f, 1.0f, 0.0f});

	VectorSet(forward, -forwardInVr.z, -forwardInVr.x, forwardInVr.y);
	VectorSet(right, -rightInVr.z, -rightInVr.x, rightInVr.y);
	VectorSet(up, -upInVr.z, -upInVr.x, upInVr.y);
	VectorNormalize(forward);
	VectorNormalize(right);
	VectorNormalize(up);

	sp = -forward[2];
	cp_x_cy = forward[0];
	cp_x_sy = forward[1];
	cp_x_sr = -right[2];
	cp_x_cr = up[2];

	yaw = atan2f(cp_x_sy, cp_x_cy);
	roll = atan2f(cp_x_sr, cp_x_cr);
	cy = cosf(yaw);
	sy = sinf(yaw);
	cr = cosf(roll);
	sr = sinf(roll);

	if (fabsf(cy) > EQUAL_EPSILON)
	{
		cp = cp_x_cy / cy;
	}
	else if (fabsf(sy) > EQUAL_EPSILON)
	{
		cp = cp_x_sy / sy;
	}
	else if (fabsf(sr) > EQUAL_EPSILON)
	{
		cp = cp_x_sr / sr;
	}
	else if (fabsf(cr) > EQUAL_EPSILON)
	{
		cp = cp_x_cr / cr;
	}
	else
	{
		cp = cosf(asinf(sp));
	}

	out[PITCH] = degrees(atan2f(sp, cp));
	out[YAW] = degrees(yaw);
	out[ROLL] = degrees(roll);

	while (out[PITCH] >= 90.0f)
	{
		out[PITCH] -= 180.0f;
	}

	while (out[PITCH] < -90.0f)
	{
		out[PITCH] += 180.0f;
	}

	while (out[YAW] >= 180.0f)
	{
		out[YAW] -= 360.0f;
	}

	while (out[YAW] < -180.0f)
	{
		out[YAW] += 360.0f;
	}

	while (out[ROLL] >= 180.0f)
	{
		out[ROLL] -= 360.0f;
	}

	while (out[ROLL] < -180.0f)
	{
		out[ROLL] += 360.0f;
	}
}

void
setWorldPosition(float x, float y, float z)
{
	vec3_t oldPosition;

	VectorSet(oldPosition, worldPosition[0], worldPosition[1], worldPosition[2]);
	VectorSet(worldPosition, x, y, z);
	VectorSet(positionDeltaThisFrame,
			worldPosition[0] - oldPosition[0],
			worldPosition[1] - oldPosition[1],
			worldPosition[2] - oldPosition[2]);
}

void
setHMDPosition(float x, float y, float z, float yaw)
{
	VectorSet(hmdPosition, -x, y, -z);

	if (!player_moving)
	{
		playerYaw = yaw;
	}
}

void
getVROrigins(vec3_t _weaponoffset, vec3_t _weaponangles, vec3_t _hmdPosition)
{
	VectorCopy(weaponoffset, _weaponoffset);
	VectorCopy(weaponangles, _weaponangles);
	VectorCopy(hmdPosition, _hmdPosition);
}

void
VR_GetMove(float *forward, float *side, float *up, float *yaw, float *pitch, float *roll)
{
	/*
	 * Safety net: only let room-scale/head positional movement drive the player
	 * while on the ground. When airborne, physically leaning into geometry
	 * could otherwise shove the collision box into a wall and wedge the player
	 * mid-jump. Thumbstick (remote_*) air-control is unaffected.
	 */
	qboolean onGround = (cl.frame.playerstate.pmove.pm_flags & PMF_ON_GROUND) != 0;
	float posForward = onGround ? positional_movementForward : 0.0f;
	float posSide = onGround ? positional_movementSideways : 0.0f;

	*forward = remote_movementForward + posForward;
	*side = remote_movementSideways + posSide;
	*up = remote_movementUp;
	*yaw = hmdorientation[YAW] + snapTurn;
	*pitch = hmdorientation[PITCH];
	*roll = hmdorientation[ROLL];
}

/* ------------------------------------------------------------------------- */
/* PC entry points                                                             */
/* ------------------------------------------------------------------------- */

/*
 * Phase one: before Qcommon_Init. Creates the instance and reads the view
 * configuration, which is what makes the eye resolution available to the
 * engine while it is still starting up. Needs no GL context.
 *
 * Failure here is not fatal - it means no headset or no runtime, and the
 * engine carries on as an ordinary flatscreen build.
 */
qboolean
TBXR_InitialiseInstance(void)
{
	if (q2xrInstanceReady)
	{
		return true;
	}

	if (!q2xr_InitInstance())
	{
		Com_Printf("VR: no OpenXR instance - continuing without VR\n");
		return false;
	}

	q2xrInstanceReady = true;
	return true;
}

/*
 * Phase two: after Qcommon_Init, once the window and GL context exist. This is
 * the point their Android build reaches between onSurfaceCreated and the
 * render loop.
 */
qboolean
TBXR_InitialiseSession(void)
{
	if (q2xrInitialised)
	{
		return true;
	}

	if (!q2xrInstanceReady)
	{
		return false;
	}

	if (!q2xr_InitSession())
	{
		Com_Printf("VR: OpenXR session creation failed\n");
		q2xr_DestroyOpenXR();
		q2xrInstanceReady = false;
		return false;
	}

	q2xrInitialised = true;
	oldtime = Sys_Milliseconds();
	Com_Printf("VR: ready\n");
	return true;
}

/*
 * The eye buffer size, for VID_GetModeInfo. Valid as soon as phase one has
 * run; zero before that, which tells the caller to fall back to the desktop
 * resolution.
 */
void
TBXR_GetEyeResolution(int *width, int *height)
{
	*width = gApp.Width;
	*height = gApp.Height;
}

void
TBXR_ShutdownOpenXR(void)
{
	if (!q2xrInitialised && !q2xrInstanceReady)
	{
		return;
	}

	q2xr_DestroyOpenXR();
	q2xrInitialised = false;
	q2xrInstanceReady = false;
}

qboolean
TBXR_IsRunning(void)
{
	return q2xrInitialised;
}
