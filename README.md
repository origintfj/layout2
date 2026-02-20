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
```

## Layers file format

Layer files are plain text with one layer per line:

```text
<name> <type> <#RRGGBB> <0xPATTERN>
```

Example (`data/example_layers.txt`):

```text
Metal1 drawing #1f77b4 0x00FF
Metal2 drawing #ff7f0e 0x0F0F
Metal3 drawing #2ca02c 0xAAAA
```


## Interaction behavior

- Selecting a row in the layer palette emits `layer active <name>` to set active layer.
- Pressing `r` in the editor canvas emits `tool set rect`.
- Rectangle draw flow is Tcl-driven:
  - `canvas press` starts a rectangle on active layer
  - `canvas move` updates rubber-band preview
  - `canvas release` commits the rectangle
- Coordinates are carried as signed 64-bit integers (`int64`) in Tcl canvas commands.
- Mouse wheel emits `view zoom ...` and middle-drag emits `view pan ...`; Tcl handlers apply the view transform.

## Startup initialization

On startup, the application executes `init.tcl` (copied to the build directory).
An editable example script is provided at `scripts/init.example.tcl`, and the default `scripts/init.tcl` runs:

```tcl
layer load example_layers.txt
```

This initializes the layer palette shown in the editor pane.

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
