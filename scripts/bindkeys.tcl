# Default key bindings loaded by init.tcl.
#
# Format:
#   bindkey set <QtPortableKeySpec> <tclCommand>
#
# Examples:
#   bindkey set R {tool set rect}
#   bindkey set Esc {tool set none}
#   bindkey set Ctrl+1 {layer active Metal1 drawing}

bindkey set R {tool set rect}
#bindkey set Esc {tool set none}
#bindkey set S {tool set select}
bindkey set Esc {tool set select}
bindkey set Shift+R {tool set rect}

bindkey set Q {app properties}
bindkey set O {dlog_via}

bindkey set Right {view pan -50   0}
bindkey set Left  {view pan  50   0}
bindkey set Up    {view pan   0 -50}
bindkey set Down  {view pan   0  50}
