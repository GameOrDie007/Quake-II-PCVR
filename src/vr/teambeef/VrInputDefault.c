/************************************************************************************

Filename	:	VrInputDefault.c
Content		:	Handles default controller input
Created		:	August 2019
Authors		:	Simon Brown

*************************************************************************************/

#include "VrInput.h"
#include "VrCvars.h"

#include "../../client/header/client.h"

extern cvar_t	*cl_forwardspeed;
extern cvar_t	*cl_run;
cvar_t	*sv_cheats;
extern cvar_t	*vr_weapon_stabilised;
extern cvar_t	*vr_use_wheels;
qboolean draw_item_wheel;
qboolean isItems;
vec2_t polarCursor;
int segment;



void HandleInput_Default( ovrInputStateTrackedRemote *pDominantTrackedRemoteNew, ovrInputStateTrackedRemote *pDominantTrackedRemoteOld, ovrTracking* pDominantTracking,
                          ovrInputStateTrackedRemote *pOffTrackedRemoteNew, ovrInputStateTrackedRemote *pOffTrackedRemoteOld, ovrTracking* pOffTracking,
                          int domButton1, int domButton2, int offButton1, int offButton2 )

{
	//Ensure handedness is set correctly
	Cvar_Set("hand", vr_control_scheme->value < 10 ? "0" : "1");

    //All this to allow stick and button switching!
    ovrVector2f primaryJoystickNew;
    ovrVector2f primaryJoystickOld;
    ovrVector2f secondaryJoystickNew;
    ovrVector2f secondaryJoystickOld;
    uint32_t primaryButtonsNew;
    uint32_t primaryButtonsOld;
    uint32_t secondaryButtonsNew;
    uint32_t secondaryButtonsOld;
    uint32_t primaryTouchesNew;
    uint32_t primaryTouchesOld;
    uint32_t secondaryTouchesNew;
    uint32_t secondaryTouchesOld;
    int primaryButton1;
    int primaryButton2;
    int secondaryButton1;
    int secondaryButton2;

    if (vr_control_scheme->value == 11) // Left handed (swtiched sticks)
    {
        primaryJoystickNew = pOffTrackedRemoteNew->Joystick;
        primaryJoystickOld = pOffTrackedRemoteOld->Joystick;
        secondaryJoystickNew = pDominantTrackedRemoteNew->Joystick;
        secondaryJoystickOld = pDominantTrackedRemoteOld->Joystick;

        primaryButtonsNew = pOffTrackedRemoteNew->Buttons;
        primaryButtonsOld = pOffTrackedRemoteOld->Buttons;
        secondaryButtonsNew = pDominantTrackedRemoteNew->Buttons;
        secondaryButtonsOld = pDominantTrackedRemoteOld->Buttons;

        primaryTouchesNew = pDominantTrackedRemoteNew->Touches;
        primaryTouchesOld = pDominantTrackedRemoteOld->Touches;

        secondaryTouchesNew = pOffTrackedRemoteNew->Touches;
        secondaryTouchesOld = pOffTrackedRemoteOld->Touches;

        primaryButton1 = offButton1;
        primaryButton2 = offButton2;
        secondaryButton1 = domButton1;
        secondaryButton2 = domButton2;
    }
    else // Left and right handed
    {
        primaryJoystickNew = pDominantTrackedRemoteNew->Joystick;
        primaryJoystickOld = pDominantTrackedRemoteOld->Joystick;
        secondaryJoystickNew = pOffTrackedRemoteNew->Joystick;
        secondaryJoystickOld = pOffTrackedRemoteOld->Joystick;

        primaryButtonsNew = pDominantTrackedRemoteNew->Buttons;
        primaryButtonsOld = pDominantTrackedRemoteOld->Buttons;
        secondaryButtonsNew = pOffTrackedRemoteNew->Buttons;
        secondaryButtonsOld = pOffTrackedRemoteOld->Buttons;

        primaryTouchesNew = pDominantTrackedRemoteNew->Touches;
        primaryTouchesOld = pDominantTrackedRemoteOld->Touches;

        secondaryTouchesNew = pOffTrackedRemoteNew->Touches;
        secondaryTouchesOld = pOffTrackedRemoteOld->Touches;

        primaryButton1 = domButton1;
        primaryButton2 = domButton2;
        secondaryButton1 = offButton1;
        secondaryButton2 = offButton2;
    }

	//Get the cvar
    sv_cheats = Cvar_Get("cheats", "0", CVAR_ARCHIVE);

	/* snapTurn as it stands on entry, i.e. the value the weapon placement below
	   is effectively built against. Compared at the end of this function to
	   correct the one-frame snap-turn lag; see there. */
	float snapTurnAtEntry = snapTurn;

    static qboolean dominantGripPushed = false;
	static float dominantGripPushTime = 0.0f;
    static qboolean inventoryManagementMode = false;

	//Menu button - _can_ appear on either controller if user has switched them in the oculus menu
	handleTrackedControllerButton(primaryButtonsNew, primaryButtonsOld, ovrButton_Enter, K_ESCAPE);
	handleTrackedControllerButton(secondaryButtonsNew, secondaryButtonsOld, ovrButton_Enter, K_ESCAPE);

    /*
     * A cinematic or the attract demo is not gameplay, so treat the controller
     * as a menu controller there as well.
     *
     * Only the branch below turns a button into a Key_Event. In gameplay Team
     * Beef send the dominant trigger out as a "+attack" console command and A
     * as "+movedown", and a console command never reaches Key_Event - so on PC
     * only B, which is K_SPACE, could break the attract loop into the menu
     * (cl_keyboard.c turns any key into K_ESCAPE there) or dismiss the startup
     * id movie. The trigger appeared to work on an in-game cutscene only
     * because +attack sets BUTTON_ATTACK, which CL_SendCmd checks - and that
     * path needs a connection, which the startup movie has not got.
     *
     * The reported symptom was three-sided and this is the one cause: A did not
     * skip the id movie, A did not skip the opening cutscene, and the main
     * menu came up for B but not for A or the trigger.
     *
     * Losing gameplay input for the duration costs nothing, because there is no
     * gameplay to lose.
     */
    if (cls.key_dest == key_menu || cl.attractloop || cl.cinematictime > 0)
    {
        //Allow both sticks and all buttons to work in the menu
        {
            int leftJoyState = (secondaryJoystickNew.x > 0.7f ? 1 : 0);
            if (leftJoyState != (secondaryJoystickOld.x > 0.7f ? 1 : 0)) {
                Key_Event(K_RIGHTARROW, leftJoyState, global_time);
            }
            leftJoyState = (secondaryJoystickNew.x < -0.7f ? 1 : 0);
            if (leftJoyState != (secondaryJoystickOld.x < -0.7f ? 1 : 0)) {
                Key_Event(K_LEFTARROW, leftJoyState, global_time);
            }
            leftJoyState = (secondaryJoystickNew.y < -0.7f ? 1 : 0);
            if (leftJoyState != (secondaryJoystickOld.y < -0.7f ? 1 : 0)) {
                Key_Event(K_DOWNARROW, leftJoyState, global_time);
            }
            leftJoyState = (secondaryJoystickNew.y > 0.7f ? 1 : 0);
            if (leftJoyState != (secondaryJoystickOld.y > 0.7f ? 1 : 0)) {
                Key_Event(K_UPARROW, leftJoyState, global_time);
            }
        }
        {
            int leftJoyState = (primaryJoystickNew.x > 0.7f ? 1 : 0);
            if (leftJoyState != (primaryJoystickOld.x > 0.7f ? 1 : 0)) {
                Key_Event(K_RIGHTARROW, leftJoyState, global_time);
            }
            leftJoyState = (primaryJoystickNew.x < -0.7f ? 1 : 0);
            if (leftJoyState != (primaryJoystickOld.x < -0.7f ? 1 : 0)) {
                Key_Event(K_LEFTARROW, leftJoyState, global_time);
            }
            leftJoyState = (primaryJoystickNew.y < -0.7f ? 1 : 0);
            if (leftJoyState != (primaryJoystickOld.y < -0.7f ? 1 : 0)) {
                Key_Event(K_DOWNARROW, leftJoyState, global_time);
            }
            leftJoyState = (primaryJoystickNew.y > 0.7f ? 1 : 0);
            if (leftJoyState != (primaryJoystickOld.y > 0.7f ? 1 : 0)) {
                Key_Event(K_UPARROW, leftJoyState, global_time);
            }
        }

        handleTrackedControllerButton(primaryButtonsNew, primaryButtonsOld, primaryButton1, K_ENTER);
        handleTrackedControllerButton(primaryButtonsNew, primaryButtonsOld, ovrButton_Trigger, K_ENTER);
        handleTrackedControllerButton(primaryButtonsNew, primaryButtonsOld, primaryButton2, K_ESCAPE);
        handleTrackedControllerButton(secondaryButtonsNew, secondaryButtonsOld, secondaryButton1, K_ENTER);
        handleTrackedControllerButton(secondaryButtonsNew, secondaryButtonsOld, ovrButton_Trigger, K_ENTER);
        handleTrackedControllerButton(secondaryButtonsNew, secondaryButtonsOld, secondaryButton2, K_ESCAPE);
    }
    else
    {
        float distance = sqrtf(powf(pOffTracking->HeadPose.Pose.Position.x - pDominantTracking->HeadPose.Pose.Position.x, 2) +
                               powf(pOffTracking->HeadPose.Pose.Position.y - pDominantTracking->HeadPose.Pose.Position.y, 2) +
                               powf(pOffTracking->HeadPose.Pose.Position.z - pDominantTracking->HeadPose.Pose.Position.z, 2));

        //Turn on weapon stabilisation?
        if ((pOffTrackedRemoteNew->Buttons & ovrButton_GripTrigger) !=
            (pOffTrackedRemoteOld->Buttons & ovrButton_GripTrigger)) {

            if (pOffTrackedRemoteNew->Buttons & ovrButton_GripTrigger)
            {
                if (distance < 0.50f)
                {
                    Cvar_ForceSet("vr_weapon_stabilised", "1.0");
                }
            }
            else
            {
                Cvar_ForceSet("vr_weapon_stabilised", "0.0");
            }
        }

        //dominant hand stuff first
        {
			///Weapon location relative to view
            weaponoffset[0] = pDominantTracking->HeadPose.Pose.Position.x - hmdPosition[0];
            weaponoffset[1] = pDominantTracking->HeadPose.Pose.Position.y - hmdPosition[1];
            weaponoffset[2] = pDominantTracking->HeadPose.Pose.Position.z - hmdPosition[2];

			{
				vec2_t v;
				rotateAboutOrigin(-weaponoffset[0], weaponoffset[2], (cl.refdef.viewangles[YAW] - hmdorientation[YAW]), v);
				weaponoffset[0] = v[0];
				weaponoffset[2] = v[1];
			}

            //Set gun angles - We need to calculate all those we might need (including adjustments) for the client to then take its pick
            const ovrQuatf quatRemote = pDominantTracking->HeadPose.Pose.Orientation;
            QuatToYawPitchRoll(quatRemote, vr_weapon_pitchadjust->value, weaponangles);
            weaponangles[YAW] += (cl.refdef.viewangles[YAW] - hmdorientation[YAW]);
            weaponangles[ROLL] *= -1.0f;


            if (vr_weapon_stabilised->value == 1.0f)
            {
                float z = pOffTracking->HeadPose.Pose.Position.z - pDominantTracking->HeadPose.Pose.Position.z;
                float x = pOffTracking->HeadPose.Pose.Position.x - pDominantTracking->HeadPose.Pose.Position.x;
                float y = pOffTracking->HeadPose.Pose.Position.y - pDominantTracking->HeadPose.Pose.Position.y;
                float zxDist = length(x, z);

                if (zxDist != 0.0f && z != 0.0f) {
                    VectorSet(weaponangles, -degrees(atanf(y / zxDist)), (cl.refdef.viewangles[YAW] - hmdorientation[YAW]) - degrees(atan2f(x, -z)), weaponangles[ROLL]);
                }
            }

            if (vr_use_wheels->value > 0) {
                {
                    // weapon selection wheel
                    if ((secondaryButtonsNew & secondaryButton2) !=
                        (secondaryButtonsOld & secondaryButton2)) {
                        sendButtonActionSimple("inven");
                        inventoryManagementMode = (secondaryButtonsNew & secondaryButton2) > 0;
                    }
                    static qboolean active = false;
                    static qboolean touching = false;
                    static qboolean runTouchLogic = false;
                    static int totalIcons;
                    static int t_rel_t;
                    static vec3_t initialAngles;
                    static const wheel_icon_t *iconList;
                    ovrTracking *activeController;
                    vec3_t currentAngles;
                    vec2_t relativeAngles;

                    if ((primaryButtonsNew & ovrButton_GripTrigger) &&
                        !(secondaryButtonsNew & ovrButton_GripTrigger)) {
                        activeController = pDominantTracking; // regardless of handedness or stick assignment, makes sense to use the weapon hand for the weapon wheel
                        isItems = false;
                        iconList = CL_WheelIcons(isItems, &totalIcons); // was weaponIcons and a hardcoded 11 - the mission packs have their own
                        active = true;
                    } else if ((primaryJoystickNew.y < -0.9f) &&
                               !(secondaryJoystickNew.y < -0.9f)) {
                        activeController = pDominantTracking; // conversely, makes sense to use the off hand for the item wheel
                        isItems = true;
                        iconList = CL_WheelIcons(isItems, &totalIcons); // was itemIcons and a hardcoded 6
                        active = true;
                    } else {
                        active = false;
                    }
                    if (active) {
                        if (!touching) {
                            draw_item_wheel = true;
                            sendButtonActionSimple(
                                    "inven"); // send the "inven" command to force cl.inventory to be populated
                            QuatToYawPitchRoll(activeController->HeadPose.Pose.Orientation, 0.0f,
                                               initialAngles);
                            relativeAngles[0] = initialAngles[0];
                            relativeAngles[1] = initialAngles[1];
                            touching = true;
                        } else {
                            QuatToYawPitchRoll(activeController->HeadPose.Pose.Orientation, 0.0f,
                                               currentAngles);
                            relativeAngles[0] = initialAngles[1] -
                                                currentAngles[1]; // relative x -> pitch | Inverted to make right = positive
                            relativeAngles[1] = currentAngles[0] -
                                                initialAngles[0]; // relative y -> yaw | to match display coordinates, down = positive
                            polarCursor[0] = sqrtf(
                                    powf(relativeAngles[0], 2.0f) +
                                    powf(relativeAngles[1], 2.0f)); // r
                            if (polarCursor[0] > 15)
                                polarCursor[0] = 15; // to keep it within the ring
                            polarCursor[1] = atan2f(relativeAngles[1], relativeAngles[0]); // theta
                            segment =
                                    (int) (((polarCursor[1] + (M_PI / totalIcons)) + (2.5 * M_PI)) *
                                           (totalIcons / (2 * M_PI))) %
                                    totalIcons; // Top segment index = 0, clockwise up to 10

                            /*float th = M_PI/-2;
                            int r = 160;
                            float factor = M_PI * 2/6;
                            for(int i = 0; i < 6; i++){
                                int x, y;
                                x = r * cosf(th + (i * factor));
                                y = r * sinf(th + (i * factor));
                                ALOGV("     segment %i: x: %i y=%i", i, x, y);
                            }*/ // this snippet generated precalculated coordinates for each icon, relative to the ring center
                            // I'll leave it here in case those values need to be calculated again

                        }
                    } else {
                        if (touching) {
                            sendButtonActionSimple(
                                    "inven"); // send the "inven" command again to close the inventory
                            touching = false;
                            runTouchLogic = true;
                            t_rel_t = Sys_Milliseconds();
                            if (polarCursor[0] > 8) {
                                char useCom[50];
                                snprintf(useCom, sizeof(useCom), "use %s", iconList[segment].command);
                                sendButtonActionSimple(useCom);
                            }
                        }
                        // give it time for the "inven" commmand to be sent,
                        // preventing the "invisible" inventory window to be shown for a little bit
                        if (runTouchLogic && Sys_Milliseconds() - t_rel_t > 100) {
                            draw_item_wheel = false;
                            runTouchLogic = false;
                            polarCursor[0] = 0;
                        }
                    }
                }
            }
        }

        float controllerYawHeading = 0.0f;
        //off-hand stuff
        {
            flashlightoffset[0] = pOffTracking->HeadPose.Pose.Position.x - hmdPosition[0];
            flashlightoffset[1] = pOffTracking->HeadPose.Pose.Position.y - hmdPosition[1];
            flashlightoffset[2] = pOffTracking->HeadPose.Pose.Position.z - hmdPosition[2];

			vec2_t v;
			rotateAboutOrigin(-flashlightoffset[0], flashlightoffset[2], (cl.refdef.viewangles[YAW] - hmdorientation[YAW]), v);
			flashlightoffset[0] = v[0];
			flashlightoffset[2] = v[1];

            QuatToYawPitchRoll(pOffTracking->HeadPose.Pose.Orientation, 15.0f, flashlightangles);

            flashlightangles[YAW] += (cl.refdef.viewangles[YAW] - hmdorientation[YAW]);

			if (vr_walkdirection->value == 0) {
				controllerYawHeading = -cl.refdef.viewangles[YAW] + flashlightangles[YAW];
			}
			else
			{
				controllerYawHeading = 0.0f;//-cl.viewangles[YAW];
			}
        }

        {
            //This section corrects for the fact that the controller actually controls direction of movement, but we want to move relative to the direction the
            //player is facing for positional tracking
            float positionalFactor = (hmdType == XR_DEVICE_TYPE_META)  ? 2500 : 2000;
            // Cancel out the x1.5 run boost that CL_BaseMove applies so room-scale (6DoF)
            // movement always covers the walking distance. Run is active when the speed key
            // (off-hand trigger) is held XOR'd with the always-run cvar - mirror that exactly,
            // otherwise toggling always-run makes head movement carry the player too far.
            qboolean speedHeld = (pOffTrackedRemoteNew->Buttons & ovrButton_Trigger) != 0;
            qboolean running = (speedHeld ? 1 : 0) ^ ((cl_run && cl_run->value != 0) ? 1 : 0);
            float multiplier = positionalFactor / (cl_forwardspeed->value *
					(running ? 1.5f : 1.0f));

            vec2_t v;
            rotateAboutOrigin(-positionDeltaThisFrame[0] * multiplier,
                              positionDeltaThisFrame[2] * multiplier, /*cl.refdef.viewangles[YAW]*/ - hmdorientation[YAW], v);
            positional_movementSideways = v[0];
            positional_movementForward = v[1];

            //Jump (B Button)
            handleTrackedControllerButton(primaryButtonsNew,
                                          primaryButtonsOld, primaryButton2, K_SPACE);

            //We need to record if we have started firing primary so that releasing trigger will stop firing, if user has pushed grip
            //in meantime, then it wouldn't stop the gun firing and it would get stuck
            static bool firingPrimary = false;

			{
				//Fire Primary
				if ((pDominantTrackedRemoteNew->Buttons & ovrButton_Trigger) !=
					(pDominantTrackedRemoteOld->Buttons & ovrButton_Trigger)) {

					firingPrimary = (pDominantTrackedRemoteNew->Buttons & ovrButton_Trigger);

					if (inventoryManagementMode)
                    {
                        if (firingPrimary)
                            sendButtonActionSimple("invuse");
                    }
                    else
                    {
                        sendButtonAction("+attack", firingPrimary);
                    }
				}
			}

            //Duck with A
            if ((primaryButtonsNew & primaryButton1) !=
                (primaryButtonsOld & primaryButton1) &&
                ducked != DUCK_CROUCHED) {
                ducked = (primaryButtonsNew & primaryButton1) ? DUCK_BUTTON : DUCK_NOTDUCKED;
                sendButtonAction("+movedown", (primaryButtonsNew & primaryButton1));
            }

			//Weapon/Inventory Chooser
			static qboolean itemSwitched = false;
			if (between(-0.2f, primaryJoystickNew.x, 0.2f) &&
				(between(0.8f, primaryJoystickNew.y, 1.0f) ||
				 between(-1.0f, primaryJoystickNew.y, -0.8f)))
			{
				if (!itemSwitched && vr_use_wheels->value == 0) {
					if (between(0.8f, primaryJoystickNew.y, 1.0f))
					{
					    if (inventoryManagementMode)
                        {
                            sendButtonActionSimple("invprev");
                        }
                        else
                        {
                            sendButtonActionSimple("weapprev");
					    }
						
					}
					else
					{
                        if (inventoryManagementMode)
                        {
                            sendButtonActionSimple("invnext");
                        } 
                        else
                        {
                            sendButtonActionSimple("weapnext");
                        }
					}
					itemSwitched = true;
				}
			} else {
				itemSwitched = false;
			}
        }

        {
			//Laser-sight
            if ((pDominantTrackedRemoteNew->Buttons & ovrButton_Joystick) !=
                (pDominantTrackedRemoteOld->Buttons & ovrButton_Joystick)
                && (pDominantTrackedRemoteNew->Buttons & ovrButton_Joystick))
            {
                float current = vr_lasersight->value;

                if (current == 0.0f)
                {
                    Cvar_ForceSet("vr_lasersight", "1.0");
                }
                else if (current == 1.0f)
                {
                    Cvar_ForceSet("vr_lasersight", "2.0");
                }
                else
                {
                    Cvar_ForceSet("vr_lasersight", "0.0");
                }
            }

			//Apply a filter and quadratic scaler so small movements are easier to make
			float dist = length(secondaryJoystickNew.x, secondaryJoystickNew.y);
			float nlf = nonLinearFilter(dist);
            float x = nlf * secondaryJoystickNew.x;
            float y = nlf * secondaryJoystickNew.y;

            player_moving = (fabs(x) + fabs(y)) > 0.05f;

			//Adjust to be off-hand controller oriented
            vec2_t v;
            rotateAboutOrigin(x, y, controllerYawHeading, v);

            remote_movementSideways = v[0];
            remote_movementForward = v[1];


            //show help computer while X/A pressed
            if ((secondaryButtonsNew & secondaryButton1) !=
                 (secondaryButtonsOld & secondaryButton1)) {
                sendButtonActionSimple("cmd help");
            }


            //Use (Action)
            if ((pOffTrackedRemoteNew->Buttons & ovrButton_Joystick) !=
                (pOffTrackedRemoteOld->Buttons & ovrButton_Joystick)
                && (pOffTrackedRemoteNew->Buttons & ovrButton_Joystick)) {

            }

            //We need to record if we have started firing primary so that releasing trigger will stop definitely firing, if user has pushed grip
            //in meantime, then it wouldn't stop the gun firing and it would get stuck

			//Run
			handleTrackedControllerButton(pOffTrackedRemoteNew->Buttons,
                                          pOffTrackedRemoteOld->Buttons,
										  ovrButton_Trigger, K_SHIFT);

            static qboolean snapping = true;
            static float turn_rate;
            if (vr_snapturn_angle->value > 10.0f){ // snap turning
                if (primaryJoystickNew.x > 0.6f)
                {
                    if (snapping)
                    {
                        snapTurn -= vr_snapturn_angle->value;
                        snapping = false;

                        if (snapTurn < -180.0f)
                        {
                            snapTurn += 360.f;
                        }
                    }
                }
                else if (primaryJoystickNew.x < -0.6f)
                {
                    if (snapping)
                    {
                        snapTurn += vr_snapturn_angle->value;
                        snapping = false;

                        if (snapTurn > 180.0f)
                        {
                            snapTurn -= 360.f;
                        }
                    }
                } else
                {
                    snapping = true;
                }
            }else{ // continuous turning
                turn_rate = vr_snapturn_angle->value < 1.0f ? 1.0f : vr_snapturn_angle->value;
                if(fabsf(primaryJoystickNew.x) > vr_turn_deadzone->value) {
                    snapTurn -= (10.0f * primaryJoystickNew.x) / turn_rate;
                }
            }
        }
    }

	/*
	 * Snap-turn correction. The weapon is placed near the top of this function
	 * using cl.refdef.viewangles, which the client last wrote while rendering the
	 * previous frame - but snapTurn is not updated until the joystick handling
	 * further down. So on the frame a snap turn happens, the view rotates by
	 * vr_snapturn_angle while the weapon still carries the old rotation, and the
	 * gun appears thrown out to one side for exactly one frame before it catches
	 * up. Turning right shows it on the left.
	 *
	 * This is in their code too - the ordering is the same on Quest - and at
	 * 72Hz it is a single-frame flash. It reads as a glitch rather than as
	 * latency, so it is corrected here rather than reproduced.
	 *
	 * Snap turning only. Smooth turning changes snapTurn a little on every
	 * frame rather than jumping once, so there is no discontinuity to cancel -
	 * and applying the correction anyway made the weapon fight the view
	 * instead, leaving it hanging in place through a continuous turn. The
	 * artifact this fixes is specifically the single-frame jump of a snap.
	 *
	 * Which mode is running is decided by vr_snapturn_angle, above: over 10
	 * degrees is a snap, at or under it is continuous. This used to test
	 * vr_smoothturn instead, which is ours rather than Team Beef's and is only
	 * the Options page's display flag - it decides which control that page
	 * shows, not how the engine turns. The two agree as long as turning is only
	 * ever changed through that page, so the gate worked by coincidence rather
	 * than by construction, and anything that set one without the other put the
	 * correction into the wrong mode: cancelling a snap that never happened
	 * through a continuous turn, which is the weapon hanging in the air, or
	 * leaving a real snap uncancelled, which is the weapon thrown aside for a
	 * frame. Asking the same question the turning code asks cannot drift.
	 *
	 * The delta is measured strictly within this call - snapTurn on entry
	 * against snapTurn once the joystick handling has finished - so it is the
	 * amount the view is about to rotate that the weapon has not accounted for.
	 *
	 * An earlier attempt derived it from (cl.refdef.viewangles[YAW] -
	 * hmdorientation[YAW]) instead, on the assumption that this equals
	 * snapTurn. It does not, so a bogus rotation was applied on every frame
	 * rather than only on turns, and the gun ended up pointing at the player.
	 * Measuring the in-frame change assumes nothing about what viewangles
	 * holds, and is provably inert when no turn happens: if the sticks are
	 * untouched the delta is exactly zero and their tuned placement is
	 * arithmetically unchanged.
	 */
	if (vr_snapturn_angle->value > 10.0f)
	{
		float delta = snapTurn - snapTurnAtEntry;

		while (delta > 180.0f) delta -= 360.0f;
		while (delta < -180.0f) delta += 360.0f;

		if (fabsf(delta) > 0.01f)
		{
			vec2_t v;

			rotateAboutOrigin(-weaponoffset[0], weaponoffset[2], delta, v);
			weaponoffset[0] = v[0];
			weaponoffset[2] = v[1];
			weaponangles[YAW] += delta;
		}
	}
}
