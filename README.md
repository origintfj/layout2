# layout2

Prototype Qt + Tcl desktop application for IC layout editing.

## Architecture

- **Main window**: Tcl interpreter console (`TclConsoleWindow`)
- **Child window**: Layout editor (`LayoutEditorWindow`) with:
  - Layer list on the left
  - Editing canvas on the right
- **Command flow**:
  - GUI interactions emit Tcl commands
  - Tcl command execution updates shared layer state (`LayerManager`)
  - UI refresh is driven by command results

## Tcl command reference

### `layer` command family

```tcl
layer list
layer load <layersFilePath>
layer active
layer active <LayerName> <LayerType>
layer configure <LayerName> <LayerType> -visible 0|1
layer configure <LayerName> <LayerType> -selectable 0|1
```

- `layer list`
  - Returns one line per layer using this format:
    ```text
    <name> {<type>} <name_id>/<type_id> <#RRGGBB> <pattern> <visible|hidden> <selectable|locked> <active|inactive>
    ```
- `layer load <layersFilePath>`
  - Loads a layers file. Relative paths are resolved from the executable directory.
- `layer active`
  - Query form: returns a Tcl list of two elements: `{<activeName> <activeType>}`.
- `layer active <LayerName> <LayerType>`
  - Sets active layer (name/type matching is case-insensitive).
- `layer configure ...`
  - Only `-visible` and `-selectable` are supported.
  - Value must be exactly `0` or `1`.

### `tool` command family

```tcl
tool set <toolName>
```

- Sets the active tool string used by the editor (for example `select` or `rect`).

### `canvas` command family

```tcl
canvas press <x:int64> <y:int64> <button>
canvas move <x:int64> <y:int64> <leftDown>
canvas release <x:int64> <y:int64> <button>
```

- `x`/`y` are signed 64-bit integer world coordinates.
- Rectangle draw flow is Tcl-driven:
  - `canvas press` starts preview when `tool` is `rect`, left button is `1`, and an active layer exists.
  - `canvas move` updates preview while `leftDown == 1`.
  - `canvas release` commits the rectangle when `button == 1` and a draw is in progress.

### `view` command family

```tcl
view pan <dx> <dy>
view zoom <wheelDelta> <anchorX> <anchorY>
view grid
view grid <size>
```

- `view pan` applies a delta to pan offsets.
- `view zoom` performs anchor-preserving zoom; zoom is clamped to a safe range.
- `view grid` (query) returns current grid size.
- `view grid <size>` sets grid spacing; `size` must be `> 0`.

### `bindkey` command family

```tcl
bindkey set <keySpec> <tclCommand>
bindkey dispatch <keySpec>
bindkey list
bindkey clear
bindkey clear <keySpec>
```

- `keySpec` uses Qt portable key text (examples: `R`, `Esc`, `Shift+R`, `Ctrl+1`).
- `bindkey set` stores an exact Tcl command string (brace it if it contains spaces).
- `bindkey dispatch` executes the bound Tcl command if present (no-op if missing).
- `bindkey list` returns a Tcl list of `{keySpec command}` pairs.
- `bindkey clear` clears all bindings; `bindkey clear <keySpec>` removes one.

### `transcript` command family

```tcl
transcript filter add <globPattern>
transcript filter remove <globPattern>
transcript filter list
transcript filter clear
```

- Filters are glob patterns matched against the full command text before console echo.
- Commands entered directly in the console input are always echoed (filters apply to scripted/automated command execution).
- `add` ignores case when checking duplicates.
- `remove` returns the number of removed exact matches.
- `list` returns a Tcl list of active patterns.

### `app` command family

```tcl
app layout_editor
app editor active
app editor active <editorId>
app exit
```

- `app layout_editor` opens a new editor window, makes it active, and recenters world origin in view.
- `app editor active` returns the currently active editor id.
- `app editor active <editorId>` sets the active editor id (must reference an existing editor).
- `app exit` closes the application.

## File and script formats

### 1) Layers data files (`*.txt`, e.g. `data/example_layers.txt`)

Plain text, one layer per non-comment line:

```text
<name> <type> <name_id>/<type_id> <#RRGGBB> <0xPATTERN>
```

Rules:
- Blank lines and lines starting with `#` are ignored.
- Exactly 5 whitespace-separated fields are required.
- `<name_id>/<type_id>` must parse as unsigned integers (decimal or `0x...`).
- Color must be a valid Qt color token (normally `#RRGGBB`).
- Pattern must parse as an unsigned integer token (typically hex like `0x8040201008040201`).
- Duplicate `<name,type>` identities are rejected.
- On successful load, all layers default to `visible` + `selectable`, and the first layer becomes active.

Example:

```text
Metal1 drawing 1/10 #1f77b4 0x8040201008040201
Metal2 drawing 2/10 #ff7f0e 0x0F0F
Metal3 drawing 3/10 #2ca02c 0xAAAA
```

### 2) Init script format (`scripts/init.tcl`, `scripts/init.example.tcl`)

A normal Tcl startup script that is executed at app launch (for `init.tcl`).
Typical content:

```tcl
layer load sky130_layers.txt
source scripts/bindkeys.tcl
source scripts/transcript_filters.tcl
tool set select
app layout_editor
```

Notes:
- `init.tcl` is the active startup file.
- `init.example.tcl` is a template for customization.
- `source` paths should be written to match where scripts are copied/located in your run directory.
- Add `app layout_editor` if you want a layout editor window to open automatically at startup.

### 3) Keybinding script format (`scripts/bindkeys.tcl`)

Each binding line is:

```tcl
bindkey set <QtPortableKeySpec> <tclCommand>
```

Examples:

```tcl
bindkey set R {tool set rect}
bindkey set Esc {tool set select}
bindkey set Shift+R {tool set rect}
```

### 4) Transcript filter script format (`scripts/transcript_filters.tcl`)

Each rule line is:

```tcl
transcript filter add <globPattern>
```

Examples:

```tcl
transcript filter add {canvas move *}
transcript filter add {canvas *}
```

## Interaction behavior

- Selecting a row in the layer palette emits `layer active <name> <type>`.
- Key presses in the editor canvas emit `bindkey dispatch <keySpec>`.
- Mouse wheel emits `view zoom ...`; middle-drag emits `view pan ...`.
- The canvas grid is anchored to world coordinates and scales/pans with the view.

## Ubuntu dependencies

Install build tools + Tcl + either Qt5 or Qt6 development packages:

```bash
sudo apt update
sudo apt install -y build-essential cmake tcl-dev qtbase5-dev
# Optional alternative if you prefer Qt6 and your distro provides it:
# sudo apt install -y qt6-base-dev
```

## Build

## Rendering backend selection (experimental)

By default, the layout canvas uses the raster backend.

You can enable the experimental OpenGL-oriented backend seam with:

```bash
export LAYOUT2_RENDER_BACKEND=opengl
```

Current status:
- `raster`: full current rendering path.
- `opengl`: uses a `QOpenGLWidget` canvas path with GPU triangle/line submission across detail levels; detailed level applies the layer stipple pattern in the GL fragment path for parity.

Optional diagnostics:

```bash
export LAYOUT2_RENDER_STATS=1
```

When enabled, the OpenGL backend prints periodic render statistics (frames, triangles, lines, and per-frame averages).

```bash
cmake -S . -B build
cmake --build build
```

`CMakeLists.txt` is set up to use Qt6 when available and automatically fall back to Qt5.

## Run

```bash
./build/layout2
```
