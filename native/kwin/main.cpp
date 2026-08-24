// SPDX-License-Identifier: GPL-3.0-or-later

#include "radiantcursoreffect.h"

namespace KWin
{

KWIN_EFFECT_FACTORY(RadiantCursorEffect, "metadata.json")

} // namespace KWin

// KWIN_EFFECT_FACTORY expands to a QObject plugin factory in this .cpp file.
// Its generated meta-object code must be compiled into the shared object;
// otherwise KWin sees an ordinary library and cannot discover the effect.
#include "main.moc"
