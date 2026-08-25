/*
 * Desktop replacement for Quake2Quest's VrCommon.h.
 *
 * Quake2Quest, Copyright (C) Simon Brown / Team Beef, GPLv2.
 * This file is a PCVR platform header written for that code to compile
 * against; the types, constants and symbol names are theirs so that
 * VrInputCommon.c, VrInputDefault.c, mathlib.c and matrixlib.c can be used
 * unmodified.
 *
 * Their original header pulls in <android/log.h>, <jni.h>, EGL and GLES3. The
 * input layer needs none of those - it needs the tracking types, the button
 * constants and the shared globals, all of which are already expressed in
 * OpenXR terms because Quake2Quest is itself an OpenXR port. That is what
 * makes this a platform swap rather than a rewrite.
 *
 * Anything declared here is defined either in their files or in
 * vr_teambeef_bridge.c, which is the only new code in the port.
 */

#if !defined(vrcommon_h)
#define vrcommon_h

#include <stdbool.h>
#include <stdint.h>

/* Team Beef's VrInputCommon.c spells this the BSD/Android way. MinGW has no
   such typedef, so the declaration below failed to parse and every caller of
   handleTrackedControllerButton then looked undeclared. */
#if !defined(__ANDROID__) && !defined(__GLIBC__)
typedef uint32_t u_int32_t;
#endif

#include <openxr/openxr.h>

#include "../../common/header/shared.h"
#include "mathlib.h"

/* MSVC has no u_int32_t; their signatures use it. */
#ifdef _MSC_VER
typedef unsigned int u_int32_t;
#endif

#define NUM_EYES 2

typedef struct {
	XrVector3f Position;
	XrQuaternionf Orientation;
} q2xrPose;

typedef struct {
	q2xrPose Pose;
} q2xrHeadPose;

typedef struct {
	q2xrHeadPose HeadPose;
	XrSpaceVelocity Velocity;
	bool Active;
} ovrTracking;

typedef XrQuaternionf ovrQuatf;

typedef struct {
	float x;
	float y;
} ovrVector2f;

typedef struct {
	uint32_t Buttons;
	uint32_t Touches;
	float IndexTrigger;
	float GripTrigger;
	ovrVector2f Joystick;
} ovrInputStateTrackedRemote;

typedef uint64_t ovrDeviceID;

#define ovrButton_A             0x00000001u
#define ovrButton_B             0x00000002u
#define ovrButton_RThumb        0x00000004u
#define ovrButton_X             0x00000100u
#define ovrButton_Y             0x00000200u
#define ovrButton_LThumb        0x00000400u
#define ovrButton_Enter         0x00100000u
#define ovrButton_GripTrigger   0x04000000u
#define ovrButton_Trigger       0x20000000u
#define ovrButton_Joystick      0x80000000u
#define ovrTouch_ThumbRest      0x00000010u

#define XR_DEVICE_TYPE_GENERIC  0
#define XR_DEVICE_TYPE_META     1
#define XR_DEVICE_TYPE_PICO     2

extern bool quake2_initialised;

extern long long global_time;

extern float playerHeight;
extern float playerYaw;

extern bool showingScreenLayer;

extern vec3_t worldPosition;

extern vec3_t hmdPosition;
extern vec3_t hmdorientation;
extern vec3_t positionDeltaThisFrame;

extern vec3_t weaponangles;
extern vec3_t weaponoffset;

extern vec3_t flashlightangles;
extern vec3_t flashlightoffset;

#define DUCK_NOTDUCKED 0
#define DUCK_BUTTON 1
#define DUCK_CROUCHED 2
extern int ducked;

extern bool player_moving;

float radians(float deg);
float degrees(float rad);
qboolean isMultiplayer(void);
double GetTimeInMilliSeconds(void);
float length(float x, float y);
float nonLinearFilter(float in);
bool between(float min, float val, float max);
void rotateAboutOrigin(float v1, float v2, float rotation, vec2_t out);
void QuatToYawPitchRoll(ovrQuatf q, float pitchAdjust, vec3_t out);
bool useScreenLayer(void);
void handleTrackedControllerButton(u_int32_t buttonsNew, u_int32_t buttonsOld, uint32_t button, int key);

#endif /* vrcommon_h */
