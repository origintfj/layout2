# Default transcript filters loaded by init.tcl.
#
# Format:
#   transcript filter add <globPattern>
#
# Example:
#   transcript filter add {canvas *}

# Suppress high-frequency canvas and view command echoes in the Tcl console.
transcript filter add {canvas *}
transcript filter add {view zoom *}
transcript filter add {view pan *}
