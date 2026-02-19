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
layer configure <LayerName> -visible 0|1
layer configure <LayerName> -selectable 0|1
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/layout2
```
