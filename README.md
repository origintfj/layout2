# layout2

Prototype Qt + Tcl desktop application for IC layout editing.

## Architecture

- **Main window**: Tcl interpreter console (`TclConsoleWindow`)
- **Child window**: Layout editor (`LayoutEditorWindow`) with:
  - Layer list on the left
  - Editing canvas placeholder on the right
- **Command flow**:
  - GUI interactions emit Tcl commands
  - Tcl command execution updates shared layer state (`LayerManager`)
  - UI refresh is driven by command results

## Supported Tcl commands

```tcl
layer list
layer load <layersFilePath>
layer configure <LayerName> -visible 0|1
layer configure <LayerName> -selectable 0|1
layer active ?<LayerName>?
tool set <toolName>
canvas press <x:int64> <y:int64> <button>
canvas move <x:int64> <y:int64> <leftDown>
canvas release <x:int64> <y:int64> <button>
view pan <dx> <dy>
view zoom <wheelDelta> <anchorX> <anchorY>
view grid ?<size>?
bindkey set <keySpec> <tclCommand>
bindkey dispatch <keySpec>
bindkey list
bindkey clear ?<keySpec>?
transcript filter add <globPattern>
transcript filter remove <globPattern>
transcript filter list
transcript filter clear
```

## Layers file format

Layer files are plain text with one layer per line:

```text
<name> <type> <#RRGGBB> <0xPATTERN64>
```

`PATTERN64` is interpreted as a 64-bit bitmap (8x8 stipple tile), where each set bit draws a darker point over the layer color.

Example (`data/example_layers.txt`):

```text
Metal1 drawing #1f77b4 0x00FF
Metal2 drawing #ff7f0e 0x0F0F
Metal3 drawing #2ca02c 0xAAAA
```


## Interaction behavior

- Selecting a row in the layer palette emits `layer active <name>` to set active layer.
- Key presses in the editor canvas emit `bindkey dispatch <keySpec>` and execute whatever command is configured for that key.
- Key combinations are supported via portable key specs (for example `Shift+R`).
- Console command echo supports glob-based filtering so noisy commands like `canvas move ...` can be hidden.
- Rectangle draw flow is Tcl-driven:
  - `canvas press` starts a rectangle on active layer
  - `canvas move` updates rubber-band preview
  - `canvas release` commits the rectangle
- Coordinates are carried as signed 64-bit integers (`int64`) in Tcl canvas commands.
- Mouse wheel emits `view zoom ...` and middle-drag emits `view pan ...`; Tcl handlers apply the view transform.
- The canvas grid is anchored to world coordinates (it pans/zooms with the view), and `view grid <size>` updates the grid spacing in world units (`view grid` queries current size).

## Startup initialization

On startup, the application executes `init.tcl` (copied to the build directory).
An editable example script is provided at `scripts/init.example.tcl`, and the default `scripts/init.tcl` runs:

```tcl
layer load example_layers.txt
source bindkeys.tcl
source transcript_filters.tcl
```

This initializes the layer palette, loads default key bindings, and applies transcript filters. `scripts/bindkeys.tcl` defines key-to-command mappings (including combinations like `Shift+R`) and `scripts/transcript_filters.tcl` defines command-echo suppression rules.

## Ubuntu dependencies

Install build tools + Tcl + either Qt5 or Qt6 development packages:

```bash
sudo apt update
sudo apt install -y build-essential cmake tcl-dev qtbase5-dev
# Optional alternative if you prefer Qt6 and your distro provides it:
# sudo apt install -y qt6-base-dev
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

`CMakeLists.txt` is set up to use Qt6 when available and automatically fall back to Qt5.

## Run

```bash
./build/layout2
```
