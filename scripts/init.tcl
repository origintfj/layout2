# Default startup script executed by TclConsoleWindow at application launch.
#
# This initializes the palette by loading the example layers definition.
# Users can customize startup behavior by editing this file or using
# scripts/init.example.tcl as a template.
layer load sky130_layers.txt
source scripts/bindkeys.tcl
source scripts/transcript_filters.tcl
# NOTE: Tcl `source` returns the result of the last command in the file.
# Keep `tool set select` last so its status text is shown in startup transcript.
app layout_editor
tool set select
