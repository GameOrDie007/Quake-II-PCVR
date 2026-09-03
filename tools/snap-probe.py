"""Desk harness for the snap-turn weapon flick. Apply, measure, revert.

Not part of the build - it edits VrInputDefault.c in place. Revert with
    git checkout -- src/vr/teambeef/VrInputDefault.c
once the numbers are read.

With `dbg_snap 1`, every 60th frame the harness forces the turn stick hard over
for a single frame, alternating right and left, so the *real* snap code runs -
latch, wrap, correction and all. Nothing is reimplemented; only the stick is
synthesised, which is the one thing the desk cannot supply.

Every frame it logs, in the order the frame actually does things:

    place = the body yaw the weapon was built from, captured at placement time
            (cl.refdef.viewangles[YAW] - hmdorientation[YAW])
    snap  = snapTurn once the joystick handling has run
    corr  = the delta the end-of-function correction applied, 0 if it did not
    view  = cl.refdef.viewangles[YAW] as it stands at the end of the frame
    hmd   = hmdorientation[YAW]

The weapon's final body yaw is place+corr. The view's is view-hmd, but read one
frame later, because the render that writes cl.refdef.viewangles happens after
this function returns. So compare place+corr on frame N against view-hmd on
frame N+1. If they agree the gun is where the view is; the gap is the flick.
"""
import io, os, sys

ROOT = r"E:\Tools\Games\Quake2VR-741"
F = os.path.join(ROOT, "src/vr/teambeef/VrInputDefault.c")

with io.open(F, "r", encoding="utf-8", newline="") as f:
    s = f.read()

BS = chr(92)
NL = '"' + BS + 'n"'

# 1. State, and capture of the body yaw at placement time.
anchor1 = "\tfloat snapTurnAtEntry = snapTurn;\n"
if s.count(anchor1) != 1:
    sys.exit("entry anchor not found once")

decl = (
    anchor1 +
    "\n"
    "\t/* TEMPORARY desk harness: dbg_snap */\n"
    "\tstatic int dbgFrame = 0;\n"
    "\tstatic int dbgDir = 1;\n"
    "\tfloat dbgPlace = 0.0f;\n"
    "\tfloat dbgForce = 0.0f;\n"
    "\tconst qboolean dbgOn = (Cvar_VariableValue(\"dbg_snap\") != 0.0f);\n"
    "\tdbgFrame++;\n"
    "\tif (dbgOn && (dbgFrame %% 60) == 0)\n"
    "\t{\n"
    "\t\tdbgDir = -dbgDir;\n"
    "\t\tdbgForce = (dbgDir > 0) ? 0.7f : -0.7f;\n"
    "\t}\n"
).replace("%%", "%")

s = s.replace(anchor1, decl, 1)

# 2. Record the body yaw the weapon placement actually used.
anchor2 = ("\t\t\t\trotateAboutOrigin(-weaponoffset[0], weaponoffset[2], "
           "(cl.refdef.viewangles[YAW] - hmdorientation[YAW]), v);\n")
if s.count(anchor2) != 1:
    sys.exit("placement anchor not found once")
s = s.replace(anchor2,
              "\t\t\t\tdbgPlace = cl.refdef.viewangles[YAW] - hmdorientation[YAW];\n" + anchor2,
              1)

# 3. Drive the real snap code with a synthetic stick.
anchor3 = "            if (vr_snapturn_angle->value > 10.0f){ // snap turning\n"
if s.count(anchor3) != 1:
    sys.exit("turn anchor not found once")
s = s.replace(anchor3,
              "            if (dbgForce != 0.0f) { primaryJoystickNew.x = dbgForce; }\n" + anchor3,
              1)

# 4. Log at the very end, after the correction has run.
anchor4 = "\tif (vr_snapturn_angle->value > 10.0f)\n\t{\n\t\tfloat delta = snapTurn - snapTurnAtEntry;\n"
if s.count(anchor4) != 1:
    sys.exit("correction anchor not found once")

logged = anchor4.replace("\t\tfloat delta = snapTurn - snapTurnAtEntry;\n",
                         "\t\tfloat delta = snapTurn - snapTurnAtEntry;\n"
                         "\t\tdbgCorr = delta;\n")
s = s.replace(anchor4, logged, 1)

# dbgCorr has to exist before the correction block.
s = s.replace("\tconst qboolean dbgOn =",
              "\tfloat dbgCorr = 0.0f;\n\tconst qboolean dbgOn =", 1)

tail = "\t\t\tweaponangles[YAW] += delta;\n\t\t}\n\t}\n}"
if s.count(tail) != 1:
    sys.exit("tail anchor not found once")

log = (
    "\t\t\tweaponangles[YAW] += delta;\n"
    "\t\t}\n"
    "\t}\n"
    "\n"
    "\tif (dbgOn)\n"
    "\t{\n"
    "\t\tCom_Printf(\"DBGSNAP f%d place %8.2f snap %8.2f corr %8.2f wx %8.4f wz %8.4f gun %8.2f" + BS + "n\",\n"
    "\t\t\t\tdbgFrame, dbgPlace, snapTurn, dbgCorr,\n"
    "\t\t\t\tweaponoffset[0], weaponoffset[2], dbgPlace + dbgCorr);\n"
    "\t}\n"
    "}"
)
s = s.replace(tail, log, 1)

with io.open(F, "w", encoding="utf-8", newline="") as f:
    f.write(s)
print("snap harness added")
