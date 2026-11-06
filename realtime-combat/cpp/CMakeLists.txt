# The mod's C++ for wcmods' own build, `python wcmods.py cpp build <this mod's folder>`, which makes dlls/main.dll the
# same way the Visual Studio solution beside this folder does (wcmods' docs/SDK.md, "C++ mods").
#
# A new .cpp goes in this list and in realtime-combat.vcxproj (Source Files), so both builds see it.
wcmods_mod(${WCMODS_TARGET}
    src/main.cpp
)
