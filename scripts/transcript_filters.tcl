# Default transcript filters loaded by init.tcl.
#
# Format:
#   transcript filter add <globPattern>
#
# Example:
#   transcript filter add {canvas move *}

# Suppress high-frequency mouse move command echoes in the Tcl console.
transcript filter add {canvas move *}
