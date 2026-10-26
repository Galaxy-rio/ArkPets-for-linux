# material-components-qml

Source: https://github.com/sudoevolve/material-components-qml
Revision: 900a537824812bb592a761a0fd0b301422bd1a79
Copyright (C) 2024–2026 sudoevolve (Aino).

The library's src/Core/Styles/Theme.qml carries the upstream LGPL-3.0-or-later
notice. We retain the original source notices and include the LGPL v3 and GPL v3
texts in LICENSES/. This application statically links md3.Core; its corresponding
source archive contains the application, the vendored library and the CMake files
needed to modify and relink them. Only Core and its color utilities are included.

The bundled material-color-utilities C++ sources and Google Material Icons Round
font use Apache-2.0 (LICENSES/Apache-2.0.txt); original Google copyright headers are
preserved. Text uses system fonts; HarmonyOS fonts are no longer included.

Local integration changes, 2026-10-26:
- Controls use inherited Item.enabled instead of shadowing it, so disabled state
  reaches their input areas.
- TextField exposes validator/inputMethodHints/input and keeps its TextInput
  binding when users edit text, supporting validated numeric fields and reset.
- NavigationDrawer destinations have names and keyboard/accessibility actions.
- Button/IconButton/Switch expose keyboard and accessibility actions; Switch has
  a 48 dp input target while retaining the upstream 52×32 dp track.

- ScrollBar keeps its mouse grab when the pointer enters list content, cancels
  existing flick momentum, and starts a fresh drag after a track click. Its input
  area and thumb length stay stable while pressed; thickness/color still animate.
  See https://doc.qt.io/qt-6/qml-qtquick-mousearea.html#preventStealing-prop.

- NavigationDrawer adds footerModel for pinned settings destinations, reusing its
  standard destination components. SegmentedButton fills available width and
  supports keyboard/accessibility actions.
- NavigationDrawer scrolls long model lists with the library ScrollBar and keys
  selection by destination page, so models with the same display name remain
  independently selectable.
- NavigationDrawer adds a fixed headerModel above its scrollable destinations
  and optional trailing Switch controls. Switch clicks emit itemToggled without
  activating the destination; the footer remains pinned for empty lists too.
- StyleManager accepts explicit theme control, complete external color schemes,
  and Google's SchemeMonochrome. Preset previews use the same Material color
  utilities as the active theme.

Application-specific layout, labels, numeric composition and window drag/resize
regions live in qml/, outside the third-party component library.
