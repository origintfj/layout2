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
canvas preview <anchorX:int64> <anchorY:int64> <currentX:int64> <currentY:int64>
canvas preview clear
canvas drag <anchorX:int64> <anchorY:int64> <releaseX:int64> <releaseY:int64> <button>
canvas click <x:int64> <y:int64>
```

- Coordinates are signed 64-bit integer world coordinates.
- Drag flow is Tcl-driven:
  - `canvas preview` updates tool-dependent drag preview during mouse motion.
  - `canvas drag` is emitted on left-button release and carries both anchor and release points for commit.
  - Commit semantics are based on the tool active at release time.
  - `canvas click` carries world-space click locations for tool-dependent actions.

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
transcript filter add {canvas preview *}
transcript filter add {canvas click *}
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

## Architecture / Microarchitecture

This section describes the runtime architecture as it exists today, with emphasis on rendering, hit detection, scene modeling, and editor interaction boundaries.

### 1. High-level subsystem map

At a high level, the editor is split into five major subsystems:

1. **UI Shell / Window Composition**
   - `LayoutEditorWindow` composes the left and right tool panes, layer list, status/console integration, and the central canvas.
   - This layer owns user-facing widgets and translates most interactions into model updates or Tcl command emissions.

2. **Canvas / Viewport Engine**
   - `LayoutCanvas` is the visual viewport and input surface.
   - It owns view state (pan, zoom), transforms (world<->screen), hover/selection bridge logic, and render item preparation.

3. **Rendering Backends**
   - A small backend interface (`PrimitiveRenderBackend`) abstracts drawing implementation details from canvas orchestration.
   - Two concrete implementations currently exist:
     - `OpenGLPrimitiveRenderBackend` (default)
     - `RasterPrimitiveRenderBackend` (compatibility/fallback)

4. **Scene Model / Spatial Query Engine**
   - `LayoutSceneNode` and `LayoutObjectModel` represent committed geometry in a hierarchical scene graph.
   - The scene owns object identity, bounds, tile indexing, and query operations used by both drawing and hit-testing.

5. **Layer / Technology Metadata**
   - Layer definitions (color, stipple pattern, visibility, etc.) are loaded by the layer manager stack.
   - Rendering receives layer styling through primitives and converts it into brushes/colors/shader uniforms.

### 2. Data flow: from scene to pixels

Rendering is intentionally staged:

1. **Visible world bounds derivation**
   - The canvas computes visible world-space extents from viewport size, pan offset, and zoom.
   - This prevents full-scene render traversal each frame.

2. **Rect-limited primitive collection**
   - The canvas asks the root scene node for primitives only in the visible rect (`collectRenderPrimitivesInRect`).
   - Spatial indexing narrows candidates before object-level primitive expansion.

3. **Render-item construction**
   - Primitives are transformed into backend-agnostic `RenderItem` records containing:
     - polygon in screen-space
     - fill/outline colors
     - stipple metadata (`pattern`, cached brush)
     - preview/selection flags
     - detail level and tiny-on-screen flags

4. **Detail-level policy application**
   - Detail level is computed from zoom and attached to each item.
   - Tiny geometry skipping and simplified/coarse appearance decisions are applied before backend draw submission.

5. **Backend draw submission**
   - Canvas delegates to the selected backend (`beginFrame -> drawPrimitives -> endFrame`).
   - Backend receives a fully prepared immutable list for that frame.

### 3. OpenGL backend internals (default)

The OpenGL path is designed as a hybrid optimized pipeline:

1. **GL resource bootstrap**
   - Shader program and buffers are initialized lazily in the first valid GL context.
   - If GL init fails, rendering falls back to painter path for resilience.

2. **Frame hashing and geometry cache reuse**
   - A frame hash is computed from viewport and render-item signatures.
   - If unchanged, cached VBO data is reused to avoid repacking/reuploading every frame.

3. **Simplified/coarse batching**
   - Non-detailed items are bucketed and triangulated into contiguous float buffers.
   - A packed vertex format is used: `[x, y, r, g, b, a]`.
   - Draw calls use:
     - `GL_TRIANGLES` for fills
     - `GL_LINES` for selected outlines

4. **Detailed stipple rendering in GL**
   - Detailed items are rendered through GL with stipple enabled in fragment shader.
   - Layer map stipple is passed as eight 8-bit row values (`uPatternRows[8]`).
   - Shader computes screen-space stipple bits (2x magnification to match existing visual density) and discards fragments for clear bits.

5. **Optional telemetry**
   - With `LAYOUT2_RENDER_STATS=1`, backend emits periodic frame/primitive statistics for tuning.

### 4. Raster backend internals (compatibility path)

The raster backend retains the previous QPainter-driven path:

- Draws polygons directly with `QPainter` brushes/pens.
- Uses cached stipple brushes for detailed mode and solid fills for simplified/coarse modes.
- Obeys the same render-item semantics (selection, preview, detail level).

This backend remains useful for:
- fast regression checks,
- fallback on systems with problematic GL stacks,
- isolating renderer-specific bugs.

### 5. Scene model and spatial indexing

`LayoutSceneNode` stores both hierarchy and acceleration structures:

- **Object storage**: ordered local vector of object models.
- **ID maps**: direct object lookup by object ID.
- **Bounds table**: cached world-space AABB per object.
- **Tile index**: object IDs grouped into fixed-size world tiles (`kSpatialTileSize = 2048`).

Indexing lifecycle:

1. On object add:
   - object is inserted,
   - bounds queried (`tryGetBounds`),
   - object ID is inserted into overlapping tile buckets.

2. On object remove:
   - object is removed from tile buckets and bounds maps,
   - ordering bookkeeping is compacted.

Query patterns:

- **Rendering query** (`collectRenderPrimitivesInRect`)
  - collect tile candidates for viewport rect,
  - preserve deterministic order with object-order map,
  - bounds-filter candidates,
  - append object primitives.

- **Hit query** (`matchingObjectIdsAt`)
  - collect tile candidates for point tile,
  - sort by reverse paint order for topmost-first semantics,
  - bounds-filter before expensive `containsPoint`,
  - recurse to children and merge.

### 6. Hit detection and interaction flow

For pointer interactions:

1. Mouse event is converted to world coordinates by canvas transform.
2. Scene hit query returns ordered candidate IDs.
3. Canvas/editor resolves selection/hover state.
4. UI and command layers are notified (including Tcl command emission where applicable).

This keeps hit-testing cost proportional to nearby tile/object density instead of total scene size.

### 7. Layer style and stipple fidelity model

Layer style comes from technology/layer map metadata and influences both renderers:

- **Color**: base fill + alpha policy for preview/selection/detail level.
- **Pattern**: layer stipple token is preserved on render item.
- **Detailed rendering fidelity**:
  - raster backend uses stipple brush,
  - OpenGL backend uses shader-based stipple evaluation from the same pattern data.

This design keeps a single authoritative stipple source while allowing backend-specific implementation.

### 8. Extension points for new contributors

Recommended areas for clean incremental improvements:

1. **Renderer performance**
   - batch detailed stipple draws by pattern/material to reduce per-item uploads,
   - persistent mapped buffers or ring-buffered dynamic VBO updates,
   - instancing for repeated geometry classes.

2. **Scene acceleration**
   - adaptive tile sizing or multi-level index for skewed geometries,
   - cache invalidation strategy for mutable/animated objects,
   - optional parallel primitive extraction for large visible windows.

3. **Correctness/tooling**
   - screenshot-based visual parity tests (raster vs OpenGL),
   - microbenchmarks for pan/zoom/hit latency,
   - telemetry export for frame-time percentile tracking.

4. **Architecture hygiene**
   - move backend classes into dedicated files/modules to reduce `LayoutEditorWindow.cpp` size,
   - introduce explicit render graph/frame object to make backend API easier to evolve.

### 9. Mental model summary for new engineers

If you are new to the project, the most useful mental model is:

- The **scene model** owns truth (objects, bounds, IDs, spatial index).
- The **canvas** owns view state and transforms, then builds frame-local render items.
- The **backend** owns drawing strategy (OpenGL or painter), but not scene semantics.
- The **layer map** owns style semantics (color/stipple), which are propagated unchanged to rendering.

That separation is what allows performance work (backend and culling) to proceed without destabilizing editing behavior and scene correctness.

## Build

## Rendering backend selection

By default, the layout canvas now uses the OpenGL backend.

Backend override options:

```bash
# Default behavior (same as leaving the variable unset)
export LAYOUT2_RENDER_BACKEND=opengl

# Force legacy/painter path
export LAYOUT2_RENDER_BACKEND=raster
```

Current status:
- `opengl` (default): `QOpenGLWidget` canvas path with GPU triangle/line submission across detail levels; detailed level applies the layer stipple pattern in the GL fragment path for parity.
- `raster`: legacy painter rendering path retained as a compatibility and fallback option.

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
