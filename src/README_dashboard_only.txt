RetroSpectrum dashboard-only integration

This version makes RetroSpectrum.c itself the dashboard. There is no dashboard.c or dashboard.h.

Files in this package:
  RetroSpectrum_dashboard_only.c   Replace your RetroSpectrum.c with this file.
  world_map_bin_loader.c           Keep next to RetroSpectrum.c. It is included directly by RetroSpectrum.c.
  world_map.bin                    Runtime map binary. Keep next to the executable or run from this folder.
  RetroSpectrum_dashboard_only.patch Optional patch against your uploaded RetroSpectrum(39).c.

Required runtime folders/files:
  world_map.bin
  flags/                           Optional but recommended; run download_world_flags.py if missing.

Controls:
  1 / F1      Map dashboard
  2 / F2      RetroSpectrum waterfall station
  3 / F3      Analysis workstation
  4 / F4      Classification workstation
  Ctrl+D      Return to map/dashboard from any station
  Esc         Reset the map while on the dashboard
  Q           Quit from the dashboard

Build example:
  gcc -Wall -Wextra -O2 \
      RetroSpectrum.c GUIs.c AnalysisWorkstation.c ClassificationWorkstation.c \
      -o retrospectrum \
      -lhackrf -lfftw3 -lSDL2 -lSDL2_ttf -lSDL2_image -lm -lpthread

Important:
  Do not compile world_map_bin_loader.c separately. RetroSpectrum.c includes it internally with WORLD_MAP_NO_DEMO.
  If the map does not appear, run the executable from the folder that contains world_map.bin.
