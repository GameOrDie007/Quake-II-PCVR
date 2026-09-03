"""Desk harness for the smooth/snap turn weapon lag. Apply, measure, revert.

Not part of the build - it edits VrInputDefault.c in place. Revert with git
checkout once the numbers are read.

dbg_turn <deg-per-frame> drives snapTurn as if the stick were held, so a smooth
turn can be reproduced with no controller. Each frame it logs what the weapon
placement is actually built from against what it should be:

    want = snapTurn          (the yaw the view has turned by)
    have = cl.refdef.viewangles[YAW] - hmdorientation[YAW]   (what the gun uses)

If the gun tracks the view, those are equal. The gap is the weapon's angular
error in degrees, which is the bug the owner is describing.
"""
import io, os, sys

ROOT = r"E:\Tools\Games\Quake2VR-741"
F = os.path.join(ROOT, "src/vr/teambeef/VrInputDefault.c")

with io.open(F, "r", encoding="utf-8", newline="") as f:
    s = f.read()

anchor = "\tfloat snapTurnAtEntry = snapTurn;\n"
if s.count(anchor) != 1:
    sys.exit("anchor not found once")

BS = chr(92)
probe = (
    anchor +
    "\n"
    "\t{\t/* TEMPORARY desk harness: dbg_turn */\n"
    "\t\tfloat dbgRate = Cvar_VariableValue(\"dbg_turn\");\n"
    "\t\tif (dbgRate != 0.0f)\n"
    "\t\t{\n"
    "\t\t\tfloat want, have;\n"
    "\t\t\tsnapTurn += dbgRate;\n"
    "\t\t\twhile (snapTurn > 180.0f) snapTurn -= 360.0f;\n"
    "\t\t\twhile (snapTurn < -180.0f) snapTurn += 360.0f;\n"
    "\t\t\twant = snapTurn;\n"
    "\t\t\thave = cl.refdef.viewangles[YAW] - hmdorientation[YAW];\n"
    "\t\t\twhile (have - want > 180.0f) have -= 360.0f;\n"
    "\t\t\twhile (want - have > 180.0f) have += 360.0f;\n"
    "\t\t\tCom_Printf(\"DBGTURN want %8.2f have %8.2f err %8.2f  vang %8.2f hmd %8.2f\" + BS + \"n\",\n"
    "\t\t\t\twant, have, have - want, cl.refdef.viewangles[YAW], hmdorientation[YAW]);\n"
    "\t\t}\n"
    "\t}\n"
)
probe = probe.replace('" + BS + "', BS)

s = s.replace(anchor, probe, 1)

with io.open(F, "w", encoding="utf-8", newline="") as f:
    f.write(s)
print("turn harness added")
