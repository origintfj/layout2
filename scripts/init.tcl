# Default startup script executed by TclConsoleWindow at application launch.
#
# This initializes the palette by loading the example layers definition.
# Users can customize startup behavior by editing this file or using
# scripts/init.example.tcl as a template.
layer load sky130_layers.txt
source scripts/bindkeys.tcl
source scripts/transcript_filters.tcl
tool set select
app layout_editor
